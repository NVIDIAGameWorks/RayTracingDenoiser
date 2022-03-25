/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define REBLUR_DEBUG                                            0 // 0+ ( output can be in radiance, AO or SO )
#define REBLUR_DEBUG_SPATIAL_DENSITY_CHECK                      0

// Switches ( default 1 )
#define REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA              1
#define REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA              1
#define REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS              1
#define REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS              1
#define REBLUR_USE_ANISOTROPIC_KERNEL                           1
#define REBLUR_USE_ACCUM_SPEED_NONLINEAR_INTERPOLATION          1
#define REBLUR_USE_HISTORY_FIX                                  1
#define REBLUR_USE_5X5_HISTORY_CLAMPING                         1

// Switches ( default 0 )
#define REBLUR_USE_COLOR_CLAMPING_AABB                          0
#define REBLUR_USE_WIDER_KERNEL_IN_HISTORY_FIX                  0 // 1 - 1.9.0 behavior, 0 - new ( is it better? )
#define REBLUR_USE_SPATIAL_REUSE_FOR_HIT_DIST                   0 // spatial reuse is about multi-bounce nature of light. Corners become darker if reuse is ON
#define REBLUR_USE_COMPRESSION_FOR_DIFFUSE                      0 // we know that exposure should be 0 for roughess = 1
#define REBLUR_USE_SCREEN_SPACE_SAMPLING                        0
#define REBLUR_USE_5X5_ANTI_FIREFLY                             0
#define REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX             0
#define REBLUR_USE_BILINEAR_FOR_VIRTUAL_NORMAL_WEIGHT           0

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
#define REBLUR_PRE_BLUR_INTERNAL_DATA                           float2( 1.0 / ( 1.0 + 8.0 ), 8.0 )
#define REBLUR_PRE_BLUR_RADIUS_SCALE( r )                       ( 1.0 / ( gMinConvergedStateBaseRadiusScale + 0.05 ) ) // TODO: better cancel it more naturally
#define REBLUR_PRE_BLUR_SPATIAL_REUSE_BASE_RADIUS_SCALE         0.5

#define REBLUR_BLUR_ROTATOR_MODE                                NRD_FRAME
#define REBLUR_BLUR_FRACTION_SCALE                              2.0

#define REBLUR_POST_BLUR_ROTATOR_MODE                           NRD_FRAME
#define REBLUR_POST_BLUR_RADIUS_SCALE                           3.0
#define REBLUR_POST_BLUR_FRACTION_SCALE                         0.5

#define REBLUR_VIRTUAL_MOTION_NORMAL_WEIGHT_ITERATION_NUM       2 // if > 2 then "prev" normal test is included
#define REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE               3.0
#define REBLUR_SPEC_ACCUM_BASE_POWER                            0.5 // previously was 0.66 ( less agressive accumulation, but virtual reprojection works well on flat surfaces and fixes the issue )
#define REBLUR_SPEC_ACCUM_CURVE                                 0.5 // aggressiveness of history rejection depending on viewing angle ( 1 - low, 0.66 - medium, 0.5 - high )
#define REBLUR_SPEC_BASIS_ROUGHNESS_THRESHOLD                   0.8
#define REBLUR_SPEC_DOMINANT_DIRECTION                          STL_SPECULAR_DOMINANT_DIRECTION_G2
#define REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE                 0.05
#define REBLUR_TS_SIGMA_AMPLITUDE                               ( 3.0 * gFramerateScale )
#define REBLUR_TS_ACCUM_TIME                                    ( 30 * 0.5 ) // "gFramerateScale to FPS scale" * "seconds"
#define REBLUR_MIP_NUM                                          3.999
#define REBLUR_HIT_DIST_MIN_WEIGHT                              0.1
#define REBLUR_HIT_DIST_ACCELERATION                            float2( 1.0, 0.5 ) // .y is used to accelerate hit distance accumulation
#define REBLUR_MIN_PDF                                          0.01
#define REBLUR_MAX_FIREFLY_RELATIVE_INTENSITY                   8.0

/*
IMPORTANT:
0 can be used to unblock accumulation of hitDist, but due to strict hitDist weight and effects of feedback loop
color banding (crunched colors) will appear. It can be solved in two ways:
- accelerating hitDist accumulation to preserve noise a bit
- adding some hitDist input to the output in spatial passes
*/
#define REBLUR_HIT_DIST_MIN_ACCUM_SPEED( r )                    lerp( 0.2, 0.1, STL::Math::Sqrt01( r ) )

// Shared data
#define REBLUR_SHARED_CB_DATA \
    NRD_CONSTANT( float4x4, gViewToClip ) \
    NRD_CONSTANT( float4, gFrustum ) \
    NRD_CONSTANT( float4, gHitDistParams ) \
    NRD_CONSTANT( float4, gViewVectorWorld ) \
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
    NRD_CONSTANT( float, gResidualNoiseLevel ) \
    NRD_CONSTANT( float, gJitterDelta ) \
    NRD_CONSTANT( float, gInputMix ) \
    NRD_CONSTANT( float, gMinConvergedStateBaseRadiusScale ) \
    NRD_CONSTANT( float, gLobeAngleFraction ) \
    NRD_CONSTANT( float, gRoughnessFraction ) \
    NRD_CONSTANT( float, gResponsiveAccumulationRoughnessThreshold ) \
    NRD_CONSTANT( uint, gIsWorldSpaceMotionEnabled ) \
    NRD_CONSTANT( uint, gFrameIndex ) \
    NRD_CONSTANT( uint, gResetHistory ) \
    NRD_CONSTANT( uint, gDiffMaterialMask ) \
    NRD_CONSTANT( uint, gSpecMaterialMask ) \
    NRD_CONSTANT( uint, gIsAntiFireflyEnabled )

#if( !defined REBLUR_DIFFUSE && !defined REBLUR_SPECULAR )
    #define REBLUR_DIFFUSE
    #define REBLUR_SPECULAR
#endif

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

    #undef REBLUR_USE_5X5_HISTORY_CLAMPING
    #define REBLUR_USE_5X5_HISTORY_CLAMPING                     0

    #undef REBLUR_USE_SCREEN_SPACE_SAMPLING
    #define REBLUR_USE_SCREEN_SPACE_SAMPLING                    1

    #undef REBLUR_USE_5X5_ANTI_FIREFLY
    #define REBLUR_USE_5X5_ANTI_FIREFLY                         0

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
