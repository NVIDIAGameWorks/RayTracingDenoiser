/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define SIGMA

// Switches ( default 1 )
#define SIGMA_USE_CATROM                                1 // sharper reprojection
#define SIGMA_5X5_TEMPORAL_KERNEL                       1 // provides variance estimation in a wider radius
#define SIGMA_5X5_BLUR_RADIUS_ESTIMATION_KERNEL         1 // helps to improve stability, but adds 10% of overhead

// Switches ( default 0 )
#define SIGMA_SHOW_TILES                                0
#define SIGMA_SHOW_PENUMBRA_SIZE                        0

// Settings
#define SIGMA_ROTATOR_MODE                              NRD_FRAME
#define SIGMA_POISSON_SAMPLE_NUM                        8
#define SIGMA_POISSON_SAMPLES                           g_Special8
#define SIGMA_MAX_PIXEL_RADIUS                          32.0
#define SIGMA_PENUMBRA_WEIGHT_SCALE                     10.0
#define SIGMA_MAX_SIGMA_SCALE                           3.0
#define SIGMA_SPATIAL_PASSES_NUM                        2
#define SIGMA_TS_MOTION_MAX_REUSE                       0.11

// Data type
#ifdef SIGMA_TRANSLUCENT
    #define SIGMA_TYPE                                  float4
#else
    #define SIGMA_TYPE                                  float
#endif

// Shared constants
#define SIGMA_SHARED_CONSTANTS \
    NRD_CONSTANT( float4x4, gWorldToView ) \
    NRD_CONSTANT( float4x4, gViewToClip ) \
    NRD_CONSTANT( float4x4, gWorldToClipPrev ) \
    NRD_CONSTANT( float4, gLightDirectionView ) \
    NRD_CONSTANT( float4, gFrustum ) \
    NRD_CONSTANT( float4, gMvScale ) \
    NRD_CONSTANT( float2, gResourceSizeInv ) \
    NRD_CONSTANT( float2, gResourceSizeInvPrev ) \
    NRD_CONSTANT( float2, gRectSize ) \
    NRD_CONSTANT( float2, gRectSizeInv ) \
    NRD_CONSTANT( float2, gRectSizePrev ) \
    NRD_CONSTANT( float2, gResolutionScale ) \
    NRD_CONSTANT( float2, gRectOffset ) \
    NRD_CONSTANT( uint2, gPrintfAt ) \
    NRD_CONSTANT( uint2, gRectOrigin ) \
    NRD_CONSTANT( int2, gRectSizeMinusOne ) \
    NRD_CONSTANT( int2, gTilesSizeMinusOne ) \
    NRD_CONSTANT( float, gOrthoMode ) \
    NRD_CONSTANT( float, gUnproject ) \
    NRD_CONSTANT( float, gDenoisingRange ) \
    NRD_CONSTANT( float, gPlaneDistSensitivity ) \
    NRD_CONSTANT( float, gStabilizationStrength ) \
    NRD_CONSTANT( float, gDebug ) \
    NRD_CONSTANT( float, gSplitScreen ) \
    NRD_CONSTANT( uint, gFrameIndex )
