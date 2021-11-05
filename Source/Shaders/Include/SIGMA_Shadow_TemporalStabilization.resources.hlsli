/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SIGMA_Config.hlsli"

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_Hit_ViewZ, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<SIGMA_TYPE>, gIn_Shadow_Translucency, t, 2 ) \
    NRD_INPUT_TEXTURE( Texture2D<SIGMA_TYPE>, gIn_History, t, 3 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Tiles, t, 4 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<SIGMA_TYPE>, gOut_Shadow_Translucency, u, 0 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        SIGMA_SHARED_CB_DATA \
        NRD_CONSTANT( float4x4, gWorldToClipPrev ) \
        NRD_CONSTANT( float4x4, gViewToWorld ) \
        NRD_CONSTANT( uint, gResetHistory ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
