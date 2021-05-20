/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "NRD.hlsl"
#include "STL.hlsl"
#include "RELAX_Config.hlsl"

NRI_RESOURCE(cbuffer, globalConstants, b, 0, 0)
{
    uint2 gRectOrigin;
};

#include "NRD_Common.hlsl"
#include "RELAX_Common.hlsl"

// Inputs
NRI_RESOURCE(Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0);
NRI_RESOURCE(Texture2D<float>, gIn_ViewZ, t, 1, 0);

// Outputs
NRI_RESOURCE(RWTexture2D<uint2>, gOut_Normal_Roughness_ViewZ, u, 0, 0);

[numthreads(16, 16, 1)]
void NRD_CS_MAIN(uint2 pixelPos : SV_DispatchThreadID)
{
    uint2 pixelPosUser = pixelPos + gRectOrigin;

    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[pixelPosUser] );
    float roughness = normalAndRoughness.w;
    float3 normal = normalAndRoughness.xyz;

    float viewZ = abs( gIn_ViewZ[pixelPosUser] );

    // Output
    gOut_Normal_Roughness_ViewZ[pixelPos] = PackNormalRoughnessDepth(normal, roughness, viewZ);
}
