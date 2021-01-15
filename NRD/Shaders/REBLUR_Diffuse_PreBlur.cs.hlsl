/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    float4x4 gViewToClip;
    float4 gFrustum;
    float2 gInvScreenSize;
    float2 gScreenSize;
    uint gBools;
    float gIsOrtho;
    float gUnproject;
    float gDebug;
    float gInf;
    float gPlaneDistSensitivity;
    uint gFrameIndex;
    float gFramerateScale;

    float4x4 gWorldToView;
    float4 gRotator;
    float4 gDiffHitDistParams;
    float gDiffBlurRadius;
    uint gDiffCheckerboard;
};

#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 2, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float>, gOut_ScaledViewZ, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 1, 0 );

void Preload( int2 sharedId, int2 globalId )
{
    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = gIn_ViewZ[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    PRELOAD_INTO_SMEM;

    // Checkerboard
    bool2 hasData = true;
    uint2 checkerboardPixelPos = pixelPos.xx;
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    if( gDiffCheckerboard != 2 )
    {
        hasData.x = checkerboard == gDiffCheckerboard;
        checkerboardPixelPos.x >>= 1;
    }

    // Early out
    int2 smemPos = threadId + BORDER;
    float centerZ = s_ViewZ[ smemPos.y ][ smemPos.x ];

    [branch]
    if( abs( centerZ ) > abs( gInf ) )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Diff[ pixelPos ] = 0;
        #endif
        gOut_ScaledViewZ[ pixelPos ] = NRD_FP16_MAX;
        return;
    }

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float4 diff = gIn_Diff[ uint2( checkerboardPixelPos.x, pixelPos.y ) ];

    int3 smemCheckerboardPos = smemPos.xyx + int3( -1, 0, 1 );
    float viewZ0 = s_ViewZ[ smemCheckerboardPos.y ][ smemCheckerboardPos.x ];
    float viewZ1 = s_ViewZ[ smemCheckerboardPos.y ][ smemCheckerboardPos.z ];
    float2 w = GetBilateralWeight( float2( viewZ0, viewZ1 ), centerZ );
    w *= STL::Math::PositiveRcp( w.x + w.y );

    int3 checkerboardPos = pixelPos.xyx + int3( -1, 0, 1 );
    checkerboardPos.xz >>= 1;

    float4 d0 = gIn_Diff[ checkerboardPos.xy ];
    float4 d1 = gIn_Diff[ checkerboardPos.zy ];
    if( !hasData.x )
    {
        diff *= saturate( 1.0 - w.x - w.y );
        diff += d0 * w.x + d1 * w.y;
    }
    float diffCenterNormHitDist = diff.w;

    // Normal and roughness
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Blur radius
    float diffHitDist = GetHitDistance( diff.w, centerZ, gDiffHitDistParams );
    float diffBlurRadius = GetBlurRadius( gDiffBlurRadius, 1.0, diffHitDist, centerPos, PRE_BLUR_NON_LINEAR_ACCUM_SPEED, PRE_BLUR_RADIUS_SCALE( 1.0 ) );
    float diffWorldBlurRadius = PixelRadiusToWorld( diffBlurRadius, centerZ );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( PRE_BLUR_ROTATOR_MODE, pixelPos, gRotator );

    // Edge detection
    float edge = DetectEdge( N, smemPos );

    // Denoising
    float2 diffSum = 1.0;
    float diffNormalWeightParams = GetNormalWeightParams( 1.0, edge, PRE_BLUR_NON_LINEAR_ACCUM_SPEED );
    float2 diffHitDistanceWeightParams = GetHitDistanceWeightParams( diffCenterNormHitDist, PRE_BLUR_NON_LINEAR_ACCUM_SPEED, diffHitDist, centerPos );
    float2x3 diffTvBv = GetKernelBasis( centerPos, Nv, diffWorldBlurRadius, PRE_BLUR_NON_LINEAR_ACCUM_SPEED );

    float2 geometryWeightParams = GetGeometryWeightParams( centerPos, Nv, centerZ );

    UNROLL
    for( uint i = 0; i < POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = POISSON_SAMPLES[ i ];

        // Sample coordinates
        float2 uv = GetKernelSampleCoordinates( offset, centerPos, diffTvBv[ 0 ], diffTvBv[ 1 ], rotator );

        // Handle half res input in the checkerboard mode
        float3 checkerboardUv = float3( uv, 1.0 );
        if( gDiffCheckerboard != 2 )
            checkerboardUv = ApplyCheckerboard( uv, gDiffCheckerboard, i );

        // Fetch data
        float4 d = gIn_Diff.SampleLevel( gNearestMirror, checkerboardUv.xy, 0 );
        float z = gIn_ViewZ.SampleLevel( gNearestMirror, uv, 0 );
        float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0 );

        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );
        normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

        // Sample weight
        float w = GetGeometryWeight( geometryWeightParams, Nv, samplePos );
        w *= GetNormalWeight( diffNormalWeightParams, N, normal.xyz );
        w *= checkerboardUv.z;

        float wh = GetHitDistanceWeight( diffHitDistanceWeightParams, d.w );
        float2 ww = w * lerp( float2( 0.0, HIT_DIST_MIN_WEIGHT * 2.0 ), 1.0, wh );

        diff += d * ww.xxxy;
        diffSum += ww;
    }

    diff *= STL::Math::PositiveRcp( diffSum ).xxxy;

    // Special case for hit distance
    diff.w = lerp( diff.w, diffCenterNormHitDist, HIT_DIST_INPUT_MIX );

    // Output
    float scaledViewZ = clamp( centerZ * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );

    gOut_ScaledViewZ[ pixelPos ] = scaledViewZ;
    gOut_Diff[ pixelPos ] = diff;
}
