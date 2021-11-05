/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_A, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_B, t, 1 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_A_x2, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_B_x2, u, 1 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_A_x4, u, 2 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_B_x4, u, 3 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_A_x8, u, 4 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_B_x8, u, 5 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_A_x16, u, 6 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_B_x16, u, 7 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        NRD_CONSTANT( float, gInf ) \
        NRD_CONSTANT( float, gDebug ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
