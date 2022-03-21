/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_A, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ScaledViewZ, t, 1 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_A_x2, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_ScaledViewZ_x2, u, 1 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_A_x4, u, 2 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_ScaledViewZ_x4, u, 3 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_A_x8, u, 4 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_ScaledViewZ_x8, u, 5 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        NRD_CONSTANT( uint2, gRectSize ) \
        NRD_CONSTANT( float, gDenoisingRange ) \
        NRD_CONSTANT( float, gDebug ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
