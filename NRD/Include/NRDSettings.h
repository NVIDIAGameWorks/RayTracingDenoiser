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
    Requirements:
    Matrix:
        -usage - vector is a column
        -layout - column-major
        -non jittered!
        - if IN_VIEWZ has "+" values, "viewToClip" should be left-handed
        - if IN_VIEWZ has "-" values, "viewToClip" should be right-handed
        - "worldToView" matrices are expected to be right-handed only!
    Jitter range: [-0.5; 0.5]
    */
    struct CommonSettings
    {
        float worldToViewMatrix[16] = {};
        float worldToViewMatrixPrev[16] = {};
        float viewToClipMatrix[16] = {};
        float viewToClipMatrixPrev[16] = {};
        float motionVectorScale[2] = {1.0f, 1.0f};  // if "worldSpaceMotion = true" will be used as "MV * motionVectorScale.xyy"
        float cameraJitter[2] = {0.0f, 0.0f};
        float denoisingRange = 10000.0f;            // (m) (> 0)
        float debug = 0.0f;
        uint32_t frameIndex = 0;                    // pass 0 for a single frame to reset history
        bool worldSpaceMotion = false;              // if "true" IN_MV is 3D motion in world space (0 should be everywhere if the scene is static), otherwise it's 2D screen-space motion (0 should be everywhere if the camera doesn't move)
        bool forceReferenceAccumulation = false;
    };

    // "Normalized hit distance" = saturate( "hit distance" / f ), where:
    // f = ( A + viewZ * B ) * lerp( 1.0, C, exp2( D * roughness ^ 2 ) ), see "NRD.hlsl/REBLUR_FrontEnd_CompressHitDistance"
    struct HitDistanceParameters
    {
        float A = 3.0f;     // constant value (m)
        float B = 0.1f;     // viewZ based linear scale (m / units) (1 m - 10 cm, 10 m - 1 m, 100 m - 10 m)
        float C = 5.0f;     // roughness based scale, "> 1" to get bigger hit distance for low roughness
        float D = -50.0f;   // roughness based exponential scale, "< 0", absolute value should be big enough to collapse "exp2( D * roughness ^ 2 )" to "~0" for roughness = 1
    };

    // Optional specular lobe trimming = A * smoothstep( B, C, roughness )
    // Recommended settings if lobe trimming is used = { 0.85f, 0.04f, 0.11f }
    struct LobeTrimmingParameters
    {
        float A = 1.0f;
        float B = 0.0f;
        float C = 0.0001f;
    };

    // Optional antilag settings
    // delta = remap "abs( old - new ) - localVariance * sigmaScale" to [0; 1] range using thresholds
    // antilag = F( delta ), delta = 0 - keep accumulation, delta = 1 - history reset
    struct IntensityAntilagSettings
    {
        float thresholdMin = 0.05f;          // normalized %
        float thresholdMax = 0.15f;          // max > min, usually 3-4x times greater than min
        float sigmaScale = 2.0f;            // plain "delta" is reduced by local variance multiplied by this value (2 - is a good start, 0.5-1.5 - can be used in many cases)
        bool enable = true;
    };

    struct HitDistanceAntilagSettings
    {
        float thresholdMin = 0.02f;         // normalized %, can be slightly increased if noise in AO/SO is high
        float thresholdMax = 0.10f;          // 10% is a good start
        float sigmaScale = 2.0f;            // "delta" will be reduced by local variance multiplied by this value (2 - is a good start, 0.5-1.5 - can be used in many cases)
        bool enable = true;
    };

    // REBLUR_DIFFUSE

    const uint32_t REBLUR_MAX_HISTORY_FRAME_NUM = 63;

    struct ReblurDiffuseSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        IntensityAntilagSettings intensityAntilagSettings = {};
        HitDistanceAntilagSettings hitDistanceAntilagSettings = {};
        float disocclusionThreshold = 0.005f;                           // normalized %
        float planeDistanceSensitivity = 0.002f;                        // > 0 (m) - viewZ 1m => only 2 mm deviations from surface plane are allowed
        uint32_t maxAccumulatedFrameNum = 31;                           // 0 - REBLUR_MAX_HISTORY_FRAME_NUM
        float blurRadius = 30.0f;                                       // base (worst) denoising radius (pixels)
        float maxAdaptiveRadiusScale = 5.0f;                            // adaptive radius scale, comes into play if error is high (0-10)
        float noisinessBlurrinessBalance = 1.0f;
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;
    };

    // REBLUR_SPECULAR

    struct ReblurSpecularSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        LobeTrimmingParameters lobeTrimmingParameters = {};
        IntensityAntilagSettings intensityAntilagSettings = {};
        HitDistanceAntilagSettings hitDistanceAntilagSettings = {};
        float disocclusionThreshold = 0.005f;                           // normalized %
        float planeDistanceSensitivity = 0.002f;                        // > 0 (m) - viewZ 1m => only 2 mm deviations from surface plane are allowed
        uint32_t maxAccumulatedFrameNum = 31;                           // 0 - REBLUR_MAX_HISTORY_FRAME_NUM
        float blurRadius = 30.0f;                                       // base (worst) denoising radius (pixels)
        float maxAdaptiveRadiusScale = 5.0f;                            // adaptive radius scale, comes into play if error is high (0-10)
        float noisinessBlurrinessBalance = 1.0f;
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;
    };

    // REBLUR_DIFFUSE_SPECULAR

    struct ReblurDiffuseSpecularSettings
    {
        HitDistanceParameters diffHitDistanceParameters = {};
        HitDistanceParameters specHitDistanceParameters = {};
        LobeTrimmingParameters specLobeTrimmingParameters = {};
        IntensityAntilagSettings intensityAntilagSettings = {};
        HitDistanceAntilagSettings hitDistanceAntilagSettings = {};
        float disocclusionThreshold = 0.005f;                            // normalized %
        float planeDistanceSensitivity = 0.002f;                         // > 0 (m) - viewZ 1m => only 2 mm deviations from surface plane are allowed
        uint32_t diffMaxAccumulatedFrameNum = 31;                        // 0 - REBLUR_MAX_HISTORY_FRAME_NUM
        uint32_t specMaxAccumulatedFrameNum = 31;                        // 0 - REBLUR_MAX_HISTORY_FRAME_NUM
        float diffBlurRadius = 30.0f;                                    // base (worst) diffuse denoising radius (pixels)
        float specBlurRadius = 30.0f;                                    // base (worst) specular denoising radius (pixels)
        float diffMaxAdaptiveRadiusScale = 5.0f;                         // adaptive radius scale, comes into play if error is high (0-10)
        float specMaxAdaptiveRadiusScale = 5.0f;                         // adaptive radius scale, comes into play if error is high (0-10)
        float diffNoisinessBlurrinessBalance = 1.0f;
        float specNoisinessBlurrinessBalance = 1.0f;
        CheckerboardMode diffCheckerboardMode = CheckerboardMode::OFF;
        CheckerboardMode specCheckerboardMode = CheckerboardMode::OFF;
    };

    // SIGMA_SHADOW and SIGMA_TRANSLUCENT_SHADOW

    struct SigmaShadowSettings
    {
        float lightSourceAngularDiameter = 0.533f;  // angular diameter (deg) (0.533 = sun)
        float planeDistanceSensitivity = 0.002f;    // > 0 (m) - viewZ 1m => only 2 mm deviations from surface plane are allowed
        float blurRadiusScale = 1.0f;               // adds bias if > 1, but if shadows are still unstable (have you tried blue noise?)... can be set in range [1; 1.5]
    };

    // RELAX_DIFFUSE_SPECULAR

    struct RelaxDiffuseSpecularSettings
    {
        bool bicubicFilterForReprojectionEnabled = true;        // slower but sharper filtering of the history during reprojection
        float specularAlpha = 0.016f;                           // new data blend weight for normal illumination temporal accumulation
        float specularResponsiveAlpha = 0.1f;                   // new data blend weight for responsive illumination temporal accumulation
        float diffuseAlpha = 0.016f;                            // new data blend weight for normal illumination temporal accumulation
        float diffuseResponsiveAlpha = 0.1f;                    // new data blend weight for responsive illumination temporal accumulation
        float specularVarianceBoost = 1.0f;                     // how much variance we inject to specular if reprojection confidence is low
        bool debugOutputReprojectionEnabled = false;            // enable debug output with reprojection results

        float disocclusionFixEdgeStoppingZFraction = 0.01f;     // depth edge stopper for cross-bilateral sparse filter
        float disocclusionFixEdgeStoppingNormalPower = 8.0f;    // normal edge stopper for cross-bilateral sparse filter
        float disocclusionFixMaxRadius = 14.0f;                 // maximum radius for sparse bilateral filter, expressed in pixels
        int32_t disocclusionFixNumFramesToFix = 3;              // cross-bilateral sparse filter will be applied to frames with history length shorter than this value

        float historyClampingColorBoxSigmaScale = 1.0f;         // scale for standard deviation of color box for clamping normal history color to responsive history color
        float specularAntiLagColorBoxSigmaScale = 2.0f;         // scale for standard deviation of color box for lag detection
        float specularAntiLagPower = 0.0f;                      // amount of history shortening when lag is detected
        float diffuseAntiLagColorBoxSigmaScale = 2.0f;          // scale for standard deviation of color box for lag detection
        float diffuseAntiLagPower = 0.0f;                       // amount of history shortening when lag is detected

        bool fireflySuppressionEnabled = false;                 // firefly suppression

        int32_t spatialVarianceEstimationHistoryThreshold = 3;  // history length threshold below which spatial variance estimation will be executed
        int32_t atrousIterations = 5;                           // number of iteration for A-Trous wavelet transform
        float specularPhiLuminance = 2.0f;                      // A-trous edge stopping Luminance sensitivity
        float diffusePhiLuminance = 2.0f;                       // A-trous edge stopping Luminance sensitivity
        float phiNormal = 64.0f;                                // A-trous edge stopping Normal sensitivity for diffuse
        float phiDepth = 0.05f;                                 // A-trous edge stopping Depth sensitivity
        float roughnessEdgeStoppingRelaxation = 0.3f;           // how much we relax roughness based rejection in areas where specular reprojection is low
        float normalEdgeStoppingRelaxation = 0.3f;              // how much we relax normal based rejection in areas where specular reprojection is low
        float luminanceEdgeStoppingRelaxation = 1.0f;           // how much we relax luminance based rejection in areas where specular reprojection is low
    };

    // SVGF

    const uint32_t SVGF_MAX_HISTORY_FRAME_NUM = 255;

    struct SvgfSettings
    {
        uint32_t maxAccumulatedFrameNum = 31;           // 0 - SVGF_MAX_HISTORY_FRAME_NUM
        uint32_t momentsMaxAccumulatedFrameNum = 5;     // 0 - SVGF_MAX_HISTORY_FRAME_NUM
        float disocclusionThreshold = 0.005f;           // 0.005 - 0.05 (normalized %)
        float varianceScale = 1.5f;
        float zDeltaScale = 200.0f;
        bool isDiffuse = false;
    };
}
