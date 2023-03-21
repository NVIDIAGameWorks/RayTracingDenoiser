/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"

#include "SpecularReflectionMv_Compute.resources.hlsli"

#include "Common.hlsli"

groupshared float4 s_Normal_Roughness[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );
    uint2 globalIdUser = gRectOrigin + globalPos;

    s_Normal_Roughness[ sharedPos.y ][ sharedPos.x ] = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
}

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

    // Previous position and surface motion uv
    float3 mv = gIn_Mv[ pixelPosUser ] * gMvScale;
    float3 Xprev = X;

    float2 smbPixelUv = pixelUv + mv.xy;
    if( gIsWorldSpaceMotionEnabled )
    {
        Xprev += mv;
        smbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xprev );
    }
    else if( gMvScale.z != 0.0 )
    {
        float viewZprev = viewZ + mv.z;
        float3 Xvprevlocal = STL::Geometry::ReconstructViewPosition( smbPixelUv, gFrustumPrev, viewZprev, gOrthoMode ); // TODO: use gOrthoModePrev

        Xprev = STL::Geometry::RotateVectorInverse( gWorldToViewPrev, Xvprevlocal ) + gCameraDelta;
    }

    // Modified roughness
    float3 Navg = N;

    [unroll]
    for( j = 0; j <= BORDER * 2; j++ )
    {
        [unroll]
        for( i = 0; i <= BORDER * 2; i++ )
        {
            if( i == BORDER && j == BORDER )
                continue;

            int2 pos = threadPos + int2( i, j );
            Navg += s_Normal_Roughness[ pos.y ][ pos.x ].xyz;
        }
    }

    Navg /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ); // needs to be unnormalized!

    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );

    // Parallax
    float smbParallaxInPixels = ComputeParallaxInPixels( Xprev - gCameraDelta, gOrthoMode == 0.0 ? pixelUv : smbPixelUv, gWorldToClip, gRectSize );

    // Camera motion in screen-space
    float2 motionUv = STL::Geometry::GetScreenUv( gWorldToClip, Xprev - gCameraDelta );
    float2 cameraMotion2d = ( motionUv - pixelUv ) * gRectSize;
    cameraMotion2d /= max( length( cameraMotion2d ), 1.0 / 64.0 );
    cameraMotion2d *= gInvRectSize;

    // Low parallax
    float2 uv = pixelUv + cameraMotion2d * 0.99;
    STL::Filtering::Bilinear f = STL::Filtering::GetBilinearFilter( uv, gRectSize );

    int2 pos = threadPos + BORDER + int2( f.origin ) - pixelPos;
    pos = clamp( pos, 0, int2( BUFFER_X, BUFFER_Y ) - 2 );

    float3 n00 = s_Normal_Roughness[ pos.y ][ pos.x ].xyz;
    float3 n10 = s_Normal_Roughness[ pos.y ][ pos.x + 1 ].xyz;
    float3 n01 = s_Normal_Roughness[ pos.y + 1 ][ pos.x ].xyz;
    float3 n11 = s_Normal_Roughness[ pos.y + 1 ][ pos.x + 1 ].xyz;

    float3 n = STL::Filtering::ApplyBilinearFilter( n00, n10, n01, n11, f );
    n = normalize( n );

    // High parallax
    float2 uvHigh = pixelUv + cameraMotion2d * smbParallaxInPixels;

    #if( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_R10G10B10A2_UNORM )
        f = STL::Filtering::GetBilinearFilter( uvHigh, gRectSize );

        pos = gRectOrigin + int2( f.origin );
        pos = clamp( pos, 0, int2( gRectSize ) - 2 );

        n00 = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pos ] ).xyz;
        n10 = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pos + int2( 1, 0 ) ] ).xyz;
        n01 = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pos + int2( 0, 1 ) ] ).xyz;
        n11 = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pos + int2( 1, 1 ) ] ).xyz;

        float3 nHigh = STL::Filtering::ApplyBilinearFilter( n00, n10, n01, n11, f );
        nHigh = normalize( nHigh );
    #else
        float3 nHigh = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness.SampleLevel( gLinearClamp, gRectOffset + uvHigh * gResolutionScale, 0 ) ).xyz;
    #endif

    float zHigh = abs( gIn_ViewZ.SampleLevel( gLinearClamp, gRectOffset + uvHigh * gResolutionScale, 0 ) );
    float zError = abs( zHigh - viewZ ) * rcp( max( zHigh, viewZ ) );
    bool cmp = smbParallaxInPixels > 1.0 && zError < 0.1 && IsInScreen( uvHigh );

    uv = cmp ? uvHigh : uv;
    n = cmp ? nHigh : n;

    // Estimate curvature
    float3 xv = STL::Geometry::ReconstructViewPosition( uv, gFrustum, 1.0, gOrthoMode );
    float3 x = STL::Geometry::RotateVector( gViewToWorld, xv );
    float3 v = GetViewVector( x );

    // Values below this threshold get turned into garbage due to numerical imprecision
    float d = STL::Math::ManhattanDistance( N, n );
    float s = STL::Math::LinearStep( NRD_NORMAL_ENCODING_ERROR, 2.0 * NRD_NORMAL_ENCODING_ERROR, d );

    float curvature = EstimateCurvature( normalize( Navg ), n, v, N, X ) * s;

    // Virtual motion
    float3 V = GetViewVector( X );
    float NoV = abs( dot( N, V ) );
    float hitDist = gIn_HitDist[ pixelPosUser ];

    float dominantFactor = STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughnessModified, STL_SPECULAR_DOMINANT_DIRECTION_G2 );

    float3 Xvirtual = GetXvirtual( NoV, hitDist, curvature, X, Xprev, V, dominantFactor );
    float2 vmbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );

    float2 vmbDelta = vmbPixelUv - smbPixelUv;
    float vmbPixelsTraveled = length( vmbDelta * gRectSize );

    // Adjust curvature if curvature sign oscillation is forseen // TODO: is there a better way? fix curvature?
    float curvatureCorrectionThreshold = smbParallaxInPixels + gInvRectSize.x;
    float curvatureCorrection = STL::Math::SmoothStep( 1.05 * curvatureCorrectionThreshold, 0.95 * curvatureCorrectionThreshold, vmbPixelsTraveled );
    curvature *= curvatureCorrection;

    Xvirtual = GetXvirtual( NoV, hitDist, curvature, X, Xprev, V, dominantFactor );
    vmbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );

    gOut_SpecularReflectionMv[ pixelPos ] = vmbPixelUv - pixelUv;
}
