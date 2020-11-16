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
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_InternalData, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalA, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalB, t, 3, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalA, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalB, u, 1, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Early out
    float4 finalB = gIn_SignalB[ pixelPos ];
    float centerZ = finalB.w / NRD_FP16_VIEWZ_SCALE;

    [branch]
    if ( abs( centerZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_SignalA[ pixelPos ] = 0;
        #endif
        gOut_SignalB[ pixelPos ] = NRD_INF_DIFF_B;
        return;
    }

    // Normal
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );

    // Accumulations speeds
    float3 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ] );
    float diffNormAccumSpeed = saturate( diffInternalData.x * STL::Math::PositiveRcp( diffInternalData.y ) );
    float diffNonLinearAccumSpeed = 1.0 / ( 1.0 + diffInternalData.x );

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float4 finalA = gIn_SignalA[ pixelPos ];
    float diffCenterNormHitDist = finalA.w;

    // Blur radius
    float diffHitDist = GetHitDistance( finalA.w, centerZ, gDiffScalingParams );
    float diffBlurRadius = GetBlurRadius( gDiffBlurRadius, 1.0, diffHitDist, centerPos, diffNonLinearAccumSpeed );
    float diffWorldBlurRadius = PixelRadiusToWorld( diffBlurRadius, centerZ );

    // Tangent basis
    float2x3 diffTvBv = GetKernelBasis( centerPos, Nv, diffWorldBlurRadius, diffNormAccumSpeed );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( BLUR_ROTATOR_MODE, pixelPos, gRotator );

    // Denoising
    float diffSum = 1.0;

    float2 geometryWeightParams = GetGeometryWeightParams( centerPos, Nv, gMetersToUnits, centerZ );
    float2 diffNormalWeightParams = GetNormalWeightParams( 1.0, diffInternalData.z, diffNormAccumSpeed );

    UNROLL
    for( uint i = 0; i < POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = POISSON_SAMPLES[ i ];

        // Sample coordinates
        float2 uv = GetKernelSampleCoordinates( offset, centerPos, diffTvBv[ 0 ], diffTvBv[ 1 ], rotator );

        // Fetch data
        float4 sA = gIn_SignalA.SampleLevel( gNearestMirror, uv, 0 );
        float4 sB = gIn_SignalB.SampleLevel( gNearestMirror, uv, 0 );
        float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0 );

        float z = sB.w / NRD_FP16_VIEWZ_SCALE;
        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );
        normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

        // Sample weight
        float w = GetGeometryWeight( Nv, samplePos, geometryWeightParams );
        w *= GetNormalWeight( diffNormalWeightParams, N, normal.xyz );

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
    gOut_SignalA[ pixelPos ] = finalA;
    gOut_SignalB[ pixelPos ] = finalB;
}
