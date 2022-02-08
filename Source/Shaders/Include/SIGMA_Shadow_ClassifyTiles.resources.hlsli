/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SIGMA_Config.hlsli"

#ifdef SIGMA_TRANSLUCENT
    #define EXTRA_INPUTS \
        NRD_INPUT_TEXTURE( Texture2D<SIGMA_TYPE>, gIn_Shadow_Translucency, t, 1 )
#else
    #define EXTRA_INPUTS
#endif

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_Hit_ViewZ, t, 0 ) \
    EXTRA_INPUTS

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_Tiles, u, 0 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        SIGMA_SHARED_CB_DATA \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
