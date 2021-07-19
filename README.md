# NVIDIA Real-time (Ray tracing) Denoisers v2.5.0

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
  - Shadows from multiple sources (experimental)

*NRD* is distributed as source as well with a “ready-to-use” library (if used in a precompiled form).
It can be integrated into any DX12, VULKAN or DX11 engine using 2 methods:
1. Native implementation of the *NRD* API using engine capabilities
2. Integration via an abstraction layer. In this case, the engine should expose native Graphics API
pointers for certain types of objects. The integration layer, provided as a part of SDK, can be used
to simplify this kind of integration.

## Build instructions
- Windows: install *WindowsSDK* and *VulkanSDK*
- Linux: install *VulkanSDK* or *DXC*
  - Compilation of *DXBC* and *DXIL* shaders is disabled on this platform
- Generate and build project using *CMake*

Or by running scripts only (*Windows* only):
- Run ``1-Deploy.bat``
- Run ``2-Build.bat``

### CMake options
`-DNRD_DISABLE_INTERPROCEDURAL_OPTIMIZATION=ON` - disable interprocedural optimization

`-DNRD_DISABLE_SHADER_COMPILATION=ON` - disable shader compilation (shaders can be built on another platform)

`-DNRD_STATIC_LIBRARY=ON` - build NRD as a static library

`-DNRD_DXC_CUSTOM_PATH="my/path/to/dxc"` - custom path to DXC

### Cross compilation
If target architecture is *ARM64*, set `NRD_CROSSCOMPILE_AARCH64=ON` in addition to setting correct cmake kit.

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

### Integration Method 3: White-box library (using the application-side Render Hardware Interface)

Logically it's close to the Method 1, but the integration takes place in the full source code
(only NRD project is needed). In this case NRD shaders are handled by the application shader
compilation pipeline. The application should still use NRD via NRD API to preserve forward
compatibility. This method suits best for compilation on other platforms (consoles, ARM),
unlocks NRD modification on the application side and increases portability.

NOTE: this method is WIP. It works, but in the future it will work better out of the box.

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
denoising and specular tracking. To simplify this, *REBLUR* suggests using ``REBLUR_FrontEnd_GetNormHitDist``
for hit distance normalization. This value needs to be passed into ``REBLUR_FrontEnd_PackRadiance``.
Normalization parameters should be passed into *NRD* as ``HitDistanceParameters`` for diffuse and specular
separately for internal hit distance de-normalization.

* **IN\_SHADOWDATA** (RG16f+) - *SIGMA* input, needs to be packed using a ``XXX_FrontEnd_PackShadow``
function from ``NRD.hlsl``. Infinite (sky) pixels must be cleared using ``XXX_INF_SHADOW`` macros

* **IN\_SHADOW\_TRANSLUCENCY** (RGBA8+) - ``.x`` - shadow, ``.yzw`` - translucency.
One variant if ``XXX_FrontEnd_PackShadow`` returns a merged ``float``.

### NRD OUTPUTS

NOTE: In some denoisers these textures can potentially be used as history buffers!

* **OUT\_SHADOW\_TRANSLUCENCY** (R8+ for SIGMA_SHADOW or RGBA8+ for SIGMA_TRANSLUCENT_SHADOW) - denoised shadow
``.x`` with optional translucency ``.yzw``. Must be unpacked using ``XXX_BackEnd_UnpackShadow`` function from
``NRD.hlsl``. Usage:

  ```
  shadowData = SIGMA_BackEnd_UnpackShadow( shadowData );

  float3 finalShadowCommon = lerp( shadowData.yzw, 1.0, shadowData.x ); // or
  float3 finalShadowExotic = shadowData.yzw * shadowData.x; // or
  float3 finalShadowMoreExotic = shadowData.yzw;
  ```

* **OUT\_DIFF\_HIT** (RGBA16f+) - ``.xyz`` - denoisied diffuse radiance, ``.w`` - denoised normalized
hit distance (will be already unpacked internally)

* **OUT\_SPEC\_HIT** (RGBA16f+) - ``.xyz`` - denoised specular radiance, ``.w`` - normalized hit
distance (will be already unpacked internally)

## SAMPLE APP

See README.md in *NRI samples* project.

## INTEGRATION GUIDE, RECOMMENDATIONS AND GOOD PRACTICES

Denoising is not a panacea or miracle. Denoising works best with ray tracing results produced by a
suitable form of importance sampling. Additionally, *NRD* has its own restrictions. The following
suggestions should help to achieve best image quality:

**[NRD]** NRD API has been designed to support integration into native VULKAN apps. If the RHI, you
work with, is DX11-like, not all fields will be needed.

**[NRD]** Read all comments from ``NRDDescs.h`` and ``NRD.hlsl``.

**[NRD]** If you are unsure of which parameters to use - use defaults.

**[NRD]** *NRD* works with linear roughness and world-space normals. See ``NRD.hlsl`` for more details
and supported customizations.

**[NRD]** *NRD* works with non-jittered matrices.

**[NRD]** The library doesn’t care about motion vectors much. For the first time pass all MVs set to 0
(you can use ``CommonSettings::motionVectorScale = {0}`` for this) and enable ``CommonSettings::worldSpaceMotion = true``,
*NRD* can track camera motion internally. Enable application provided MVs after getting denoising working.

**[NRD]** Using of 2D MVs can lead to massive history reset on fast moving objects, because 2D motion provides information
only about pixel screen position but not about real 3D world position. Consider using 3D MVs instead.

**[NRD]** *NRD* has been designed to work with pure radiance coming from a particular direction.
This means that data in the form "something / probability" should be avoided (but it doesn't mean
that it won't work), because spatial filtering passes can start to distribute wrong energy to the
neighboring pixels. Additionally, it means that BRDF should be applied **after** denoising:

    Denoising( DiffuseRadiance * Albedo ) → Denosing( DiffuseRadiance ) * Albedo
    Denoising( SpecularRadiance * BRDF( micro params ) ) → Denoising( SpecularRadiance ) * INTEGRATED_BRDF( macro params )

It's worth noting that *RELAX* has a better capability to preserve details in this case due to usage of
A-trous filter with luminance stoppers.

**[NRD]** Denoising logic is driven by provided hit distances. For REBLUR diffuse-only denoiser only hit
distance for the 1st bounce is needed. For specular denoisers just a length of the path can be used,
excluding distance to the primary hit. But this solution is suboptimal, better use ``NRD_GetCorrectedHitDist``
from ``NRD.hlsl``.

**[NRD]** Denoisers (currently only *REBLUR*) can perform optional color compression in spatial filtering passes to improve
overall IQ by sacrificing energy correctness a bit. For better IQ HDR inputs need to be in a sane range (0 - 10 / 100).
Color compression mode is roughness based and can be tuned (or turned off) in ``NRD.hlsl``.

**[NRD]** Importance sampling is recommended to achieve good results in case of complex
lighting environments. Consider using:
   - Cosine distribution for diffuse from non-local light sources
   - VNDF sampling for specular
   - Custom importance sampling for local light sources (*RTXDI*)

**[NRD]** It's a good idea to run a *spatial reuse* pass on noisy inputs before passing
them to a denoiser (*Tomasz Stachowiak - Stochastic all the things: raytracing in hybrid real-time rendering*
https://youtu.be/MyTOGHqyquU?t=1015).

**[NRD]** Hit distances should come from an importance sampling method. But if in case of
*REBLUR*, for example, denoising of AO and a custom direct / indirect lighting is needed, AO
can come from cos-weighted sampling and radiance can be computed by a different method
in a tradeoff of IQ.

**[NRD]** Low discrepancy sampling helps to have a more stable output. The sample has a macro
switch declared in ``09_Resources.hlsl`` named ``USE_BLUE_NOISE``. You can experiment with it.

**[NRD]** If history reset is needed set ``CommonSettings::frameIndex`` to 0 for a single frame. Currently NRD
is not responsible for clearing of most important resources. If a history buffer has a NAN / INF it
can be propagated to neighboring pixels, filling the entire texture with "bad" values forever in a few frames.
This behavior varies per denoiser. NRD is NAN / INF free, but good initial state of history buffers should be
provided from the application side. This might be improved in the future.

**[NRD]** Functions ``XXX_FrontEnd_PackRadiance`` perform optional NAN / INF clearing of the input signal.
There is a boolean to skip this checks.

**[NRD]** The number of accumulated frames in fast history needs to be carefully tuned to avoid introducing
significant bias and dirt. Initial integration should be done by setting ``maxFastAccumulatedFrameNum`` to
``maxAccumulatedFrameNum``.

**[REBLUR]** In case of *REBLUR* ensure that ``CommonSettings::forceReferenceAccumulation = true`` works properly
first. It's not mandatory needed, but will help to simplify debugging of potential issues by implicitly
disabling spatial filtering entirely.

**[REBLUR]** For diffuse and specular *REBLUR* expects hit distance input in a normalized form.
To avoid mismatching ``REBLUR_FrontEnd_GetNormHitDist`` should be used for normalization. Some
tweaking can be needed here, but in most cases normalization to the default ``HitDistanceParameters``
works well. *REBLUR* outputs denoised normalized hit distance, which can be used by the application
as ambient or specular occlusion (AO & SO) (see unpacking functions from ``NRD.hlsl``).

**[REBLUR]** *REBLUR* handles specular lobe trimming, trying to reconstruct trimmed signal.
Similarly to hit distance normalization, *REBLUR* needs to be aware about trimming parameters.
If this feature is used in a ray tracer, ``LobeTrimmingParameters`` must be passed into *REBLUR*.
To avoid code duplication ``NRD_GetTrimmingFactor`` can be used in a shader code on the application side.

**[REBLUR]** Intensity antilag parameters need to be carefully tuned. Initial integration should work with
intensity antilag turned off.

**[RELAX]** Works incredibly well with signals produced by RTXDI or very clean high RPP signals.
The Sweet Home of *RELAX* is *RTXDI* sample. Please, consider getting familiar with this application.

**[SIGMA]** To avoid shadow shimmering blue noise can be used, it works best if the pattern is static on the screen.
Additionally, ``blurRadiusScale`` can be set to 2-4 to mitigate such problems in complicated cases.

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
