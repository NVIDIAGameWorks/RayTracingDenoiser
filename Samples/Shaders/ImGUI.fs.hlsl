/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};

NRI_RESOURCE( SamplerState, sampler0, s, 0, 0 );
NRI_RESOURCE( Texture2D, texture0, t, 0, 0 );

float4 main( PS_INPUT input ) : SV_Target
{
    float4 out_col = input.col * texture0.Sample( sampler0, input.uv );
    return out_col;
}