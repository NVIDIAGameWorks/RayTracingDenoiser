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
    NRD_INPUT_TEXTURE( Texture2D<uint2>, gSpecularAndDiffuseIlluminationLogLuv, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<uint2>, gSpecularAndDiffuseResponsiveIlluminationLogLuv, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<float2>, gSpecularAndDiffuseHistoryLength, t, 2 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<uint2>, gOutSpecularAndDiffuseIlluminationLogLuv, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOutSpecularAndDiffuseHistoryLength, u, 1 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        NRD_CONSTANT( int2, gResolution ) \
        NRD_CONSTANT( float, gColorBoxSigmaScale ) \
        NRD_CONSTANT( float, gSpecularAntiLagSigmaScale ) \
        NRD_CONSTANT( float, gSpecularAntiLagPower ) \
        NRD_CONSTANT( float, gDiffuseAntiLagSigmaScale ) \
        NRD_CONSTANT( float, gDiffuseAntiLagPower ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
