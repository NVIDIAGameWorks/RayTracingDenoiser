/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define SIGMA_POISSON_SAMPLE_NUM                        8
#define SIGMA_POISSON_SAMPLES                           g_Poisson8
#define SIGMA_MAX_PIXEL_RADIUS                          32.0
#define SIGMA_PLANE_DISTANCE_SCALE                      0.25
#define SIGMA_MIN_HIT_DISTANCE_OUTPUT                   0.0001
#define SIGMA_PENUMBRA_FIX_BLUR_RADIUS_ADDON            5.0
#define SIGMA_MAX_SIGMA_SCALE                           3.5
#define SIGMA_TS_MOTION_MAX_REUSE                       0.11
#define SIGMA_USE_CATROM                                1
#define SIGMA_5X5_TEMPORAL_KERNEL                       1

#ifdef SIGMA_TRANSLUCENT
    #define SIGMA_TYPE                                  float4
#else
    #define SIGMA_TYPE                                  float
#endif

// Shared data
#define SIGMA_SHARED_CB_DATA \
    NRD_CONSTANT( float4x4, gViewToClip ) \
    NRD_CONSTANT( float4, gFrustum ) \
    NRD_CONSTANT( float2, gMotionVectorScale ) \
    NRD_CONSTANT( float2, gInvScreenSize ) \
    NRD_CONSTANT( float2, gScreenSize ) \
    NRD_CONSTANT( float2, gInvRectSize ) \
    NRD_CONSTANT( float2, gRectSize ) \
    NRD_CONSTANT( float2, gRectSizePrev ) \
    NRD_CONSTANT( float2, gResolutionScale ) \
    NRD_CONSTANT( float2, gRectOffset ) \
    NRD_CONSTANT( uint2, gRectOrigin ) \
    NRD_CONSTANT( float, gOrthoMode ) \
    NRD_CONSTANT( float, gUnproject ) \
    NRD_CONSTANT( float, gDebug ) \
    NRD_CONSTANT( float, gDenoisingRange ) \
    NRD_CONSTANT( float, gPlaneDistSensitivity ) \
    NRD_CONSTANT( float, gBlurRadiusScale ) \
    NRD_CONSTANT( float, gUnused2 ) \
    NRD_CONSTANT( uint, gIsWorldSpaceMotionEnabled ) \
    NRD_CONSTANT( uint, gFrameIndex ) \
    NRD_CONSTANT( uint, gUnused1 )
