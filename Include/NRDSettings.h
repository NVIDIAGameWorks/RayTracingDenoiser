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
#define NRD_SETTINGS_VERSION_MINOR 4

static_assert(NRD_VERSION_MAJOR == NRD_SETTINGS_VERSION_MAJOR && NRD_VERSION_MINOR == NRD_SETTINGS_VERSION_MINOR, "Please, update all NRD SDK files");

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

        // Flexible dynamic resolution scaling support
        uint16_t resourceSize[2] = {};
        uint16_t resourceSizePrev[2] = {};
        uint16_t rectSize[2] = {};
        uint16_t rectSizePrev[2] = {};

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

        // (pixels) - viewport origin
        // IMPORTANT: gets applied only to non-noisy guides (aka g-buffer), including IN_DIFF_CONFIDENCE, IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX and IN_BASECOLOR_METALNESS
        // Must be manually enabled via NRD_USE_VIEWPORT_OFFSET macro switch
        uint32_t rectOrigin[2] = {};

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

        // Enables debug overlay in OUT_VALIDATION
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

    struct ReblurAntilagSettings
    {
        // [1; 3] - delta is reduced by local variance multiplied by this value
        float luminanceSigmaScale = 2.0f;
        float hitDistanceSigmaScale = 2.0f;

        // (0; 1] - antilag = pow( antilag, power )
        float luminanceAntilagPower = 0.5f;
        float hitDistanceAntilagPower = 1.0f;
    };

    struct ReblurSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        ReblurAntilagSettings antilagSettings = {};

        // [0; REBLUR_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames (= FPS * "time of accumulation")
        uint32_t maxAccumulatedFrameNum = 30;

        // [0; REBLUR_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames in fast history (less than "maxAccumulatedFrameNum")
        uint32_t maxFastAccumulatedFrameNum = 6;

        // [0; 3] - number of reconstructed frames after history reset (less than "maxFastAccumulatedFrameNum")
        uint32_t historyFixFrameNum = 3;

        // (pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, recommended in case of probabilistic sampling)
        float diffusePrepassBlurRadius = 30.0f;
        float specularPrepassBlurRadius = 50.0f;

        // (pixels) - base denoising radius (30 is a baseline for 1440p)
        float blurRadius = 15.0f;

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

        // IN_MV = lerp(IN_MV, specularMotion, smoothstep(this[0], this[1], specularProbability))
        float specularProbabilityThresholdsForMvModification[2] = {0.5f, 0.9f};

        // If not OFF and used for DIFFUSE_SPECULAR, defines diffuse orientation, specular orientation is the opposite
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        // Must be used only in case of probabilistic sampling (not checkerboarding), when a pixel can be skipped and have "0" (invalid) hit distance
        HitDistanceReconstructionMode hitDistanceReconstructionMode = HitDistanceReconstructionMode::OFF;

        // Adds bias in case of badly defined signals, but tries to fight with fireflies
        bool enableAntiFirefly = false;

        // Boosts performance by sacrificing IQ
        bool enablePerformanceMode = false;

        // Spatial passes do optional material index comparison as: ( materialEnabled ? material[ center ] == material[ sample ] : 1 )
        bool enableMaterialTestForDiffuse = false;
        bool enableMaterialTestForSpecular = false;

        // In rare cases, when bright samples are so sparse that any other bright neighbor can't
        // be reached, pre-pass transforms a standalone bright pixel into a standalone bright blob,
        // worsening the situation. Despite that it's a problem of sampling, the denoiser needs to
        // handle it somehow on its side too. Diffuse pre-pass can be just disabled, but for specular
        // it's still needed to find optimal hit distance for tracking. This boolean allow to use
        // specular pre-pass for tracking purposes only
        bool usePrepassOnlyForSpecularMotionEstimation = false;
    };

    // SIGMA

    struct SigmaSettings
    {
        // (normalized %) - represents maximum allowed deviation from local tangent plane
        float planeDistanceSensitivity = 0.005f;

        // [1; 3] - adds bias and stability if > 1
        float blurRadiusScale = 2.0f;
    };

    // RELAX

    const uint32_t RELAX_MAX_HISTORY_FRAME_NUM = 255;

    struct RelaxAntilagSettings
    {
        // IMPORTANT: History acceleration and reset amounts for specular are made 2x-3x weaker than values for diffuse below
        // due to specific specular logic that does additional history acceleration and reset

        // [0; 1] - amount of history acceleration if history clamping happened in pixel
        float accelerationAmount = 0.3f;

        // (> 0) - history is being reset if delta between history and raw input is larger than spatial sigma + temporal sigma
        float spatialSigmaScale = 4.5f;

        // (> 0) - history is being reset if delta between history and raw input is larger than spatial sigma + temporal sigma
        float temporalSigmaScale = 0.5f;

        // [0; 1] - amount of history reset, 0.0 - no reset, 1.0 - full reset
        float resetAmount = 0.5f;
    };

    struct RelaxSettings
    {
        RelaxAntilagSettings antilagSettings = {};

        // (pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, must be used in case of probabilistic sampling)
        float diffusePrepassBlurRadius = 30.0f;
        float specularPrepassBlurRadius = 50.0f;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames ( = FPS * "time of accumulation")
        uint32_t diffuseMaxAccumulatedFrameNum = 30;
        uint32_t specularMaxAccumulatedFrameNum = 30;

        // [0; RELAX_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames in fast history (less than "maxAccumulatedFrameNum")
        uint32_t diffuseMaxFastAccumulatedFrameNum = 6;
        uint32_t specularMaxFastAccumulatedFrameNum = 6;

        // [0; 3] - number of reconstructed frames after history reset (less than "maxFastAccumulatedFrameNum")
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

        // (> 0) - normal edge stopper for history reconstruction pass
        float historyFixEdgeStoppingNormalPower = 8.0f;

        // [1; 3] - standard deviation scale of color box for clamping main "slow" history to responsive "fast" history
        float historyClampingColorBoxSigmaScale = 2.0f;

        // (>= 0) - history length threshold below which spatial variance estimation will be executed
        uint32_t spatialVarianceEstimationHistoryThreshold = 3;

        // [2; 8] - number of iterations for A-Trous wavelet transform
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

        // Roughness based rejection
        bool enableRoughnessEdgeStopping = true;

        // Spatial passes do optional material index comparison as: ( materialEnabled ? material[ center ] == material[ sample ] : 1 )
        bool enableMaterialTestForDiffuse = false;
        bool enableMaterialTestForSpecular = false;
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
