/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#define NRD_SETTINGS_VERSION_MAJOR 4
#define NRD_SETTINGS_VERSION_MINOR 2

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

    enum class HitDistanceReconstructionMode : uint8_t
    {
        // Probabilistic split at primary hit is not used, hence hit distance is always valid (reconstruction is not needed)
        OFF,

        // If hit distance is invalid due to probabilistic sampling, reconstruct using 3x3 neighbors.
        // Probability at primary hit must be clamped to [1/4; 3/4] range to guarantee a sample in this area
        AREA_3X3,

        // If hit distance is invalid due to probabilistic sampling, reconstruct using 5x5 neighbors.
        // Probability at primary hit must be clamped to [1/16; 15/16] range to guarantee a sample in this area
        AREA_5X5,

        MAX_NUM
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

        // (Optional) Previous world-space to current world-space matrix. It is for virtual normals, where a coordinate
        // system of the virtual space changes frame to frame, such as in a case of animated intermediary reflecting
        // surfaces when primary surface replacement is used for them.
        float worldPrevToWorldMatrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        // used as "IN_MV * motionVectorScale" (use .z = 0 for 2D screen-space motion)
        float motionVectorScale[3] = {1.0f, 1.0f, 0.0f};

        // [-0.5; 0.5] - sampleUv = pixelUv + cameraJitter
        float cameraJitter[2] = {};
        float cameraJitterPrev[2] = {};

        // (0; 1] - dynamic resolution scaling
        float resolutionScale[2] = {1.0f, 1.0f};
        float resolutionScalePrev[2] = {1.0f, 1.0f};

        // (ms) - user provided if > 0, otherwise - tracked internally
        float timeDeltaBetweenFrames = 0.0f;

        // (units) > 0 - use TLAS or tracing range (max value = NRD_FP16_MAX / NRD_FP16_VIEWZ_SCALE - 1 = 524031)
        float denoisingRange = 500000.0f;

        // (normalized %) - if relative distance difference is greater than threshold, history gets reset (0.5-2.5% works well)
        float disocclusionThreshold = 0.01f;

        // (normalized %) - alternative disocclusion threshold, which is mixed to based on IN_DISOCCLUSION_THRESHOLD_MIX
        float disocclusionThresholdAlternate = 0.05f;

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

        // If "true" IN_MV is 3D motion in world-space (0 should be everywhere if the scene is static),
        // otherwise it's 2D (+ optional Z delta) screen-space motion (0 should be everywhere if the camera doesn't move) (recommended value = true)
        bool isMotionVectorInWorldSpace = false;

        // If "true" IN_DIFF_CONFIDENCE and IN_SPEC_CONFIDENCE are available
        bool isHistoryConfidenceAvailable = false;

        // If "true" IN_DISOCCLUSION_THRESHOLD_MIX is available
        bool isDisocclusionThresholdMixAvailable = false;

        // If "true" IN_BASECOLOR_METALNESS is available
        bool isBaseColorMetalnessAvailable = false;

        // Enables debug overlay in OUT_VALIDATION, requires "InstanceCreationDesc::allowValidation = true"
        bool enableValidation = false;
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
        bool enable = false; // IMPORTANT: doesn't affect "occlusion" denoisers
    };

    struct AntilagHitDistanceSettings
    {
        // (normalized %) - must almost ignore residual noise (boiling), default is tuned for 0.5rpp for the worst case
        float thresholdMin = 0.03f;

        // (normalized %) - max > min, usually 2-4x times greater than min
        float thresholdMax = 0.2f;

        // (> 0) - real delta is reduced by local variance multiplied by this value
        float sigmaScale = 1.0f;

        // (0; 1] - hit distances are normalized
        float sensitivityToDarkness = 0.1f;

        // Enabled by default
        bool enable = true;
    };

    struct ReblurSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        AntilagIntensitySettings antilagIntensitySettings = {};
        AntilagHitDistanceSettings antilagHitDistanceSettings = {};

        // [0; REBLUR_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames (= FPS * "time of accumulation")
        uint32_t maxAccumulatedFrameNum = 30;

        // [0; REBLUR_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames in fast history (less than "maxAccumulatedFrameNum")
        uint32_t maxFastAccumulatedFrameNum = 6;

        // [0; REBLUR_MAX_HISTORY_FRAME_NUM] - number of reconstructed frames after history reset (less than "maxFastAccumulatedFrameNum")
        uint32_t historyFixFrameNum = 3;

        // (pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, must be used in case of probabilistic sampling)
        float diffusePrepassBlurRadius = 30.0f;
        float specularPrepassBlurRadius = 50.0f;

        // (pixels) - base denoising radius (30 is a baseline for 1440p)
        float blurRadius = 15.0f;

        // (pixels) - base stride between samples in history reconstruction pass
        float historyFixStrideBetweenSamples = 14.0f;

        // (normalized %) - base fraction of diffuse or specular lobe angle used to drive normal based rejection
        float lobeAngleFraction = 0.15f;

        // (normalized %) - base fraction of center roughness used to drive roughness based rejection
        float roughnessFraction = 0.15f;

        // [0; 1] - if roughness < this, temporal accumulation becomes responsive and driven by roughness (useful for animated water)
        float responsiveAccumulationRoughnessThreshold = 0.0f;

        // (normalized %) - stabilizes output, more stabilization improves antilag (clean signals can use lower values)
        float stabilizationStrength = 1.0f;

        // (normalized %) - represents maximum allowed deviation from local tangent plane
        float planeDistanceSensitivity = 0.005f;

        // IN_MV = lerp(IN_MV, specularMotion, smoothstep(specularProbabilityThresholdsForMvModification[0], specularProbabilityThresholdsForMvModification[1], specularProbability))
        float specularProbabilityThresholdsForMvModification[2] = {0.5f, 0.9f};

        // If not OFF and used for DIFFUSE_SPECULAR, defines diffuse orientation, specular orientation is the opposite
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        // Must be used only in case of probabilistic sampling (not checkerboarding), when a pixel can be skipped and have "0" (invalid) hit distance
        HitDistanceReconstructionMode hitDistanceReconstructionMode = HitDistanceReconstructionMode::OFF;

        // Adds bias in case of badly defined signals, but tries to fight with fireflies
        bool enableAntiFirefly = false;

        // Turns off spatial filtering and virtual motion based specular tracking
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

    const uint32_t RELAX_MAX_HISTORY_FRAME_NUM = 255;

    struct RelaxDiffuseSpecularSettings
    {
        // (pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, must be used in case of probabilistic sampling)
        float diffusePrepassBlurRadius = 0.0f;
        float specularPrepassBlurRadius = 50.0f;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames ( = FPS * "time of accumulation")
        uint32_t diffuseMaxAccumulatedFrameNum = 30;
        uint32_t specularMaxAccumulatedFrameNum = 30;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames in fast history (less than "maxAccumulatedFrameNum")
        uint32_t diffuseMaxFastAccumulatedFrameNum = 6;
        uint32_t specularMaxFastAccumulatedFrameNum = 6;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM] - number of reconstructed frames after history reset (less than "maxFastAccumulatedFrameNum")
        uint32_t historyFixFrameNum = 3;

        // A-trous edge stopping Luminance sensitivity
        float diffusePhiLuminance = 2.0f;
        float specularPhiLuminance = 1.0f;

        // (normalized %) - base fraction of diffuse or specular lobe angle used to drive normal based rejection
        float diffuseLobeAngleFraction = 0.5f;
        float specularLobeAngleFraction = 0.5f;

        // (normalized %) - base fraction of center roughness used to drive roughness based rejection
        float roughnessFraction = 0.15f;

        // (>= 0) - how much variance we inject to specular if reprojection confidence is low
        float specularVarianceBoost = 0.0f;

        // (degrees) - slack for the specular lobe angle used in normal based rejection of specular during A-Trous passes
        float specularLobeAngleSlack = 0.15f;

        // (pixels) - base stride between samples in history reconstruction pass
        float historyFixStrideBetweenSamples = 14.0f;

        // (> 0) - normal edge stopper for history reconstruction pass
        float historyFixEdgeStoppingNormalPower = 8.0f;

        // [1; 3] - standard deviation scale of color box for clamping main "slow" history to responsive "fast" history
        float historyClampingColorBoxSigmaScale = 2.0f;

        // (>= 0) - history length threshold below which spatial variance estimation will be executed
        uint32_t spatialVarianceEstimationHistoryThreshold = 3;

        // [2; 8] - number of iteration for A-Trous wavelet transform
        uint32_t atrousIterationNum = 5;

        // [0; 1] - A-trous edge stopping Luminance weight minimum
        float diffuseMinLuminanceWeight = 0.0f;
        float specularMinLuminanceWeight = 0.0f;

        // (normalized %) - Depth threshold for spatial passes
        float depthThreshold = 0.003f;

        // Confidence inputs can affect spatial blurs, relaxing some weights in areas with low confidence
        float confidenceDrivenRelaxationMultiplier = 0.0f;
        float confidenceDrivenLuminanceEdgeStoppingRelaxation = 0.0f;
        float confidenceDrivenNormalEdgeStoppingRelaxation = 0.0f;

        // How much we relax roughness based rejection for spatial filter in areas where specular reprojection is low
        float luminanceEdgeStoppingRelaxation = 0.5f;
        float normalEdgeStoppingRelaxation = 0.3f;

        // How much we relax rejection for spatial filter based on roughness and view vector
        float roughnessEdgeStoppingRelaxation = 1.0f;

        // If not OFF and used for DIFFUSE_SPECULAR, defines diffuse orientation, specular orientation is the opposite
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        // Must be used only in case of probabilistic sampling (not checkerboarding), when a pixel can be skipped and have "0" (invalid) hit distance
        HitDistanceReconstructionMode hitDistanceReconstructionMode = HitDistanceReconstructionMode::OFF;

        // Firefly suppression
        bool enableAntiFirefly = false;

        // Skip reprojection test when there is no motion, might improve quality along the edges for static camera with a jitter
        bool enableReprojectionTestSkippingWithoutMotion = false;

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

        uint32_t diffuseMaxAccumulatedFrameNum = 30;
        uint32_t diffuseMaxFastAccumulatedFrameNum = 6;
        uint32_t historyFixFrameNum = 3;

        float diffusePhiLuminance = 2.0f;
        float diffuseLobeAngleFraction = 0.5f;

        float historyFixEdgeStoppingNormalPower = 8.0f;
        float historyFixStrideBetweenSamples = 14.0f;

        float historyClampingColorBoxSigmaScale = 2.0f;

        uint32_t spatialVarianceEstimationHistoryThreshold = 3;
        uint32_t atrousIterationNum = 5;
        float minLuminanceWeight = 0.0f;
        float depthThreshold = 0.01f;

        float confidenceDrivenRelaxationMultiplier = 0.0f;
        float confidenceDrivenLuminanceEdgeStoppingRelaxation = 0.0f;
        float confidenceDrivenNormalEdgeStoppingRelaxation = 0.0f;

        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;
        HitDistanceReconstructionMode hitDistanceReconstructionMode = HitDistanceReconstructionMode::OFF;

        bool enableAntiFirefly = false;
        bool enableReprojectionTestSkippingWithoutMotion = false;
        bool enableMaterialTest = false;
    };

    // RELAX_SPECULAR

    struct RelaxSpecularSettings
    {
        float prepassBlurRadius = 50.0f;

        uint32_t specularMaxAccumulatedFrameNum = 30;
        uint32_t specularMaxFastAccumulatedFrameNum = 6;
        uint32_t historyFixFrameNum = 3;

        float specularPhiLuminance = 1.0f;
        float diffuseLobeAngleFraction = 0.5f;
        float specularLobeAngleFraction = 0.5f;
        float roughnessFraction = 0.15f;

        float specularVarianceBoost = 0.0f;
        float specularLobeAngleSlack = 0.15f;

        float historyFixEdgeStoppingNormalPower = 8.0f;
        float historyFixStrideBetweenSamples = 14.0f;

        float historyClampingColorBoxSigmaScale = 2.0f;

        uint32_t spatialVarianceEstimationHistoryThreshold = 3;
        uint32_t atrousIterationNum = 5;
        float minLuminanceWeight = 0.0f;
        float depthThreshold = 0.01f;

        float confidenceDrivenRelaxationMultiplier = 0.0f;
        float confidenceDrivenLuminanceEdgeStoppingRelaxation = 0.0f;
        float confidenceDrivenNormalEdgeStoppingRelaxation = 0.0f;

        float luminanceEdgeStoppingRelaxation = 0.5f;
        float normalEdgeStoppingRelaxation = 0.3f;
        float roughnessEdgeStoppingRelaxation = 1.0f;

        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;
        HitDistanceReconstructionMode hitDistanceReconstructionMode = HitDistanceReconstructionMode::OFF;

        bool enableAntiFirefly = false;
        bool enableReprojectionTestSkippingWithoutMotion = false;
        bool enableRoughnessEdgeStopping = true;
        bool enableMaterialTest = false;
    };

    // REFERENCE

    struct ReferenceSettings
    {
        // (>= 0) - maximum number of linearly accumulated frames ( = FPS * "time of accumulation")
        uint32_t maxAccumulatedFrameNum = 1024;
    };

    // SPECULAR_REFLECTION_MV

    struct SpecularReflectionMvSettings
    {
        float unused;
    };

    // SPECULAR_DELTA_MV

    struct SpecularDeltaMvSettings
    {
        float unused;
    };
}
