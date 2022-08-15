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

groupshared float4 s_Normal_Roughness[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );
    uint2 globalIdUser = gRectOrigin + globalPos;

    s_Normal_Roughness[ sharedPos.y ][ sharedPos.x ] = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
}

// TODO: The below utility functions are copied over from REBLUR_Common.hlsli, perhaps they should be moved to something like NRD_Common.hlsli?
float3 GetViewVector( float3 X, bool isViewSpace = false )
{
    return gOrthoMode == 0.0 ? normalize( -X ) : ( isViewSpace ? float3( 0, 0, -1 ) : gViewVectorWorld.xyz );
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    uint2 pixelPosUser = gRectOrigin + pixelPos;

    PRELOAD_INTO_SMEM;

    // Early out
    float viewZ = gIn_ViewZ[ pixelPosUser ];

    [branch]
    if( viewZ > gDenoisingRange )
        return;

    // Current position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float3 X = STL::Geometry::RotateVector( gViewToWorld, Xv );

    // Normal and roughness
    int2 smemPos = threadPos + BORDER;
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser ] * gMotionVectorScale.xyy;
    float2 smbPixelUv = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gIsWorldSpaceMotionEnabled );
    float isInScreen = IsInScreen( smbPixelUv );
    float3 Xprev = X + motionVector * float( gIsWorldSpaceMotionEnabled != 0 );

    // Modified roughness
    float3 Navg = N;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadPos + int2( dx, dy );
            Navg += s_Normal_Roughness[ pos.y ][ pos.x ].xyz;
        }
    }

    Navg /= 9.0; // needs to be unnormalized!

    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );

    // Curvature
    float3 cameraMotion3d = Xprev - X - gCameraDelta.xyz;
    float2 cameraMotion2d = STL::Geometry::RotateVectorInverse( gViewToWorld, cameraMotion3d ).xy;
    float mvLen = length( cameraMotion2d );
    cameraMotion2d = mvLen > 1e-7 ? cameraMotion2d / mvLen : float2( 1, 0 );
    cameraMotion2d *= 0.5 * gInvRectSize;

    float curvature = 0;

    [unroll]
    for( int dir = -1; dir <= 1; dir += 2 )
    {
        float2 uv = pixelUv + dir * cameraMotion2d;
        STL::Filtering::Bilinear f = STL::Filtering::GetBilinearFilter( uv, gRectSize );

        smemPos = threadPos + BORDER + uint2( f.origin ) - pixelPos;
        float3 n00 = s_Normal_Roughness[ smemPos.y ][ smemPos.x ].xyz;
        float3 n10 = s_Normal_Roughness[ smemPos.y ][ smemPos.x + 1 ].xyz;
        float3 n01 = s_Normal_Roughness[ smemPos.y + 1 ][ smemPos.x ].xyz;
        float3 n11 = s_Normal_Roughness[ smemPos.y + 1 ][ smemPos.x + 1 ].xyz;

        float3 n = STL::Filtering::ApplyBilinearFilter( n00, n10, n01, n11, f );
        n = normalize( n );

        float3 xv = STL::Geometry::ReconstructViewPosition( uv, gFrustum, 1.0, gOrthoMode );
        float3 x = STL::Geometry::RotateVector( gViewToWorld, xv );
        float3 v = GetViewVector( x );

        curvature += EstimateCurvature( n, v, N, X );
    }

    curvature *= 0.5;

    // Parallax
    float smbParallax = ComputeParallax( Xprev - gCameraDelta.xyz, gOrthoMode == 0.0 ? pixelUv : smbPixelUv, gWorldToClip, gRectSize, gUnproject, gOrthoMode );
    float smbParallaxInPixels = GetParallaxInPixels( smbParallax, gUnproject );

    // Virtual motion
    float3 V = GetViewVector( X );
    float NoV = abs( dot( N, V ) );
    float hitDist = gIn_HitDist[ pixelPosUser ];

    float dominantFactor = STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughnessModified, STL_SPECULAR_DOMINANT_DIRECTION_G2 );
    float3 Xvirtual = GetXvirtual( NoV, hitDist, curvature, X, Xprev, V, dominantFactor );
    float2 vmbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual, false );

    // Adjust curvature if curvature sign oscillation is forseen
    float pixelsBetweenSurfaceAndVirtualMotion = length( ( vmbPixelUv - smbPixelUv ) * gRectSize );
    float curvatureCorrectionThreshold = smbParallaxInPixels + gInvRectSize.x;
    float curvatureCorrection = STL::Math::SmoothStep( 2.0 * curvatureCorrectionThreshold, curvatureCorrectionThreshold, pixelsBetweenSurfaceAndVirtualMotion );
    curvature *= curvatureCorrection;

    Xvirtual = GetXvirtual( NoV, hitDist, curvature, X, Xprev, V, dominantFactor );
    vmbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual, false );

    gOut_SpecularReflectionMv[ pixelPos ] = vmbPixelUv - pixelUv;
}
