/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

struct InputVS
{
    float3 position : POSITION;
    float2 texCoords : TEXCOORD0;
};

struct OutputVS
{
    float4 position : SV_Position;
    float2 texCoords : TEXCOORD0;
};

NRI_RESOURCE( cbuffer, Constants, b, 0, 0 )
{
    float4x4 transform;
};

OutputVS main( in InputVS input )
{
    OutputVS output;
    output.position = mul( transform, float4( input.position, 1 ) );
    output.texCoords = input.texCoords;
    return output;
}
