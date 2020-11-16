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
    float2 gScreenSize;
    float gMetersToUnits;
    float gIsOrtho;
    float gUnproject;
    float gDebug;
    float gInf;
    float gReference;
    uint gFrameIndex;
    uint gWorldSpaceMotion;

    float4x4 gWorldToView;
    float4 gRotator;
    float4 gDiffScalingParams;
    float gDiffBlurRadius;
    uint gDiffCheckerboard;
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
    bool hasData = true;
    uint2 checkerboardPixelPos = pixelPos;
    float checkerboardScale = 1.0;
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    if( gDiffCheckerboard != 2 )
    {
        hasData = checkerboard == gDiffCheckerboard;
        checkerboardPixelPos.x >>= 1;
        checkerboardScale = 0.5;
    }

    // Early out
    float centerZ = gIn_ViewZ[ pixelPos ];

    [branch]
    if ( abs( centerZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
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

        float4 signalB0 = gIn_SignalB[ pos.xy ];
        float4 signalA0 = gIn_SignalA[ pos.xy ];
        float4 signalB1 = gIn_SignalB[ pos.zw ];
        float4 signalA1 = gIn_SignalA[ pos.zw ];

        float2 w = GetBilateralWeight( float2( viewZ0, viewZ1 ), centerZ );
        w *= STL::Math::PositiveRcp( w.x + w.y );

        finalA = signalA0 * w.x + signalA1 * w.y;
        finalB = signalB0 * w.x + signalB1 * w.y;
    }

    float diffCenterNormHitDist = finalA.w;

    // Normal
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );

    // Blur radius
    float diffHitDist = GetHitDistance( finalA.w, centerZ, gDiffScalingParams );
    float diffBlurRadius = DIFF_PRE_BLUR_RADIUS_SCALE * GetBlurRadius( gDiffBlurRadius, 1.0, diffHitDist, centerPos, 1.0 );
    float diffWorldBlurRadius = PixelRadiusToWorld( diffBlurRadius, centerZ );

    // Tangent basis
    float2x3 diffTvBv = GetKernelBasis( centerPos, Nv, diffWorldBlurRadius );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( PRE_BLUR_ROTATOR_MODE, pixelPos, gRotator );

    // Denoising
    float diffSum = 1.0;

    float2 geometryWeightParams = GetGeometryWeightParams( centerPos, Nv, gMetersToUnits, centerZ );
    float2 diffNormalWeightParams = GetNormalWeightParams( 1.0 );

    UNROLL
    for( uint i = 0; i < POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = POISSON_SAMPLES[ i ];

        // Sample coordinates
        float2 uv = GetKernelSampleCoordinates( offset, centerPos, diffTvBv[ 0 ], diffTvBv[ 1 ], rotator );

        // Handle half res input in the checkerboard mode
        float2 checkerboardUv = uv;
        checkerboardUv.x *= checkerboardScale;
        checkerboardUv.x = checkerboardUv.x > checkerboardScale ? ( 2.0 * checkerboardScale - checkerboardUv.x ) : checkerboardUv.x;

        // Fetch data
        float4 sA = gIn_SignalA.SampleLevel( gNearestMirror, checkerboardUv, 0 );
        float4 sB = gIn_SignalB.SampleLevel( gNearestMirror, checkerboardUv, 0 );

        float z = sB.w / NRD_FP16_VIEWZ_SCALE; // TODO: DIFF_B is half res in checkerboard mode. Do we need viewZ at full res here?
        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );

        // Sample weight
        float w = GetGeometryWeight( Nv, samplePos, geometryWeightParams );

        #if( USE_NORMAL_WEIGHT_IN_DIFF_PRE_BLUR == 1 )
            float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0 );
            normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );
            w *= GetNormalWeight( diffNormalWeightParams, N, normal.xyz );
        #endif

        finalA += sA * w;
        finalB.xyz += sB.xyz * w;
        diffSum += w;
    }

    float invSum = 1.0 / diffSum;
    finalA *= invSum;
    finalB.xyz *= invSum;

    // Special case for hit distance
    finalA.w = lerp( finalA.w, diffCenterNormHitDist, HIT_DIST_INPUT_MIX );

    // Output
    finalB.w = clamp( centerZ * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );

    gOut_SignalA[ pixelPos ] = finalA;
    gOut_SignalB[ pixelPos ] = finalB;
}
