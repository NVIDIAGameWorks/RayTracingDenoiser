# NVIDIA Real-time Denoisers v3.0.0 (NRD)

## SAMPLE APP

See *[NRD sample](https://github.com/NVIDIAGameWorks/NRDSample)* project.

## QUICK START GUIDE

### Intro

*NVIDIA Real-Time Denoisers (NRD)* is a spatio-temporal API agnostic denoising library. The library has been designed to work with low rpp (ray per pixel) signals. *NRD* is a fast solution that slightly depends on input signals and environment conditions.

*NRD* includes the following denoisers:
- *REBLUR* - recurrent blur based denoiser
- *RELAX* - SVGF based denoiser using clamping to fast history to minimize temporal lag, has been designed for *[RTXDI (RTX Direct Illumination)](https://developer.nvidia.com/rtxdi)*
- *SIGMA* - shadow-only denoiser

Supported signal types (modulated irradiance can be used instead of radiance):
- *RELAX*:
  - Diffuse radiance
  - Specular radiance
- *REBLUR*:
  - Diffuse radiance, ambient occlusion (AO) or directional occlusion
  - Specular radiance, specular occlusion (SO)
- *SIGMA*:
  - Shadows from an infinite light source (sun, moon)
  - Shadows from a local light source (omni, spot)
  - Shadows from multiple sources (experimental).

*NRD* is distributed as a source as well with a “ready-to-use” library (if used in a precompiled form). It can be integrated into any DX12, VULKAN or DX11 engine using 2 methods:
1. Native implementation of the *NRD* API using engine capabilities
2. Integration via an abstraction layer. In this case, the engine should expose native Graphics API pointers for certain types of objects. The integration layer, provided as a part of SDK, can be used to simplify this kind of integration.

## Build instructions

- Install [*Cmake*](https://cmake.org/download/) 3.15+
- Install on
    - Windows: latest *WindowsSDK*, *VulkanSDK*
    - Linux (x86-64): latest *VulkanSDK*
    - Linux (aarch64): find a precompiled binary for [*DXC*](https://github.com/microsoft/DirectXShaderCompiler) or disable shader compilation `NRD_DISABLE_SHADER_COMPILATION=OFF`
- Build (variant 1) - using *Git* and *CMake* explicitly
    - Clone project and init submodules
    - Generate and build the project using *CMake*
- Build (variant 2) - by running scripts:
    - Run `1-Deploy`
    - Run `2-Build`

### CMake options

- `NRD_DISABLE_INTERPROCEDURAL_OPTIMIZATION=ON` - disable interprocedural optimization
- `NRD_DISABLE_SHADER_COMPILATION=ON` - disable shader compilation (shaders can be built on another platform)
- `NRD_STATIC_LIBRARY=ON` - build NRD as a static library
- `NRD_DXC_CUSTOM_PATH="custom/path/to/dxc"` - custom path to *DXC*
- `NRD_CROSSCOMPILE_AARCH64=ON` - required for *ARM64*

### Tested platforms

| OS                | Architectures  | Compilers   |
|-------------------|----------------|-------------|
| Windows           | AMD64          | MSVC, Clang |
| Linux             | AMD64, ARM64   | GCC, Clang  |

### NRD SDK package generation

- Compile the solution (*Debug* / *Release* or both, depending on what you want to get in *NRD* package)
- Run `3-Prepare NRD SDK`
- Grab generated in the root directory `_NRD_SDK` and `_NRI_SDK` (if needed) folders and use them in your project

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

In rare cases, when the integration via the engine’s RHI is not possible and the integration using native pointers is complicated, a "DoDenoising" call can be added explicitly to the application-side RHI. It helps to avoid increasing code entropy.

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

// !!! IMPORTANT !!!
// NRD integration layer binds its own descriptor pool, don't forget to re-bind back your own descriptor pool (heap)

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

* *Denoiser method (or method)* - a method for denoising of a particular signal (for example: `Method::DIFFUSE`)
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

*NRD* doesn't make any graphics API calls. The application is supposed to invoke a set of compute Dispatch() calls to actually denoise input signals. Please, refer to `NrdIntegration::Denoise()` and `NrdIntegration::Dispatch()` calls in NRDIntegration.hpp file as an example of an integration using low level RHI.

*NRD* doesn’t have a "resize" functionality. On resolution change the old denoiser needs to be destroyed and a new one needs to be created with new parameters.

NOTE: `XXX` below is a replacement for a denoiser you choose from *REBLUR*, *RELAX* or *SIGMA*.

The following textures can be requested as inputs or outputs for a method. Required resources are specified near a method declaration in `Method`.

### NRD INPUTS & OUTPUTS

Commons inputs:

* **IN\_MV** - non-jittered primary surface motion (`old = new + MV`)

  Supported variants:
  - 3D world space motion (recommended) - camera motion should not be included (it's already in the matrices). In other words, if there are no moving objects all motion vectors = 0. The `.w` channel is unused and can be used by the app;
  - 2D screen space motion

  Motion vector scaling can be provided via `CommonSettings::motionVectorScale`.

* **IN\_NORMAL\_ROUGHNESS** - primary surface normal in world space and *linear* roughness

  Normal encoding can be controlled by the following macros located in `NRD.hlsli`:
  - _NRD\_NORMAL\_ENCODING = NRD\_NORMAL\_ENCODING\_UNORM_ - `.xyz` - normal (decoding is `normalize( .xyz * 2 - 1 )`), `.w` - roughness
  - _NRD\_NORMAL\_ENCODING = NRD\_NORMAL\_ENCODING\_OCT_ - `.xy` - normal (octahedron decoding). `.z` - roughness, `.w` - optional material ID (only 2 lower bits are used).

  Roughness encoding can be controlled by the following macros located in `NRD.hlsli`:
  - _NRD\_USE\_SQRT\_LINEAR\_ROUGHNESS_ = 0 - roughness decoding is `m = alpha = roughness ^ 2`
  - _NRD\_USE\_SQRT\_LINEAR\_ROUGHNESS_ = 1 - roughness decoding is `m = alpha = roughness ^ 4`
  - _NRD\_NORMAL\_ENCODING = NRD\_NORMAL\_ENCODING\_UNORM_ - `.xyz` - normal (decoding is `normalize( .xyz * 2 - 1 )`), `.w` - roughness
  - _NRD\_NORMAL\_ENCODING = NRD\_NORMAL\_ENCODING\_OCT_ - `.xy` - normal (octahedron decoding). `.z` - roughness, `.w` - optional material ID (only 2 lower bits are used).

  NRD computes local curvature using provided normals. Less accurate normals can lead to banding in curvature and local flatness. RGBA8 normals is a good baseline, but R10G10B10A10 oct-packed normals improve curvature calculations and specular tracking in the result.

  If `materialID` is provided, NRD diffuse and specular denoisers won't mix up surfaces with different material IDs.

* **IN\_VIEWZ** - `.x` - view-space Z coordinate of the primary surface

  Positive and negative values are supported.

See `NRDDescs.h` for more details and descriptions of other inputs and outputs.

## INTERACTION WITH PATH TRACERS

- Path length must be separated into diffuse and specular paths
- Do not pass *sum of lengths of all segments* as `hitT`. Use `NRD_GetCorrectedHitDist` instead (a suitable baseline is to use hit distance for the first bounce only)
- `hitT`, passed to NRD, must not include primary hit distance
- Noisy radiance inputs must not include material information at primary hits, i.e. material de-modulation is needed
- Noise in provided hit distances must follow diffuse or specular lobe. It implies the following:
  - `hitT` for `roughness = 0` must be clean
  - Probability based diffuse / specular split at the origin of the path makes `hitT` barely usable for driving denoising in case of 1rpp for the following reasons:
    - `hitT / pdf` leads to wrong values and, as the result, wrong specular tracking
    - `hitT = 0` (if probability requirements are not met) breaks tracking and denoising guiding
- Probabilistic sampling for 2nd+ bounces is absolutely acceptable
- NRD sample is a good start to familiarize yourself with input requirements and best practices

## INTEGRATION GUIDE, RECOMMENDATIONS AND GOOD PRACTICES

Denoising is not a panacea or miracle. Denoising works best with ray tracing results produced by a suitable form of importance sampling. Additionally, *NRD* has its own restrictions. The following suggestions should help to achieve best image quality:

**[NRD]** The NRD API has been designed to support integration into native VULKAN apps. If the RHI you work with is DX11-like, not all provided data will be needed.

**[NRD]** Read all comments in `NRDDescs.h`, `NRDSettings.h` and `NRD.hlsli`.

**[NRD]** If you are unsure of which parameters to use - use defaults via `{}` construction. It will help to improve compatibility with future versions.

**[NRD]** When upgrading to the latest version keep an eye on `ResourceType` enumeration. The order of the input slots can be changed or something can be added, you need to adjust the inputs accordingly to match the mapping.

**[NRD]** All pixels in floating point textures should be INF / NAN free to avoid propagation, because such values are used in weight calculations and accumulation of a weighted sum.

**[NRD]** All NRD denoisers work with positive inputs.

**[NRD]** *NRD* works with linear roughness and world-space normals. See `NRD.hlsli` for more details and supported customizations.

**[NRD]** *NRD* works with non-jittered matrices.

**[NRD]** *NRD* can track camera motion internally. For the first time pass all MVs set to 0 (you can use `CommonSettings::motionVectorScale = {0}` for this) and set `CommonSettings::isMotionVectorInWorldSpace = true`, it will allow you to simplify the initial integration. Enable application provided MVs after getting denoising working on static objects.

**[NRD]** Using of 2D MVs can lead to massive history reset on fast moving objects, because 2D motion provides information only about pixel screen position but not about real 3D world position. Consider using 3D MVs instead.

**[NRD]** Firstly, try to get correctly working reprojection on a diffuse signal for camera rotations only (without camera motion).

**[NRD]** Diffuse and specular signals must be separated at the start of the path.

**[NRD]** *NRD* has been designed to work with pure radiance coming from a particular direction. This means that data in the form "something / probability" should be avoided because overall entropy of the input signal will be increased (but it doesn't mean that denoising won't work). Additionally, it means that primary materials needs to be decoupled from the input signal, i.e. primary BRDF should be applied **after** denoising:

    // Diffuse
    Denoising( diffuseRadiance * albedo ) → NRD( diffuseRadiance / albedo ) * albedo

    // Specular
    float3 envBRDF = EnvBRDF( Rf0, N, V, roughness );
    Denoising( specularRadiance * BRDF( Rf0, VoH ) ) → NRD( specularRadiance * BRDF( Rf0, VoH ) / EnvBRDF ) * EnvBRDF

It's worth noting that *RELAX* has a better capability to preserve details in this case due to usage of A-trous filter with luminance stoppers.

**[NRD]** Denoising logic is driven by provided hit distances. For indirect lighting denoising passing hit distance for the 1st bounce only is a good baseline. For direct lighting a distance to an occluder or a light source is needed. Primary hit distance must be excluded in any case. Read notes for the `NRD_GetCorrectedHitDist` function from `NRD.hlsli`.

**[NRD]** For better image quality HDR inputs need to be in a sane range (0 - 10 / 100).

**[NRD]** Denoisers can perform optional color compression in spatial filtering passes to improve overall IQ by sacrificing energy correctness a bit. Color compression mode is roughness-based and can be tuned (or turned off) in `NRD.hlsli`.

**[NRD]** Passing pre-exposured colors (i.e. `color * exposure`) is not recommended, because a significant momentary change in exposure is hard to react to in this case.

**[NRD]** Importance sampling is recommended to achieve good results in case of complex lighting environments. Consider using:
   - Cosine distribution for diffuse from non-local light sources;
   - VNDF sampling for specular;
   - Custom importance sampling for local light sources (*RTXDI*).

**[NRD]** Hit distances should come from an importance sampling method. But if denoising of AO/SO is needed, AO/SO can come from cos-weighted sampling in a tradeoff of IQ.

**[NRD]** Low discrepancy sampling helps to have more stable output in 0.5-1 rpp mode. It's a must for REBLUR-based Ambient and Specular Occlusion denoisers and SIGMA.

**[NRD]** It's recommended to set `CommonSettings::accumulationMode` to `RESET` for a single frame, if a history reset is needed. If history buffers are recreated or contain garbage, it's recommended to use `CLEAR_AND_RESET` for a single frame. `CLEAR_AND_RESET` is not free because clearing is done in a compute shader. Render target clears on the application side should be prioritized over this solution.

**[NRD]** Functions `XXX_FrontEnd_PackRadianceAndHitDist` perform optional NAN / INF clearing of the input signal. There is a boolean to skip these checks.

**[NRD]** If there are areas (besides sky), which don't require denoising (for example, casting a specular ray only if roughness is less than some threshold), providing `viewZ > CommonSettings::denoisingRange` in **IN\_VIEWZ** texture for such pixels will effectively skip denoising. Additionally, the data in such areas won't contribute to the final result.

**[NRD]** If there are areas (besides sky), which don't require denoising (for example, skipped diffuse rays for true metals). `materialID` and `materialMask` can be used to drive spatial passes.

**[NRD]** If a denoiser performs spatial filtering before accumulation, its behavior can be controlled via `PrePassMode` enumeration. `ADVANCED` mode offers better quality but requires valid data in `IN_DIFF_DIRECTION_PDF` and / or `IN_SPEC_DIRECTION_PDF` inputs (see sample for more details).

**[NRD]** Maximum number of accumulated frames can be FPS dependent. The following formula can be used on the application side:
```
maxAccumulatedFrameNum = accumulationPeriodInSeconds * FPS
```

**[NRD INTEGRATION]** Ensure that all slots in `NrdUserPool` are filled. Not referenced slots can be set to 0.

**[REBLUR]** In case of *REBLUR* ensure that `enableReferenceAccumulation = true` works properly first. It's not mandatory, but will help to simplify debugging of potential issues by implicitly disabling spatial filtering entirely.

**[REBLUR]** If more performance is needed, consider using `enablePerformanceMode = true`.

**[REBLUR]** For diffuse and specular *REBLUR* expects hit distance input in a normalized form. To avoid mismatching, `REBLUR_FrontEnd_GetNormHitDist` should be used for normalization. Some tweaking can be needed here, but in most cases normalization to the default `HitDistanceParameters` works well. *REBLUR* outputs denoised normalized hit distance, which can be used by the application as ambient or specular occlusion (AO & SO) (see unpacking functions from `NRD.hlsli`).

**[REBLUR]** *REBLUR* handles specular lobe trimming, trying to reconstruct trimmed signals. Similarly to hit distance normalization, *REBLUR* needs to be aware about trimming parameters. If this feature is used in a ray tracer, `SpecularLobeTrimmingParameters` must be passed into *REBLUR*. To avoid code duplication, `NRD_GetTrimmingFactor` can be used in a shader code on the application side.

**[REBLUR]** Intensity antilag parameters need to be carefully tuned. The defaults are good but `AntilagIntensitySettings::sensitivityToDarkness` needs to be tuned for a given HDR range. Initial integration should work with intensity antilag turned off.

**[REBLUR]** Even if antilag is off, it's recommended to tune `AntilagIntensitySettings::sensitivityToDarkness`, because it is used for error estimation.

**[REBLUR]** Using "blue" noise can help to minimize shimmering in the output of AO/SO-only denoisers.

**[RELAX]** *RELAX* works well with signals produced by *RTXDI* or very clean high RPP signals. The Sweet Home of *RELAX* is *RTXDI* sample. Please, consider getting familiar with this application.

**[RELAX]** The number of accumulated frames in fast history needs to be carefully tuned to avoid introducing significant bias and dirt. Initial integration should be done by setting `maxFastAccumulatedFrameNum` to `maxAccumulatedFrameNum`.

**[SIGMA]** Using "blue" noise can help to avoid shadow shimmering, it works best if the pattern is static on the screen. Additionally, `blurRadiusScale` can be set to `2-4` to mitigate such problems in complicated cases.

**[SIGMA]** *SIGMA_TRANSLUCENT_SHADOW* can be used for shadow denoising from multiple light sources:

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
- it is not recommended to mix 1 cd and 100000 cd lights, since FP32 texture will be needed for a weighted sum.
In this case, it's better to process the sun and other bright light sources separately.

## HOW TO REPORT ISSUES

NRD sample has *TESTS* section in the bottom of the UI (`--testMode` required), a new test can be added if needed. The following procedure is recommended:
- Try to reproduce a problem in the NRD sample first
  - if reproducible
    - add a test (by pressing `Add` button)
    - describe the issue and steps to reproduce
    - attach depending on the selected scene `.bin` file from the `_Data\Tests` folder
  - if not
    - verify the integration
- If nothing helps
  - describe the issue, attach a video and steps to reproduce
