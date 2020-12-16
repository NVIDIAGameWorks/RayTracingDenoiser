/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE(cbuffer, globalConstants, b, 0, 0)
{
    float4x4 gViewToClip;
};

#include "RELAX_Common.hlsl"

// Inputs
NRI_RESOURCE(Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0);
NRI_RESOURCE(Texture2D<float>, gIn_ViewZ, t, 1, 0);

// Outputs
NRI_RESOURCE(RWTexture2D<uint2>, gOutNormalRoughnessDepth, u, 0, 0);

// Helper functions

// Converts linear view depth to device depth
float deLinearizeDepth(float linearDepth)
{
    float4 viewPos = float4(0, 0, linearDepth, 1);
    float4 clipPos = mul(gViewToClip, viewPos);
    return clipPos.z / clipPos.w;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 pixelPos = dispatchThreadId.xy;

    // Read normal and roughness
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness(gIn_Normal_Roughness[pixelPos]);
    float roughness = normalAndRoughness.w;
    float3 normal = normalAndRoughness.xyz;

    // Read depth
    float depth = deLinearizeDepth(gIn_ViewZ[pixelPos]);

    // Pack and store
    gOutNormalRoughnessDepth[pixelPos] = PackNormalRoughnessDepth(normal, roughness, depth);
}
