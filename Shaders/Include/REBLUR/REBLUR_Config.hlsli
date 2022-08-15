/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define REBLUR_DEBUG                                            0 // 0+ ( output can be in radiance, AO or SO )

// Switches ( default 1 )
#define REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA              1
#define REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA              1
#define REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS              1
#define REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS              1
#define REBLUR_USE_HISTORY_FIX                                  1
#define REBLUR_USE_YCOCG                                        1
#define REBLUR_USE_FAST_HISTORY                                 1
#define REBLUR_USE_5X5_ANTI_FIREFLY                             1 // can be 0 for clean signals

// Switches ( default 0 )
#define REBLUR_USE_SCREEN_SPACE_SAMPLING                        0
#define REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX             0
#define REBLUR_USE_ACCUM_SPEED_NONLINEAR_INTERPOLATION          0
#define REBLUR_USE_DECOMPRESSED_HIT_DIST_IN_RECONSTRUCTION      0 // compression helps to preserve "lobe important" values

// Experimental kernels
#ifndef __cplusplus
    // https://www.desmos.com/calculator/e5mttzlg6v
    static const float3 g_Special6[ 6 ] =
    {
        float3( -0.50 * sqrt(3.0) , -0.50             , 1.0 ),
        float3(  0.00             ,  1.00             , 1.0 ),
        float3(  0.50 * sqrt(3.0) , -0.50             , 1.0 ),
        float3(  0.00             , -0.30             , 0.3 ),
        float3(  0.15 * sqrt(3.0) ,  0.15             , 0.3 ),
        float3( -0.15 * sqrt(3.0) ,  0.15             , 0.3 ),
    };

    // https://www.desmos.com/calculator/abaqyvswem
    static const float3 g_Special8[ 8 ] =
    {
        float3( -1.00             ,  0.00             , 1.0 ),
        float3(  0.00             ,  1.00             , 1.0 ),
        float3(  1.00             ,  0.00             , 1.0 ),
        float3(  0.00             , -1.00             , 1.0 ),
        float3( -0.25 * sqrt(2.0) ,  0.25 * sqrt(2.0) , 0.5 ),
        float3(  0.25 * sqrt(2.0) ,  0.25 * sqrt(2.0) , 0.5 ),
        float3(  0.25 * sqrt(2.0) , -0.25 * sqrt(2.0) , 0.5 ),
        float3( -0.25 * sqrt(2.0) , -0.25 * sqrt(2.0) , 0.5 )
    };
#endif

// Other
#define REBLUR_POISSON_SAMPLE_NUM                               8
#define REBLUR_POISSON_SAMPLES( i )                             g_Poisson8[ i ]
#define REBLUR_PRE_BLUR_POISSON_SAMPLE_NUM                      8
#define REBLUR_PRE_BLUR_POISSON_SAMPLES( i )                    g_Poisson8[ i ]

#define REBLUR_PRE_BLUR_ROTATOR_MODE                            NRD_FRAME
#define REBLUR_PRE_BLUR_INTERNAL_DATA                           float2( 1.0 / ( 1.0 + 8.0 ), 8.0 ) // TODO: 15-30?

#define REBLUR_BLUR_ROTATOR_MODE                                NRD_FRAME
#define REBLUR_BLUR_FRACTION_SCALE                              2.0

#define REBLUR_POST_BLUR_ROTATOR_MODE                           NRD_FRAME
#define REBLUR_POST_BLUR_RADIUS_SCALE                           3.0
#define REBLUR_POST_BLUR_FRACTION_SCALE                         0.5

#define REBLUR_VIRTUAL_MOTION_NORMAL_WEIGHT_ITERATION_NUM       2
#define REBLUR_COLOR_CLAMPING_SIGMA_SCALE                       2.0
#define REBLUR_SPEC_ACCUM_BASE_POWER                            ( 0.4 + 0.2 * exp2( -gFramerateScale ) ) // bigger values = more aggressive rejection
#define REBLUR_SPEC_ACCUM_CURVE                                 ( 1.0 - exp2( -gFramerateScale ) ) // smaller values = more aggressive rejection
#define REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE                 0.05
#define REBLUR_TS_SIGMA_AMPLITUDE                               ( 3.0 * gFramerateScale )
#define REBLUR_TS_ACCUM_TIME                                    ( gFramerateScale * 30.0 * 0.5 ) // = FPS * seconds
#define REBLUR_PARALLAX_SCALE                                   ( 2.0 * gFramerateScale ) // TODO: is it possible to use 1 with tweaks in other parameters?
#define REBLUR_FIXED_FRAME_NUM                                  3.0 // TODO: move to settings
#define REBLUR_HISTORY_FIX_STEP                                 ( 10.0 * ( gRectSize.y / 1440.0 ) )  // pixels // TODO: gBlurRadius dependent if != 0?
#define REBLUR_HISTORY_FIX_THRESHOLD_1                          0.111 // was 0.01
#define REBLUR_HISTORY_FIX_THRESHOLD_2                          0.333 // was 0.25
#define REBLUR_HIT_DIST_MIN_WEIGHT                              0.1 // TODO: reduce?
#define REBLUR_MIN_PDF                                          0.001
#define REBLUR_MAX_FIREFLY_RELATIVE_INTENSITY                   float2( 10.0, 1.1 )

// Shared data
#define REBLUR_SHARED_CB_DATA \
    NRD_CONSTANT( float4x4, gViewToClip ) \
    NRD_CONSTANT( float4x4, gViewToWorld ) \
    NRD_CONSTANT( float4, gFrustum ) \
    NRD_CONSTANT( float4, gHitDistParams ) \
    NRD_CONSTANT( float4, gViewVectorWorld ) \
    NRD_CONSTANT( float4, gViewVectorWorldPrev ) \
    NRD_CONSTANT( float2, gInvScreenSize ) \
    NRD_CONSTANT( float2, gScreenSize ) \
    NRD_CONSTANT( float2, gInvRectSize ) \
    NRD_CONSTANT( float2, gRectSize ) \
    NRD_CONSTANT( float2, gRectSizePrev ) \
    NRD_CONSTANT( float2, gResolutionScale ) \
    NRD_CONSTANT( float2, gRectOffset ) \
    NRD_CONSTANT( float2, gSensitivityToDarkness ) \
    NRD_CONSTANT( uint2, gRectOrigin ) \
    NRD_CONSTANT( float, gReference ) \
    NRD_CONSTANT( float, gOrthoMode ) \
    NRD_CONSTANT( float, gUnproject ) \
    NRD_CONSTANT( float, gDebug ) \
    NRD_CONSTANT( float, gDenoisingRange ) \
    NRD_CONSTANT( float, gPlaneDistSensitivity ) \
    NRD_CONSTANT( float, gFramerateScale ) \
    NRD_CONSTANT( float, gBlurRadius ) \
    NRD_CONSTANT( float, gMaxAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gMaxFastAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gAntiFirefly ) \
    NRD_CONSTANT( float, gMinConvergedStateBaseRadiusScale ) \
    NRD_CONSTANT( float, gLobeAngleFraction ) \
    NRD_CONSTANT( float, gRoughnessFraction ) \
    NRD_CONSTANT( float, gResponsiveAccumulationRoughnessThreshold ) \
    NRD_CONSTANT( float, gDiffPrepassBlurRadius ) \
    NRD_CONSTANT( float, gSpecPrepassBlurRadius ) \
    NRD_CONSTANT( uint, gIsWorldSpaceMotionEnabled ) \
    NRD_CONSTANT( uint, gFrameIndex ) \
    NRD_CONSTANT( uint, gResetHistory ) \
    NRD_CONSTANT( uint, gDiffMaterialMask ) \
    NRD_CONSTANT( uint, gSpecMaterialMask )

// PERFORMANCE MODE: x1.25 perf boost by sacrificing IQ ( DIFFUSE_SPECULAR on RTX 3090 @ 1440p 2.05 vs 2.55 ms )
#ifdef REBLUR_PERFORMANCE_MODE
    #undef REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA
    #define REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA          0

    #undef REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA
    #define REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA          0

    #undef REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS
    #define REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS          0

    #undef REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS
    #define REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS          0

    #undef REBLUR_USE_FAST_HISTORY
    #define REBLUR_USE_FAST_HISTORY                             0

    #undef REBLUR_USE_SCREEN_SPACE_SAMPLING
    #define REBLUR_USE_SCREEN_SPACE_SAMPLING                    1

    #undef REBLUR_POISSON_SAMPLE_NUM
    #define REBLUR_POISSON_SAMPLE_NUM                           6

    #undef REBLUR_POISSON_SAMPLES
    #define REBLUR_POISSON_SAMPLES( i )                         g_Special6[ i ]

    #undef REBLUR_PRE_BLUR_POISSON_SAMPLE_NUM
    #define REBLUR_PRE_BLUR_POISSON_SAMPLE_NUM                  6

    #undef REBLUR_PRE_BLUR_POISSON_SAMPLES
    #define REBLUR_PRE_BLUR_POISSON_SAMPLES( i )                g_Special6[ i ]

    #undef REBLUR_PRE_BLUR_ROTATOR_MODE
    #define REBLUR_PRE_BLUR_ROTATOR_MODE                        NRD_FRAME

    #undef REBLUR_BLUR_ROTATOR_MODE
    #define REBLUR_BLUR_ROTATOR_MODE                            NRD_FRAME

    #undef REBLUR_POST_BLUR_ROTATOR_MODE
    #define REBLUR_POST_BLUR_ROTATOR_MODE                       NRD_FRAME
#endif
