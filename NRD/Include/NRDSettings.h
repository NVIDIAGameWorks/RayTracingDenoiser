/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nrd
{
    const uint32_t MAX_HISTORY_FRAME_NUM = 31;

    struct CommonSettings
    {
        // Requirements:
        // Matrix:
        //  -usage - vector is a column
        //  -layout - column-major
        //  -non jittered!
        //  - if IN_VIEWZ has "+" values, "viewToClip" should be left-handed
        //  - if IN_VIEWZ has "-" values, "viewToClip" should be right-handed
        // Jitter range:
        //  [-0.5; 0.5]
        // In "checkerboard" mode the input data has layout:
        //  Frame 0    Frame 1    ...
        //  D S        S D
        //  S D        D S

        float worldToViewMatrix[16] = {};
        float worldToViewMatrixPrev[16] = {};
        float viewToClipMatrix[16] = {};
        float viewToClipMatrixPrev[16] = {};
        float metersToUnitsMultiplier = 1.0f;
        float denoisingRange = 1e6f; // units
        float xMotionVectorScale = 1.0f;
        float yzMotionVectorScale = 1.0f;
        float xJitter = 0.0f;
        float yJitter = 0.0f;
        float debug = 0.0f;
        uint32_t frameIndex = 0;
        bool worldSpaceMotion = false;
        bool forceReferenceAccumulation = false;
    };

    struct HitDistanceParameters
    {
        // Hit distance (m) = "normalized hit distance" * ( A + viewZ * B ) * lerp( C, 1.0, exp2( -D * roughness ^ 2 ) )
        float A = 1.0f; // m
        float B = 0.0f; // m / units
        float C = 0.0f; // -
        float D = 0.0f; // -
    };

    struct LobeTrimmingParameters
    {
        // Trimming = A * smoothstep( B, C, roughness )
        float A = 0.85f;
        float B = 0.04f;
        float C = 0.11f;
    };

    struct AntilagSettings
    {
        // intensityOld and intensityNew come from "laggy" history buffers, so the actual radiance thresholds should be divided by ~64
        // delta = remap "abs( intensityOld - intensityNew )" to [0; 1] range using thresholds
        // antilag = F( delta )
        float intensityThresholdMin = 99999.0f; // depends on many factors, but in general it's "some percent of the local average of the final image"
        float intensityThresholdMax = 100000.0f; // max > min, usually 3-4x times greater than min

        // Enables "hit distance" and "intensity" tracking (the latter can be turned off by huge thresholds)
        bool enable = true;
    };

    struct DiffuseSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        AntilagSettings antilagSettings = {};
        uint32_t maxAccumulatedFrameNum = MAX_HISTORY_FRAME_NUM; // count (use 0 for one frame to reset history)
        float disocclusionThreshold = 0.005f; // normalized %
        float denoisingRadius = 30.0f; // pixels
        float maxAdaptiveRadiusScale = 5.0f; // 0-10
        bool checkerboard = false;
    };

    struct SpecularSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        LobeTrimmingParameters lobeTrimmingParameters = {};
        AntilagSettings antilagSettings = {};
        uint32_t maxAccumulatedFrameNum = MAX_HISTORY_FRAME_NUM; // count (use 0 for one frame to reset history)
        float disocclusionThreshold = 0.005f; // normalized %
        float denoisingRadius = 40.0f; // pixels
        float minAdaptiveRadiusScale = 0.5f;
        bool anisotropicFiltering = false;
        bool checkerboard = false;
    };

    struct ShadowSettings
    {
        float directionToLightSource[3] = {};
        float lightSourceAngularDiameter = 0.533f; // deg (0.533 = sun)
    };
}
