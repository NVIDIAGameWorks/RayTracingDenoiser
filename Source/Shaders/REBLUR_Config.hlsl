/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define REBLUR_USE_LIMITED_ANTILAG                              1 // especially useful if the input signal has fireflies with gigantic energy
#define REBLUR_USE_FASTER_BUT_DIRTIER_ANTILAG                   0 // a bit better response, but potentially leaves more dirt (if variance is high)
#define REBLUR_USE_HISTORY_FIX                                  1
#define REBLUR_USE_ANTILAG                                      1
#define REBLUR_USE_CATROM_RESAMPLING_IN_TA                      1
#define REBLUR_USE_CATROM_RESAMPLING_IN_TS                      1
#define REBLUR_USE_ANISOTROPIC_KERNEL                           1
#define REBLUR_USE_FAST_HISTORY                                 1
#define REBLUR_USE_ANTI_FIREFLY                                 1
#define REBLUR_USE_ACCUM_SPEED_NONLINEAR_INTERPOLATION          1
#define REBLUR_USE_5X5_HISTORY_CLAMPING                         1
#define REBLUR_USE_COLOR_CLAMPING_AABB                          0
#define REBLUR_USE_DOMINANT_DIRECTION_IN_WEIGHT                 0
#define REBLUR_USE_WIDER_KERNEL_IN_HISTORY_FIX                  0 // 1 - 1.9.0 behavior, 0 - new (is it better?)
#define REBLUR_USE_CURRENT_HIT_DIST_FOR_VIRTUAL_MOTION          1

#define REBLUR_PRE_BLUR_ROTATOR_MODE                            NRD_FRAME
#define REBLUR_PRE_BLUR_INTERNAL_DATA                           float2( 1.0 / ( 1.0 + 8.0 ), 8.0 )
#define REBLUR_PRE_BLUR_RADIUS_SCALE( r )                       ( lerp( 1.0, 0.5, r ) / REBLUR_PRE_BLUR_INTERNAL_DATA.x )

#define REBLUR_BLUR_ROTATOR_MODE                                NRD_FRAME

#define REBLUR_POST_BLUR_ROTATOR_MODE                           NRD_PIXEL
#define REBLUR_POST_BLUR_RADIUS_SCALE                           2.0

#define REBLUR_SPEC_ACCUM_BASE_POWER                            0.5 // previously was 0.66 (less agressive accumulation, but virtual reprojection works well on flat surfaces and fixes the issue)
#define REBLUR_SPEC_ACCUM_CURVE                                 1.0 // aggressiveness of history rejection depending on viewing angle (1 - low, 0.66 - medium, 0.5 - high)
#define REBLUR_SPEC_BASIS_ROUGHNESS_THRESHOLD                   0.8
#define REBLUR_SPEC_MAX_VIRTUAL_MOTION_COSA                     cos( STL::Math::DegToRad( 2.0 ) )
#define REBLUR_SPEC_DOMINANT_DIRECTION                          STL_SPECULAR_DOMINANT_DIRECTION_G2

#define REBLUR_POISSON_SAMPLE_NUM                               8
#define REBLUR_POISSON_SAMPLES                                  g_Poisson8
#define REBLUR_NORMAL_BANDING_FIX                               STL::Math::DegToRad( 0.625 ) // mitigate banding introduced by normals stored in RGB8 format // TODO: move to CommonSettings?
#define REBLUR_PARALLAX_NORMALIZATION                           ( 30 + 30.0 * gReference )
#define REBLUR_CHECKERBOARD_SIDE_WEIGHT                         0.6
#define REBLUR_ANTILAG_MIN                                      0.02
#define REBLUR_ANTILAG_MAX                                      0.1
#define REBLUR_TS_MOTION_MAX_REUSE                              0.11
#define REBLUR_TS_WEIGHT_BOOST_POWER                            0.25
#define REBLUR_TS_SIGMA_AMPLITUDE                               ( 3.0 * max( gFramerateScale, 0.5 ) )
#define REBLUR_TS_MAX_HISTORY_WEIGHT                            ( ( 16.0 * gFramerateScale - 1.0 ) / ( 16.0 * gFramerateScale ) )
#define REBLUR_MIP_NUM                                          4.999
#define REBLUR_HIT_DIST_MIN_WEIGHT                              0.2
#define REBLUR_MAX_ERROR_AMPLITUDE                              0.05 // 0-1
#define REBLUR_HIT_DIST_MIN_ACCUM_SPEED( r )                    lerp( 0.2, 0.1, STL::Math::Sqrt01( r ) )
#define REBLUR_INPUT_MIX                                        float2( 0, 0 ) // (.x - radiance, .y - hit distance) preserves sharpness (0 - take output, 1 - take input), can affects antilag and hit distance tracking if variance is high

// Overrides
#define NRD_USE_CATROM_RESAMPLING                               REBLUR_USE_CATROM_RESAMPLING_IN_TS

// Shared data
#define REBLUR_DIFF_SHARED_CB_DATA \
    NRD_CONSTANT( float4x4, gViewToClip ) \
    NRD_CONSTANT( float4, gFrustum ) \
    NRD_CONSTANT( float4, gDiffHitDistParams ) \
    NRD_CONSTANT( float2, gInvScreenSize ) \
    NRD_CONSTANT( float2, gScreenSize ) \
    NRD_CONSTANT( float2, gInvRectSize ) \
    NRD_CONSTANT( float2, gRectSize ) \
    NRD_CONSTANT( float2, gRectSizePrev ) \
    NRD_CONSTANT( float2, gResolutionScale ) \
    NRD_CONSTANT( float2, gRectOffset ) \
    NRD_CONSTANT( uint2, gRectOrigin ) \
    NRD_CONSTANT( float, gReference ) \
    NRD_CONSTANT( float, gIsOrtho ) \
    NRD_CONSTANT( float, gUnproject ) \
    NRD_CONSTANT( float, gDebug ) \
    NRD_CONSTANT( float, gInf ) \
    NRD_CONSTANT( float, gPlaneDistSensitivity ) \
    NRD_CONSTANT( float, gFramerateScale ) \
    NRD_CONSTANT( float, gDiffBlurRadius ) \
    NRD_CONSTANT( float, gDiffMaxAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gDiffMaxFastAccumulatedFrameNum ) \
    NRD_CONSTANT( uint, gWorldSpaceMotion ) \
    NRD_CONSTANT( uint, gFrameIndex )

#define REBLUR_DIFF_SPEC_SHARED_CB_DATA \
    NRD_CONSTANT( float4x4, gViewToClip ) \
    NRD_CONSTANT( float4, gFrustum ) \
    NRD_CONSTANT( float4, gDiffHitDistParams ) \
    NRD_CONSTANT( float4, gSpecHitDistParams ) \
    NRD_CONSTANT( float2, gInvScreenSize ) \
    NRD_CONSTANT( float2, gScreenSize ) \
    NRD_CONSTANT( float2, gInvRectSize ) \
    NRD_CONSTANT( float2, gRectSize ) \
    NRD_CONSTANT( float2, gRectSizePrev ) \
    NRD_CONSTANT( float2, gResolutionScale ) \
    NRD_CONSTANT( float2, gRectOffset ) \
    NRD_CONSTANT( uint2, gRectOrigin ) \
    NRD_CONSTANT( float, gReference ) \
    NRD_CONSTANT( float, gIsOrtho ) \
    NRD_CONSTANT( float, gUnproject ) \
    NRD_CONSTANT( float, gDebug ) \
    NRD_CONSTANT( float, gInf ) \
    NRD_CONSTANT( float, gPlaneDistSensitivity ) \
    NRD_CONSTANT( float, gFramerateScale ) \
    NRD_CONSTANT( float, gJitterDelta ) \
    NRD_CONSTANT( float, gDiffMaxAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gDiffMaxFastAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gSpecMaxAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gSpecMaxFastAccumulatedFrameNum ) \
    NRD_CONSTANT( uint, gWorldSpaceMotion ) \
    NRD_CONSTANT( uint, gFrameIndex ) \
    NRD_CONSTANT( uint, gUnused0 ) \
    NRD_CONSTANT( uint, gUnused1 ) // TODO: unused

#define REBLUR_SPEC_SHARED_CB_DATA \
    NRD_CONSTANT( float4x4, gViewToClip ) \
    NRD_CONSTANT( float4, gFrustum ) \
    NRD_CONSTANT( float4, gSpecHitDistParams ) \
    NRD_CONSTANT( float2, gInvScreenSize ) \
    NRD_CONSTANT( float2, gScreenSize ) \
    NRD_CONSTANT( float2, gInvRectSize ) \
    NRD_CONSTANT( float2, gRectSize ) \
    NRD_CONSTANT( float2, gRectSizePrev ) \
    NRD_CONSTANT( float2, gResolutionScale ) \
    NRD_CONSTANT( float2, gRectOffset ) \
    NRD_CONSTANT( uint2, gRectOrigin ) \
    NRD_CONSTANT( float, gReference ) \
    NRD_CONSTANT( float, gIsOrtho ) \
    NRD_CONSTANT( float, gUnproject ) \
    NRD_CONSTANT( float, gDebug ) \
    NRD_CONSTANT( float, gInf ) \
    NRD_CONSTANT( float, gPlaneDistSensitivity ) \
    NRD_CONSTANT( float, gFramerateScale ) \
    NRD_CONSTANT( float, gJitterDelta ) \
    NRD_CONSTANT( float, gSpecBlurRadius ) \
    NRD_CONSTANT( float, gSpecMaxAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gSpecMaxFastAccumulatedFrameNum ) \
    NRD_CONSTANT( uint, gWorldSpaceMotion ) \
    NRD_CONSTANT( uint, gFrameIndex ) \
    NRD_CONSTANT( uint, gUnused2 ) \
    NRD_CONSTANT( uint, gUnused3 ) \
    NRD_CONSTANT( uint, gUnused4 ) // TODO: unused
