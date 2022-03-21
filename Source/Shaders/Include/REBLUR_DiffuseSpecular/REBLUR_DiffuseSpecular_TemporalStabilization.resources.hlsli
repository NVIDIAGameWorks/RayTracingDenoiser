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

    #define NRD_DECLARE_INPUT_TEXTURES \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 ) \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 ) \
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_InternalData, t, 3 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Error, t, 4 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_HistoryStabilized_Diff, t, 5 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 6 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_HistoryStabilized_Spec, t, 7 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 8 )

    #define NRD_DECLARE_OUTPUT_TEXTURES \
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_ViewZ_DiffAccumSpeed, u, 0 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Normal_SpecAccumSpeed, u, 1 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Roughness, u, 2 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 3 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 4 )

#elif( defined REBLUR_DIFFUSE )

    #define NRD_DECLARE_INPUT_TEXTURES \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 ) \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 ) \
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_InternalData, t, 3 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Error, t, 4 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_HistoryStabilized_Diff, t, 5 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 6 )

    #define NRD_DECLARE_OUTPUT_TEXTURES \
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_ViewZ_DiffAccumSpeed, u, 0 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Normal_SpecAccumSpeed, u, 1 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 2 )

#else

    #define NRD_DECLARE_INPUT_TEXTURES \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 ) \
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 ) \
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_InternalData, t, 3 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Error, t, 4 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_HistoryStabilized_Spec, t, 5 ) \
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 6 )

    #define NRD_DECLARE_OUTPUT_TEXTURES \
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_ViewZ_DiffAccumSpeed, u, 0 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Normal_SpecAccumSpeed, u, 1 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Roughness, u, 2 ) \
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 3 )

#endif

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        REBLUR_SHARED_CB_DATA \
        NRD_CONSTANT( float4x4, gWorldToClipPrev ) \
        NRD_CONSTANT( float4x4, gViewToWorld ) \
        NRD_CONSTANT( float4, gCameraDelta ) \
        NRD_CONSTANT( float4, gAntilagMinMaxThreshold ) \
        NRD_CONSTANT( float2, gAntilagSigmaScale ) \
        NRD_CONSTANT( float2, gMotionVectorScale ) \
        NRD_CONSTANT( float, gStabilizationStrength ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
