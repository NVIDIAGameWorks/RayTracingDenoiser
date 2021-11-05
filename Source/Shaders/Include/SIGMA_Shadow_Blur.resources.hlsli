/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SIGMA_Config.hlsli"

#ifdef SIGMA_FIRST_PASS
    #define EXTRA_INPUTS_1 \
        NRD_INPUT_TEXTURE( Texture2D<SIGMA_TYPE>, gIn_History, t, 2 ) \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Tiles, t, 3 )
    #ifdef SIGMA_TRANSLUCENT
        #define EXTRA_INPUTS_2 \
            NRD_INPUT_TEXTURE( Texture2D<SIGMA_TYPE>, gIn_Shadow_Translucency, t, 4 )
    #else
        #define EXTRA_INPUTS_2
    #endif
#else
    #define EXTRA_INPUTS_1 \
        NRD_INPUT_TEXTURE( Texture2D<SIGMA_TYPE>, gIn_Shadow_Translucency, t, 2 )
    #define EXTRA_INPUTS_2
#endif

#ifdef SIGMA_FIRST_PASS
    #define EXTRA_OUTPUTS NRD_OUTPUT_TEXTURE( RWTexture2D<SIGMA_TYPE>, gOut_History, u, 2 )
#else
    #define EXTRA_OUTPUTS
#endif

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_Hit_ViewZ, t, 1 ) \
    EXTRA_INPUTS_1 \
    EXTRA_INPUTS_2

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_Hit_ViewZ, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<SIGMA_TYPE>, gOut_Shadow_Translucency, u, 1 ) \
    EXTRA_OUTPUTS

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        SIGMA_SHARED_CB_DATA \
        NRD_CONSTANT( float4x4, gWorldToView ) \
        NRD_CONSTANT( float4, gRotator ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
