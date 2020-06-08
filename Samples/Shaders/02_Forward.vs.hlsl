/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

struct Input
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
};

struct Attributes
{
    float4 Position : SV_Position;
    float4 Normal : TEXCOORD0; //.w = TexCoord.x
    float4 View : TEXCOORD1; //.w = TexCoord.y
    float4 Tangent : TEXCOORD2;
};

NRI_RESOURCE( cbuffer, Global, b, 0, 0 )
{
    float4x4 gWorldToClip;
    float3 gCameraPos;
};

Attributes main( in Input input )
{
    Attributes output;

    float3 N = input.Normal * 2.0 - 1.0;
    float4 T = input.Tangent * 2.0 - 1.0;
    float3 V = gCameraPos - input.Position;

    output.Position = mul( gWorldToClip, float4( input.Position, 1 ) );
    output.Normal = float4( N, input.TexCoord.x );
    output.View = float4( V, input.TexCoord.y );
    output.Tangent = T;

    return output;
}
