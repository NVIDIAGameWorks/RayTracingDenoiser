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
