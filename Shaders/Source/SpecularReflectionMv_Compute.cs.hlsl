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

float4 GetXvirtual( float3 X, float3 V, float NoV, float roughness, float hitDist, float viewZ, float c )
{
    /*
    The lens equation:
        - c - local curvature
        - C - curvature = c / edgeLength
        - O - object height
        - I - image height
        - F - focal distance
    [Eq 1] 1 / O + 1 / I = 1 / F
    [Eq 2] For a spherical mirror F = -0.5 * R
    [Eq 3] R = 1 / C

    Find I from [Eq 1]:
        1 / I = 1 / F - 1 / O
        1 / I = ( O - F ) / ( F * O )
        I = F * O / ( O - F )

    Apply [Eq 2]:
        I = -0.5 * R * O / ( O + 0.5 * R )

    Apply [Eq 3]:
        I = ( -0.5 * O / C ) / ( O + 0.5 / C )
        I = ( -0.5 * O / C ) / ( ( O * C + 0.5 ) / C )
        I = ( -0.5 * O / C ) * ( C / ( O * C + 0.5 ) )
        I = -0.5 * O / ( 0.5 + C * O )

    Reverse sign because I is negative:
        I = 0.5 * O / ( 0.5 + C * O )

    Real curvature from local curvature:
        edgeLength = pixelSize / NoV
        C = c * NoV / pixelSize
    */

    // TODO: better use "edge" from EstimateCurvature?
    float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
    c *= NoV / pixelSize;

    float denom = 0.5 + c * hitDist;
    denom = abs( denom ) < 1e-6 ? 0.5 : denom; // fixing imprecision problems
    float hitDistFocused = 0.5 * hitDist / denom;

    // "saturate" is needed to clamp values > 1 if curvature is negative
    float compressionRatio = saturate( ( abs( hitDistFocused ) + 1e-6 ) / ( hitDist + 1e-6 ) );

    // TODO: more complicated method is needed, because if elongation is very small X should become Xprev (signal starts to follow with surface motion)
    float f = STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughness, STL_SPECULAR_DOMINANT_DIRECTION_G2);
    float3 Xvirtual = X - V * hitDistFocused * f;

    return float4( Xvirtual, compressionRatio );
}

/*
Based on:
https://computergraphics.stackexchange.com/questions/1718/what-is-the-simplest-way-to-compute-principal-curvature-for-a-mesh-triangle

curvature = 1 / R = localCurvature / edgeLength
localCurvature ( unsigned ) = length( N[i] - N )
localCurvature ( signed ) = dot( N[i] - N, X[i] - X ) / length( X[i] - X )
edgeLength = length( X[i] - X )
To fit into 8-bits only local curvature is encoded
*/

float EstimateCurvature(float3 Ni, float3 Vi, float3 N, float3 X)
{
    float3 Xi = 0 + Vi * dot(X - 0, N) / dot(Vi, N);
    float3 edge = Xi - X;
    float curvature = dot(Ni - N, edge) * rsqrt(STL::Math::LengthSquared(edge));

    // TODO: potentially imprecision mitigation is needed here...

    return curvature;
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
    float3 X = STL::Geometry::AffineTransform(gViewToWorld, Xv);
    float3 V = GetViewVector(X);
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Curvature
    float3 Nflat = N;
    float curvature = 0.f;
    float curvatureSum = 0.f;
    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            if (dx == 0 && dy == 0)
                continue;

            float3 n = NRD_FrontEnd_UnpackNormalAndRoughness(gIn_Normal_Roughness[pixelPosUser + int2(dx, dy)]).xyz;
            Nflat += n;

            float2 d = float2( dx, dy );
            float3 xv = STL::Geometry::ReconstructViewPosition( pixelUv + d * gInvRectSize, gFrustum, 1.0, gOrthoMode );
            float3 x = STL::Geometry::AffineTransform( gViewToWorld, xv );
            float3 v = GetViewVector( x );
            float c = EstimateCurvature( n, v, N, X );

            float w = exp2( -0.5 * STL::Math::LengthSquared( d ) );
            curvature += c * w;
            curvatureSum += w;
        }
    }

    curvature /= curvatureSum;
    curvature *= STL::Math::LinearStep(0.0, NRD_ENCODING_ERRORS.y, abs(curvature));

    float3 Navg = Nflat / 9.f;
    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance(roughness, Navg);

    // Virtual motion
    float NoV = abs(dot(N, V));
    float4 Xvirtual = GetXvirtual(X, V, NoV, roughnessModified, hitDist, viewZ, curvature);
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv(gWorldToClipPrev, Xvirtual.xyz, false);

    gOut_SpecularReflectionMv[pixelPos] = pixelUvVirtualPrev - pixelUv;
}
