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
#include "REBLUR_Config.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    REBLUR_DIFF_SHARED_CB_DATA;

    float4x4 gWorldToView;
    float4 gRotator;
    float4 gDiffHitDistParams;
};

#include "NRD_Common.hlsl"
#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_InternalData, t, 1, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 3, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 0, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    // Debug
    #if( REBLUR_DEBUG == REBLUR_SHOW_MIPS )
        gOut_Diff[ pixelPos ] = gIn_Diff[ pixelPos ];
        return;
    #endif

    // Early out
    float viewZ = gIn_ScaledViewZ[ pixelPos ] / NRD_FP16_VIEWZ_SCALE;

    [branch]
    if( viewZ > gInf )
        return;

    // Normal and roughness
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Accumulations speeds
    float edge;
    float2 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ], edge );

    // Center data
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float4 diff = gIn_Diff[ pixelPos ];
    float diffCenterNormHitDist = diff.w;

    // Blur radius
    float diffHitDist = GetHitDist( diffCenterNormHitDist, viewZ, gDiffHitDistParams );
    float diffBlurRadius = GetBlurRadius( gDiffBlurRadius, 1.0, diffHitDist, Xv, diffInternalData.x );
    float diffWorldBlurRadius = PixelRadiusToWorld( gUnproject, gIsOrtho, diffBlurRadius, viewZ );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( REBLUR_BLUR_ROTATOR_MODE, pixelPos, gRotator, gFrameIndex );

    // Denoising
    float2 diffSum = 1.0;
    float diffNormalWeightParams = GetNormalWeightParams( viewZ, 1.0, edge, diffInternalData.x );
    float2 diffHitDistanceWeightParams = GetHitDistanceWeightParams( diffCenterNormHitDist, diffInternalData.x, diffHitDist, Xv );
    float2x3 diffTvBv = GetKernelBasis( Xv, Nv, diffWorldBlurRadius, edge );

    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, Xv, Nv, viewZ );

    [unroll]
    for( uint i = 0; i < REBLUR_POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = REBLUR_POISSON_SAMPLES[ i ];

        // Sample coordinates
        float2 uv = GetKernelSampleCoordinates( gViewToClip, offset, Xv, diffTvBv[ 0 ], diffTvBv[ 1 ], rotator );

        // Fetch data
        float2 uvScaled = uv * gResolutionScale;

        float4 d = gIn_Diff.SampleLevel( gNearestMirror, uvScaled, 0 );
        float scaledViewZ = gIn_ScaledViewZ.SampleLevel( gNearestMirror, uvScaled, 0 );
        float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 );

        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, scaledViewZ / NRD_FP16_VIEWZ_SCALE, gIsOrtho );
        normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

        // Sample weight
        float w = GetGeometryWeight( geometryWeightParams, Nv, samplePos );
        w *= GetNormalWeight( diffNormalWeightParams, N, normal.xyz );
        w *= IsInScreen( uv );

        float wh = GetHitDistanceWeight( diffHitDistanceWeightParams, d.w );
        float2 ww = w * lerp( float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT ), 1.0, wh );

        diff += d * ww.xxxy;
        diffSum += ww;
    }

    diff /= diffSum.xxxy;

    // Special case for hit distance
    diff.w = lerp( diff.w, diffCenterNormHitDist, REBLUR_HIT_DIST_INPUT_MIX );

    // Output
    gOut_Diff[ pixelPos ] = diff;
}
