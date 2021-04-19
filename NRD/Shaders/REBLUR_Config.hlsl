/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define REBLUR_USE_LIMITED_ANTILAG                              0 // TODO: useful if the input signal has fireflies with gigantic energy
#define REBLUR_USE_FASTER_BUT_DIRTIER_ANTILAG                   0 // a bit better response, but potentially leaves more dirt (if variance is high)
#define REBLUR_USE_HISTORY_FIX                                  1
#define REBLUR_USE_ANTILAG                                      1
#define REBLUR_USE_CATROM_RESAMPLING_IN_TA                      1
#define REBLUR_USE_CATROM_RESAMPLING_IN_TS                      1
#define REBLUR_USE_ANISOTROPIC_KERNEL                           1 // TODO: add anisotropy support for shadows!
#define REBLUR_USE_FAST_HISTORY                                 1
#define REBLUR_USE_ANTI_FIREFLY                                 1
#define REBLUR_USE_ACCUM_SPEED_NONLINEAR_INTERPOLATION          1
#define REBLUR_5X5_HISTORY_CLAMPING                             1

#define REBLUR_PRE_BLUR_ROTATOR_MODE                            FRAME
#define REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED                  ( 1.0 / 8.0 )
#define REBLUR_PRE_BLUR_RADIUS_SCALE( r )                       ( lerp( 1.0, 0.5, r ) / REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED )

#define REBLUR_BLUR_ROTATOR_MODE                                FRAME

#define REBLUR_POST_BLUR_ROTATOR_MODE                           PIXEL
#define REBLUR_POST_BLUR_RADIUS_SCALE                           2.0

#define REBLUR_SPEC_ACCUM_BASE_POWER                            0.5 // previously was 0.66 (less agressive accumulation, but virtual reprojection works well on flat surfaces and fixes the issue)
#define REBLUR_SPEC_ACCUM_CURVE                                 1.0 // aggressiveness of history rejection depending on viewing angle (1 - low, 0.66 - medium, 0.5 - high)
#define REBLUR_SPEC_BASIS_ROUGHNESS_THRESHOLD                   0.8
#define REBLUR_SPEC_MAX_VIRTUAL_MOTION_COSA                     cos( STL::Math::DegToRad( 2.0 ) )
#define REBLUR_SPEC_DOMINANT_DIRECTION                          STL_SPECULAR_DOMINANT_DIRECTION_G2

#define REBLUR_POISSON_SAMPLE_NUM                               8
#define REBLUR_POISSON_SAMPLES                                  g_Poisson8
#define REBLUR_NORMAL_BANDING_FIX                               STL::Math::DegToRad( 0.625 ) // mitigate banding introduced by normals stored in RGB8 format // TODO: move to CommonSettings?
#define REBLUR_PARALLAX_COMPRESSION_STRENGTH                    0 // TODO: 0.1?
#define REBLUR_PARALLAX_NORMALIZATION                           ( 60.0 - 30.0 * ( 1.0 - gReference ) * REBLUR_USE_FAST_HISTORY )
#define REBLUR_CHECKERBOARD_SIDE_WEIGHT                         0.6
#define REBLUR_FAST_HISTORY_SIGMA_AMPLITUDE                     1.0
#define REBLUR_ANTILAG_MIN                                      0.02
#define REBLUR_ANTILAG_MAX                                      0.1
#define REBLUR_TS_MOTION_MAX_REUSE                              0.11
#define REBLUR_TS_WEIGHT_BOOST_POWER                            0.25
#define REBLUR_TS_SIGMA_AMPLITUDE                               ( 3.0 * gFramerateScale )
#define REBLUR_TS_MAX_HISTORY_WEIGHT                            ( ( 16.0 * gFramerateScale - 1.0 ) / ( 16.0 * gFramerateScale ) )
#define REBLUR_MIP_NUM                                          3.999
#define REBLUR_HIT_DIST_MIN_WEIGHT                              0.2
#define REBLUR_HIT_DIST_MIN_ACCUM_SPEED( r )                    lerp( 0.2, 0.1, STL::Math::Sqrt01( r ) )
#define REBLUR_HIT_DIST_INPUT_MIX                               0 // preserves sharpness of hit distances (0 - take output, 1 - take input), can affects antilag and hit distance tracking if variance is high

// Overrides
#define NRD_USE_CATROM_RESAMPLING                               REBLUR_USE_CATROM_RESAMPLING_IN_TS

// Shared data
#define REBLUR_DIFF_SHARED_CB_DATA \
    float4x4 gViewToClip; \
    float4 gFrustum; \
    float2 gInvScreenSize; \
    float2 gScreenSize; \
    float2 gInvRectSize; \
    float2 gRectSize; \
    float2 gRectSizePrev; \
    float2 gResolutionScale; \
    float2 gRectOffset; \
    uint2 gRectOrigin; \
    float gReference; \
    float gIsOrtho; \
    float gUnproject; \
    float gDebug; \
    float gInf; \
    float gPlaneDistSensitivity; \
    float gFramerateScale; \
    float gDiffBlurRadius; \
    float gDiffMaxAccumulatedFrameNum; \
    float gDiffMaxFastAccumulatedFrameNum; \
    uint gWorldSpaceMotion; \
    uint gFrameIndex

#define REBLUR_DIFF_SPEC_SHARED_CB_DATA \
    float4x4 gViewToClip; \
    float4 gFrustum; \
    float2 gInvScreenSize; \
    float2 gScreenSize; \
    float2 gInvRectSize; \
    float2 gRectSize; \
    float2 gRectSizePrev; \
    float2 gResolutionScale; \
    float2 gRectOffset; \
    uint2 gRectOrigin; \
    float gReference; \
    float gIsOrtho; \
    float gUnproject; \
    float gDebug; \
    float gInf; \
    float gPlaneDistSensitivity; \
    float gFramerateScale; \
    float gJitterDelta; \
    float gDiffMaxAccumulatedFrameNum; \
    float gSpecMaxAccumulatedFrameNum; \
    uint gWorldSpaceMotion; \
    uint gFrameIndex

#define REBLUR_SPEC_SHARED_CB_DATA \
    float4x4 gViewToClip; \
    float4 gFrustum; \
    float2 gInvScreenSize; \
    float2 gScreenSize; \
    float2 gInvRectSize; \
    float2 gRectSize; \
    float2 gRectSizePrev; \
    float2 gResolutionScale; \
    float2 gRectOffset; \
    uint2 gRectOrigin; \
    float gReference; \
    float gIsOrtho; \
    float gUnproject; \
    float gDebug; \
    float gInf; \
    float gPlaneDistSensitivity; \
    float gFramerateScale; \
    float gJitterDelta; \
    float gSpecBlurRadius; \
    float gSpecMaxAccumulatedFrameNum; \
    uint gWorldSpaceMotion; \
    uint gFrameIndex

