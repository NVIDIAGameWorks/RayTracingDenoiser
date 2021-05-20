/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nrd
{
    // IMPORTANT: default values assume that "meter" is the primary measurement unit. If other units are used, values marked as "m" need to be adjusted.
    // NRD inputs (viewZ, hit distance) can be scaled instead of input settings.

    /*
    Checkerboard modes:
    1. Internally, NRD uses the following sequence based on "CommonSettings::frameIndex":
        Even frame (0)  Odd frame (1)   ...
            B W             W B
            W B             B W
        BLACK and WHITE modes define cells with VALID data
    2. Checkerboard can be only horizontal
    3. Notes:
        - all inputs have the same resolution - logical FULL resolution
        - the noisy input signal is tightly packed to the LEFT HALF of the texture (the input pixel = 2x1 screen pixel)
        - for others, the input pixel = 1x1 screen pixel
        - upsampling will be handled internally in checkerboard mode
    */
    enum class CheckerboardMode : uint32_t
    {
        OFF,
        BLACK,
        WHITE,

        MAX_NUM
    };

    /*
    Common / shared settings for all methods
      Matrix requirements:
        -usage - vector is a column
        -layout - column-major
        -non jittered!
    */
    struct CommonSettings
    {
        float viewToClipMatrix[16] = {};
        float viewToClipMatrixPrev[16] = {};
        float worldToViewRotationMatrix[16] = {};       // translation is not used
        float worldToViewRotationMatrixPrev[16] = {};   // translation is not used
        float cameraMotion[3] = {};                     // previous - current
        float motionVectorScale[2] = {1.0f, 1.0f};      // if "worldSpaceMotion = true" will be used as "MV * motionVectorScale.xyy"
        float cameraJitter[2] = {};                     // [-0.5; 0.5] sampleUv = pixelUv + cameraJitter
        uint32_t inputDataOrigin[2] = {};
        float resolutionScale = 1.0f;                   // scaled resolution will be "rounded" to the nearest integer
        float timeDeltaBetweenFrames = 0.0f;            // (ms) > 0 - user provided, otherwise - tracked internally
        float denoisingRange = 10000.0f;                // (m) > 0
        float disocclusionThreshold = 0.01f;            // normalized %
        float splitScreen = 0.0f;                       // [0; 1) enables "noisy input / denoised output" comparison
        float debug = 0.0f;
        uint32_t frameIndex = 0;                        // pass 0 for a single frame to reset history
        bool worldSpaceMotion = false;                  // if "true" IN_MV is 3D motion in world space (0 should be everywhere if the scene is static), otherwise it's 2D screen-space motion (0 should be everywhere if the camera doesn't move)
        bool forceReferenceAccumulation = false;        // can be used in various ways depending on the denoiser, but implies that spatial filtering will be turned off
    };

    // "Normalized hit distance" = saturate( "hit distance" / f ), where:
    // f = ( A + viewZ * B ) * lerp( 1.0, C, exp2( D * roughness ^ 2 ) ), see "NRD.hlsl/REBLUR_FrontEnd_CompressHitDistance"
    struct HitDistanceParameters
    {
        float A = 3.0f;     // constant value (m)
        float B = 0.1f;     // viewZ based linear scale (m / units) (1 m - 10 cm, 10 m - 1 m, 100 m - 10 m)
        float C = 10.0f;    // roughness based scale, "> 1" to get bigger hit distance for low roughness
        float D = -25.0f;   // roughness based exponential scale, "< 0", absolute value should be big enough to collapse "exp2( D * roughness ^ 2 )" to "~0" for roughness = 1
    };

    // Optional specular lobe trimming = A * smoothstep( B, C, roughness )
    // Recommended settings if lobe trimming is needed = { 0.85f, 0.04f, 0.11f }
    struct LobeTrimmingParameters
    {
        float A = 1.0f;
        float B = 0.0f;
        float C = 0.0001f;
    };

    /*
    Optional antilag settings:
        delta = ( abs( old - new ) - localVariance * sigmaScale ) / ( max( old, new ) + localVariance * sigmaScale + sensitivityToDarkness )
        delta = LinearStep( thresholdMax, thresholdMin, delta )
            - 1 - keep accumulation
            - 0 - history reset
    */
    struct AntilagIntensitySettings
    {
        float thresholdMin = 0.02f;             // normalized %
        float thresholdMax = 0.15f;             // max > min, usually 2-4x times greater than min
        float sigmaScale = 2.0f;                // plain "delta" is reduced by local variance multiplied by this value (2 - is a good start, 0.5-1.5 - can be used in many cases)
        float sensitivityToDarkness = 0.75f;    // the only value which is a real intensity!
        bool enable = false;                    // ideally, must be enabled, but since "sensitivityToDarkness" requires fine tuning from the app side it is disabled by default
    };

    struct AntilagHitDistanceSettings
    {
        float thresholdMin = 0.01f;             // normalized %, can be slightly increased if noise in AO/SO is high
        float thresholdMax = 0.10f;             // 10% is a good start
        float sigmaScale = 2.0f;                // "delta" will be reduced by local variance multiplied by this value (2 - is a good start, 0.5-1.5 - can be used in many cases)
        float sensitivityToDarkness = 0.5f;     // hit distances are normalized, this value is in range (0; 1]
        bool enable = true;
    };

    // REBLUR_DIFFUSE

    const uint32_t REBLUR_MAX_HISTORY_FRAME_NUM = 63;

    struct ReblurDiffuseSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        AntilagIntensitySettings antilagIntensitySettings = {};
        AntilagHitDistanceSettings antilagHitDistanceSettings = {};
        uint32_t maxAccumulatedFrameNum = 31;                           // [0; REBLUR_MAX_HISTORY_FRAME_NUM]
        uint32_t maxFastAccumulatedFrameNum = 8;                        // [0; REBLUR_MAX_HISTORY_FRAME_NUM]
        float blurRadius = 30.0f;                                       // pixels - base (worst case) denoising radius
        float maxAdaptiveRadiusScale = 5.0f;                            // [0; 10] - adaptive radius scale, comes into play if the algorithm detects boiling
        float normalWeightStrictness = 1.0f;                            // [0; 1] - smaller values make normal weight more strict
        float stabilizationStrength = 1.0f;                             // [0; 1] - stabilizes output, more stabilization improves antilag (clean signals can use lower values)
        float planeDistanceSensitivity = 0.002f;                        // > 0 (m) - viewZ 1m => only 2 mm deviations from surface plane are allowed
        float historyClampingColorBoxSigmaScale = 1.5f;                 // scale for standard deviation of color box for clamping normal history color to responsive history color
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;      // see CheckerboardMode
        bool antifirefly = false;                                       // adds a bit of bias, but tries to fight with fireflies
        bool usePrePass = true;                                         // pre-pass can be skipped if signal is relatively clean
    };

    // REBLUR_SPECULAR

    struct ReblurSpecularSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        LobeTrimmingParameters lobeTrimmingParameters = {};
        AntilagIntensitySettings antilagIntensitySettings = {};
        AntilagHitDistanceSettings antilagHitDistanceSettings = {};
        uint32_t maxAccumulatedFrameNum = 31;
        uint32_t maxFastAccumulatedFrameNum = 8;
        float blurRadius = 30.0f;
        float maxAdaptiveRadiusScale = 5.0f;
        float normalWeightStrictness = 1.0f;
        float stabilizationStrength = 1.0f;
        float planeDistanceSensitivity = 0.002f;
        float historyClampingColorBoxSigmaScale = 1.5f;
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;
        bool antifirefly = false;
        bool usePrePass = true;
    };

    // REBLUR_DIFFUSE_SPECULAR

    struct ReblurDiffuseSpecularSettings
    {
        ReblurDiffuseSettings diffuseSettings;
        ReblurSpecularSettings specularSettings;
    };

    // SIGMA_SHADOW and SIGMA_SHADOW_TRANSLUCENCY

    struct SigmaShadowSettings
    {
        float planeDistanceSensitivity = 0.002f;                    // > 0 (m) - viewZ 1m => only 2 mm deviations from surface plane are allowed
        float blurRadiusScale = 2.0f;                               // adds bias and stability if > 1, recommended range is [1; 3]
    };

    // RELAX_DIFFUSE_SPECULAR

    const uint32_t RELAX_MAX_HISTORY_FRAME_NUM = 63;

    struct RelaxDiffuseSpecularSettings
    {
        bool bicubicFilterForReprojectionEnabled = true;            // slower but sharper filtering of the history during reprojection
        uint32_t specularMaxAccumulatedFrameNum = 31;               // [0; RELAX_MAX_HISTORY_FRAME_NUM]
        uint32_t specularMaxFastAccumulatedFrameNum = 8;            // [0; RELAX_MAX_HISTORY_FRAME_NUM]
        uint32_t diffuseMaxAccumulatedFrameNum = 31;                // [0; RELAX_MAX_HISTORY_FRAME_NUM]
        uint32_t diffuseMaxFastAccumulatedFrameNum = 8;             // [0; RELAX_MAX_HISTORY_FRAME_NUM]
        float specularVarianceBoost = 1.0f;                         // how much variance we inject to specular if reprojection confidence is low
        bool specularVirtualHistoryClamping = true;                 // clamp specular virtual history to the current frame neighborhood
        bool roughnessBasedSpecularAccumulation = true;             // limit specular accumulation based on roughness

        float disocclusionFixEdgeStoppingNormalPower = 8.0f;        // normal edge stopper for cross-bilateral sparse filter
        float disocclusionFixMaxRadius = 14.0f;                     // maximum radius for sparse bilateral filter, expressed in pixels
        uint32_t disocclusionFixNumFramesToFix = 3;                 // cross-bilateral sparse filter will be applied to frames with history length shorter than this value

        float historyClampingColorBoxSigmaScale = 1.5f;             // scale for standard deviation of color box for clamping normal history color to responsive history color
        float specularAntiLagColorBoxSigmaScale = 2.0f;             // scale for standard deviation of color box for lag detection
        float specularAntiLagPower = 0.0f;                          // amount of history shortening when lag is detected
        float diffuseAntiLagColorBoxSigmaScale = 2.0f;              // scale for standard deviation of color box for lag detection
        float diffuseAntiLagPower = 0.0f;                           // amount of history shortening when lag is detected

        uint32_t spatialVarianceEstimationHistoryThreshold = 3;     // history length threshold below which spatial variance estimation will be executed
        uint32_t atrousIterationNum = 5;                            // [2; 8] - number of iteration for A-Trous wavelet transform
        float specularPhiLuminance = 2.0f;                          // A-trous edge stopping Luminance sensitivity
        float diffusePhiLuminance = 2.0f;                           // A-trous edge stopping Luminance sensitivity
        float minLuminanceWeight = 0.0f;                            // [0; 1] - A-trous edge stopping Luminance weight minimum
        float phiNormal = 64.0f;                                    // A-trous edge stopping normal sensitivity for diffuse
        float phiDepth = 0.05f;                                     // A-trous edge stopping depth sensitivity
        float specularLobeAngleFraction = 0.333f;                   // base fraction of the specular lobe angle used in normal based rejection of specular during A-Trous passes; 0.333 works well perceptually
        float specularLobeAngleSlack = 1.0f;                        // slack (in degrees) for the specular lobe angle used in normal based rejection of specular during A-Trous passes
        bool roughnessEdgeStoppingEnabled = true;                   // roughness based rejection
        float roughnessEdgeStoppingRelaxation = 0.3f;               // how much we relax roughness based rejection in areas where specular reprojection is low
        float normalEdgeStoppingRelaxation = 0.3f;                  // how much we relax normal based rejection in areas where specular reprojection is low
        float luminanceEdgeStoppingRelaxation = 1.0f;               // how much we relax luminance based rejection in areas where specular reprojection is low

        bool antifirefly = false;                                   // firefly suppression
    };

    // REFERENCE ACCUMULATION MODE RECOMMENDATIONS (CommonSettings::forceReferenceAccumulation = true)

    // REBLUR

    /*
    maxAccumulatedFrameNum              = REBLUR_MAX_HISTORY_FRAME_NUM
    maxFastAccumulatedFrameNum          = REBLUR_MAX_HISTORY_FRAME_NUM
    blurRadius                          = 0.0f
    stabilizationStrength               = 1.0f
    disocclusionThreshold               = 0.005f
    AntilagIntensitySettings::enable    = false
    AntilagHitDistanceSettings::enable  = false
    */
}
