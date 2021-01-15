/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE( cbuffer, CommonConstants, b, 0, 0 )
{
    float3 color;
    float scale;
};

struct PushConstants
{
    float transparency;
};

NRI_PUSH_CONSTANTS( PushConstants, g_PushConstants, 1 );

NRI_RESOURCE( Texture2D, g_DiffuseTexture, t, 0, 1 );
NRI_RESOURCE( SamplerState, g_Sampler, s, 0, 1 );

struct outputVS
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

float4 main( in outputVS input ) : SV_Target
{
    float4 output;
    output.xyz = g_DiffuseTexture.Sample( g_Sampler, input.texCoord ).xyz * color;
    output.w = g_PushConstants.transparency;

    return output;
}
