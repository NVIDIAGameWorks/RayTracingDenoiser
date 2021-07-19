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
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 ) \
    NRD_INPUT_TEXTURE( Texture2D<uint2>, gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds, t, 3 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_History_Spec, t, 4 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_HistoryFast_Spec, t, 5 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 6 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<unorm float3>, gOut_InternalData, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Error, u, 1 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 2 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Fast_Spec, u, 3 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        REBLUR_SPEC_SHARED_CB_DATA \
        NRD_CONSTANT( float4x4, gWorldToViewPrev ) \
        NRD_CONSTANT( float4x4, gWorldToClipPrev ) \
        NRD_CONSTANT( float4x4, gViewToWorld ) \
        NRD_CONSTANT( float4x4, gWorldToClip ) \
        NRD_CONSTANT( float4, gCameraDelta ) \
        NRD_CONSTANT( float2, gMotionVectorScale ) \
        NRD_CONSTANT( float, gCheckerboardResolveAccumSpeed ) \
        NRD_CONSTANT( float, gDisocclusionThreshold ) \
        NRD_CONSTANT( uint, gSpecCheckerboard ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
