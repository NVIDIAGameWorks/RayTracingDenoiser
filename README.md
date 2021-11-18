# NVIDIA Real-time (Ray tracing) Denoisers v2.10.0

## SAMPLE APP

See *[NRD sample](https://github.com/NVIDIAGameWorks/NRDSample)* project.

## QUICK START GUIDE

### Intro
*NVIDIA Real-Time Denoisers (NRD)* is a spatio-temporal API agnostic denoising library. The library has been designed to work with low rpp (ray per pixel) signals (1 rpp and 0.5 rpp). *NRD* is a fast solution that slightly depends on input signals and environment conditions.

*NRD* includes the following denoisers:
- *REBLUR* - recurrent blur based denoiser
- *RELAX* - SVGF based denoiser using clamping to fast history to minimize temporal lag, has been designed for *[RTXDI (RTX Direct Illumination)](https://developer.nvidia.com/rtxdi)*
- *SIGMA* - shadow-only denoiser

Supported signal types (modulated irradiance can be used instead of radiance):
- *RELAX*:
  - Diffuse radiance or ambient occlusion (AO)
  - Specular radiance
- *REBLUR*:
  - Diffuse radiance and / or ambient occlusion (AO)
  - Specular radiance and / or specular occlusion (SO)
- *SIGMA*:
  - Shadows from an infinite light source (sun, moon)
  - Shadows from a local light source (omni, spot)
  - Shadows from multiple sources (experimental).

*NRD* is distributed as source as well with a “ready-to-use” library (if used in a precompiled form). It can be integrated into any DX12, VULKAN or DX11 engine using 2 methods:
1. Native implementation of the *NRD* API using engine capabilities
2. Integration via an abstraction layer. In this case, the engine should expose native Graphics API pointers for certain types of objects. The integration layer, provided as a part of SDK, can be used to simplify this kind of integration.

## Build instructions
- Windows: install latest *WindowsSDK* and *VulkanSDK*
- Linux: install latest *VulkanSDK* or *DXC*
  - Compilation of *DXBC* and *DXIL* shaders is disabled on this platform
- Generate and build project using *CMake*

Or by running scripts (*Windows* only):
- Run ``1-Deploy.bat``
- Run ``2-Build.bat``

### CMake options
`-DNRD_DISABLE_INTERPROCEDURAL_OPTIMIZATION=ON` - disable interprocedural optimization

`-DNRD_DISABLE_SHADER_COMPILATION=ON` - disable shader compilation (shaders can be built on another platform)

`-DNRD_STATIC_LIBRARY=ON` - build NRD as a static library

`-DNRD_DXC_CUSTOM_PATH="my/path/to/dxc"` - custom path to DXC

### Cross compilation
If target architecture is *ARM64*, set `NRD_CROSSCOMPILE_AARCH64=ON` in addition to setting a correct cmake kit.

### Tested platforms
| OS                | Architectures  | Compilers   |
|-------------------|----------------|-------------|
| Windows           | AMD64          | MSVC, Clang |
| Linux             | AMD64, ARM64   | GCC, Clang  |

### NRD SDK package generation
- Compile the solution (*Debug* / *Release* or both, depending on what you want to get in *NRD* package)
- If you plan to use *NRD integration* layer, run ``3-Prepare NRI SDK.bat`` from *NRI* project
- Run ``3-Prepare NRD SDK.bat``
- Grab generated in the root directory ``_NRD_SDK`` folder and use it in your project

## INTEGRATION VARIANTS

### Integration Method 1: Black-box library (using the application-side Render Hardware Interface)
RHI must have the ability to do the following:
* Create shaders from precompiled binary blobs
* Create an SRV for a specific range of subresources (a real example from the library - SRV = mips { 1, 2, 3, 4 }, UAV = mip 0)
* Create and bind 4 predefined samplers
* Invoke a Dispatch call (no raster, no VS/PS)
* Create 2D textures with SRV / UAV access

### Integration Method 2: Black-box library (using native API pointers)

If Graphics API's native pointers are retrievable from the RHI, the standard *NRD* integration layer can be used to greatly simplify the integration. In this case, the application should only wrap up native pointers for the *Device*, *CommandList* and some input / output *Resources* into entities, compatible with an API abstraction layer (*[NRI](https://github.com/NVIDIAGameWorks/NRI)*), and all work with *NRD* library will be hidden inside the integration layer:

*Engine or App → native objects → NRD integration layer → NRI → NRD*

*NRI = NVIDIA Rendering Interface* - an abstraction layer on top of Graphics APIs: DX11, DX12 and VULKAN. *NRI* has been designed to provide low overhead access to the Graphics APIs and simplify development of DX12 and VULKAN applications. *NRI* API has been influenced by VULKAN as the common denominator among these 3 APIs.

*NRI* and *NRD* are ready-to-use products. The application must expose native pointers only for Device, Resource and CommandList entities (no SRVs and UAVs - they are not needed, everything will be created internally). Native resource pointers are needed only for the denoiser inputs and outputs (all intermediate textures will be handled internally). Descriptor heap will be changed to an internal one, so the application needs to bind its original descriptor heap after invoking the denoiser.

In rare cases, when the integration via engine’s RHI is not possible and the integration using native pointers is complicated, a "DoDenoising" call can be added explicitly to the application-side RHI. It helps to avoid increasing code entropy.

The pseudo code below demonstrates how *NRD Integration* and *NRI* can be used to wrap native Graphics API pointers into NRI objects to establish connection between the application and NRD:

```cpp
//====================================================================================================================
// STEP 1 - DECLARATIONS
//====================================================================================================================

#include "NRIDescs.hpp"
#include "Extensions/NRIWrapperD3D12.h"
#include "Extensions/NRIHelper.h"

#include "NRD.h"

NrdIntegration NRD = NrdIntegration(maxNumberOfFramesInFlight);

struct NriInterface
    : public nri::CoreInterface
    , public nri::HelperInterface
    , public nri::WrapperD3D12Interface
{};
NriInterface NRI;

nri::Device* nriDevice = nullptr;

//====================================================================================================================
// STEP 2 - WRAP NATIVE DEVICE
//====================================================================================================================

nri::DeviceCreationD3D12Desc deviceDesc = {};
deviceDesc.d3d12Device = ...;
deviceDesc.d3d12PhysicalAdapter = ...;
deviceDesc.d3d12GraphicsQueue = ...;
deviceDesc.enableNRIValidation = false;

// Wrap the device
nri::Result result = nri::CreateDeviceFromD3D12Device(deviceDesc, nriDevice);

// Get needed functionality
result = nri::GetInterface(*nriDevice, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI);
result = nri::GetInterface(*nriDevice, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI);

// Get needed "wrapper" extension, XXX - can be D3D11, D3D12 or VULKAN
result = nri::GetInterface(*nriDevice, NRI_INTERFACE(nri::WrapperXXXInterface), (nri::WrapperXXXInterface*)&NRI);

//====================================================================================================================
// STEP 3 - INITIALIZE NRD
//====================================================================================================================

const nrd::MethodDesc methodDescs[] =
{
    // put neeeded methods here, like:
    { nrd::Method::REBLUR_DIFFUSE_SPECULAR, renderResolution.x, renderResolution.y },
};

nrd::DenoiserCreationDesc denoiserCreationDesc = {};
denoiserCreationDesc.requestedMethods = methodDescs;
denoiserCreationDesc.requestedMethodNum = methodNum;

bool result = NRD.Initialize(*nriDevice, NRI, NRI, denoiserCreationDesc);

//====================================================================================================================
// STEP 4 - WRAP NATIVE POINTERS
//====================================================================================================================

// Wrap the command buffer
nri::CommandBufferD3D12Desc cmdDesc = {};
cmdDesc.d3d12CommandList = (ID3D12GraphicsCommandList*)&d3d12CommandList;
cmdDesc.d3d12CommandAllocator = nullptr; // Not needed for NRD Integration layer

nri::CommandBuffer* cmdBuffer = nullptr;
NRI.CreateCommandBufferD3D12(*nriDevice, cmdDesc, cmdBuffer);

// Wrap required textures
nri::TextureTransitionBarrierDesc entryDescs[N] = {};
nri::Format entryFormat[N] = {};

for (uint32_t i = 0; i < N; i++)
{
    nri::TextureTransitionBarrierDesc& entryDesc = entryDescs[i];
    const MyResource& myResource = GetMyResource(i);

    nri::TextureD3D12Desc textureDesc = {};
    textureDesc.d3d12Resource = myResource->GetNativePointer();
    NRI.CreateTextureD3D12(*nriDevice, textureDesc, (nri::Texture*&)entryDesc.texture );

    // You need to specify the current state of the resource here, after denoising NRD can modify
    // this state. Application must continue state tracking from this point.
    // Useful information:
    //    SRV = nri::AccessBits::SHADER_RESOURCE, nri::TextureLayout::SHADER_RESOURCE
    //    UAV = nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::TextureLayout::GENERAL
    entryDesc.nextAccess = ConvertResourceStateToAccessBits( myResource->GetCurrentState() );
    entryDesc.nextLayout = ConvertResourceStateToLayout( myResource->GetCurrentState() );
}

//====================================================================================================================
// STEP 5 - DENOISE
//====================================================================================================================

// Populate common settings
//  - for the first time use defaults
//  - currently NRD supports only the following view space: X - right, Y - top, Z - forward or backward
nrd::CommonSettings commonSettings = {};
PopulateCommonSettings(commonSettings);

// Set settings for each denoiser
nrd::NrdXxxSettings settings = {};
PopulateDenoiserSettings(settings);

NRD.SetMethodSettings(nrd::Method::NRD_XXX, &settings);

// Fill up the user pool
NrdUserPool userPool =
{{
    // Fill the required inputs and outputs in appropriate slots using entryDescs & entryFormat,
    // applying remapping if necessary. Unused slots can be {nullptr, nri::Format::UNKNOWN}
}};

NRD.Denoise(*cmdBuffer, commonSettings, userPool);

//====================================================================================================================
// STEP 6 - CLEANUP
//====================================================================================================================

for (uint32_t i = 0; i < N; i++)
    NRI.DestroyTexture(entryDescs[i].texture);

NRI.DestroyCommandBuffer(*cmdBuffer);

//====================================================================================================================
// STEP 7 - DESTROY
//====================================================================================================================

NRD.Destroy();
```

### Integration Method 3: White-box library (using the application-side Render Hardware Interface)

Logically it's close to the Method 1, but the integration takes place in the full source code (only the NRD project is needed). In this case NRD shaders are handled by the application shader compilation pipeline. The application should still use NRD via NRD API to preserve forward compatibility. This method suits best for compilation on other platforms (consoles, ARM), unlocks NRD modification on the application side and increases portability.

NOTE: this method is WIP. It works, but in the future it will work better out of the box.

## NRD TERMINOLOGY

* *Denoiser method (or method)* - a method for denoising of a particular signal (for example: ``Method::DIFFUSE``)
* *Denoiser* - a set of methods aggregated into a monolithic entity (the library is free to rearrange passes without dependencies)
* *Resource* - an input, output or internal resource. Currently can only be a texture
* *Texture pool (or pool)* - a texture pool that stores permanent or transient resources needed for denoising. Textures from the permanent pool are dedicated to NRD and can not be reused by the application (history buffers are stored here). Textures from the transient pool can be reused by the application right after denoising. NRD doesn’t allocate anything. *NRD* provides resource descriptions, but resource creations are done on the application side.

## NRD API OVERVIEW

### API flow

1. *GetLibraryDesc* - contains general *NRD* library information (supported denoising methods, SPIRV binding offsets). This call can be skipped if this information is known in advance (for example, is diffuse denoiser available?), but it can’t be skipped if SPIRV binding offsets are needed for VULKAN
2. *CreateDenoiser* - creates a denoiser based on requested methods (it means that diffuse, specular and shadow logical denoisers can be merged into a single denoiser instance)
3. *GetDenoiserDesc* - returns descriptions for pipelines, static samplers, texture pools, constant buffer and descriptor set. All this stuff is needed during the initialization step. Commonly used for initialization.
4. *SetMethodSettings* - can be called to change parameters dynamically before applying the denoiser on each new frame / denoiser call
5. *GetComputeDispatches* - returns per-dispatch data (bound subresources with required state, constant buffer data)
6. *DestroyDenoiser* - destroys a denoiser

## HOW TO RUN DENOISING?

*NRD* doesn't make any graphics API calls. The application is supposed to invoke a set of compute Dispatch() calls to actually denoise input signals. Please, refer to ``NrdIntegration::Denoise()`` and ``NrdIntegration::Dispatch()`` calls in NRDIntegration.hpp file as an example of an integration using low level RHI.

*NRD* doesn’t have a "resize" functionality. On resolution change the old denoiser needs to be destroyed and a new one needs to be created with new parameters.

NOTE: ``XXX`` below is a replacement for a denoiser you choose from *REBLUR*, *RELAX* or *SIGMA*.

The following textures can be requested as inputs or outputs for a method. Required resources are specified near a method declaration in ``Method``.

### NRD INPUTS

Brackets contain recommended precision:

* **IN\_MV** (RGBA16f+ or RG16f+) - surface motion (a common part of the g-buffer). MVs must be non-jittered, ``old = new + MV``. Supported motion:
  - 3D world space motion (recommended). Camera motion should not be included (it's already in the matrices). In other words, if there are no moving objects all motion vectors = 0. The alpha channel is unused and can be used by the app
  - 2D screen space motion
Motion vector scaling can be provided via ``CommonSettings::motionVectorScale``.

* **IN\_NORMAL\_ROUGHNESS** (RGBA8, R10G10B10A2 or RGBA16 depending on encoding) - ``.xyz`` - normal in world space, ``.w`` - roughness. Normal and roughness encoding can be controlled by the following macros located in ``NRD.hlsli``:
  - _NRD\_USE\_SQRT\_LINEAR\_ROUGHNESS_ = 0 - roughness is ``linearRoughness = sqrt( mathematicalRoughness )``
  - _NRD\_USE\_SQRT\_LINEAR\_ROUGHNESS_ = 1 - roughness is ``sqrt( linearRoughness )``
  - _NRD\_NORMAL\_ENCODING = NRD\_NORMAL\_ENCODING\_UNORM8 or NRD\_NORMAL\_ENCODING\_UNORM16_ - normal unpacking is ``normalize( .xyz * 2 - 1 )``
  - _NRD\_NORMAL\_ENCODING = NRD\_NORMAL\_ENCODING\_OCT10_ - normals are packed using octahedron encoding.

NRD computes local curvature using provided normals. Less accurate normals can lead to banding in curvature and local flatness. RGBA8 normals is a good baseline, but R10G10B10A10 oct-packed normals improve curvature calculations and specular tracking in the result.

Spatial filtering can be made material ID aware by enabling ``NRD_USE_MATERIAL_ID_AWARE_FILTERING``. In this case material ID will be taken from ``.w`` channel by default. The decoding can be changed in ``NRD_FrontEnd_UnpackNormalAndRoughness`` function.

* **IN\_VIEWZ** (R16f+) - ``.x`` - view-space Z coordinates of the primary surfaces (positive and negative values are supported).

* **IN\_DIFF\_RADIANCE\_HITDIST** and **IN\_SPEC\_RADIANCE\_HITDIST** (RGBA16f+) - main inputs for diffuse and specular methods respectively. These inputs should be prepared using the ``XXX_FrontEnd_PackRadianceAndHitDist`` function from ``NRD.hlsli``. *REBLUR* denoises AO and SO for free, but at the same time real hit distances are needed for controlling denoising and specular tracking. To simplify this, *REBLUR* suggests using ``REBLUR_FrontEnd_GetNormHitDist`` for hit distance normalization. This value needs to be passed into ``REBLUR_FrontEnd_PackRadianceAndHitDist``. Normalization parameters should be passed into *NRD* as ``HitDistanceParameters`` for diffuse and specular separately for internal hit distance denormalization.

* **IN\_DIFF\_HITDIST** and **IN\_SPEC\_HITDIST** (R8+) - main inputs for Ambient Occlusion and Specular Occlusion denoisers. Taking into account that AO / SO for a given ray is a normalized hit distance by the definition and that at least *REBLUR* must be aware about hit distance encoding, same hit distance encoding requirements are applicable here.

* **IN\_DIFF\_DIRECTION\_PDF** and **IN\_SPEC\_DIRECTION\_PDF** (RGBA8+) - these inputs are needed only if ``PrePassMode::ADVANCED`` is used. ``.xyz`` - ray direction, ``.w`` - ray probability (PDF). The data can be averaged or weighted in case of many RPP. Encoding / decoding can be changed in ``NRD_FrontEnd_PackDirectionAndPdf`` / ``NRD_FrontEnd_UnpackDirectionAndPdf`` functions in ``NRD.hlsli``.

* **IN\_DIFF\_CONFIDENCE** and **IN\_SPEC\_CONFIDENCE** (R8+) - there inputs needed only if ``CommonSettings::isHistoryConfidenceInputsAvailable = true``. ``.x`` - history confidence in range 0-1.

* **IN\_SHADOWDATA** (RG16f+) - *SIGMA* input, needs to be packed using a ``XXX_FrontEnd_PackShadow`` function from ``NRD.hlsli``. Infinite (sky) pixels must be cleared using ``XXX_INF_SHADOW`` macros.

* **IN\_SHADOW\_TRANSLUCENCY** (RGBA8+) - ``.x`` - shadow, ``.yzw`` - translucency. See corresponding variant of ``XXX_FrontEnd_PackShadow``.

### NRD OUTPUTS

NOTE: In some denoisers these textures can potentially be used as history buffers!

* **OUT\_DIFF\_RADIANCE\_HITDIST** (RGBA16f+) - ``.xyz`` - denoised diffuse radiance, ``.w`` - denoised normalized hit distance (AO)

* **OUT\_SPEC\_RADIANCE\_HITDIST** (RGBA16f+) - ``.xyz`` - denoised specular radiance, ``.w`` - denoised normalized hit distance (SO)

* **OUT\_DIFF\_HITDIST** (R8+) - ``.x`` - denoised normalized hit distance (AO)

* **OUT\_SPEC\_HITDIST** (R8+) - ``.x`` - denoised normalized hit distance (SO)

* **OUT\_SHADOW\_TRANSLUCENCY** (R8+ for SIGMA_SHADOW or RGBA8+ for SIGMA_TRANSLUCENT_SHADOW) - ``.x`` - denoised shadow, ``.yzw`` - denoised translucency (for *SIGMA_SHADOW_TRANSCLUCENCY*) . Must be unpacked using ``XXX_BackEnd_UnpackShadow`` function from ``NRD.hlsli``. Usage:

  ```cpp
  shadowData = SIGMA_BackEnd_UnpackShadow( shadowData );

  float3 finalShadowCommon = lerp( shadowData.yzw, 1.0, shadowData.x ); // or
  float3 finalShadowExotic = shadowData.yzw * shadowData.x; // or
  float3 finalShadowMoreExotic = shadowData.yzw;
  ```

## INTERACTION WITH PATH TRACERS

Notes:
- read *INTEGRATION GUIDE, RECOMMENDATIONS AND GOOD PRACTICES* section (below)
- path length MUST be separated into diffuse path and specular path. To familiarize yourself with required data, run the NRD sample and look at ambient / specular occlusion outputs (it's denoised hit distance)
- do not pass *sum of lengths of all segments* as *hit distance*. Use `NRD_GetCorrectedHitDist` instead (for diffuse signal only hit distance of the first bounce is needed for *REBLUR*)
- hit distance (path length) MUST not include primary hit distance
- accumulation in NRD is driven by roughness. Zero roughness means "less accumulation". It implicitly means that stochastically sampled smooth dielectrics (getting very low probability of specular sampling) are a problematic case (at least for classic path tracers)
- for *REBLUR* normalized hit distances should be averaged in case of many paths sampling (not real distances)
- noisy radiance inputs MUST not include material information at primary hits, i.e. radiance (not irradiance) is needed
- Probabilistic sampling for 2nd+ bounces is absolutely acceptable, but when casting rays from the primary hit position it's better cast 2 paths - one for diffuse and another one for specular (it also solves the problem of hit distance separation)

## INTEGRATION GUIDE, RECOMMENDATIONS AND GOOD PRACTICES

Denoising is not a panacea or miracle. Denoising works best with ray tracing results produced by a suitable form of importance sampling. Additionally, *NRD* has its own restrictions. The following suggestions should help to achieve best image quality:

**[NRD]** NRD API has been designed to support integration into native VULKAN apps. If the RHI, you work with, is DX11-like, not all provided data will be needed.

**[NRD]** Read all comments in ``NRDDescs.h``, ``NRDSettings.h`` and ``NRD.hlsli``.

**[NRD]** If you are unsure of which parameters to use - use defaults via ``{}`` construction. It will help to improve compatibility with future versions.

**[NRD]** When upgrading to the latest version keep an eye on ``ResourceType`` enumeration. The order of the input slots can be changed or something can be added, you need to adjust the inputs accordingly to match the mapping.

**[NRD]** All pixels in floating point textures should be INF / NAN free to avoid propagation, because such values are used in weight calculations and accumulation of weigted sum.

**[NRD]** *NRD* works with linear roughness and world-space normals. See ``NRD.hlsli`` for more details and supported customizations.

**[NRD]** *NRD* works with non-jittered matrices.

**[NRD]** The library doesn’t care about motion vectors much. For the first time pass all MVs set to 0 (you can use ``CommonSettings::motionVectorScale = {0}`` for this) and set ``CommonSettings::isMotionVectorInWorldSpace = true``, *NRD* can track camera motion internally. Enable application provided MVs after getting denoising working on static objects.

**[NRD]** Using of 2D MVs can lead to massive history reset on fast moving objects, because 2D motion provides information only about pixel screen position but not about real 3D world position. Consider using 3D MVs instead.

**[NRD]** Firstly, try to get correctly working reprojection on a diffuse signal for camera rotations only (without camera motion).

**[NRD]** *NRD* has been designed to work with pure radiance coming from a particular direction. This means that data in the form "something / probability" should be avoided (but it doesn't mean that it won't work), because spatial filtering passes can start to distribute wrong energy to the neighboring pixels. Additionally, it means that BRDF should be applied **after** denoising:

    Denoising( DiffuseRadiance * Albedo ) → Denosing( DiffuseRadiance ) * Albedo
    Denoising( SpecularRadiance * BRDF( micro params ) ) → Denoising( SpecularRadiance ) * INTEGRATED_BRDF( macro params )

It's worth noting that *RELAX* has a better capability to preserve details in this case due to usage of A-trous filter with luminance stoppers.

If passing radiance is impossible, "de-modulation" trick should be used instead. The idea is simple - the input signal needs to be divided by diffuse or specular albedo before denoising, and multiplied back after denoising. Preintegrated F-term suits best for specular (search for ``USE_MODULATED_IRRADIANCE`` in the shader code of the sample).

**[NRD]** Better use non-noisy lighting models, i.e. avoid stochastic lighting in reflections.

**[NRD]** Denoising logic is driven by provided hit distances. For REBLUR diffuse-only denoiser only hit distance for the 1st bounce is needed. For specular denoisers just a length of the path can be used, excluding distance to the primary hit. But this solution is suboptimal, better use ``NRD_GetCorrectedHitDist`` from ``NRD.hlsli``.

**[NRD]** Denoisers (currently only *REBLUR*) can perform optional color compression in spatial filtering passes to improve overall IQ by sacrificing energy correctness a bit. For better IQ HDR inputs need to be in a sane range (0 - 10 / 100). Passing pre-exposured values (i.e. ``color * exposure``) is preferable. Color compression mode is roughness based and can be tuned (or turned off) in ``NRD.hlsli``.

**[NRD]** Importance sampling is recommended to achieve good results in case of complex lighting environments. Consider using:
   - Cosine distribution for diffuse from non-local light sources;
   - VNDF sampling for specular;
   - Custom importance sampling for local light sources (*RTXDI*).

**[NRD]** Hit distances should come from an importance sampling method. But if in case of *REBLUR*, for example, denoising of AO and a custom direct / indirect lighting is needed, AO can come from cos-weighted sampling and radiance can be computed by a different method in a tradeoff of IQ.

**[NRD]** Low discrepancy sampling helps to have more stable output in 0.5-1 rpp mode. It's a must for REBLUR-based Ambient and Specular Occlusion denoisers and SIGMA. You can experiment with *Blue noise* setting in the sample.

**[NRD]** It's recommended to set ``CommonSettings::accumulationMode`` to ``RESET`` for a single frame, if history reset is needed. If history buffers are recreated or contain garbage, it's recommended to use ``CLEAR_AND_RESET`` for a single frame. ``CLEAR_AND_RESET`` is not free because clearing is done in a compute shader. Render target clears on the application side should be prioritized over this solution.

**[NRD]** Functions ``XXX_FrontEnd_PackRadianceAndHitDist`` perform optional NAN / INF clearing of the input signal. There is a boolean to skip these checks.

**[NRD]** The number of accumulated frames in fast history needs to be carefully tuned to avoid introducing significant bias and dirt. Initial integration should be done by setting ``maxFastAccumulatedFrameNum`` to ``maxAccumulatedFrameNum``.

**[NRD]** If there are areas (besides sky), which don't require denoising (for example, casting a specular ray only if roughness is less than some threshold), providing ``viewZ > CommonSettings::denoisingRange`` in **IN\_VIEWZ** texture for such pixels will effectively skip denoising. Additionally, the data in such areas won't contribute to the final result.

**[NRD]** If a denoiser performs spatial filtering before accumulation, the behavior can be controlled via ``PrePassMode`` enumeration. ``ADVANCED`` mode offers better quality but requires valid data in ``IN_DIFF_DIRECTION_PDF`` and / or ``IN_SPEC_DIRECTION_PDF`` inputs (see sample for more details).

**[REBLUR]** ``CommonSettings::meterToUnitsMultiplier`` is important. It allows to work in meters and use default parameters just by passing proper scene scale.

**[REBLUR]** In case of *REBLUR* ensure that ``enableReferenceAccumulation = true`` works properly first. It's not mandatory needed, but will help to simplify debugging of potential issues by implicitly disabling spatial filtering entirely.

**[REBLUR]** For diffuse and specular *REBLUR* expects hit distance input in a normalized form. To avoid mismatching ``REBLUR_FrontEnd_GetNormHitDist`` should be used for normalization. Some tweaking can be needed here, but in most cases normalization to the default ``HitDistanceParameters`` works well. *REBLUR* outputs denoised normalized hit distance, which can be used by the application as ambient or specular occlusion (AO & SO) (see unpacking functions from ``NRD.hlsli``).

**[REBLUR]** *REBLUR* handles specular lobe trimming, trying to reconstruct trimmed signal. Similarly to hit distance normalization, *REBLUR* needs to be aware about trimming parameters. If this feature is used in a ray tracer, ``LobeTrimmingParameters`` must be passed into *REBLUR*. To avoid code duplication ``NRD_GetTrimmingFactor`` can be used in a shader code on the application side.

**[REBLUR]** Intensity antilag parameters need to be carefully tuned. Initial integration should work with intensity antilag turned off.

**[RELAX]** *RELAX* works incredibly well with signals produced by *RTXDI* or very clean high RPP signals. The Sweet Home of *RELAX* is *RTXDI* sample. Please, consider getting familiar with this application.

**[SIGMA]** To avoid shadow shimmering blue noise can be used, it works best if the pattern is static on the screen. Additionally, ``blurRadiusScale`` can be set to 2-4 to mitigate such problems in complicated cases.

**[SIGMA]** *SIGMA_TRANSLUCENT_SHADOW* can be used for denoising of shadows from multiple light sources:

*L[i]* - unshadowed analytical lighting from a single light source (**not noisy**)<br/>
*S[i]* - stochastically sampled light visibility for *L[i]* (**noisy**)<br/>
*&Sigma;( L[i] )* - unshadowed analytical lighting, typically a result of tiled lighting (HDR, not in range [0; 1])<br/>
*&Sigma;( L[i] &times; S[i] )* - final lighting (what we need to get)

The idea:<br/>
*L1 &times; S1 + L2 &times; S2 + L3 &times; S3 = ( L1 + L2 + L3 ) &times; [ ( L1 &times; S1 + L2 &times; S2 + L3 &times; S3 ) / ( L1 + L2 + L3 ) ]*

Or:<br/>
*&Sigma;( L[i] &times; S[i] ) = &Sigma;( L[i] ) &times; [ &Sigma;( L[i] &times; S[i] ) / &Sigma;( L[i] ) ]*<br/>
*&Sigma;( L[i] &times; S[i] ) / &Sigma;( L[i] )* - normalized weighted sum, i.e. pseudo translucency (LDR, in range [0; 1])

Input data preparation example:
```cpp
float3 Lsum = 0;
float2x3 multiLightShadowData = SIGMA_FrontEnd_MultiLightStart( );

for( uint i = 0; i < N; i++ )
{
    float3 L = ComputeLighting( i );
    Lsum += L;

    // "distanceToOccluder" should respect rules described in NRD.hlsli in "INPUT PARAMETERS" section
    float distanceToOccluder = SampleShadow( i );

    // The weight should be zero if a pixel is not in the penumbra, but it is not trivial to compute...
    float weight = ...;

    SIGMA_FrontEnd_MultiLightUpdate( L, distanceToOccluder, tanOfLightAngularRadius, weight, multiLightShadowData );
}

float4 shadowTranslucency;
float2 shadowData = SIGMA_FrontEnd_MultiLightEnd( viewZ, multiLightShadowData, Lsum, shadowTranslucency );
```

After denoising final result can be computed as:

*&Sigma;( L[i] &times; S[i] )* = *&Sigma;( L[i] )* &times; *OUT_SHADOW_TRANSLUCENCY.yzw*

Is this a biased solution? If spatial filtering is off - no, because we just reorganized the math equation. If spatial filtering is on - yes, because denoising will be driven by most important light in a given pixel.

**This solution is limited** and hard to use:
- obviously, can be used "as is" if shadows don't overlap (*weight* = 1)
- if shadows overlap, a separate pass is needed to analyze noisy input and classify pixels as *umbra* - *penumbra* (and optionally *empty space*). Raster shadow maps can be used for this if available
- it is not recommended to mix 1 cd and 100000 cd lights, since FP32 texture will be needed for weighted sum.
In this case, it's better to process the sun and other bright light sources separately.

## HOW TO REPORT ISSUES

NRD sample has *TESTS* section in the bottom of the UI (``--testMode`` required), a new test can be added if needed. The following procedure is recommended:
- Try to reproduce a problem in the NRD sample first
  - if reproducible
    - add a test (by pressing `Add` button)
    - describe the issue and steps to reproduce
    - attach depending on selected scene `.bin` file from `_Data\Tests` folder
  - if not
    - verify the integration
- If nothing helps
  - describe the issue, attach a video and steps to reproduce
