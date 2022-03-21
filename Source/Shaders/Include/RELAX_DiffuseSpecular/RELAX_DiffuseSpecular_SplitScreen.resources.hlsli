/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "RELAX_Config.hlsli"

#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )

    #define NRD_DECLARE_INPUT_TEXTURES \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 0 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 1 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 2 )

    #define NRD_DECLARE_OUTPUT_TEXTURES \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 0 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 1 )

    #define NRD_DECLARE_CONSTANTS \
        NRD_CONSTANTS_START \
            RELAX_SHARED_CB_DATA \
            NRD_CONSTANT( float, gSplitScreen ) \
            NRD_CONSTANT( uint, gDiffCheckerboard ) \
            NRD_CONSTANT( uint, gSpecCheckerboard ) \
        NRD_CONSTANTS_END

#elif( defined RELAX_DIFFUSE )

    #define NRD_DECLARE_INPUT_TEXTURES \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 0 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 1 )

    #define NRD_DECLARE_OUTPUT_TEXTURES \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 0 ) \

    #define NRD_DECLARE_CONSTANTS \
        NRD_CONSTANTS_START \
            RELAX_SHARED_CB_DATA \
            NRD_CONSTANT( float, gSplitScreen ) \
            NRD_CONSTANT( uint, gDiffCheckerboard ) \
        NRD_CONSTANTS_END

#else

    #define NRD_DECLARE_INPUT_TEXTURES \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 0 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 1 )

    #define NRD_DECLARE_OUTPUT_TEXTURES \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 0 )

    #define NRD_DECLARE_CONSTANTS \
        NRD_CONSTANTS_START \
            RELAX_SHARED_CB_DATA \
            NRD_CONSTANT( float, gSplitScreen ) \
            NRD_CONSTANT( uint, gSpecCheckerboard ) \
        NRD_CONSTANTS_END

#endif

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
