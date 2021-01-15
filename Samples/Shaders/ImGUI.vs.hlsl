/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

struct PushConstants
{
    float2 invScreenSize;
};

NRI_PUSH_CONSTANTS( PushConstants, g_PushConstants, 0 );

struct VS_INPUT
{
    float2 pos : POSITION0;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};

PS_INPUT main( VS_INPUT input )
{
    float2 p = input.pos.xy * g_PushConstants.invScreenSize;
    p = p * 2.0 - 1.0;
    p.y = -p.y;

    PS_INPUT output;
    output.pos = float4( p, 0, 1 );
    output.col = input.col;
    output.uv  = input.uv;
    return output;
}