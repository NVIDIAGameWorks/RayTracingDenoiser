/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

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

struct outputVS
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

outputVS main
(
    float2 inPos : POSITION0,
    float2 inTexCoord : TEXCOORD0
)
{
    outputVS output;

    output.position.xy = inPos * scale;
    output.position.zw = float2( 0.0, 1.0 );

    output.texCoord = inTexCoord;

    return output;
}
