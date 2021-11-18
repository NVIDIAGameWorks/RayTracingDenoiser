/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "RELAX_Config.hlsli"

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIllumination, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gNormalRoughness, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gViewZ, t, 2)

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIllumination, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutViewZ, u, 1) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutScaledViewZ, u, 2)

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        NRD_CONSTANT( float4x4, gWorldToClip ) \
        NRD_CONSTANT( float4x4, gWorldToView ) \
        NRD_CONSTANT( float4x4, gViewToClip ) \
        NRD_CONSTANT( float4, gRotator) \
        NRD_CONSTANT( float4, gFrustumRight ) \
        NRD_CONSTANT( float4, gFrustumUp ) \
        NRD_CONSTANT( float4, gFrustumForward ) \
        NRD_CONSTANT( uint2, gRectOrigin ) \
        NRD_CONSTANT( float2, gRectOffset) \
        NRD_CONSTANT( uint2, gResolution ) \
        NRD_CONSTANT( float2, gInvViewSize ) \
        NRD_CONSTANT( float2, gInvRectSize ) \
        NRD_CONSTANT( float2, gResolutionScale ) \
        NRD_CONSTANT( float, gIsOrtho ) \
        NRD_CONSTANT( float, gUnproject ) \
        NRD_CONSTANT( uint, gFrameIndex ) \
        NRD_CONSTANT( float, gDenoisingRange ) \
        NRD_CONSTANT( uint, gSpecCheckerboard ) \
        NRD_CONSTANT( float, gSpecularBlurRadius ) \
        NRD_CONSTANT( float, gMeterToUnitsMultiplier ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
