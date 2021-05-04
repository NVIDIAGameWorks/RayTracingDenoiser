# NVIDIA Real-time (Ray tracing) Denoisers v2.1.0

## QUICK START GUIDE

### Intro
*NVIDIA Real-Time Denoisers (NRD)* is a spatio-temporal API agnostic denoising library.
The library has been designed to work with low rpp (ray per pixel) signals (1 rpp and 0.5 rpp).
*NRD* is a fast solution that slightly depends on input signals and environment conditions.

*NRD* includes the following denoisers:
- *REBLUR* - recurrent blur based denoiser
- *RELAX* - SVGF based denoiser using clamping to fast history to minimize temporal lag,
has been designed for *RTXDI (RTX Direct Illumination)*
- *SIGMA* - shadow-only denoiser

Supported signal types:
- *RELAX*:
  - Diffuse
  - Specular
- *REBLUR*:
  - Diffuse + ambient occlusion (AO)
  - Specular + specular occlusion (SO)
- *SIGMA*:
  - Shadows from an infinite light source (sun, moon)
  - Shadows from a local light source (omni, spot)
  - Shadows from multiple sources

*NRD* is distributed as source as well with a “ready-to-use” library (if used in a precompiled form).
It can be integrated into any DX12, VULKAN or DX11 engine using 2 methods:
1. Native implementation of the *NRD* API using engine capabilities
2. Integration via an abstraction layer. In this case, the engine should expose native Graphics API
pointers for certain types of objects. The integration layer, provided as a part of SDK, can be used
to simplify this kind of integration.

### Minimum Requirements
Any Ray Tracing compatible GPU:
- RTX 3000 series
- RTX 2000 series
- GTX 1660 (Ti, S)
- GTX 1000 series (GPUs with at least 6GB of memory)
- AMD RX 6000 series

### How to Compile and Run
- Update *Windows* to the latest version (it's a requirement for the sample framework)
- Install latest *Vulkan SDK*
- Set the variable ``WINSDK_PATH`` in the ``CompileShaders\CompileShader.bat`` file to where
you installed your Windows SDK
- Run ``1-Deploy.bat`` and choose *Visual Studio* version
- Install required *Windows SDK* (you will be prompted)
- Open ``_Compiler\vs(2017/2019)\SANDBOX.sln``
- Rebuild the solution
- It's recommended to install *Smart Command Line Arguments Extension for Visual Studio*
https://marketplace.visualstudio.com/items?itemName=MBulli.SmartCommandlineArguments.
In this case "Command Line Arguments" tab will contain all possible command line arguments
for the *NRD* sample (API, resolution, scene selection...)
- Exit out of *Visual Studio* once completed
- Run the sample
  - Use *Smart Command Line arguments* or set this minimal command line ``--width=1920 --height=1080 --api=D3D12 --testMode --scene=Bistro/BistroInterior.fbx``
  - Or use ``3b-Run NRD Sample.bat`` to view the *NRD* sample application

If you only need to run the sample and don't want to interact with the code:
- Update *Windows* to the latest version (2004)
- Install latest *Vulkan SDK*
- Set the variable ``WINSDK_PATH`` in the ``CompileShaders\CompileShader.bat`` file to where you installed your Windows SDK
- Run ``1-Deploy.bat`` and choose *Visual Studio* version
- Install required *Windows SDK* (you will be prompted)
- Run ``2-Build.bat``
- Run ``3b-Run NRD Sample.bat`` to view the *NRD* sample application

Notes:
- to recompile with modified shaders, you need to rebuild *CompileShaders* project, then build the entire solution
- to reload shaders in the running application, you need to rebuild *CompileShaders* project, then press ``ReloadShaders`` button in the UI

### NRD SDK package generation
- Compile the solution (*Debug* / *Release* or both, depending on what you want to get in *NRD* package)
- Compile the solution (at least one configuration)
- If you plan to use *NRD integration* layer, run ``4a-Prepare NRI SDK.bat``
- Run ``4b-Prepare NRD SDK.bat``
- Grab generated in the root directory ``_NRD_SDK`` folder and use it in your project

### NRD sample usage
- Press MOUSE_RIGHT to move...
- W/S/A/D - move camera
- MOUSE_SCROLL - accelerate / decelerate
- F1 - hide UI toggle
- F2 - switch to the next denoiser (*REBLUR* => *RELAX* => ..., *SIGMA* is the only denoiser for shadows)
- SPACE - pause animation toggle

Notes:
- RELAX doesn't support the checkerboard mode (0.5 rpp input). If RELAX is the current denoiser, ``0.5 rpp`` setting in the UI will be automatically unckecked
- RELAX doesn't support AO / SO denoising. If RELAX is the current denoiser, ambient term will be automatically ignored, bypassing settings in the UI
In such cases the default behavior can be returned by pressing the ``Default settings`` button or choosing a new test, if ``--testMode`` is set in the command line.

## INTEGRATION VARIANTS

### Integration Method 1: Using the application-side Render Hardware Interface (RHI)
RHI must have the ability to do the following:
* Create shaders from precompiled binary blobs
* Create an SRV for a specific range of subresources (a real example from the library - SRV = mips { 1, 2, 3, 4 }, UAV = mip 0)
* Create and bind 4 predefined samplers
* Invoke a Dispatch call (no raster, no VS/PS)
* Create 2D textures with SRV / UAV access

### Integration Method 2: Using native API pointers

If Graphics API's native pointers are retrievable from the RHI, the standard *NRD*
integration layer can be used to greatly simplify the integration. In this case,
the application should only wrap up native pointers for the *Device*, *CommandList* and some
input / output *Resources* into entities, compatible with an API abstraction layer (*NRI*),
and all work with *NRD* library will be hidden inside the integration layer:

*Engine or App → native objects → NRD integration layer → NRI → NRD*

*NRI = NVIDIA Rendering Interface* - an abstraction layer on top of Graphics APIs: DX11,
DX12 and VULKAN. *NRI* has been designed to provide low overhead access to the Graphics APIs
and simplify development of DX12 and VULKAN applications. *NRI* API has been influenced by
VULKAN as the common denominator among these 3 APIs.

*NRI* and *NRD* are ready-to-use products. The application must expose native pointers only for
Device, Resource and CommandList entities (no SRVs and UAVs - they are not needed, everything
will be created internally). Native resource pointers are needed only for the denoiser inputs
and outputs (all intermediate textures will be handled internally). Descriptor heap will be
changed to an internal one, so the application needs to bind its original descriptor heap
after invoking the denoiser.

In rare cases, when the integration via engine’s RHI is not possible and the integration
using native pointers is complicated, a "DoDenoising" call can be added explicitly to the
application-side RHI. It helps to avoid increasing code entropy.

## NRD TERMINOLOGY

* *Denoiser method (or method)* - a method for denoising of a particular signal (for example: diffuse)
* *Denoiser* - a set of methods aggregated into a monolithic entity (the library is free to rearrange passes without dependencies)
* *Resource* - an input, output or internal resource. Currently can only be a texture
* *Texture pool (or pool)* - a texture pool that stores permanent or transient resources
needed for denoising. Textures from the permanent pool are dedicated to NRD and can not be
reused by the application (history buffers are stored here). Textures from the transient
pool can be reused by the application right after denoising. NRD doesn’t allocate anything.
*NRD* provides resource descriptions, but resource creations are done on the application side.

## NRD API OVERVIEW

### API flow

1. *GetLibraryDesc* - contains general *NRD* library information (supported denoising methods,
SPIRV binding offsets). This call can be skipped if this information is known in advance (for example,
is diffuse denoiser available?), but it can’t be skipped if SPIRV binding offsets are needed for VULKAN
2. *CreateDenoiser* - creates a denoiser based on requested methods (it means that diffuse,
specular and shadow logical denoisers can be merged into a single denoiser instance)
3. *GetDenoiserDesc* - returns descriptions for pipelines, static samplers, texture pools,
constant buffer and descriptor set. All this stuff is needed during the initialization step. Commonly used for initialization.
4. *SetMethodSettings* - can be called to change parameters dynamically before applying the denoiser on each new frame / denoiser call
5. *GetComputeDispatches* - returns per-dispatch data (bound subresources with required state, constant buffer data)
6. *DestroyDenoiser* - destroys a denoiser

## HOW TO RUN DENOISING?

*NRD* doesn't make any graphics API calls. The application is supposed to invoke a set of compute
Dispatch() calls to actually denoise input signals. Please, refer to Nrd::Denoise() and Nrd::Dispatch()
calls in NRDIntegration.hpp file as an example of an integration using low level RHI.

*NRD* doesn’t have a "resize" functionality. On resolution change the old denoiser needs to be destroyed
and a new one needs to be created with new parameters.

NOTE: ``XXX`` below is a replacement for a denoiser you choose from *REBLUR*, *RELAX* or *SIGMA*.

### NRD INPUTS

The following textures can be requested as inputs for a method. Brackets contain recommended precision:

* **IN\_MV** (RGBA16f+ or RG16f+) - surface motion (a common part of the g-buffer). MVs must be non-jittered, ``old = new + MV``
  - 3D world space motion (recommended). Camera motion should not be included (it's already in the matrices).
  In other words, if there are no moving objects all motion vectors = 0. The alpha channel is unused and can be used by the app
  - 2D screen space motion

* **IN\_NORMAL\_ROUGHNESS** (RGBA8+ or R10G10B10A2+ depending on encoding) - ``.xyz`` - normal in world space,
``.w`` - roughness. Normal and roughness encoding can be controlled by the following macros located in ``NRD.hlsl``:
  - NRD_USE_SQRT_LINEAR_ROUGHNESS = 0 - roughness is ``linearRoughness = sqrt( mathematicalRoughness )``
  - NRD_USE_SQRT_LINEAR_ROUGHNESS = 1 - roughness is ``sqrt( linearRoughness )``
  - NRD_USE_OCT_PACKED_NORMALS = 0 - normal unpacking is ``normalize( .xyz * 2 - 1 )``
  - NRD_USE_OCT_PACKED_NORMALS = 1 - normals are octahedron packed

* **IN\_VIEWZ** (R32f) - ``.x`` - linear view depth (viewZ, not HW depth)

* **IN\_DIFF_HIT** (RGBA16f+), **IN\_SPEC\_HIT** (RGBA16f+) - main inputs for diffuse and specular methods
respectively. These inputs should be prepared using the ``XXX_FrontEnd_PackRadiance`` function from ``NRD.hlsl``.
It is recommended to clear infinite (sky) pixels using corresponding ``XXX_INF_DIFF / XXX_INF_SPEC`` macros.
*REBLUR* denoises AO and SO for free, but at the same time real hit distances are needed for controlling
denoising and specular tracking. To simplify this *REBLUR* suggests using ``REBLUR_FrontEnd_GetNormHitDist``
for hit distance normalization. This value needs to be passed in to ``REBLUR_FrontEnd_PackRadiance`` along
with normalization parameters for further compression. Normalization parameters should be passed into *NRD*
as ``HitDistanceParameters`` for diffuse and specular separately for internal use

* **IN\_SHADOWDATA** (RG16f+) - *SIGMA* input, needs to be packed using a ``XXX_FrontEnd_PackShadow``
function from ``NRD.hlsl``. Infinite (sky) pixels must be cleared using ``XXX_INF_SHADOW`` macros

* **IN\_SHADOW\_TRANSLUCENCY** (R8+ for SIGMA_SHADOW or RGBA8+ for SIGMA_TRANSLUCENT_SHADOW) - ``.x`` - shadow.
``.yzw`` - optional translucency. ``XXX_FrontEnd_PackShadow`` also returns ``shadow``, which needs to be placed
into ``.x`` channel. There are several ways how translucency can be used:

  ```
  finalShadow = lerp( translucency, 1.0, shadow ); // or
  finalShadow = translucency * shadow; // or
  finalShadow = translucency;
  ```

### NRD OUTPUTS

* **OUT\_SHADOW\_TRANSLUCENCY** (R8+ for SIGMA_SHADOW or RGBA8+ for SIGMA_TRANSLUCENT_SHADOW) - denoised
shadow with optional translucency. Must be unpacked using ``XXX_BackEnd_UnpackShadow`` function from ``NRD.hlsl``

* **OUT\_DIFF\_HIT** (RGBA16f+) - ``.xyz`` - denoisied diffuse radiance, ``.w`` - denoised normalized
hit distance (will be already unpacked internally)

* **OUT\_SPEC\_HIT** (RGBA16f+) - ``.xyz`` - denoised specular radiance, ``.w`` - normalized hit
distance (will be already unpacked internally)

## RECOMMENDATIONS AND GOOD PRACTICES

Denoising is not a panacea or miracle. Denoising works best with ray tracing results produced by a
suitable form of importance sampling. Additionally, *NRD* has its own restrictions. The following
suggestions should help to achieve best image quality:

1. **[NRD]** Read all comments from ``NRDDescs.h`` and ``NRD.hlsl``

2. **[NRD]** If you are unsure of which parameters to use - use defaults

3. **[NRD]** The library doesn’t care about motion vectors much. For the first time pass all MVs set to 0
(you can use ``CommonSettings::motionVectorScale = {0}`` for this) and enable ``CommonSettings::worldSpaceMotion = true``,
*NRD* can track camera motion internally. Enable application provided MVs after getting denoising working

4. **[NRD]** *NRD* has been designed to work with pure radiance coming from a particular direction.
This means that data in the form "something / probability" should be avoided (but it doesn't mean
that it won't work), because spatial filtering passes can start to distribute wrong energy to the
neighboring pixels. Additionally, it means that BRDF should be applied **after** denoising:

    Denoising( DiffuseRadiance * Albedo ) → Denosing( DiffuseRadiance ) * Albedo

    Denoising( SpecularRadiance * BRDF( micro params ) ) → Denoising( SpecularRadiance ) * INTEGRATED_BRDF( macro params )

5. **[NRD]** Importance sampling is recommended to achieve good results in case of complex
lighting environments. Consider using:
   - Cosine distribution for diffuse from non-local light sources
   - VNDF sampling for specular
   - Custom importance sampling for local light sources (*RTXDI*)

6. **[NRD]** It's a good idea to run a *spatial reuse* pass on noisy inputs before passing
them to a denoiser.

7. **[NRD]** Hit distances should come from an importance sampling method. But if in case of
*REBLUR*, for example, denoising of AO and a custom direct / indirect lighting is needed, AO
can come from cos-weighted sampling and radiance can be computed by a different method
in a tradeoff of IQ.

8. **[NRD]** Low discrepancy sampling helps to have a more stable output. The sample has a macro
switch declared in ``09_Resources.hlsl`` named ``USE_BLUE_NOISE``. You can experiment with it.

9. **[NRD]** If history reset is needed set ``CommonSettings::frameIndex`` to 0 for a single frame

10. **[NRD]** Functions ``XXX_FrontEnd_PackRadiance`` apply Reinhard-like color compression
to the provided radiance. It assumes that the input is in HDR range (less than 1 = LDR,
greater than 1 = HDR). The efficiency of compression is reduced if the input is in [0; 1] range.
But if the latter is true, for *NRD* needs the input radiance can be pre-multiplied with a known
constant and divided back after denoising.

11. **[REBLUR]** In case of *REBLUR* ensure that ``CommonSettings::forceReferenceAccumulation = true`` works properly

12. **[REBLUR]** For diffuse and specular *REBLUR* expects hit distance input in a normalized form.
To avoid mismatching ``REBLUR_FrontEnd_GetNormHitDist`` should be used for normalization. Some
tweaking can be needed here, but in most cases normalization to the default ``HitDistanceParameters``
works well. *REBLUR* outputs denoised normalized hit distance, which can be used by the application
as ambient or specular occlusion (AO & SO) (see unpacking functions from ``NRD.hlsl``)

13. **[REBLUR]** *REBLUR* handles specular lobe trimming, trying to reconstruct trimmed signal.
Similarly to hit distance normalization, *REBLUR* needs to be aware about trimming parameters.
If this feature is used in a ray tracer, ``LobeTrimmingParameters`` must be passed into *REBLUR*.
To avoid code duplication ``NRD_GetTrimmingFactor`` can be used in a shader code on the application side.

14. **[RELAX]** Works incredibly well with signals produced by RTXDI or very clean high RPP signals.
The Sweet Home of *RELAX* is *RTXDI* sample. Please, consider getting familiar with this application.

15. **[SIGMA]** To avoid shadow shimmering blue noise can be used, it works best if the pattern is static on the screen

16. **[SIGMA]** *SIGMA_TRANSLUCENT_SHADOW* can be used for denoising of shadows from multiple light sources:

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
    ```
    float3 Lsum = 0;
    float2x3 multiLightShadowData = SIGMA_FrontEnd_MultiLightStart( );

    for( uint i = 0; i < N; i++ )
    {
        float3 L = ComputeLighting( i );
        Lsum += L;

        // "distanceToOccluder" should respect rules described in NRD.hlsl in "INPUT PARAMETERS" section
        float distanceToOccluder = SampleShadow( i );

        // The weight should be zero if a pixel is not in penumbra, but it is not trivial to compute...
        float weight = ...;

        SIGMA_FrontEnd_MultiLightUpdate( L, distanceToOccluder, tanOfLightAngularRadius, weight, multiLightShadowData );
    }

    float4 shadowTranslucency;
    float2 shadowData = SIGMA_FrontEnd_MultiLightEnd( viewZ, multiLightShadowData, Lsum, shadowTranslucency );
    ```

    After denoising final result can be computed as:

    *&Sigma;( L[i] &times; S[i] )* = *&Sigma;( L[i] )* &times; *OUT_SHADOW_TRANSLUCENCY.yzw*

Is this a biased solution? If spatial filtering is off - no, because we just reorginized the math equation.
If spatial filtering is on - yes, because denoising will be driven by most important light in a given pixel.

**This solution is limited** and hard to use:
- obviously, can be used "as is" if shadows don't overlap (*weight* = 1)
- if shadows overlap, a separate pass is needed to analyze noisy input and classify pixels as *umbra* - *penumbra* (and optionally *empty space*). Raster shadow maps can be used for this if available
- it is not recommended to mix 1 cd and 100000 cd lights, since FP32 texture will be needed for weighted sum. In this case, it's better to process the sun and other bright light sources separately
