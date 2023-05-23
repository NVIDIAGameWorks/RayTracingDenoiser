# HOW TO UPDATE?

This guide has been designed to simplify migration to a newer version.

## To v3.0
*(based on v2.12)*
- `NRD_USE_MATERIAL_ID_AWARE_FILTERING` renamed to `NRD_USE_MATERIAL_ID`
- `NRD_NORMAL_ENCODING` simplified to `NRD_NORMAL_ENCODING_UNORM` and `NRD_NORMAL_ENCODING_OCT`
- `NRD_MATERIAL_ID_AWARE_FILTERING` renamed to `NRD_USE_MATERIAL_ID`
- changed parameters in `NRD_GetCorrectedHitDist`
- *REFERENCE*:
  - denoiser input & output changed to dedicated `IN_RADIANCE` and `OUT_RADIANCE`
- *REBLUR*:
  - settings collapsed to `ReblurSettings` shared across all *REBLUR* denoisers
  - `LobeTrimmingParameters` renamed to `SpecularLobeTrimmingParameters`
  - `normalWeightStrictness` replaced by `lobeAngleFraction` (a different value with similar meaning)
  - `materialMask` replaced with `enableMaterialTestForDiffuse` and `enableMaterialTestForSpecular`
  - exposed `minConvergedStateBaseRadiusScale`
  - exposed `roughnessFraction`
  - exposed `responsiveAccumulationRoughnessThreshold`
  - exposed `inputMix`
  - exposed `enablePerformanceMode`
- *RELAX*:
  - `rejectDiffuseHistoryNormalThreshold` renamed to `diffuseHistoryRejectionNormalThreshold`
  - `enableSkipReprojectionTestWithoutMotion` renamed to `enableReprojectionTestSkippingWithoutMotion`
  - `phiNormal` replaced by `diffuseLobeAngleFraction` (a different value with similar meaning)
  - exposed `roughnessFraction`
  - exposed `enableMaterialTest`

## To v3.1

*NRD* requires explicit definitions of `NRD_USE_OCT_NORMAL_ENCODING` and `NRD_USE_MATERIAL_ID` to avoid a potential confusion when NRD was compiled using one set of values, but an application uses other values. The *CMake* file has been modified to simplify *NRD* integration into projects, where it is used as a *Git* submodule. Now these macro definitions are exposed as *Cmake* options.

## To v3.2

- removed `PrePassMode`
- added `maxSupportedMaterialBitNum` and `isCompiledWithOctPackNormalEncoding` to `LibraryDesc`
- added optional `worldPrevToWorldMatrix` to `CommonSettings`
- *REBLUR*:
  - exposed `diffusePrepassBlurRadius` and `specularPrepassBlurRadius`
  - exposed `enableAdvancedPrepass` matching previously used `PrePassMode::ADVANCED`
  - exposed `enableHitDistanceReconstruction`
- *RELAX*:
  - removed `diffuseHistoryRejectionNormalThreshold`
  - removed `minLuminanceWeight` and added separated `diffuseMinLuminanceWeight` and `specularMinLuminanceWeight`
  - exposed `enableSpecularHitDistanceReconstruction`

## To v3.3

- exposed `HitDistanceReconstructionMode`
- *REBLUR*:
  - `enableHitDistanceReconstruction` replaced with `hitDistanceReconstructionMode`
  - removed `residualNoiseLevel`
- *RELAX*:
  - `enableSpecularHitDistanceReconstruction` replaced with `hitDistanceReconstructionMode`

## To v3.4

- exposed `REBLUR_DIFFUSE_SH`, `REBLUR_SPECULAR_SH` and `REBLUR_DIFFUSE_SPECULAR_SH` denoisers
- *NRD.hlsli*:
  - `REBLUR_FrontEnd_PackRadianceAndHitDist` renamed to `REBLUR_FrontEnd_PackRadianceAndNormHitDist`
  - `REBLUR_FrontEnd_PackDirectionAndHitDist` renamed to `REBLUR_FrontEnd_PackDirectionAndNormHitDist`
  - `REBLUR_BackEnd_UnpackRadianceAndHitDist` renamed to `REBLUR_BackEnd_UnpackRadianceAndNormHitDist`
  - `REBLUR_BackEnd_UnpackDirectionAndHitDist` renamed to `REBLUR_BackEnd_UnpackDirectionAndNormHitDist`
  - `RELAX_BackEnd_UnpackRadianceAndHitDist` renamed to `RELAX_BackEnd_UnpackRadiance`
  - removed `NRD_GetCorrectedHitDist`
- *API*:
  - exposed miscellaneous function `GetMethodString`
- *REBLUR*:
  - removed `inputMix`
- *RELAX*:
  - exposed `confidenceDrivenRelaxationMultiplier`
  - exposed `confidenceDrivenLuminanceEdgeStoppingRelaxation`
  - exposed `confidenceDrivenNormalEdgeStoppingRelaxation`

## To v3.5

- *NRD.hlsli*:
  - `REBLUR_FrontEnd_PackDirectionAndNormHitDist` renamed to `REBLUR_FrontEnd_PackDirectionalOcclusion`
- *REBLUR*:
  - exposed `historyFixFrameNum`
  - exposed `historyFixStrideBetweenSamples`
  - removed `historyFixStrength`
- *RELAX*:
  - `disocclusionFixEdgeStoppingNormalPower` renamed to `historyFixEdgeStoppingNormalPower`
  - `disocclusionFixMaxRadius` renamed to `historyFixStrideBetweenSamples`
  - `disocclusionFixNumFramesToFix` renamed to `historyFixFrameNum`

## To v3.6

- *REBLUR*:
  - Removed `SpecularLobeTrimmingParameters`
- *NRD.hlsli*:
  - Removed `NRD_GetTrimmingFactor`

## To v3.7

`NRD_USE_OCT_NORMAL_ENCODING` and `NRD_USE_MATERIAL_ID` replaced with more explicit `NRD_NORMAL_ENCODING` and `NRD_ROUGHNESS_ENCODING` to offer more encoding variants out of the box.

- *API*:
  - removed `maxSupportedMaterialBitNum` and `isCompiledWithOctPackNormalEncoding` from `LibraryDesc`
  - added `normalEncoding` and `roughnessEncoding` to `LibraryDesc`
- *REBLUR*:
  - Removed `enableAdvancedPrepass`

## To v3.8

Introduced optional `NRDEncoding.hlsli` file which can be included prior `NRD.hlsli` to properly setup NRD encoding for usage outside of NRD project, if encoding variants are not set via Cmake parameters. Also introduced optional `IN_DISOCCLUSION_THRESHOLD_MIX` which allows to smoothly mix `CommonSettings::disocclusionThreshold` (0) into `CommonSettings::disocclusionThresholdAlternate` (1).

- *API*:
  - Exposed `IN_DISOCCLUSION_THRESHOLD_MIX`, `CommonSettings::disocclusionThresholdAlternate` & `CommonSettings::isDisocclusionThresholdMixAvailable`
- *REBLUR*:
  - Removed `minConvergedStateBaseRadiusScale`
  - Removed `maxAdaptiveRadiusScale`

## To v3.9

Introduced optional `OUT_VALIDATION` output, which contains debug visualization layer if `CommonSettings::enableValidation = true`.

- *API*:
  - Removed `DenoiserCreationDesc::enableValidation`
  - Exposed `CommonSettings::enableValidation`
  - Extened `CommonSettings::motionVectorScale` to 3 floats

## To v4.0

Since *NRD* tracks specular motion, now, if requested, it can modify provided *diffuse*-like motion in `IN_MV` with internally computed *specular*-like motion if specularity is high. For this purpose an optional `IN_BASECOLOR_METALNESS` input has been added. This feature improves behavior of spatio-temporal upscalers, like *TAA* or *DLSS*.

- *API*:
  - Introduced `CommonSettings::isBaseColorMetalnessAvailable`
  - Reworked `DenoiserDesc` to clearly indicate that there are 3 types of resources each of which "sits" in a predefined `space` (`set` in *VK*):
    - constant buffer - binding is shared across all pipelines
    - samplers - bindings are shared across all pipelines
    - resources - bindings vary per pipeline
  - Name changes:
    - `isHistoryConfidenceInputsAvailable` => `isHistoryConfidenceAvailable`
    - `Resource` => `ResourceDesc`
    - `DescriptorRangeDesc` => `ResourceRangeDesc`
    - `ComputeShader` => `ComputeShaderDesc`
    - `DescriptorSetDesc` => `DescriptorPoolDesc`
    - all `fooNum` => `foosNum`
  - `GetComputeDispatches` return type changed to `void`
- *REBLUR*:
  - Exposed `specularProbabilityThresholdsForMvModification` to control diffuse / specular motion mixing
- *RELAX*:
  - Removed `enableSpecularVirtualHistoryClamping`

## To v4.1

SH (spherical harmonics) have been replaced with SG (spherical gaussians). It unlocks physically-based high quality diffuse & specular resolve. The following functions have been added to `NRD.hlsli` (or renamed):
- `NRD_SG_ExtractColor` - extracts unresolved denoised radiance
- `NRD_SG_ExtractDirection` - extracts light dominant direction
- `NRD_SG_ExtractRoughnessAA` - extracts modified roughness (increased in areas with high normal variance)
- `NRD_SG_ResolveDiffuse` - reconstructs diffuse macro details
- `NRD_SG_ResolveSpecular` - reconstructs specular macro details
- `NRD_SG_ReJitter` - reconstructs diffuse & specular micro details (resurrects jitter vanished out after denoising)
- `NRD_SH_ResolveDiffuse` - reconstructs diffuse macro details using SH resolve (for comparison only)
- `NRD_SH_ResolveSpecular` - reconstructs specular macro details using SH resolve (for comparison only)

## To v4.2

A single NRD instance can now include any combination of denoisers, including repeating ones (for example, `RELAX_DIFFUSE`, `RELAX_DIFFUSE` and `SIGMA_SHADOW`). Transient memory pool is internally optimized to reduce memory consumption.

- *API*:
  - Introduced `Identifier` to distinguish one denoiser in the instance from another one
  - Introduced `SetCommonSettings`
  - `GetComputeDispatches` now expects a list of identifiers, specifying which denoisers collect dispatches for
  - Added explicit `CommonSettings::cameraJitterPrev` and `CommonSettings::resolutionScalePrev` (since `SetCommonSettings` can be called several times per frame)
  - Name changes (including sub-name usages):
    - `Denoiser` => `Instance`
    - `Method` => `Denoiser`
    - `fullResolutionWidth` => `renderWidth`
    - `fullResolutionHeight` => `renderHeight`
- *NRD INTEGRATION*:
  - Introduced `NewFrame`
  - Introduced `SetCommonSettings`
