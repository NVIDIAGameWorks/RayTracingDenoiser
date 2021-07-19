/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "RELAX_Config.hlsl"

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIllumination, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gMotion, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<uint2>, gNormalRoughnessDepth, t, 2 ) \
    NRD_INPUT_TEXTURE( Texture2D<uint>, gPrevSpecularIlluminationResponsiveLogLuv, t, 3 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevSpecularIlluminationUnpacked, t, 4 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gPrevSpecular2ndMoment, t, 5 ) \
    NRD_INPUT_TEXTURE( Texture2D<uint2>, gPrevNormalRoughnessDepth, t, 6 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gPrevReflectionHitT, t, 7 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gPrevHistoryLength, t, 8)

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOutSpecularIlluminationLogLuv, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOutSpecularIlluminationResponsiveLogLuv, u, 1 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutSpecular2ndMoment, u, 2 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutReflectionHitT, u, 3 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutHistoryLength, u, 4 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutSpecularReprojectionConfidence, u, 5 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        NRD_CONSTANT( float4x4, gPrevWorldToClip ) \
        NRD_CONSTANT( float4x4, gWorldToClip ) \
        NRD_CONSTANT( float4, gFrustumRight ) \
        NRD_CONSTANT( float4, gFrustumUp ) \
        NRD_CONSTANT( float4, gFrustumForward ) \
        NRD_CONSTANT( float4, gPrevFrustumRight ) \
        NRD_CONSTANT( float4, gPrevFrustumUp ) \
        NRD_CONSTANT( float4, gPrevFrustumForward ) \
        NRD_CONSTANT( float3, gPrevCameraPosition ) \
        NRD_CONSTANT( float, gJitterDelta ) \
        NRD_CONSTANT( float2, gMotionVectorScale ) \
        NRD_CONSTANT( uint2, gRectOrigin ) \
        NRD_CONSTANT( uint2, gResolution ) \
        NRD_CONSTANT( float2, gInvViewSize ) \
        NRD_CONSTANT( float2, gInvRectSize ) \
        NRD_CONSTANT( float2, gRectSizePrev ) \
        NRD_CONSTANT( float, gUseBicubic ) \
        NRD_CONSTANT( float, gSpecularMaxAccumulatedFrameNum ) \
        NRD_CONSTANT( float, gSpecularMaxFastAccumulatedFrameNum ) \
        NRD_CONSTANT( float, gSpecularVarianceBoost ) \
        NRD_CONSTANT( float, gWorldSpaceMotion ) \
        NRD_CONSTANT( float, gIsOrtho ) \
        NRD_CONSTANT( uint, gRoughnessBasedSpecularAccumulation ) \
        NRD_CONSTANT( uint, gVirtualHistoryClampingEnabled ) \
        NRD_CONSTANT( float, gUnproject ) \
        NRD_CONSTANT( uint, gFrameIndex ) \
        NRD_CONSTANT( float, gDenoisingRange ) \
        NRD_CONSTANT( float, gDisocclusionThreshold ) \
        NRD_CONSTANT( uint, gSpecCheckerboard ) \
        NRD_CONSTANT( float, gCheckerboardResolveAccumSpeed ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
