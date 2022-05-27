/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "STL.hlsli"
#include "../Include/NRD.hlsli"

#include "../Resources/SpecularReflectionMv_Compute.resources.hlsli"

#include "../Include/Common.hlsli"

// TODO: The below utility functions are copied over from REBLUR_Common.hlsli, perhaps they should be moved to something like NRD_Common.hlsli?
float3 GetViewVector(float3 X, bool isViewSpace = false)
{
    return gOrthoMode == 0.0 ? normalize(-X) : (isViewSpace ? float3(0, 0, -1) : gViewVectorWorld.xyz);
}

[numthreads(GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint2 pixelPos : SV_DispatchThreadId)
{
    float2 pixelUv = float2(pixelPos + 0.5) * gInvRectSize;
    uint2 pixelPosUser = gRectOrigin + pixelPos;

    float viewZ = gIn_ViewZ[pixelPosUser];
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gIn_Normal_Roughness[pixelPosUser]);
    float3 motionVector = gIn_ObjectMotion[pixelPosUser] * gMotionVectorScale.xyy;
    float hitDist = gIn_HitDist[pixelPosUser];

    // Current position
    float3 Xv = STL::Geometry::ReconstructViewPosition(pixelUv, gFrustum, viewZ, gOrthoMode);
    float3 X = STL::Geometry::RotateVector(gViewToWorld, Xv);
    float3 V = GetViewVector(X);
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Curvature
    float3 Nflat = N;
    float curvature = 0.0;

    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            if (dx == 0 && dy == 0)
                continue;

            float3 n = NRD_FrontEnd_UnpackNormalAndRoughness(gIn_Normal_Roughness[pixelPosUser + int2(dx, dy)]).xyz;
            Nflat += n;

            float2 o = float2( dx, dy );
            float3 xv = STL::Geometry::ReconstructViewPosition( pixelUv + o * gInvRectSize, gFrustum, 1.0, gOrthoMode );
            float3 x = STL::Geometry::RotateVector( gViewToWorld, xv );
            float3 v = GetViewVector( x );
            float c = EstimateCurvature( n.xyz, v, N, X );
            curvature += c;
        }
    }

    curvature /= 8.0;

    float3 Navg = Nflat / 9.0;
    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance(roughness, Navg);

    // Virtual motion
    float NoV = abs(dot(N, V));
    float3 Xvirtual = GetXvirtual(NoV, hitDist, curvature, X, X, V, roughnessModified);
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv(gWorldToClipPrev, Xvirtual, false);

    gOut_SpecularReflectionMv[pixelPos] = pixelUvVirtualPrev - pixelUv;
}
