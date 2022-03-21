/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "REBLUR_Config.hlsli"

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )

    #if( defined REBLUR_PROVIDED_CONFIDENCE )
        #define EXTRA_INPUTS \
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_DiffConfidence, t, 10 ) \
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_SpecConfidence, t, 11 )
    #else
        #define EXTRA_INPUTS
    #endif

    #define NRD_DECLARE_INPUT_TEXTURES \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 ) \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 ) \
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 ) \
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_ViewZ_DiffAccumSpeed, t, 3 ) \
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_Normal_SpecAccumSpeed, t, 4 ) \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Prev_Roughness, t, 5 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 6 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_History_Diff, t, 7 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 8 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_History_Spec, t, 9 ) \
        EXTRA_INPUTS

    #define NRD_DECLARE_OUTPUT_TEXTURES \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_ScaledViewZ, u, 0 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_InternalData, u, 1 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Error, u, 2 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 3 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 4 )

#elif( defined REBLUR_DIFFUSE )

    #if( defined REBLUR_PROVIDED_CONFIDENCE )
        #define EXTRA_INPUTS \
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_DiffConfidence, t, 7 )
    #else
        #define EXTRA_INPUTS
    #endif

    #define NRD_DECLARE_INPUT_TEXTURES \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 ) \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 ) \
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 ) \
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_ViewZ_DiffAccumSpeed, t, 3 ) \
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_Normal_SpecAccumSpeed, t, 4 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 5 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_History_Diff, t, 6 ) \
        EXTRA_INPUTS

    #define NRD_DECLARE_OUTPUT_TEXTURES \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_ScaledViewZ, u, 0 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_InternalData, u, 1 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Error, u, 2 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 3 )

#else

    #if( defined REBLUR_PROVIDED_CONFIDENCE )
        #define EXTRA_INPUTS \
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_SpecConfidence, t, 8 )
    #else
        #define EXTRA_INPUTS
    #endif

    #define NRD_DECLARE_INPUT_TEXTURES \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 ) \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 ) \
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 ) \
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_ViewZ_DiffAccumSpeed, t, 3 ) \
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_Normal_SpecAccumSpeed, t, 4 ) \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Prev_Roughness, t, 5 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 6 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_History_Spec, t, 7 ) \
        EXTRA_INPUTS

    #define NRD_DECLARE_OUTPUT_TEXTURES \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_ScaledViewZ, u, 0 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_InternalData, u, 1 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Error, u, 2 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 3 )

#endif

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        REBLUR_SHARED_CB_DATA \
        NRD_CONSTANT( float4x4, gWorldToViewPrev ) \
        NRD_CONSTANT( float4x4, gWorldToClipPrev ) \
        NRD_CONSTANT( float4x4, gViewToWorld ) \
        NRD_CONSTANT( float4x4, gWorldToClip ) \
        NRD_CONSTANT( float4, gFrustumPrev ) \
        NRD_CONSTANT( float4, gCameraDelta ) \
        NRD_CONSTANT( float2, gMotionVectorScale ) \
        NRD_CONSTANT( float, gCheckerboardResolveAccumSpeed ) \
        NRD_CONSTANT( float, gDisocclusionThreshold ) \
        NRD_CONSTANT( uint, gDiffCheckerboard ) \
        NRD_CONSTANT( uint, gSpecCheckerboard ) \
        NRD_CONSTANT( uint, gPreblurEnabled ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
