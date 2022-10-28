# NVIDIA Real-time Denoisers v3.8.0 (NRD)

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
  - Diffuse & specular radiance
- *REBLUR*:
  - Diffuse & specular radiance
  - Diffuse (ambient) & specular occlusion (OCCLUSION variants)
  - Diffuse & specular radiance in spherical harmonics (SH variants)
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
    - Windows: latest *WindowsSDK* (22000+), *VulkanSDK* (1.3.216+)
    - Linux (x86-64): latest *VulkanSDK*
    - Linux (aarch64): find a precompiled binary for [*DXC*](https://github.com/microsoft/DirectXShaderCompiler) or disable shader compilation `NRD_DISABLE_SHADER_COMPILATION=OFF`
- Build (variant 1) - using *Git* and *CMake* explicitly
    - Clone project and init submodules
    - Generate and build the project using *CMake*
- Build (variant 2) - by running scripts:
    - Run `1-Deploy`
    - Run `2-Build`

### CMake options

- `NRD_DXC_CUSTOM_PATH = "custom/path/to/dxc"` - custom DXC to use if Vulkan SDK is not installed
- `NRD_SHADER_OUTPUT_PATH` - shader output path override
- `NRD_NORMAL_ENCODING` - `NRD_NORMAL_ENCODING` value for *NRD.hlsli*
- `NRD_ROUGHNESS_ENCODING` - `NRD_ROUGHNESS_ENCODING` value for *NRD.hlsli*
- `NRD_DISABLE_SHADER_COMPILATION` - disable shader compilation (shaders can be compiled on another platform)
- `NRD_USE_PRECOMPILED_SHADERS` -  use precompiled shaders (will be embedded into the library)
- `NRD_STATIC_LIBRARY` - build static library
- `NRD_STATIC_CPP_RUNTIME` - link static C++ runtime (*Linux* only), on *Windows* use *CMAKE_MSVC_RUNTIME_LIBRARY*
- `NRD_CROSSCOMPILE_AARCH64` - cross compilation for *aarch64*
- `NRD_CROSSCOMPILE_X86_64` - cross compilation for *x86_64*
- `NRD_INTERPROCEDURAL_OPTIMIZATION` - interprocedural optimization

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
* Create an SRV for a specific range of subresources
* Create and bind 4 predefined samplers
* Invoke a Dispatch call (no raster, no VS/PS)
* Create 2D textures with SRV / UAV access

### Integration Method 2: Black-box library (using native API pointers)

If Graphics API's native pointers are retrievable from the RHI, the standard *NRD integration* layer can be used to greatly simplify the integration. In this case, the application should only wrap up native pointers for the *Device*, *CommandList* and some input / output *Resources* into entities, compatible with an API abstraction layer (*[NRI](https://github.com/NVIDIAGameWorks/NRI)*), and all work with *NRD* library will be hidden inside the integration layer:

*Engine or App → native objects → NRD integration layer → NRI → NRD*

*NRI = NVIDIA Rendering Interface* - an abstraction layer on top of Graphics APIs: DX11, DX12 and VULKAN. *NRI* has been designed to provide low overhead access to the Graphics APIs and simplify development of DX12 and VULKAN applications. *NRI* API has been influenced by VULKAN as the common denominator among these 3 APIs.

*NRI* and *NRD* are ready-to-use products. The application must expose native pointers only for Device, Resource and CommandList entities (no SRVs and UAVs - they are not needed, everything will be created internally). Native resource pointers are needed only for the denoiser inputs and outputs (all intermediate textures will be handled internally). Descriptor heap will be changed to an internal one, so the application needs to bind its original descriptor heap after invoking the denoiser.

In rare cases, when the integration via the engine’s RHI is not possible and the integration using native pointers is complicated, a "DoDenoising" call can be added explicitly to the application-side RHI. It helps to avoid increasing code entropy.

The pseudo code below demonstrates how *NRD integration* and *NRI* can be used to wrap native Graphics API pointers into NRI objects to establish connection between the application and NRD:

```cpp
//=======================================================================================================
// INITIALIZATION - DECLARATIONS
//=======================================================================================================

#include "NRIDescs.hpp"
#include "Extensions/NRIWrapperD3D12.h"
#include "Extensions/NRIHelper.h"

#include "NRD.h"
#include "NRDIntegration.hpp"

NrdIntegration NRD = NrdIntegration(maxNumberOfFramesInFlight);

struct NriInterface
    : public nri::CoreInterface
    , public nri::HelperInterface
    , public nri::WrapperD3D12Interface
{};
NriInterface NRI;

//=======================================================================================================
// INITIALIZATION - WRAP NATIVE DEVICE
//=======================================================================================================

// Wrap the device
nri::DeviceCreationD3D12Desc deviceDesc = {};
deviceDesc.d3d12Device = ...;
deviceDesc.d3d12PhysicalAdapter = ...;
deviceDesc.d3d12GraphicsQueue = ...;
deviceDesc.enableNRIValidation = false;

nri::Device* nriDevice = nullptr;
nri::Result nriResult = nri::CreateDeviceFromD3D12Device(deviceDesc, nriDevice);

// Get core functionality
nriResult = nri::GetInterface(*nriDevice,
  NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI);

nriResult = nri::GetInterface(*nriDevice,
  NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI);

// Get appropriate "wrapper" extension (XXX - can be D3D11, D3D12 or VULKAN)
nriResult = nri::GetInterface(*nriDevice,
  NRI_INTERFACE(nri::WrapperXXXInterface), (nri::WrapperXXXInterface*)&NRI);

//=======================================================================================================
// INITIALIZATION - INITIALIZE NRD
//=======================================================================================================

const nrd::MethodDesc methodDescs[] =
{
    // Put neeeded methods here, like:
    { nrd::Method::XXX, renderResolution.x, renderResolution.y },
};

nrd::DenoiserCreationDesc denoiserCreationDesc = {};
denoiserCreationDesc.requestedMethods = methodDescs;
denoiserCreationDesc.requestedMethodNum = methodNum;

bool result = NRD.Initialize(*nriDevice, NRI, NRI, denoiserCreationDesc);

//=======================================================================================================
// INITIALIZATION or RENDER - WRAP NATIVE POINTERS
//=======================================================================================================

// Wrap the command buffer
nri::CommandBufferD3D12Desc commandBufferDesc = {};
commandBufferDesc.d3d12CommandList = (ID3D12GraphicsCommandList*)d3d12CommandList;

// Not needed for NRD integration layer, but needed for NRI validation layer
commandBufferDesc.d3d12CommandAllocator = (ID3D12CommandAllocator*)d3d12CommandAllocatorOrJustNonNull;

nri::CommandBuffer* nriCommandBuffer = nullptr;
NRI.CreateCommandBufferD3D12(*nriDevice, commandBufferDesc, nriCommandBuffer);

// Wrap required textures (better do it only once on initialization)
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

//=======================================================================================================
// RENDER - DENOISE
//=======================================================================================================

// Populate common settings
//  - for the first time use defaults
//  - currently NRD supports only the following view space: X - right, Y - top, Z - forward or backward
nrd::CommonSettings commonSettings = {};
PopulateCommonSettings(commonSettings);

// Set settings for each method in the NRD instance
nrd::XxxSettings settings = {};
PopulateXxxSettings(settings);

NRD.SetMethodSettings(nrd::Method::XXX, &settings);

// Fill up the user pool
NrdUserPool userPool = {};
{
    // Fill only required "in-use" inputs and outputs in appropriate slots using entryDescs & entryFormat,
    // applying remapping if necessary. Unused slots will be {nullptr, nri::Format::UNKNOWN}
    NrdIntegration_SetResource(userPool, ...);
    ...
    NrdIntegration_SetResource(userPool, ...);
};

// Better use "true" if resources are not changing between frames (i.e. are not suballocated from a heap)
bool enableDescriptorCaching = true;

NRD.Denoise(frameIndex, *nriCommandBuffer, commonSettings, userPool, enableDescriptorCaching);

// IMPORTANT: NRD integration binds own descriptor pool, don't forget to re-bind back your pool (heap)

//=======================================================================================================
// SHUTDOWN or RENDER - CLEANUP
//=======================================================================================================

// Better do it only once on shutdown
for (uint32_t i = 0; i < N; i++)
    NRI.DestroyTexture(entryDescs[i].texture);

NRI.DestroyCommandBuffer(*nriCommandBuffer);

//=======================================================================================================
// SHUTDOWN - DESTROY
//=======================================================================================================

// Release wrapped device
NRI.DestroyDevice(*nriDevice);

// Also NRD needs to be recreated on "resize"
NRD.Destroy();
```

Shader part:

```cpp
#if 1
    #include "NRDEncoding.hlsli"
#else
    // Or define NRD encoding in Cmake and deliver macro definitions to shader compilation command line
#endif

#define NRD_HEADER_ONLY
#include "NRD.hlsli"

// Call corresponding "front end" function to encode data for NRD (NRD.hlsli indicates which function
// needs to be used for a specific input for a specific denoiser). For example:

float4 nrdIn = RELAX_FrontEnd_PackRadianceAndHitDist(radiance, hitDistance);

// Call corresponding "back end" function to decode data produced by NRD. For example:

float4 nrdOut = RELAX_BackEnd_UnpackRadiance(nrdOutEncoded);
```

### Integration Method 3: White-box library (using the application-side Render Hardware Interface)

Logically it's close to the Method 1, but the integration takes place in the full source code (only the *NRD* project is needed). In this case *NRD* shaders are handled by the application shader compilation pipeline. The application should still use *NRD* via *NRD API* to preserve forward compatibility. This method suits best for compilation on other platforms (consoles, ARM), unlocks *NRD* modification on the application side and increases portability.

NOTE: this method is WIP. It works, but in the future it will work better out of the box.

## NRD TERMINOLOGY

* *Denoiser method (or method)* - a method for denoising of a particular signal (for example: `Method::DIFFUSE`)
* *Denoiser* - a set of methods aggregated into a monolithic entity (the library is free to rearrange passes without dependencies)
* *Resource* - an input, output or internal resource. Currently can only be a texture
* *Texture pool (or pool)* - a texture pool that stores permanent or transient resources needed for denoising. Textures from the permanent pool are dedicated to *NRD* and can not be reused by the application (history buffers are stored here). Textures from the transient pool can be reused by the application right after denoising. *NRD* doesn’t allocate anything. *NRD* provides resource descriptions, but resource creations are done on the application side.

## NRD API OVERVIEW

### API flow

1. *GetLibraryDesc* - contains general *NRD* library information (supported denoising methods, SPIRV binding offsets). This call can be skipped if this information is known in advance (for example, is diffuse denoiser available?), but it can’t be skipped if SPIRV binding offsets are needed for VULKAN
2. *CreateDenoiser* - creates a denoiser based on requested methods (it means that diffuse, specular and shadow logical denoisers can be merged into a single denoiser instance)
3. *GetDenoiserDesc* - returns descriptions for pipelines, static samplers, texture pools, constant buffer and descriptor set. All this stuff is needed during the initialization step. Commonly used for initialization.
4. *SetMethodSettings* - can be called to change parameters dynamically before applying the denoiser on each new frame / denoiser call
5. *GetComputeDispatches* - returns per-dispatch data (bound subresources with required state, constant buffer data)
6. *DestroyDenoiser* - destroys a denoiser

## HOW TO RUN DENOISING?

*NRD* doesn't make any graphics API calls. The application is supposed to invoke a set of compute *Dispatch* calls to actually denoise input signals. Please, refer to `NrdIntegration::Denoise()` and `NrdIntegration::Dispatch()` calls in `NRDIntegration.hpp` file as an example of an integration using low level RHI.

*NRD* doesn’t have a "resize" functionality. On resolution change the old denoiser needs to be destroyed and a new one needs to be created with new parameters. But *NRD* supports dynamic resolution scaling via `CommonSettings::resolutionScale`.

The following textures can be requested as inputs or outputs for a method. Required resources are specified near a method declaration inside the `Method` enum class. Also `NRD.hlsli` has a comment near each front-end or back-end function, clarifying which resources this function is for.

### NRD INPUTS & OUTPUTS

Commons inputs:

* **IN\_MV** - non-jittered primary surface motion (`old = new + MV`)

  Supported variants via `ComminSettings::isMotionVectorInWorldSpace`:
  - 3D world space motion (recommended) - camera motion should not be included (it's already in the matrices). In other words, if there are no moving objects all motion vectors = 0. The `.w` channel is unused and can be used by the app
  - 2D screen space motion - 2D motion doesn't provide information about movement along the view direction, it only allows to "get" the direction. *NRD* can introduce pixels with rejected history on dynamic objects in this case.

  Motion vector scaling can be provided via `CommonSettings::motionVectorScale`.

* **IN\_NORMAL\_ROUGHNESS** - primary surface world-space normal and *linear* roughness

  Normal and roughness encoding must be controlled via *Cmake* parameters `NRD_NORMAL_ENCODING` and `NRD_ROUGHNESS_ENCODING`. During project deployment optional `NRDEncoding.hlsli` file is generated, which can be included prior `NRD.hlsli` to make encoding macro definitions visible in shaders (if `NRD_NORMAL_ENCODING` and `NRD_ROUGHNESS_ENCODING` are not defined in another way by the application). Encoding settings can be known at runtime by accessing `GetLibraryDesc().normalEncoding` and `GetLibraryDesc().roghnessEncoding` respectively. `NormalEncoding` and `RoughnessEncoding` enums briefly describe encoding variants. It's recommended to use `NRD_FrontEnd_PackNormalAndRoughness` from `NRD.hlsli` to match decoding.

  *NRD* computes local curvature using provided normals. Less accurate normals can lead to banding in curvature and local flatness. `RGBA8` normals is a good baseline, but `R10G10B10A10` oct-packed normals improve curvature calculations and specular tracking as the result.

  If `materialID` is provided and supported by encoding, *NRD* diffuse and specular denoisers won't mix up surfaces with different material IDs.

* **IN\_VIEWZ** - `.x` - view-space Z coordinate of the primary hit position (linearized g-buffer depth)

  Positive and negative values are supported. Z values in all pixels must be in the same space, matching space defined by matrices passed to NRD. If, for example, the protagonist's hands are rendered using special matrices, Z values should be computed as:
  - reconstruct world position using special matrices for "hands"
  - project on screen using matrices passed to NRD
  - `.w` component is positive view Z (or just transform world space position to main view space and take `.z` component)

**IMPORTANT**: All textures should be *NaN* free at each pixel, even at pixels outside of denoising range.

See `NRDDescs.h` for more details and descriptions of other inputs and outputs.

## NOISY DATA REQUIREMENTS

NRD sample is a good start to familiarize yourself with input requirements and best practices, but main requirements can be summarized to:

- Signal for *NRD* must be separated into diffuse and specular at primary hit
- `hitT` must not include primary hit distance
- Do not pass *sum of lengths of all segments* as `hitT`. A solid baseline is to use hit distance for the 1st bounce only, it works well for diffuse and specular signals
  - *NRD sample* uses more complex method for accumulating `hitT` along the path, which takes into account energy dissipation due to lobe spread and curvature at the current hit
- Noise in provided hit distances must follow a diffuse or specular lobe. It implies that `hitT` for `roughness = 0` must be clean (if probabilistic sampling is not in use)
- In case of probabilistic diffuse / specular selection at the primary hit, provided `hitT` must follow the following rules:
  - Should not be divided by `PDF`
  - If diffuse or specular sampling is skipped, `hitT` must be set to `0` for corresponding signal type
  - `hitDistanceReconstructionMode` must be set to something other than `OFF`, but bear in mind that the search area is limited to 3x3 or 5x5. In other words, it's the application's responsibility to guarantee a valid sample in this area. It can be achieved by clamping probabilities and using Bayer-like dithering (see the sample for more details)
  - Pre-pass must be enabled (i.e. `diffusePrepassBlurRadius` and `specularPrepassBlurRadius` must be set to 20-70 pixels) to compensate entropy increase, since radiance in valid samples is divided by probability to compensate 0 values in some neighbors
- Probabilistic sampling for 2nd+ bounces is absolutely acceptable

## INTERACTION WITH PRIMARY SURFACE REPLACEMENTS

[*Primary Surface Replacement (PSR)*](https://developer.nvidia.com/blog/rendering-perfect-reflections-and-refractions-in-path-traced-games/) can be used with *NRD*. In this case `IN_MV`, `IN_NORMAL_ROUGHNESS` and `IN_VIEWZ` must have data for secondary hits (not primary hits). In case of PSR *NRD* disocclusion logic doesn't take curvature at primary hit into account, because data for primary hits is replaced. This can lead to more intense disocclusions on bumpy surfaces due to significant ray divergence. To mitigate this problem 2x-10x larger `disocclusionThreshold` can be used. This is an applicable solution if the denoiser is used to denoise surfaces with PSR only (glass only, for example). In a general case, when PSR and normal surfaces are mixed on the screen, higher disocclusion thresholds are needed only for pixels with PSR. This can be achieved by using `IN_DISOCCLUSION_THRESHOLD_MIX` input to smoothly mix baseline `disocclusionThreshold` into bigger `disocclusionThresholdAlternate` from `CommonSettings`. Most likely the increased disocclusion threshold is needed only for pixels with normal details at primary hits (local curvature is not zero).

## MEMORY REQUIREMENTS

The *Persistent* column (matches *NRD Permanent pool*) indicates how much of the *Working set* is required to be left intact for subsequent frames of the application. This memory stores the history resources consumed by NRD. The *Aliasable* column (matches *NRD Transient pool*) shows how much of the *Working set* may be aliased by textures or other resources used by the application outside of the operating boundaries of NRD.

| Resolution |                             Denoiser | Working set (Mb) |  Persistent (Mb) |   Aliasable (Mb) |
|------------|--------------------------------------|------------------|------------------|------------------|
|      1080p |                       REBLUR_DIFFUSE |            88.75 |            46.50 |            42.25 |
|            |             REBLUR_DIFFUSE_OCCLUSION |            33.88 |            21.12 |            12.75 |
|            |                    REBLUR_DIFFUSE_SH |           139.38 |            63.38 |            76.00 |
|            |                      REBLUR_SPECULAR |            97.19 |            46.50 |            50.69 |
|            |            REBLUR_SPECULAR_OCCLUSION |            33.88 |            21.12 |            12.75 |
|            |                   REBLUR_SPECULAR_SH |           147.81 |            63.38 |            84.44 |
|            |              REBLUR_DIFFUSE_SPECULAR |           160.50 |            71.88 |            88.62 |
|            |    REBLUR_DIFFUSE_SPECULAR_OCCLUSION |            46.56 |            21.12 |            25.44 |
|            |           REBLUR_DIFFUSE_SPECULAR_SH |           261.75 |           105.62 |           156.12 |
|            | REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION |            88.75 |            46.50 |            42.25 |
|            |                         SIGMA_SHADOW |            23.38 |             0.00 |            23.38 |
|            |            SIGMA_SHADOW_TRANSLUCENCY |            42.31 |             0.00 |            42.31 |
|            |                        RELAX_DIFFUSE |           120.31 |            65.44 |            54.88 |
|            |                       RELAX_SPECULAR |           130.94 |            73.94 |            57.00 |
|            |               RELAX_DIFFUSE_SPECULAR |           215.31 |           107.69 |           107.62 |
|            |                            REFERENCE |            33.75 |            33.75 |             0.00 |
|            |                                      |                  |                  |                  |
|      1440p |                       REBLUR_DIFFUSE |           157.50 |            82.50 |            75.00 |
|            |             REBLUR_DIFFUSE_OCCLUSION |            60.00 |            37.50 |            22.50 |
|            |                    REBLUR_DIFFUSE_SH |           247.50 |           112.50 |           135.00 |
|            |                      REBLUR_SPECULAR |           172.50 |            82.50 |            90.00 |
|            |            REBLUR_SPECULAR_OCCLUSION |            60.00 |            37.50 |            22.50 |
|            |                   REBLUR_SPECULAR_SH |           262.50 |           112.50 |           150.00 |
|            |              REBLUR_DIFFUSE_SPECULAR |           285.00 |           127.50 |           157.50 |
|            |    REBLUR_DIFFUSE_SPECULAR_OCCLUSION |            82.50 |            37.50 |            45.00 |
|            |           REBLUR_DIFFUSE_SPECULAR_SH |           465.00 |           187.50 |           277.50 |
|            | REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION |           157.50 |            82.50 |            75.00 |
|            |                         SIGMA_SHADOW |            41.38 |             0.00 |            41.38 |
|            |            SIGMA_SHADOW_TRANSLUCENCY |            75.12 |             0.00 |            75.12 |
|            |                        RELAX_DIFFUSE |           213.75 |           116.25 |            97.50 |
|            |                       RELAX_SPECULAR |           232.50 |           131.25 |           101.25 |
|            |               RELAX_DIFFUSE_SPECULAR |           382.50 |           191.25 |           191.25 |
|            |                            REFERENCE |            60.00 |            60.00 |             0.00 |
|            |                                      |                  |                  |                  |
|      2160p |                       REBLUR_DIFFUSE |           334.69 |           175.31 |           159.38 |
|            |             REBLUR_DIFFUSE_OCCLUSION |           127.50 |            79.69 |            47.81 |
|            |                    REBLUR_DIFFUSE_SH |           525.94 |           239.06 |           286.88 |
|            |                      REBLUR_SPECULAR |           366.56 |           175.31 |           191.25 |
|            |            REBLUR_SPECULAR_OCCLUSION |           127.50 |            79.69 |            47.81 |
|            |                   REBLUR_SPECULAR_SH |           557.81 |           239.06 |           318.75 |
|            |              REBLUR_DIFFUSE_SPECULAR |           605.62 |           270.94 |           334.69 |
|            |    REBLUR_DIFFUSE_SPECULAR_OCCLUSION |           175.31 |            79.69 |            95.62 |
|            |           REBLUR_DIFFUSE_SPECULAR_SH |           988.12 |           398.44 |           589.69 |
|            | REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION |           334.69 |           175.31 |           159.38 |
|            |                         SIGMA_SHADOW |            87.94 |             0.00 |            87.94 |
|            |            SIGMA_SHADOW_TRANSLUCENCY |           159.56 |             0.00 |           159.56 |
|            |                        RELAX_DIFFUSE |           454.31 |           247.12 |           207.19 |
|            |                       RELAX_SPECULAR |           494.19 |           279.00 |           215.19 |
|            |               RELAX_DIFFUSE_SPECULAR |           812.94 |           406.50 |           406.44 |
|            |                            REFERENCE |           127.50 |           127.50 |             0.00 |

## INTEGRATION GUIDE, RECOMMENDATIONS AND GOOD PRACTICES

Denoising is not a panacea or miracle. Denoising works best with ray tracing results produced by a suitable form of importance sampling. Additionally, *NRD* has its own restrictions. The following suggestions should help to achieve best image quality:

**[NRD]** The *NRD API* has been designed to support integration into native VULKAN apps. If the RHI you work with is DX11-like, not all provided data will be needed.

**[NRD]** Read all comments in `NRDDescs.h`, `NRDSettings.h` and `NRD.hlsli`.

**[NRD]** If you are unsure of which parameters to use - use defaults via `{}` construction. It helps to improve compatibility with future versions and offers optimal IQ, because default settings are always adjusted by recent algorithmic changes.

**[NRD]** *NRD* requires linear roughness and world-space normals. See `NRD.hlsli` for more details and supported customizations.

**[NRD]** *NRD* requires non-jittered matrices.

**[NRD]** When upgrading to the latest version keep an eye on `ResourceType` enumeration. The order of the input slots can be changed or something can be added, you need to adjust the inputs accordingly to match the mapping. Or use *NRD integration* to simplify the process.

**[NRD]** All pixels in floating point textures should be INF / NAN free to avoid propagation, because such values are used in weight calculations and accumulation of a weighted sum. Functions `XXX_FrontEnd_PackRadianceAndHitDist` perform optional NAN / INF clearing of the input signal. There is a boolean to skip these checks.

**[NRD]** All denoisers work with positive RGB inputs (some denoisers can change color space in *front end* functions). For better image quality, HDR inputs need to be in a sane range [0; 10-100]. Passing pre-exposured colors (i.e. `color * exposure`) is not recommended, because a significant momentary change in exposure is hard to react to in this case.

**[NRD]** *NRD* can track camera motion internally. For the first time pass all MVs set to 0 (you can use `CommonSettings::motionVectorScale = {0}` for this) and set `CommonSettings::isMotionVectorInWorldSpace = true`, it will allow you to simplify the initial integration. Enable application-provided MVs after getting denoising working on static objects.

**[NRD]** Using 2D MVs can lead to massive history reset on moving objects, because 2D motion provides information only about pixel screen position but not about real 3D world position. Consider using 3D MVs instead.

**[NRD]** Firstly, try to get a working reprojection on a diffuse signal for camera rotations only (without camera motion).

**[NRD]** Diffuse and specular signals must be separated at the start of the path.

**[NRD]** *NRD* has been designed to work with pure radiance coming from a particular direction. This means that data in the form "something / probability" should be avoided because overall entropy of the input signal will be increased (but it doesn't mean that denoising won't work). Additionally, it means that primary materials needs to be decoupled from the input signal, i.e. primary BRDF should be applied **after** denoising. This is achieved by using "demodulation" trick:

    // Diffuse
    Denoising( diffuseRadiance * albedo ) → NRD( diffuseRadiance / albedo ) * albedo

    // Specular
    float3 preintegratedBRDF = PreintegratedBRDF( Rf0, N, V, roughness )
    Denoising( specularRadiance * BRDF ) → NRD( specularRadiance * BRDF / preintegratedBRDF ) * preintegratedBRDF

A good approximation for pre-integrated specular BRDF can be found *[here](https://github.com/NVIDIAGameWorks/Falcor/blob/056f7b7c73b69fa8140d211bbf683ddf297a2ae0/Source/Falcor/Rendering/Materials/Microfacet.slang#L213)*.

**[NRD]** Denoising logic is driven by provided hit distances. For indirect lighting denoising passing hit distance for the 1st bounce only is a good baseline. For direct lighting a distance to an occluder or a light source is needed. Primary hit distance must be excluded in any case.

**[NRD]** Importance sampling is recommended to achieve good results in case of complex lighting environments. Consider using:
   - Cosine distribution for diffuse from non-local light sources
   - VNDF sampling for specular
   - Custom importance sampling for local light sources (*RTXDI*).

**[NRD]** Hit distances should come from an importance sampling method. But if denoising of AO/SO is needed, AO/SO can come from cos-weighted (or VNDF) sampling in a tradeoff of IQ.

**[NRD]** Low discrepancy sampling (blue noise) helps to have more stable output in 0.5-1 rpp mode. It's a must for REBLUR-based Ambient and Specular Occlusion denoisers and SIGMA.

**[NRD]** It's recommended to set `CommonSettings::accumulationMode` to `RESET` for a single frame, if a history reset is needed. If history buffers are recreated or contain garbage, it's recommended to use `CLEAR_AND_RESET` for a single frame. `CLEAR_AND_RESET` is not free because clearing is done in a compute shader. Render target clears on the application side should be prioritized over this solution.

**[NRD]** If there are areas (besides sky), which don't require denoising (for example, casting a specular ray only if roughness is less than some threshold), providing `viewZ > CommonSettings::denoisingRange` in **IN\_VIEWZ** texture for such pixels will effectively skip denoising. Additionally, the data in such areas won't contribute to the final result.

**[NRD]** If there are areas (besides sky), which don't require denoising (for example, skipped diffuse rays for true metals). `materialID` and `materialMask` can be used to drive spatial passes.

**[NRD]** Input signal quality can be improved by enabling *pre-pass* via setting `diffusePrepassBlurRadius` and `specularPrepassBlurRadius` to a non-zero value. Pre-pass is needed more for specular and less for diffuse, because pre-pass outputs optimal hit distance for specular tracking (see the sample for more details).

**[NRD]** In case of probabilistic diffuse / specular split at the primary hit, hit distance reconstruction pass must be enabled, if exposed in the denoiser (see `HitDistanceReconstructionMode`).

**[NRD]** In case of probabilistic diffuse / specular split at the primary hit, pre-pass must be enabled, if exposed in the denoiser (see `diffusePrepassBlurRadius` and `specularPrepassBlurRadius`).

**[NRD]** Maximum number of accumulated frames can be FPS dependent. The following formula can be used on the application side to adjust `maxAccumulatedFrameNum`, `maxFastAccumulatedFrameNum` and potentially `historyFixFrameNum` too:
```
maxAccumulatedFrameNum = accumulationPeriodInSeconds * FPS
```

**[NRD]** The number of accumulated frames in the fast history needs to be carefully tuned to avoid introducing significant bias and dirt. Initial integration should be done by setting `maxFastAccumulatedFrameNum` to `maxAccumulatedFrameNum`. Bare in mind the following recommendation:
```
maxAccumulatedFrameNum > maxFastAccumulatedFrameNum > historyFixFrameNum
```

**[REBLUR]** In case of *REBLUR* ensure that `enableReferenceAccumulation = true` works properly first. It's not mandatory, but will help to simplify debugging of potential issues by implicitly disabling spatial filtering entirely.

**[REBLUR]** If more performance is needed, consider using `enablePerformanceMode = true`.

**[REBLUR]** *REBLUR* expects hit distances in a normalized form. To avoid mismatching, `REBLUR_FrontEnd_GetNormHitDist` must be used for normalization. Normalization parameters should be passed into *NRD* as `HitDistanceParameters` for internal hit distance denormalization. Some tweaking can be needed here, but in most cases default `HitDistanceParameters` works well. *REBLUR* outputs denoised normalized hit distance, which can be used by the application as ambient or specular occlusion (AO & SO) (see unpacking functions from `NRD.hlsli`).

**[REBLUR]** *REBLUR* handles specular lobe trimming, trying to reconstruct trimmed signals. Similarly to hit distance normalization, *REBLUR* needs to be aware about trimming parameters. If this feature is used in a ray tracer, `SpecularLobeTrimmingParameters` must be passed into *REBLUR*. To avoid code duplication, `NRD_GetTrimmingFactor` can be used in a shader code on the application side.

**[REBLUR]** Intensity antilag parameters need to be carefully tuned. The defaults are good but `AntilagIntensitySettings::sensitivityToDarkness` needs to be tuned for a given HDR range. Initial integration should work with intensity antilag turned off.

**[REBLUR]** Even if antilag is off, it's recommended to tune `AntilagIntensitySettings::sensitivityToDarkness`, because it is used for error estimation.

**[RELAX]** *RELAX* works well with signals produced by *RTXDI* or very clean high RPP signals. The Sweet Home of *RELAX* is *RTXDI* sample. Please, consider getting familiar with this application.

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

After denoising the final result can be computed as:

*&Sigma;( L[i] &times; S[i] )* = *&Sigma;( L[i] )* &times; *OUT_SHADOW_TRANSLUCENCY.yzw*

Is this a biased solution? If spatial filtering is off - no, because we just reorganized the math equation. If spatial filtering is on - yes, because denoising will be driven by most important light in a given pixel.

**This solution is limited** and hard to use:
- obviously, can be used "as is" if shadows don't overlap (*weight* = 1)
- if shadows overlap, a separate pass is needed to analyze noisy input and classify pixels as *umbra* - *penumbra* (and optionally *empty space*). Raster shadow maps can be used for this if available
- it is not recommended to mix 1 cd and 100000 cd lights, since FP32 texture will be needed for a weighted sum.
In this case, it's better to process the sun and other bright light sources separately.

## HOW TO REPORT ISSUES

NRD sample has *TESTS* section in the bottom of the UI, a new test can be added if needed. The following procedure is recommended:
- Try to reproduce a problem in the *NRD sample* first
  - if reproducible
    - add a test (by pressing `Add` button)
    - describe the issue and steps to reproduce
    - attach depending on the selected scene `.bin` file from the `Tests` folder
  - if not
    - verify the integration
- If nothing helps
  - describe the issue, attach a video and steps to reproduce
