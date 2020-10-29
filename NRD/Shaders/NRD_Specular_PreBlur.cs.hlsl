/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

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
    float2 padding;
    float gMetersToUnits;
    float gIsOrtho;
    float gUnproject;
    float gDebug;
    float gInf;
    uint gCheckerboard;
    uint gFrameIndex;
    uint gWorldSpaceMotion;

    float4x4 gWorldToView;
    float4 gScalingParams;
    float3 gTrimmingParams;
    float gBlurRadius;
    float4 gRotator;
    float2 gScreenSize;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Signal, t, 2, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_Signal, u, 0, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Checkerboard
    bool hasData = ApplyCheckerboard( pixelPos );

    uint2 checkerboardPixelPos = pixelPos;
    float checkerboardScale = 1.0;

    if( IsCheckerboardMode( ) )
    {
        checkerboardPixelPos.x >>= 1;
        checkerboardScale = 0.5;
    }

    // Early out
    float centerZ = gIn_ViewZ[ pixelPos ];

    [branch]
    if ( abs( centerZ ) > gInf )
    {
        #if( SPEC_BLACK_OUT_INF_PIXELS == 1 )
            gOut_Signal[ pixelPos ] = 0;
        #endif
        return;
    }

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float4 final = gIn_Signal[ checkerboardPixelPos ];

    if( !hasData )
    {
        #if( CHECKERBOARD_RESOLVE_MODE == SOFT )
            int4 pos = pixelPos.xyxy + int4( 0, -1, 0, 1 );
        #else
            int4 pos = pixelPos.xyxy + int4( -1, 0, 1, 0 );
        #endif

        float viewZ0 = gIn_ViewZ[ pos.xy ];
        float viewZ1 = gIn_ViewZ[ pos.zw ];

        pos.xz >>= 1;

        float4 signal0 = gIn_Signal[ pos.xy ];
        float4 signal1 = gIn_Signal[ pos.zw ];

        float2 w = GetBilateralWeight( float2( viewZ0, viewZ1 ), centerZ );
        w *= STL::Math::PositiveRcp( w.x + w.y );

        final = signal0 * w.x + signal1 * w.y;
    }

    float centerNormHitDist = final.w;

    // Normal and roughness
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Blur radius
    float hitDist = GetHitDistance( final.w, centerZ, gScalingParams, roughness );
    float radius = SPEC_PRE_BLUR_RADIUS_SCALE * GetBlurRadius( gBlurRadius, roughness, hitDist, centerPos, 1.0 );
    radius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gTrimmingParams );
    float worldRadius = PixelRadiusToWorld( radius, centerZ );

    // Tangent basis
    float3 Tv, Bv;
    GetKernelBasis( centerPos, Nv, roughness, worldRadius, 0.65, Tv, Bv );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( SPEC_PRE_BLUR_ROTATOR_MODE, pixelPos, gRotator );

    // Denoising
    float2 sum = 1.0;

    float geometryWeightParams = GetGeometryWeightParams( gMetersToUnits, centerZ );
    float2 normalWeightParams = GetNormalWeightParams( roughness );
    float2 roughnessWeightParams = GetRoughnessWeightParams( roughness );
    float2 hitDistanceWeightParams = GetHitDistanceWeightParams( roughness, centerNormHitDist );

    SPEC_UNROLL
    for( uint s = 0; s < SPEC_POISSON_SAMPLE_NUM; s++ )
    {
        // Sample coordinates
        float3 offset = SPEC_POISSON_SAMPLES[ s ];
        float2 uv = GetKernelSampleCoordinates( offset, centerPos, Tv, Bv, rotator );

        // Handle half res input in the checkerboard mode
        float2 checkerboardUv = uv;
        checkerboardUv.x *= checkerboardScale;
        checkerboardUv.x = checkerboardUv.x > checkerboardScale ? ( 2.0 * checkerboardScale - checkerboardUv.x ) : checkerboardUv.x;

        // Fetch data
        float4 s = gIn_Signal.SampleLevel( gNearestMirror, checkerboardUv, 0 );
        float z = gIn_ViewZ.SampleLevel( gNearestMirror, uv, 0 );
        float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0.0 );

        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );
        normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

        // Sample weight
        float w = GetGeometryWeight( centerPos, Nv, samplePos, geometryWeightParams );
        w *= GetNormalWeight( normalWeightParams, N, normal.xyz );
        w *= GetRoughnessWeight( roughnessWeightParams, normal.w );

        float2 ww = w;
        ww.x *= GetHitDistanceWeight( hitDistanceWeightParams, s.w );

        final += s * ww.xxxy;
        sum += ww;
    }

    final *= STL::Math::PositiveRcp( sum.xxxy );

    // Special case for hit distance
    final.w = lerp( final.w, centerNormHitDist, HIT_DIST_INPUT_MIX );

    // Output
    gOut_Signal[ pixelPos ] = final;
}
