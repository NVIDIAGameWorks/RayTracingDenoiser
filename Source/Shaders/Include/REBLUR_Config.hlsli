/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define REBLUR_DEBUG                                            0 // 0 - 8 ( output can be in radiance, AO or SO )
#define REBLUR_DEBUG_ERROR_NORMALIZATION                        0.15 // normalized % ( 0.04 for hit distance, 0.10 for color )

// Switches
#define REBLUR_USE_LIMITED_ANTILAG                              0 // especially useful if the input signal has fireflies with gigantic energy
#define REBLUR_USE_HISTORY_FIX                                  1
#define REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA              1
#define REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA              1
#define REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS              1
#define REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS              1
#define REBLUR_USE_ANISOTROPIC_KERNEL                           1
#define REBLUR_USE_ACCUM_SPEED_NONLINEAR_INTERPOLATION          1
#define REBLUR_USE_5X5_HISTORY_CLAMPING                         1
#define REBLUR_USE_COLOR_CLAMPING_AABB                          0
#define REBLUR_USE_WIDER_KERNEL_IN_HISTORY_FIX                  0 // 1 - 1.9.0 behavior, 0 - new ( is it better? )
#define REBLUR_USE_SPATIAL_REUSE_FOR_HIT_DIST                   0 // spatial reuse is about multi-bounce nature of light. Corners become darker if reuse is ON
#define REBLUR_USE_COMPRESSION_FOR_DIFFUSE                      0 // we know that exposure should be 0 for roughess = 1
#define REBLUR_USE_SCREEN_SPACE_SAMPLING                        0

// Other
#define REBLUR_POISSON_SAMPLE_NUM                               8
#define REBLUR_POISSON_SAMPLES( i )                             g_Poisson8[ i ]
#define REBLUR_PRE_BLUR_POISSON_SAMPLE_NUM                      8
#define REBLUR_PRE_BLUR_POISSON_SAMPLES( i )                    g_Poisson8[ i ]
#define REBLUR_PRE_BLUR_ROTATOR_MODE                            NRD_FRAME
#define REBLUR_PRE_BLUR_INTERNAL_DATA                           float2( 1.0 / ( 1.0 + 8.0 ), 8.0 )
#define REBLUR_PRE_BLUR_RADIUS_SCALE( r )                       ( lerp( 1.0, 0.5, r ) / REBLUR_PRE_BLUR_INTERNAL_DATA.x )
#define REBLUR_PRE_BLUR_SPATIAL_REUSE_BASE_RADIUS_SCALE         0.5
#define REBLUR_BLUR_ROTATOR_MODE                                NRD_FRAME
#define REBLUR_BLUR_NORMAL_WEIGHT_RELAXATION                    2.0
#define REBLUR_POST_BLUR_ROTATOR_MODE                           NRD_PIXEL
#define REBLUR_POST_BLUR_RADIUS_SCALE                           2.0
#define REBLUR_POST_BLUR_STRICTNESS                             0.5
#define REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE               2.0
#define REBLUR_SPEC_ACCUM_BASE_POWER                            0.5 // previously was 0.66 ( less agressive accumulation, but virtual reprojection works well on flat surfaces and fixes the issue )
#define REBLUR_SPEC_ACCUM_CURVE                                 0.5 // aggressiveness of history rejection depending on viewing angle ( 1 - low, 0.66 - medium, 0.5 - high )
#define REBLUR_SPEC_BASIS_ROUGHNESS_THRESHOLD                   0.8
#define REBLUR_SPEC_DOMINANT_DIRECTION                          STL_SPECULAR_DOMINANT_DIRECTION_G2
#define REBLUR_PARALLAX_NORMALIZATION                           ( 30 + 30.0 * gReference )
#define REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE                 0.05
#define REBLUR_TS_SIGMA_AMPLITUDE                               ( 3.0 * max( gFramerateScale, 0.5 ) )
#define REBLUR_TS_MAX_HISTORY_WEIGHT                            ( ( 16.0 * gFramerateScale - 1.0 ) / ( 16.0 * gFramerateScale ) )
#define REBLUR_MIP_NUM                                          4.999
#define REBLUR_HIT_DIST_MIN_WEIGHT                              0.2
#define REBLUR_HIT_DIST_MIN_ACCUM_SPEED( r )                    lerp( 0.2, 0.1, STL::Math::Sqrt01( r ) )
#define REBLUR_HIT_DIST_ACCELERATION                            float2( 1.0, 0.5 ) // .y is used to accelerate hit distance accumulation
#define REBLUR_INPUT_MIX                                        float2( 0, 0 ) // ( .x - radiance, .y - hit distance ) preserves sharpness ( 0 - take output, 1 - take input ), can affects antilag and hit distance tracking if variance is high

// Shared data
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
    NRD_CONSTANT( float, gResidualNoiseLevel ) \
    NRD_CONSTANT( float, gDiffMaxAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gDiffMaxFastAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gSpecMaxAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gSpecMaxFastAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gMeterToUnitsMultiplier ) \
    NRD_CONSTANT( uint, gWorldSpaceMotion ) \
    NRD_CONSTANT( uint, gFrameIndex ) \
    NRD_CONSTANT( uint, gResetHistory ) \
    NRD_CONSTANT( uint, gIsRadianceMultipliedByExposure ) \
    NRD_CONSTANT( uint, gUnused1 ) \
    NRD_CONSTANT( uint, gUnused2 ) \
    NRD_CONSTANT( uint, gUnused3 )

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
    NRD_CONSTANT( float, gResidualNoiseLevel ) \
    NRD_CONSTANT( float, gJitterDelta ) \
    NRD_CONSTANT( float, gMeterToUnitsMultiplier ) \
    NRD_CONSTANT( uint, gWorldSpaceMotion ) \
    NRD_CONSTANT( uint, gFrameIndex ) \
    NRD_CONSTANT( uint, gResetHistory ) \
    NRD_CONSTANT( uint, gIsRadianceMultipliedByExposure ) \
    NRD_CONSTANT( uint, gUnused1 ) \
    NRD_CONSTANT( uint, gUnused2 ) \
    NRD_CONSTANT( uint, gUnused3 )

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
    NRD_CONSTANT( float, gResidualNoiseLevel ) \
    NRD_CONSTANT( float, gJitterDelta ) \
    NRD_CONSTANT( float, gSpecBlurRadius ) \
    NRD_CONSTANT( float, gSpecMaxAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gSpecMaxFastAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gMeterToUnitsMultiplier ) \
    NRD_CONSTANT( uint, gWorldSpaceMotion ) \
    NRD_CONSTANT( uint, gFrameIndex ) \
    NRD_CONSTANT( uint, gResetHistory ) \
    NRD_CONSTANT( uint, gIsRadianceMultipliedByExposure ) \
    NRD_CONSTANT( uint, gUnused1 ) \
    NRD_CONSTANT( uint, gUnused2 ) \
    NRD_CONSTANT( uint, gUnused3 )

#if( !defined REBLUR_DIFFUSE && !defined REBLUR_SPECULAR )
    #define REBLUR_DIFFUSE
    #define REBLUR_SPECULAR
#endif
