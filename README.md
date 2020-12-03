# NVIDIA Real-time (Ray tracing) Denoiser v1.8.13

## QUICK START GUIDE

NVIDIA Ray Tracing Denoiser (NRD) is a spatio-temporal API agnostic denoising library. The library has been designed to work with low rpp (ray per pixel) signals. NRD is a fast solution that slightly depends on input signals and environment conditions. NRD currently supports denoising of 3 signal types:
* Diffuse (with embedded ambient occlusion)
* Specular or reflections (with embedded specular occlusion)
* Shadows from an infinite light source (sun shadows, translucency information can be provided additionally)

NRD is distributed as source as well with a “ready-to-use” library (if used in a precompiled form).

It can easily be integrated into any DX12, VULKAN or even DX11 engines using 2 methods:
1. Native implementation of the NRD API using engine capabilities
2. Integration via an abstraction layer. In this case, the engine should expose native Graphics API pointers for certain types of objects. The integration layer, provided as a part of SDK, can be used to simplify this kind of integration

### Minimum Requirements
Any Ray Tracing compatible GPU. NVIDIA Ray Tracing compatible GPUs:
- RTX 3000 series
- RTX 2000 series
- GTX 1660 (Ti, S)
- GTX 1000 series (GPUs with at least 6GB of memory)

### How to Compile and Run
- Update Windows to the latest version (2004)
- Install latest Vulkan SDK
- Run ``Deploy.bat``
- Install required Windows SDK (you will be prompted)
- Open ```_Compiler\vs2017\SANDBOX.sln```
- Rebuild the solution
- It's recommended to install Smart Command Line Arguments Extension for Visual Studio https://marketplace.visualstudio.com/items?itemName=MBulli.SmartCommandlineArguments
  - In this case "Command Line Arguments" tab will contain all possible command line arguments for the NRD sample (API, resolution, scene selection...)
- Exit out of Visual Studio once completed
- Run the sample
  - Use Smart Command Line arguments or set this minimal command line ```--width=1920 --height=1080 --api=D3D12 --testMode --scene=Bistro/BistroInterior.fbx```
  - Or use ```TestNRD.bat``` to view the NRD sample application

### NRD sample usage
- Press MOUSE_RIGHT to move...
- W/S/A/D - move camera
- MOUSE_SCROLL - accelerate / decelerate
- F1 - hide UI toggle
- F2 - switch to the next denoiser (NRD => SVGF => ...)
- SPACE - animation pause toggle

## INTEGRATION VARIANTS

### Integration Method 1: Using the application-side Render Hardware Interface (RHI)
RHI must have the ability to do the following:
* Create shaders from precompiled binary blobs
* Create an SRV for a specific range of subresources (a real example from the library - SRV = mips { 1, 2, 3, 4 }, UAV = mip 0)
* Create and bind 4 predefined samplers
* Invoke a Dispatch call (no raster, no VS/PS)
* Create 2D textures with SRV / UAV access and formats - R32ui, RG32ui, R32f, RGBA16f, RG16f, RGBA8 (set of required texture formats can be changed in the future)

### Integration Method 2: Using native API pointers.

Engine or App → native objects → NRD integration layer → NRI → NRD

NRI = NVIDIA Rendering Interface - an abstraction layer on top of Graphics APIs: DX11, DX12 and VULKAN. NRI has been designed to provide low overhead access to the Graphics APIs and simplify development of DX12 and VULKAN applications. NRI API has been influenced by VULKAN as the common denominator among these 3 APIs.

NRI and NRD are developed and ready-to-use products. The application must expose native pointers only for Device, Resource and CommandList entities (no SRVs and UAVs - they are not needed, everything will be created internally). Native resource pointers are needed only for the denoiser inputs and outputs (all intermediate textures will be handled internally). Descriptor heap will be changed to an internal one, so the application needs to bind its original descriptor heap after invoking the denoiser.

In rare cases, when the integration via engine’s RHI is not possible and the integration using native pointers is complicated, a "DoDenoising" call can be added explicitly to the application-side RHI. It helps to avoid increasing code entropy.

## NRD TERMINOLOGY

* Denoiser method (or method) - a method for denoising of a particular signal (for example: diffuse)
* Denoiser - a set of methods aggregated into a monolithic denoiser (the library is free to rearrange passes without dependencies)
* Resource - an input, output or internal resource. Currently can only be a texture
* Texture pool (or pool) - a texture pool that stores permanent or transient resources needed for denoising. Textures from the permanent pool are dedicated to NRD and can not be reused by the application (history buffers are stored here). Textures from the transient pool can be reused by the application right after denoising. NRD doesn’t allocate anything. NRD provides resource descriptions, but resource creations are done on the application side.

## NRD API OVERVIEW

### API flow

1. GetLibraryDesc - contains general NRD library information (supported denoising methods, SPIRV binding offsets). This call can be skipped if this information is known in advance (for example, is diffuse denoiser available?), but it can’t be skipped if SPIRV binding offsets are needed for VULKAN
2. CreateDenoiser - creates a denoiser based on requested methods (it means that diffuse, specular and shadow logical denoisers can be merged into a single denoiser instance)
3. GetDenoiserDesc - returns descriptions for pipelines, static samplers, texture pools, constant buffer and descriptor set. All this stuff is needed during the initialization step. Commonly used for initialization.
4. SetMethodSettings - can be called to change parameters dynamically before applying the denoiser on each new frame / denoiser call
5. GetComputeDispatches - returns per-dispatch data (bound subresources with required state, constant buffer data)
6. DestroyDenoiser - destroys a denoiser

## HOW TO RUN DENOISING?

NRD doesn't make any graphics API calls. The application is supposed to invoke a set of compute Dispatch() calls to actually denoise input signals. Please, refer to Nrd::Denoise() and Nrd::Dispatch() calls in NRDIntegration.hpp file as an example of an integration using low level RHI.


NRD doesn’t have a "resize" functionality. On resolution change the old denoiser needs to be destroyed and a new one needs to be created with new parameters. Dynamic resolution handling is considered to be added in the future.

### NRD INPUTS

The following textures can be requested as inputs for a method. Brackets contain recommended precision:

* IN\_MV (RGBA16f+ or RG16f+) - surface motion (a common part of the g-buffer). MVs must be non-jittered, ```old = new + MV```
  - 3D world space motion (recommended). In this case, the alpha channel is unused and can be used by the app
  - 2D screen space motion

* IN\_NORMAL\_ROUGHNESS (RGBA8+) - xyz - normal in world space, unpacking ```normalize(x * 2 - 1)```, w - artistic roughness, where ```"artistic roughness" = sqrt( "mathematical roughness" )```

* IN\_VIEWZ (R32f) - .x - linear view depth ("+" for LHS, "-" for RHS)

* IN\_SHADOW (RG16f+), IN\_DIFFA (RGBA16f+), IN\_DIFFB (RGBA16f+), IN\_SPEC\_HIT (RGBA16f+) - main inputs for shadow, diffuse and specular methods respectively. These inputs should be prepared using the corresponding packing function from NRD.hlsl. Infinite (sky) pixels for shadow and diffuse must be cleared using corresponding NRD_INF_x macros
* IN\_TRANSLUCENCY - translucency to be used by NRD\_TRANSLUCENT\_SHADOW denoiser. There are two ways how it can be used (see OUT\_SHADOW)
  - ```final shadow = lerp( translucency, 1.0, shadow )``` (recommended)
  - ```final shadow = translucency * shadow```

### NRD OUTPUTS

* OUT\_SHADOW (R8+) - denoised shadow
  - R8 - for NRD\_SHADOW denoiser (.x - shadow)
  - RGBA8 - for NRD\_TRANSLUCENT\_SHADOW denoiser (.x - shadow, .yzw - translucency)

* OUT\_DIFF\_HIT (RGBA16f+) - .xyz - denoisied diffuse radiance, .w - denoised normalized hit distance

* OUT\_SPEC\_HIT (RGBA16f+) - .xyz - denoised specular radiance, .w - normalized hit distance

## RECOMMENDATIONS AND GOOD PRACTICES

Denoising is not a panacea or miracle. Denoising works best with ray tracing results produced by a suitable form of importance sampling. Additionally, NRD has its own restrictions. The following suggestions should help to achieve best image quality:

1. NRD has been designed to work with pure radiance coming from a particular direction. It means that BRDF should be applied **after** denoising:

	Denoising( DiffuseRadiance * Albedo ) → Denosing( DiffuseRadiance ) * Albedo
	Denoising( SpecularRadiance * BRDF( micro params ) ) → Denoising( SpecularRadiance ) * BRDF( macro params )

2. Importance sampling is recommended to achieve good results in case of complex lighting environments. Consider using:
   - Cosine distribution for diffuse from non-local light sources
   - VNDF sampling for specular
   - Custom importance sampling for local light sources

3. For diffuse and specular NRD expects hit distance input in normalized form. Some tweaking can be needed here, but in most cases normalization to 3-40 meters works well (can be roughness or view distance dependent). NRD outputs denoised normalized hit distance, which can be used by the application (see unpacking functions from ```NRD.hlsl```)

4. To avoid shadow shimmering blue noise can be used, it works best if the pattern is static on the screen

5. Low discrepancy sampling helps to have a cleaner output