/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

NRD_SAMPLER_START
    NRD_SAMPLER( SamplerState, gNearestClamp, s, 0 )
    NRD_SAMPLER( SamplerState, gNearestMirror, s, 1 )
    NRD_SAMPLER( SamplerState, gLinearClamp, s, 2 )
    NRD_SAMPLER( SamplerState, gLinearMirror, s, 3 )
NRD_SAMPLER_END

NRD_CONSTANTS_START
    SIGMA_SHARED_CB_DATA
    NRD_CONSTANT( float4x4, gWorldToView )
    NRD_CONSTANT( float4, gRotator )
NRD_CONSTANTS_END

NRD_INPUT_TEXTURE_START
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
    NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_Hit_ViewZ, t, 1 )
    NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_Tiles, t, 2 )
    #ifdef SIGMA_FIRST_PASS
        NRD_INPUT_TEXTURE( Texture2D<SIGMA_TYPE>, gIn_History, t, 3 )
        #ifdef SIGMA_TRANSLUCENT
            NRD_INPUT_TEXTURE( Texture2D<SIGMA_TYPE>, gIn_Shadow_Translucency, t, 4 )
        #endif
    #else
        NRD_INPUT_TEXTURE( Texture2D<SIGMA_TYPE>, gIn_Shadow_Translucency, t, 3 )
    #endif
NRD_INPUT_TEXTURE_END

NRD_OUTPUT_TEXTURE_START
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_Hit_ViewZ, u, 0 )
    NRD_OUTPUT_TEXTURE( RWTexture2D<SIGMA_TYPE>, gOut_Shadow_Translucency, u, 1 )
    #ifdef SIGMA_FIRST_PASS
        NRD_OUTPUT_TEXTURE( RWTexture2D<SIGMA_TYPE>, gOut_History, u, 2 )
    #endif
NRD_OUTPUT_TEXTURE_END

#if( SIGMA_5X5_BLUR_RADIUS_ESTIMATION_KERNEL == 1 )
    #define NRD_USE_BORDER_2
#endif
