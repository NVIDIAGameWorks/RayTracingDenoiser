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
    NRD_CONSTANT( float4x4, gWorldToClipPrev )
    NRD_CONSTANT( uint2, gRectSize )
    NRD_CONSTANT( float2, gInvRectSize )
    NRD_CONSTANT( float2, gMotionVectorScale )
    NRD_CONSTANT( uint2, gRectOrigin )
    NRD_CONSTANT( uint, gIsWorldSpaceMotionEnabled )
NRD_CONSTANTS_END

NRD_INPUT_TEXTURE_START
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 0 )
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_DeltaPrimaryPos, t, 1 )
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_DeltaSecondaryPos, t, 2 )
    NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_PrevDeltaSecondaryPos, t, 3 )
NRD_INPUT_TEXTURE_END

NRD_OUTPUT_TEXTURE_START
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOut_DeltaMv, u, 0 )
    NRD_OUTPUT_TEXTURE( RWTexture2D<float3>, gOut_DeltaSecondaryPos, u, 1 )
NRD_OUTPUT_TEXTURE_END
