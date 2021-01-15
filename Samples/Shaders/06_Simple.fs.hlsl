/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

struct OutputVS
{
    float4 position : SV_Position;
    float2 texCoords : TEXCOORD0;
};

float4 main( in OutputVS input ) : SV_Target
{
    return float4( input.texCoords, 0, 1 );
}
