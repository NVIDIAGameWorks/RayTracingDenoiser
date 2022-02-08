/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#define NRD_SETTINGS_VERSION_MAJOR 2
#define NRD_SETTINGS_VERSION_MINOR 12

static_assert (NRD_VERSION_MAJOR == NRD_SETTINGS_VERSION_MAJOR && NRD_VERSION_MINOR == NRD_SETTINGS_VERSION_MINOR, "Please, update all NRD SDK files");

namespace nrd
{
    // Internally, NRD uses the following sequence based on "CommonSettings::frameIndex":
    //     Even frame (0)  Odd frame (1)   ...
    //         B W             W B
    //         W B             B W
    //     BLACK and WHITE modes define cells with VALID data
    // Checkerboard can be only horizontal
    // Notes:
    //     - all inputs have the same resolution - logical FULL resolution
    //     - noisy input signals (IN_DIFF_XXX / IN_SPEC_XXX) are tightly packed to the LEFT HALF of the texture (the input pixel = 2x1 screen pixel)
    //     - for others the input pixel = 1x1 screen pixel
    //     - upsampling will be handled internally in checkerboard mode
    enum class CheckerboardMode : uint8_t
    {
        OFF,
        BLACK,
        WHITE,

        MAX_NUM
    };

    enum class AccumulationMode : uint8_t
    {
        // Common mode (accumulation continues normally)
        CONTINUE,

        // Discards history and resets accumulation
        RESTART,

        // Like RESTART, but additionally clears resources from potential garbage
        CLEAR_AND_RESTART,

        MAX_NUM
    };

    enum class PrePassMode
    {
        // Pre-pass is disabled
        OFF,

        // A not requiring additional inputs spatial reuse pass
        SIMPLE,

        // A requiring IN_DIFF_DIRECTION_PDF / IN_SPEC_DIRECTION_PDF spatial reuse pass
        ADVANCED
    };

    struct CommonSettings
    {
        // Matrix requirements:
        //     - usage - vector is a column
        //     - layout - column-major
        //     - non jittered!
        // LH / RH projection matrix (INF far plane is supported) with non-swizzled rows, i.e. clip-space depth = z / w
        float viewToClipMatrix[16] = {};

        // Previous projection matrix
        float viewToClipMatrixPrev[16] = {};

        // World-space to camera-space matrix
        float worldToViewMatrix[16] = {};

        // If coordinate system moves with the camera, camera delta must be included to reflect camera motion
        float worldToViewMatrixPrev[16] = {};

        // If "isMotionVectorInWorldSpace = true" will be used as "MV * motionVectorScale.xyy"
        float motionVectorScale[2] = {1.0f, 1.0f};

        // [-0.5; 0.5] - sampleUv = pixelUv + cameraJitter
        float cameraJitter[2] = {};

        // (0; 1] - dynamic resolution scaling
        float resolutionScale[2] = {1.0f, 1.0f};

        // (ms) - user provided if > 0, otherwise - tracked internally
        float timeDeltaBetweenFrames = 0.0f;

        // (units) > 0 - use TLAS or tracing range
        float denoisingRange = 1e7f;

        // (normalized %)
        float disocclusionThreshold = 0.01f;

        // [0; 1] - enables "noisy input / denoised output" comparison
        float splitScreen = 0.0f;

        // For internal needs
        float debug = 0.0f;

        // (pixels) - data rectangle origin in ALL input textures
        uint32_t inputSubrectOrigin[2] = {};

        // A consecutive number
        uint32_t frameIndex = 0;

        // To reset history set to RESTART / CLEAR_AND_RESTART for one frame
        AccumulationMode accumulationMode = AccumulationMode::CONTINUE;

        // If "true" IN_MV is 3D motion in world space (0 should be everywhere if the scene is static),
        // otherwise it's 2D screen-space motion (0 should be everywhere if the camera doesn't move) (recommended value = true)
        bool isMotionVectorInWorldSpace = false;

        // If "true" IN_DIFF_CONFIDENCE and IN_SPEC_CONFIDENCE are provided
        bool isHistoryConfidenceInputsAvailable = false;
    };

    // "Normalized hit distance" = saturate( "hit distance" / f ), where:
    // f = ( A + viewZ * B ) * lerp( 1.0, C, exp2( D * roughness ^ 2 ) ), see "NRD.hlsl/REBLUR_FrontEnd_GetNormHitDist"
    struct HitDistanceParameters
    {
        // (units) - constant value
        // IMPORTANT: if your unit is not "meter", you must convert it from "meters" to "units" manually!
        float A = 3.0f;

        // (> 0) - viewZ based linear scale (1 m - 10 cm, 10 m - 1 m, 100 m - 10 m)
        float B = 0.1f;

        // (>= 1) - roughness based scale, use values > 1 to get bigger hit distance for low roughness
        float C = 20.0f;

        // (<= 0) - absolute value should be big enough to collapse "exp2( D * roughness ^ 2 )" to "~0" for roughness = 1
        float D = -25.0f;
    };

    // Optional specular lobe trimming = A * smoothstep( B, C, roughness )
    // Recommended settings if lobe trimming is needed = { 0.85f, 0.04f, 0.11f }
    struct LobeTrimmingParameters
    {
        // [0; 1] - main level  (0 - GGX dominant direction, 1 - full lobe)
        float A = 1.0f;

        // [0; 1] - max trimming if roughness is less than this threshold
        float B = 0.0f;

        // [0; 1] - main level if roughness is greater than this threshold
        float C = 0.0001f;
    };

    // Antilag logic:
    //    delta = ( abs( old - new ) - localVariance * sigmaScale ) / ( max( old, new ) + localVariance * sigmaScale + sensitivityToDarkness )
    //    delta = LinearStep( thresholdMax, thresholdMin, delta )
    //        - 1 - keep accumulation
    //        - 0 - history reset
    struct AntilagIntensitySettings
    {
        // (normalized %) - must be big enough to almost ignore residual noise (boiling), default is tuned for 0.5rpp in general
        float thresholdMin = 0.03f;

        // (normalized %) - max > min, usually 3-5x times greater than min
        float thresholdMax = 0.2f;

        // (> 0) - real delta is reduced by local variance multiplied by this value
        float sigmaScale = 1.0f;

        // (intensity units) - bigger values make antilag less sensitive to lightness fluctuations in dark places
        float sensitivityToDarkness = 0.0f; // IMPORTANT: 0 is a bad default

        // Ideally, must be enabled, but since "sensitivityToDarkness" requires fine tuning from the app side it is disabled by default
        bool enable = false;
    };

    struct AntilagHitDistanceSettings
    {
        // (normalized %) - must almost ignore residual noise (boiling), default is tuned for 0.5rpp for the worst case
        float thresholdMin = 0.015f;

        // (normalized %) - max > min, usually 2-4x times greater than min
        float thresholdMax = 0.15f;

        // (> 0) - real delta is reduced by local variance multiplied by this value
        float sigmaScale = 1.0f;

        // (0; 1] - hit distances are normalized
        float sensitivityToDarkness = 0.1f;

        // Enabled by default
        bool enable = true;
    };

    // REBLUR_DIFFUSE, REBLUR_DIFFUSE_OCCLUSION and REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION

    const uint32_t REBLUR_MAX_HISTORY_FRAME_NUM = 63;

    struct ReblurDiffuseSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        AntilagIntensitySettings antilagIntensitySettings = {};
        AntilagHitDistanceSettings antilagHitDistanceSettings = {};

        // Spatial passes do optional material index comparison as: ( material[ center ] & materialMask ) == ( material[ sample ] & materialMask )
        uint32_t materialMask = 0;

        // [0; REBLUR_MAX_HISTORY_FRAME_NUM]
        uint32_t maxAccumulatedFrameNum = 31;

        // (pixels) - base (worst case) denoising radius
        float blurRadius = 30.0f;

        // [0; 10] - adaptive radius scale, comes into play if the algorithm detects boiling
        float maxAdaptiveRadiusScale = 5.0f;

        // [0; 1] - smaller values make normal weight more strict
        float normalWeightStrictness = 1.0f;

        // [0; 1] - stabilizes output, more stabilization improves antilag (clean signals can use lower values)
        float stabilizationStrength = 1.0f;

        // [0; 1] - aggresiveness of history reconstruction in disoccluded regions (0 - no reconstruction)
        float historyFixStrength = 1.0f;

        // (normalized %) - represents maximum allowed deviation from local tangent plane
        float planeDistanceSensitivity = 0.005f;

        // [0.01; 0.1] - default is tuned for 0.5rpp for the worst case
        float residualNoiseLevel = 0.03f;

        // If checkerboarding is enabled, defines the orientation of even numbered frames
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        // Enables a spatial reuse pass before the accumulation pass
        PrePassMode prePassMode = PrePassMode::SIMPLE;

        // Adds bias in case of badly defined signals, but tries to fight with fireflies
        bool enableAntiFirefly = false;

        // Turns off spatial filtering, more aggressive accumulation
        bool enableReferenceAccumulation = false;
    };

    // REBLUR_SPECULAR and REBLUR_SPECULAR_OCCLUSION

    struct ReblurSpecularSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        LobeTrimmingParameters lobeTrimmingParameters = {};
        AntilagIntensitySettings antilagIntensitySettings = {};
        AntilagHitDistanceSettings antilagHitDistanceSettings = {};
        uint32_t materialMask = 0;
        uint32_t maxAccumulatedFrameNum = 31;
        float blurRadius = 30.0f;
        float maxAdaptiveRadiusScale = 5.0f;
        float normalWeightStrictness = 1.0f;
        float stabilizationStrength = 1.0f;
        float historyFixStrength = 1.0f;
        float planeDistanceSensitivity = 0.005f;
        float residualNoiseLevel = 0.03f;
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;
        PrePassMode prePassMode = PrePassMode::SIMPLE;
        bool enableAntiFirefly = false;
        bool enableReferenceAccumulation = false;
    };

    // REBLUR_DIFFUSE_SPECULAR and REBLUR_DIFFUSE_SPECULAR_OCCLUSION

    struct ReblurDiffuseSpecularSettings
    {
        // normalWeightStrictness       = min( diffuse, specular )
        // stabilizationStrength        = min( diffuse, specular )
        // planeDistanceSensitivity     = min( diffuse, specular )
        // residualNoiseLevel           = min( diffuse, specular )
        // prePassMode                  = min( diffuse, specular )
        // enableAntiFirefly            = min( diffuse, specular )
        // enableReferenceAccumulation  = min( diffuse, specular )

        ReblurDiffuseSettings diffuse;
        ReblurSpecularSettings specular;
    };

    // SIGMA_SHADOW and SIGMA_SHADOW_TRANSLUCENCY

    struct SigmaShadowSettings
    {
        // (normalized %) - represents maximum allowed deviation from local tangent plane
        float planeDistanceSensitivity = 0.005f;

        // [1; 3] - adds bias and stability if > 1
        float blurRadiusScale = 2.0f;
    };

    // RELAX_DIFFUSE_SPECULAR

    const uint32_t RELAX_MAX_HISTORY_FRAME_NUM = 63;

    struct RelaxDiffuseSpecularSettings
    {
        // [0; 100] - radius in pixels (0 disables prepass)
        float specularPrepassBlurRadius = 50.0f;

        // [0; 100] - radius in pixels (0 disables prepass)
        float diffusePrepassBlurRadius = 0.0f;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM]
        uint32_t specularMaxAccumulatedFrameNum = 31;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM]
        uint32_t specularMaxFastAccumulatedFrameNum = 8;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM]
        uint32_t diffuseMaxAccumulatedFrameNum = 31;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM]
        uint32_t diffuseMaxFastAccumulatedFrameNum = 8;

        // How much variance we inject to specular if reprojection confidence is low
        float specularVarianceBoost = 1.0f;

        // [0; 1], shorten diffuse history if dot (N, previousN) is less than  (1 - this value), this maintains sharpness
        float rejectDiffuseHistoryNormalThreshold = 0.0f;

        // Normal edge stopper for cross-bilateral sparse filter
        float disocclusionFixEdgeStoppingNormalPower = 8.0f;

        // Maximum radius for sparse bilateral filter, expressed in pixels
        float disocclusionFixMaxRadius = 14.0f;

        // Cross-bilateral sparse filter will be applied to frames with history length shorter than this value
        uint32_t disocclusionFixNumFramesToFix = 3;

        // [1; 3] - standard deviation scale of color box for clamping main "slow" history to responsive "fast" history
        float historyClampingColorBoxSigmaScale = 2.0f;

        // History length threshold below which spatial variance estimation will be executed
        uint32_t spatialVarianceEstimationHistoryThreshold = 3;

        // [2; 8] - number of iteration for A-Trous wavelet transform
        uint32_t atrousIterationNum = 5;

        // A-trous edge stopping Luminance sensitivity
        float specularPhiLuminance = 2.0f;

        // A-trous edge stopping Luminance sensitivity
        float diffusePhiLuminance = 2.0f;

        // [0; 1] - A-trous edge stopping Luminance weight minimum
        float minLuminanceWeight = 0.0f;

        // A-trous edge stopping normal sensitivity for diffuse, spatial variance estimation normal sensitivity
        float phiNormal = 64.0f;

        // A-trous edge stopping depth threshold
        float depthThreshold = 0.01f;

        // Base fraction of the specular lobe angle used in normal based rejection of specular during A-Trous passes; 0.333 works well perceptually
        float specularLobeAngleFraction = 0.333f;

        // Slack (in degrees) for the specular lobe angle used in normal based rejection of specular during A-Trous passes
        float specularLobeAngleSlack = 0.3f;

        // How much we relax roughness based rejection in areas where specular reprojection is low
        float roughnessEdgeStoppingRelaxation = 0.3f;

        // How much we relax normal based rejection in areas where specular reprojection is low
        float normalEdgeStoppingRelaxation = 0.3f;

        // How much we relax luminance based rejection in areas where specular reprojection is low
        float luminanceEdgeStoppingRelaxation = 1.0f;

        // If not OFF, diffuse mode equals checkerboard mode set here, and specular mode opposite: WHITE if diffuse is BLACK and vice versa
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        // Skip reprojection test when there is no motion, might improve quality along the edges for static camera with a jitter
        bool enableSkipReprojectionTestWithoutMotion = false;

        // Clamp specular virtual history to the current frame neighborhood
        bool enableSpecularVirtualHistoryClamping = true;

        // Roughness based rejection
        bool enableRoughnessEdgeStopping = true;

        // Firefly suppression
        bool enableAntiFirefly = false;
    };

    // RELAX_DIFFUSE

    struct RelaxDiffuseSettings
    {
        // [0; 100] - radius in pixels (0 disables prepass)
        float prepassBlurRadius = 0.0f;

        uint32_t diffuseMaxAccumulatedFrameNum = 31;
        uint32_t diffuseMaxFastAccumulatedFrameNum = 8;

        // [0; 1], shorten diffuse history if dot (N, previousN) is less than  (1 - this value), this maintains sharpness
        float rejectDiffuseHistoryNormalThreshold = 0.0f;


        float disocclusionFixEdgeStoppingNormalPower = 8.0f;
        float disocclusionFixMaxRadius = 14.0f;
        uint32_t disocclusionFixNumFramesToFix = 3;

        float historyClampingColorBoxSigmaScale = 2.0f;

        uint32_t spatialVarianceEstimationHistoryThreshold = 3;
        uint32_t atrousIterationNum = 5;
        float diffusePhiLuminance = 2.0f;
        float minLuminanceWeight = 0.0f;
        float phiNormal = 64.0f;
        float depthThreshold = 0.01f;

        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;
        bool enableSkipReprojectionTestWithoutMotion = false;
        bool enableAntiFirefly = false;
    };

    // RELAX_SPECULAR

    struct RelaxSpecularSettings
    {
        float prepassBlurRadius = 50.0f;

        uint32_t specularMaxAccumulatedFrameNum = 31;
        uint32_t specularMaxFastAccumulatedFrameNum = 8;
        float specularVarianceBoost = 1.0f;

        float disocclusionFixEdgeStoppingNormalPower = 8.0f;
        float disocclusionFixMaxRadius = 14.0f;
        uint32_t disocclusionFixNumFramesToFix = 3;

        float historyClampingColorBoxSigmaScale = 2.0f;

        uint32_t spatialVarianceEstimationHistoryThreshold = 3;
        uint32_t atrousIterationNum = 5;
        float specularPhiLuminance = 2.0f;
        float minLuminanceWeight = 0.0f;
        float phiNormal = 64.0f;
        float depthThreshold = 0.01f;
        float specularLobeAngleFraction = 0.333f;
        float specularLobeAngleSlack = 0.3f;
        float roughnessEdgeStoppingRelaxation = 0.3f;
        float normalEdgeStoppingRelaxation = 0.3f;
        float luminanceEdgeStoppingRelaxation = 1.0f;

        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;
        bool enableSkipReprojectionTestWithoutMotion = false;
        bool enableSpecularVirtualHistoryClamping = true;
        bool enableRoughnessEdgeStopping = true;
        bool enableAntiFirefly = false;
    };

    // REFERENCE

    struct ReferenceSettings
    {
        // (> 0) - Maximum number of linearly accumulated frames ( = FPS * "time of accumulation")
        uint32_t maxAccumulatedFrameNum = 3600;
    };
}
