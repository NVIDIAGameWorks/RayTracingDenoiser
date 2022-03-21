/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#define NRD_SETTINGS_VERSION_MAJOR 3
#define NRD_SETTINGS_VERSION_MINOR 0

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
    //     - if checkerboarding is enabled, "mode" defines the orientation of even numbered frames
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

    // REBLUR

    const uint32_t REBLUR_MAX_HISTORY_FRAME_NUM = 63;

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
    struct SpecularLobeTrimmingParameters
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

    struct ReblurSettings
    {
        SpecularLobeTrimmingParameters specularLobeTrimmingParameters = {};
        HitDistanceParameters hitDistanceParameters = {};
        AntilagIntensitySettings antilagIntensitySettings = {};
        AntilagHitDistanceSettings antilagHitDistanceSettings = {};

        // [0; REBLUR_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames ( = FPS * "time of accumulation")
        uint32_t maxAccumulatedFrameNum = 31;

        // (pixels) - base (worst case) denoising radius
        float blurRadius = 30.0f;

        // (normalized %) - defines base blur radius shrinking when number of accumulated frames increases
        float minConvergedStateBaseRadiusScale = 0.25f;

        // [0; 10] - adaptive radius scale, comes into play if boiling is detected
        float maxAdaptiveRadiusScale = 5.0f;

        // (normalized %) - base fraction of diffuse or specular lobe angle used to drive normal based rejection
        float lobeAngleFraction = 0.1f;

        // (normalized %) - base fraction of center roughness used to drive roughness based rejection
        float roughnessFraction = 0.05f;

        // [0; 1] - if roughness < this, temporal accumulation becomes responsive and driven by roughness (useful for animated water)
        float responsiveAccumulationRoughnessThreshold = 0.0f;

        // (normalized %) - stabilizes output, more stabilization improves antilag (clean signals can use lower values)
        float stabilizationStrength = 1.0f;

        // (normalized %) - aggresiveness of history reconstruction in disoccluded regions (0 - no reconstruction)
        float historyFixStrength = 1.0f;

        // (normalized %) - represents maximum allowed deviation from local tangent plane
        float planeDistanceSensitivity = 0.005f;

        // (normalized %) - adds a portion of input to the output of spatial passes
        float inputMix = 0.0f;

        // [0.01; 0.1] - default is tuned for 0.5rpp for the worst case
        float residualNoiseLevel = 0.03f;

        // If not OFF and used for DIFFUSE_SPECULAR, defines diffuse orientation, specular orientation is the opposite
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        // Enables a spatial reuse pass before the accumulation pass
        PrePassMode prePassMode = PrePassMode::SIMPLE;

        // Adds bias in case of badly defined signals, but tries to fight with fireflies
        bool enableAntiFirefly = false;

        // Turns off spatial filtering, does more aggressive accumulation
        bool enableReferenceAccumulation = false;

        // Boosts performance by sacrificing IQ
        bool enablePerformanceMode = false;

        // Spatial passes do optional material index comparison as: ( materialEnabled ? material[ center ] == material[ sample ] : 1 )
        bool enableMaterialTestForDiffuse = false;
        bool enableMaterialTestForSpecular = false;
    };

    // SIGMA

    struct SigmaSettings
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
        float diffusePrepassBlurRadius = 0.0f;
        float specularPrepassBlurRadius = 50.0f;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames ( = FPS * "time of accumulation")
        uint32_t diffuseMaxAccumulatedFrameNum = 31;
        uint32_t specularMaxAccumulatedFrameNum = 31;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames in fast history
        uint32_t diffuseMaxFastAccumulatedFrameNum = 8;
        uint32_t specularMaxFastAccumulatedFrameNum = 8;

        // A-trous edge stopping Luminance sensitivity
        float diffusePhiLuminance = 2.0f;
        float specularPhiLuminance = 1.0f;

        // (normalized %) - base fraction of diffuse or specular lobe angle used to drive normal based rejection
        float diffuseLobeAngleFraction = 0.5f;
        float specularLobeAngleFraction = 0.333f;

        // (normalized %) - base fraction of center roughness used to drive roughness based rejection
        float roughnessFraction = 0.05f;

        // [0; 1] - shorten diffuse history if "dot(N, Nprev)" is less than "1 - this" to maintain sharpness
        float diffuseHistoryRejectionNormalThreshold = 0.0f;

        // (>= 0) - how much variance we inject to specular if reprojection confidence is low
        float specularVarianceBoost = 1.0f;

        // (degrees) - slack for the specular lobe angle used in normal based rejection of specular during A-Trous passes
        float specularLobeAngleSlack = 0.3f;

        // (> 0) - normal edge stopper for cross-bilateral sparse filter
        float disocclusionFixEdgeStoppingNormalPower = 8.0f;

        // Maximum radius for sparse bilateral filter, expressed in pixels
        float disocclusionFixMaxRadius = 14.0f;

        // Cross-bilateral sparse filter will be applied to frames with history length shorter than this value
        uint32_t disocclusionFixNumFramesToFix = 3;

        // [1; 3] - standard deviation scale of color box for clamping main "slow" history to responsive "fast" history
        float historyClampingColorBoxSigmaScale = 2.0f;

        // (>= 0) - history length threshold below which spatial variance estimation will be executed
        uint32_t spatialVarianceEstimationHistoryThreshold = 3;

        // [2; 8] - number of iteration for A-Trous wavelet transform
        uint32_t atrousIterationNum = 5;

        // [0; 1] - A-trous edge stopping Luminance weight minimum
        float minLuminanceWeight = 0.0f;

        // (normalized %) - A-trous edge stopping depth threshold
        float depthThreshold = 0.01f;

        // How much we relax roughness based rejection in areas where specular reprojection is low
        float luminanceEdgeStoppingRelaxation = 0.5f;
        float normalEdgeStoppingRelaxation = 0.3f;
        float roughnessEdgeStoppingRelaxation = 0.3f;

        // If not OFF and used for DIFFUSE_SPECULAR, defines diffuse orientation, specular orientation is the opposite
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        // Firefly suppression
        bool enableAntiFirefly = false;

        // Skip reprojection test when there is no motion, might improve quality along the edges for static camera with a jitter
        bool enableReprojectionTestSkippingWithoutMotion = false;

        // Clamp specular virtual history to the current frame neighborhood
        bool enableSpecularVirtualHistoryClamping = true;

        // Roughness based rejection
        bool enableRoughnessEdgeStopping = true;

        // Spatial passes do optional material index comparison as: ( materialEnabled ? material[ center ] == material[ sample ] : 1 )
        bool enableMaterialTestForDiffuse = false;
        bool enableMaterialTestForSpecular = false;
    };

    // RELAX_DIFFUSE

    struct RelaxDiffuseSettings
    {
        float prepassBlurRadius = 0.0f;

        uint32_t diffuseMaxAccumulatedFrameNum = 31;
        uint32_t diffuseMaxFastAccumulatedFrameNum = 8;

        float diffusePhiLuminance = 2.0f;
        float diffuseLobeAngleFraction = 0.5f;
        float diffuseHistoryRejectionNormalThreshold = 0.0f;

        float disocclusionFixEdgeStoppingNormalPower = 8.0f;
        float disocclusionFixMaxRadius = 14.0f;
        uint32_t disocclusionFixNumFramesToFix = 3;

        float historyClampingColorBoxSigmaScale = 2.0f;

        uint32_t spatialVarianceEstimationHistoryThreshold = 3;
        uint32_t atrousIterationNum = 5;
        float minLuminanceWeight = 0.0f;
        float depthThreshold = 0.01f;

        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        bool enableAntiFirefly = false;
        bool enableReprojectionTestSkippingWithoutMotion = false;
        bool enableMaterialTest = false;
    };

    // RELAX_SPECULAR

    struct RelaxSpecularSettings
    {
        float prepassBlurRadius = 50.0f;

        uint32_t specularMaxAccumulatedFrameNum = 31;
        uint32_t specularMaxFastAccumulatedFrameNum = 8;

        float specularPhiLuminance = 1.0f;
        float diffuseLobeAngleFraction = 0.5f;
        float specularLobeAngleFraction = 0.333f;
        float roughnessFraction = 0.05f;

        float specularVarianceBoost = 1.0f;
        float specularLobeAngleSlack = 0.3f;

        float disocclusionFixEdgeStoppingNormalPower = 8.0f;
        float disocclusionFixMaxRadius = 14.0f;
        uint32_t disocclusionFixNumFramesToFix = 3;

        float historyClampingColorBoxSigmaScale = 2.0f;

        uint32_t spatialVarianceEstimationHistoryThreshold = 3;
        uint32_t atrousIterationNum = 5;
        float minLuminanceWeight = 0.0f;
        float depthThreshold = 0.01f;

        float luminanceEdgeStoppingRelaxation = 0.5f;
        float normalEdgeStoppingRelaxation = 0.3f;
        float roughnessEdgeStoppingRelaxation = 0.3f;

        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        bool enableAntiFirefly = false;
        bool enableReprojectionTestSkippingWithoutMotion = false;
        bool enableSpecularVirtualHistoryClamping = true;
        bool enableRoughnessEdgeStopping = true;
        bool enableMaterialTest = false;
    };

    // REFERENCE

    struct ReferenceSettings
    {
        // (>= 0) - maximum number of linearly accumulated frames ( = FPS * "time of accumulation")
        uint32_t maxAccumulatedFrameNum = 3600;
    };

    // SPEC_REFLECTION_MV

    struct SpecularReflectionMvSettings
    {
        float unused;
    };

    // DELTA_OPTIMIZATION_MV

    struct DeltaOptimizationMvSettings
    {
        float unused;
    };
}
