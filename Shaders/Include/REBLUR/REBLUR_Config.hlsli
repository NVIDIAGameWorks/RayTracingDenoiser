/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// Data types
#ifdef REBLUR_OCCLUSION
    #define REBLUR_TYPE                                         float
#else
    #define REBLUR_TYPE                                         float4
#endif

#define REBLUR_SH_TYPE                                          float4

#ifndef REBLUR_SPECULAR
    #define REBLUR_FAST_TYPE                                    float
#else
    #define REBLUR_FAST_TYPE                                    float2
#endif

// Switches ( default 1 )
#define REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA              1
#define REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA              1
#define REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS              1
#define REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS              1
#define REBLUR_USE_HISTORY_FIX                                  1
#define REBLUR_USE_YCOCG                                        1
#define REBLUR_USE_MORE_STRICT_PARALLAX_BASED_CHECK             1

#ifdef REBLUR_OCCLUSION
    #define REBLUR_USE_ANTIFIREFLY                              0
#else
    #define REBLUR_USE_ANTIFIREFLY                              1
#endif

// Switches ( default 0 )
#define REBLUR_USE_SCREEN_SPACE_SAMPLING                        0
#define REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX             0
#define REBLUR_USE_DECOMPRESSED_HIT_DIST_IN_RECONSTRUCTION      0 // compression helps to preserve "lobe important" values
#define REBLUR_USE_ADJUSTED_ON_THE_FLY_BLUR_RADIUS_IN_PRE_BLUR  0 // TODO: verify that not needed and delete...

// Kernels
static const float3 g_Special6[ 6 ] =
{
    // https://www.desmos.com/calculator/e5mttzlg6v
    float3( -0.50 * sqrt(3.0) , -0.50             , 1.0 ),
    float3(  0.00             ,  1.00             , 1.0 ),
    float3(  0.50 * sqrt(3.0) , -0.50             , 1.0 ),
    float3(  0.00             , -0.30             , 0.3 ),
    float3(  0.15 * sqrt(3.0) ,  0.15             , 0.3 ),
    float3( -0.15 * sqrt(3.0) ,  0.15             , 0.3 ),
};

static const float3 g_Special8[ 8 ] =
{
    // https://www.desmos.com/calculator/abaqyvswem
    float3( -1.00             ,  0.00             , 1.0 ),
    float3(  0.00             ,  1.00             , 1.0 ),
    float3(  1.00             ,  0.00             , 1.0 ),
    float3(  0.00             , -1.00             , 1.0 ),
    float3( -0.25 * sqrt(2.0) ,  0.25 * sqrt(2.0) , 0.5 ),
    float3(  0.25 * sqrt(2.0) ,  0.25 * sqrt(2.0) , 0.5 ),
    float3(  0.25 * sqrt(2.0) , -0.25 * sqrt(2.0) , 0.5 ),
    float3( -0.25 * sqrt(2.0) , -0.25 * sqrt(2.0) , 0.5 )
};

// Other
#define REBLUR_PRE_BLUR_POISSON_SAMPLE_NUM                      8
#define REBLUR_PRE_BLUR_POISSON_SAMPLES( i )                    g_Special8[ i ]

#define REBLUR_POISSON_SAMPLE_NUM                               8
#define REBLUR_POISSON_SAMPLES( i )                             g_Special8[ i ]

#define REBLUR_PRE_BLUR_ROTATOR_MODE                            NRD_FRAME
#define REBLUR_PRE_BLUR_FRACTION_SCALE                          2.0
#define REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED                  ( 1.0 / ( 1.0 + 8.0 ) ) // TODO: 10-20?

#define REBLUR_BLUR_ROTATOR_MODE                                NRD_FRAME
#define REBLUR_BLUR_FRACTION_SCALE                              1.0

#define REBLUR_POST_BLUR_ROTATOR_MODE                           NRD_FRAME
#define REBLUR_POST_BLUR_FRACTION_SCALE                         0.5
#define REBLUR_POST_BLUR_RADIUS_SCALE                           2.0

#define REBLUR_VIRTUAL_MOTION_PREV_PREV_WEIGHT_ITERATION_NUM    2
#define REBLUR_COLOR_CLAMPING_SIGMA_SCALE                       1.5 // TODO: was 2.0, but we can use even 1.0 because the fast history is noisy, while the main history is denoised
#define REBLUR_HISTORY_FIX_THRESHOLD_1                          0.111 // was 0.01
#define REBLUR_HISTORY_FIX_THRESHOLD_2                          0.333 // was 0.25
#define REBLUR_HISTORY_FIX_BUMPED_ROUGHNESS                     0.08
#define REBLUR_HIT_DIST_MIN_WEIGHT                              0.1 // TODO: reduce?
#define REBLUR_FIREFLY_SUPPRESSOR_MAX_RELATIVE_INTENSITY        float2( 10.0, 1.1 )
#define REBLUR_FIREFLY_SUPPRESSOR_RADIUS_SCALE                  0.1
#define REBLUR_ANTI_FIREFLY_FILTER_RADIUS                       4 // pixels
#define REBLUR_ANTI_FIREFLY_SIGMA_SCALE                         2.0

// Shared data
#define REBLUR_SHARED_CB_DATA \
    NRD_CONSTANT( float4x4, gViewToClip ) \
    NRD_CONSTANT( float4x4, gViewToWorld ) \
    NRD_CONSTANT( float4, gFrustum ) \
    NRD_CONSTANT( float4, gHitDistParams ) \
    NRD_CONSTANT( float4, gViewVectorWorld ) \
    NRD_CONSTANT( float4, gViewVectorWorldPrev ) \
    NRD_CONSTANT( float3, gMvScale ) \
    NRD_CONSTANT( float, gDebug ) \
    NRD_CONSTANT( float2, gInvScreenSize ) \
    NRD_CONSTANT( float2, gScreenSize ) \
    NRD_CONSTANT( float2, gInvRectSize ) \
    NRD_CONSTANT( float2, gRectSize ) \
    NRD_CONSTANT( float2, gResolutionScale ) \
    NRD_CONSTANT( float2, gResolutionScalePrev ) \
    NRD_CONSTANT( float2, gRectSizePrev ) \
    NRD_CONSTANT( float2, gSensitivityToDarkness ) \
    NRD_CONSTANT( float2, gRectOffset ) \
    NRD_CONSTANT( uint2, gRectOrigin ) \
    NRD_CONSTANT( float, gNonReferenceAccumulation ) \
    NRD_CONSTANT( float, gOrthoMode ) \
    NRD_CONSTANT( float, gUnproject ) \
    NRD_CONSTANT( float, gDenoisingRange ) \
    NRD_CONSTANT( float, gPlaneDistSensitivity ) \
    NRD_CONSTANT( float, gFramerateScale ) \
    NRD_CONSTANT( float, gBlurRadius ) \
    NRD_CONSTANT( float, gMaxAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gMaxFastAccumulatedFrameNum ) \
    NRD_CONSTANT( float, gAntiFirefly ) \
    NRD_CONSTANT( float, gLobeAngleFraction ) \
    NRD_CONSTANT( float, gRoughnessFraction ) \
    NRD_CONSTANT( float, gResponsiveAccumulationRoughnessThreshold ) \
    NRD_CONSTANT( float, gDiffPrepassBlurRadius ) \
    NRD_CONSTANT( float, gSpecPrepassBlurRadius ) \
    NRD_CONSTANT( float, gHistoryFixFrameNum ) \
    NRD_CONSTANT( float, gMinRectDimMulUnproject ) \
    NRD_CONSTANT( uint, gIsWorldSpaceMotionEnabled ) \
    NRD_CONSTANT( uint, gFrameIndex ) \
    NRD_CONSTANT( uint, gDiffMaterialMask ) \
    NRD_CONSTANT( uint, gSpecMaterialMask ) \
    NRD_CONSTANT( uint, gResetHistory ) \
    NRD_CONSTANT( uint, gUnused1 ) \
    NRD_CONSTANT( uint, gUnused2 )

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

    #undef REBLUR_ANTI_FIREFLY_FILTER_RADIUS
    #define REBLUR_ANTI_FIREFLY_FILTER_RADIUS                   3
#endif
