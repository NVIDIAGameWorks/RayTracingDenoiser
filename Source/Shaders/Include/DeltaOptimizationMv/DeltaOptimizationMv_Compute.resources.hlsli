/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_DeltaPrimaryPosW, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_DeltaSecondaryPosW, t, 2 ) \
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_PrevDeltaSecondaryPosW, t, 3 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_DeltaMv, u, 0 )
    NRD_OUTPUT_TEXTURE( RWTexture2D<float3>, gOut_DeltaSecondaryPosW, u, 1 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        NRD_CONSTANT( float4x4, gWorldToClipPrev ) \
        NRD_CONSTANT( uint2, gRectSize ) \
        NRD_CONSTANT( float2, gInvRectSize ) \
        NRD_CONSTANT( float2, gMotionVectorScale ) \
        NRD_CONSTANT( uint2, gRectOrigin ) \
        NRD_CONSTANT( uint, gIsWorldSpaceMotionEnabled ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
