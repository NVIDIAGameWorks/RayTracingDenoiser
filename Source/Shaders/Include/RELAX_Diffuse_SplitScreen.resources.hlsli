/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "RELAX_Config.hlsli"

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_Diff, t, 1 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float3>, gOut_Diff, u, 0 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        RELAX_SHARED_CB_DATA \
        NRD_CONSTANT( uint, gDiffuseCheckerboard ) \
        NRD_CONSTANT( float, gSplitScreen ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
