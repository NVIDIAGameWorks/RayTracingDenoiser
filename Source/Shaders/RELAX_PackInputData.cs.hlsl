/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsl"
#include "STL.hlsl"
#include "RELAX_PackInputData.resources.hlsl"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsl"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

[numthreads(16, 16, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint2 pixelPos : SV_DispatchThreadID)
{
    uint2 pixelPosUser = pixelPos + gRectOrigin;

    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[pixelPosUser] );
    float roughness = normalAndRoughness.w;
    float3 normal = normalAndRoughness.xyz;

    float viewZ = abs( gIn_ViewZ[pixelPosUser] );

    // Output
    gOut_Normal_Roughness_ViewZ[pixelPos] = PackNormalRoughnessDepth(normal, roughness, viewZ);
}
