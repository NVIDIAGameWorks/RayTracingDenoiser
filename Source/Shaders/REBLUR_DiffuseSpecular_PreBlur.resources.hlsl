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
    NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 2 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 3 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_ScaledViewZ, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 1 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 2 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        REBLUR_DIFF_SPEC_SHARED_CB_DATA \
        NRD_CONSTANT( float4x4, gWorldToView ) \
        NRD_CONSTANT( float4, gRotator ) \
        NRD_CONSTANT( float3, gSpecTrimmingParams ) \
        NRD_CONSTANT( float, gSpecBlurRadius ) \
        NRD_CONSTANT( uint, gSpecCheckerboard ) \
        NRD_CONSTANT( float, gDiffBlurRadius ) \
        NRD_CONSTANT( uint, gDiffCheckerboard ) \
        NRD_CONSTANT( uint, gSpatialFiltering ) \
        NRD_CONSTANT( float, gNormalWeightStrictness ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
