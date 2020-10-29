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
    float4 gRotator;
    float2 gScreenSize;
    float gBlurRadius;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalA, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalB, t, 3, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalA, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalB, u, 1, 0 );

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
        #if( DIFF_BLACK_OUT_INF_PIXELS == 1 )
            gOut_SignalA[ pixelPos ] = 0;
        #endif
        gOut_SignalB[ pixelPos ] = NRD_INF_DIFF_B;
        return;
    }

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float4 finalA = gIn_SignalA[ checkerboardPixelPos ];
    float4 finalB = gIn_SignalB[ checkerboardPixelPos ];

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

        float4 signalA0 = gIn_SignalA[ pos.xy ];
        float4 signalB0 = gIn_SignalB[ pos.xy ];
        float4 signalA1 = gIn_SignalA[ pos.zw ];
        float4 signalB1 = gIn_SignalB[ pos.zw ];

        float2 w = GetBilateralWeight( float2( viewZ0, viewZ1 ), centerZ );
        w *= STL::Math::PositiveRcp( w.x + w.y );

        finalA = signalA0 * w.x + signalA1 * w.y;
        finalB = signalB0 * w.x + signalB1 * w.y;
    }

    float centerNormHitDist = finalA.w;

    // Store full res viewZ in checkerboard mode
    finalB.w = centerZ * NRD_FP16_VIEWZ_SCALE;

    // Normal
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );

    // Blur radius
    float hitDist = GetHitDistance( finalA.w, centerZ, gScalingParams );
    float radius = DIFF_PRE_BLUR_RADIUS_SCALE * GetBlurRadius( gBlurRadius, 1.0, hitDist, centerPos, 1.0 );
    float worldRadius = PixelRadiusToWorld( radius, centerZ );

    // Tangent basis
    float3 Tv, Bv;
    GetKernelBasis( centerPos, Nv, 1.0, worldRadius, 0.75, Tv, Bv );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( DIFF_PRE_BLUR_ROTATOR_MODE, pixelPos, gRotator );

    // Denoising
    float sum = 1.0;

    float geometryWeightParams = GetGeometryWeightParams( gMetersToUnits, centerZ );
    float2 normalWeightParams = GetNormalWeightParams( 1.0 );

    DIFF_UNROLL
    for( uint s = 0; s < DIFF_POISSON_SAMPLE_NUM; s++ )
    {
        // Sample coordinates
        float3 offset = DIFF_POISSON_SAMPLES[ s ];
        float2 uv = GetKernelSampleCoordinates( offset, centerPos, Tv, Bv, rotator );

        // Handle half res input in the checkerboard mode
        float2 checkerboardUv = uv;
        checkerboardUv.x *= checkerboardScale;
        checkerboardUv.x = checkerboardUv.x > checkerboardScale ? ( 2.0 * checkerboardScale - checkerboardUv.x ) : checkerboardUv.x;

        // Fetch data
        float4 sA = gIn_SignalA.SampleLevel( gNearestMirror, checkerboardUv, 0.0 );
        float4 sB = gIn_SignalB.SampleLevel( gNearestMirror, checkerboardUv, 0.0 );

        float z = sB.w / NRD_FP16_VIEWZ_SCALE;
        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );

        // Sample weight
        float w = GetGeometryWeight( centerPos, Nv, samplePos, geometryWeightParams );

        #if( USE_NORMAL_WEIGHT_IN_DIFF_PRE_BLUR == 1 )
            float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0.0 );
            normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );
            w *= GetNormalWeight( normalWeightParams, N, normal.xyz );
        #endif

        finalA += sA * w;
        finalB.xyz += sB.xyz * w;
        sum += w;
    }

    float invSum = STL::Math::PositiveRcp( sum );
    finalA *= invSum;
    finalB.xyz *= invSum;

    // Special case for hit distance
    finalA.w = lerp( finalA.w, centerNormHitDist, HIT_DIST_INPUT_MIX );

    // Output
    gOut_SignalA[ pixelPos ] = finalA;
    gOut_SignalB[ pixelPos ] = finalB;
}
