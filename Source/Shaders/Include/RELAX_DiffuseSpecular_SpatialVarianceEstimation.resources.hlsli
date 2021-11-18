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
    NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIllumination, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<float2>, gHistoryLength, t, 2 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gNormalRoughness, t, 3 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gViewZ, t, 4)

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIlluminationAndVariance, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIlluminationAndVariance, u, 1 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutNormalRoughness, u, 2)

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        NRD_CONSTANT( int2, gResolution ) \
        NRD_CONSTANT( float, gPhiNormal ) \
        NRD_CONSTANT( uint, gHistoryThreshold ) \
        NRD_CONSTANT( float, gDenoisingRange ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
