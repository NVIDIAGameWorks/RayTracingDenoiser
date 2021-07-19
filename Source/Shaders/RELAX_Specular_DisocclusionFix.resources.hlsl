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
    NRD_INPUT_TEXTURE( Texture2D<uint>, gSpecularIlluminationLogLuv, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<uint>, gSpecularIlluminationResponsiveLogLuv, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gSpecular2ndMoment, t, 2 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gHistoryLength, t, 3 ) \
    NRD_INPUT_TEXTURE( Texture2D<uint2>, gNormalRoughnessDepth, t, 4 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOutSpecularIlluminationLogLuv, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOutSpecularIlluminationResponsiveLogLuv, u, 1 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutSpecular2ndMoment, u, 2 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        NRD_CONSTANT( float4, gFrustumRight ) \
        NRD_CONSTANT( float4, gFrustumUp ) \
        NRD_CONSTANT( float4, gFrustumForward ) \
        NRD_CONSTANT( int2, gResolution ) \
        NRD_CONSTANT( float2, gInvRectSize ) \
        NRD_CONSTANT( float, gDisocclusionThreshold ) \
        NRD_CONSTANT( float, gDisocclusionFixEdgeStoppingNormalPower ) \
        NRD_CONSTANT( float, gMaxRadius ) \
        NRD_CONSTANT( int, gFramesToFix ) \
        NRD_CONSTANT( float, gDenoisingRange ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
