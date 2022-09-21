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
