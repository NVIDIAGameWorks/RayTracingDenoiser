/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

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
#define SIGMA_USE_CATROM_RESAMPLING                     1
#define SIGMA_5X5_TEMPORAL_KERNEL                       1

#ifdef SIGMA_TRANSLUCENT
    #define SIGMA_TYPE                                  float4
#else
    #define SIGMA_TYPE                                  float
#endif

// Overrides
#define NRD_USE_CATROM_RESAMPLING                       SIGMA_USE_CATROM_RESAMPLING

// Shared data
#define SIGMA_SHARED_CB_DATA \
    float4x4 gViewToClip; \
    float4 gFrustum; \
    float2 gMotionVectorScale; \
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
    float gBlurRadiusScale; \
    float gHistoryCorrection; \
    uint gWorldSpaceMotion; \
    uint gFrameIndex; \
