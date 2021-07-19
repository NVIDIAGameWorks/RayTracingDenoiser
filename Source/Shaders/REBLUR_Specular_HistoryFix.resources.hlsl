/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "REBLUR_Config.hlsl"

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_InternalData, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ScaledViewZ, t, 2 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 3 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Fast_Spec, t, 4 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 0 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        REBLUR_SPEC_SHARED_CB_DATA \
        NRD_CONSTANT( float, gSpecFastHistoryClampingColorBoxSigmaScale ) \
        NRD_CONSTANT( uint, gSpecAntiFirefly ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
