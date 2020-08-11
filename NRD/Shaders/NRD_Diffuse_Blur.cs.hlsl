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
    float4x4 gWorldToView;
    float4x4 gViewToClip;
    float4 gFrustum;
    float4 gScalingParams;
    float2 gJitter;
    float2 gInvScreenSize;
    float2 gBlueNoiseSinCos;
    float gIsOrtho;
    float gMetersToUnits;
    float gBlurRadius;
    float gInf;
    float gUnproject;
    uint gFrameIndex;
    float gDebug;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<uint>, gIn_InternalData, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalA, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalB, t, 3, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalA, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalB, u, 1, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;
    float2 sampleUv = pixelUv + gJitter;

    float4 finalB = gIn_SignalB[ pixelPos ];
    float centerZ = finalB.w / NRD_FP16_VIEWZ_SCALE;

    // Early out
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
    float4 normalAndRoughness = UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );

    // Accumulations speeds
    uint pack = gIn_InternalData[ pixelPos ];
    float4 internalData = UnpackDiffInternalData( pack );
    float normAccumSpeed = saturate( internalData.x * STL::Math::PositiveRcp( internalData.y ) );
    float nonLinearAccumSpeed = 1.0 / ( 1.0 + internalData.x );

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( sampleUv, gFrustum, centerZ, gIsOrtho );
    float4 finalA = gIn_SignalA[ pixelPos ];
    float centerNormHitDist = finalA.w;

    // Blur radius
    float hitDist = GetHitDistance( finalA.w, centerZ, gScalingParams );
    float radius = GetBlurRadius( gBlurRadius, 1.0, hitDist, centerPos, nonLinearAccumSpeed );
    float worldRadius = PixelRadiusToWorld( radius, centerZ, gUnproject, gIsOrtho );

    // Tangent basis
    float3 Tv, Bv;
    GetKernelBasis( centerPos, Nv, 1.0, worldRadius, normAccumSpeed, 0, Tv, Bv );

    // Random rotation
    float4 rotator = float4( 1, 0, 0, 1 );
    #if( DIFF_BLUR_ROTATOR_MODE == FRAME )
        rotator = STL::Geometry::GetRotator( gBlueNoiseSinCos.x, gBlueNoiseSinCos.y );
    #elif( DIFF_BLUR_ROTATOR_MODE == PIXEL )
        float angle = STL::Sequence::Bayer4x4( pixelPos, gFrameIndex + 7 );
        rotator = STL::Geometry::GetRotator( angle * STL::Math::Pi( 2.0 ) );
    #endif

    // Denoising
    float sum = 1.0;

    float geometryWeightParams = GetGeometryWeightParams( gMetersToUnits, centerZ );
    float2 normalWeightParams = GetNormalWeightParams( 1.0, internalData.z, normAccumSpeed );

    DIFF_UNROLL
    for( uint s = 0; s < DIFF_POISSON_SAMPLE_NUM; s++ )
    {
        // Sample coordinates
        float3 offset = DIFF_POISSON_SAMPLES[ s ];
        float2 uv = GetKernelSampleCoordinates( gViewToClip, gJitter, offset, centerPos, Tv, Bv, rotator );

        // Fetch data
        float4 sA = gIn_SignalA.SampleLevel( gNearestMirror, uv, 0.0 );
        float4 sB = gIn_SignalB.SampleLevel( gNearestMirror, uv, 0.0 );
        float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0.0 );

        float z = sB.w / NRD_FP16_VIEWZ_SCALE;
        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );
        normal = UnpackNormalAndRoughness( normal, false );

        // Sample weight
        float w = GetGeometryWeight( centerPos, Nv, samplePos, geometryWeightParams );
        w *= GetNormalWeight( normalWeightParams, N, normal.xyz );

        finalA += sA * w;
        finalB.xyz += sB.xyz * w;
        sum += w;
    }

    finalA /= sum;
    finalB.xyz /= sum;

    // Special case for hit distance
    finalA.w = lerp( finalA.w, centerNormHitDist, HIT_DIST_INPUT_MIX );

    // Output
    gOut_SignalA[ pixelPos ] = finalA;
    gOut_SignalB[ pixelPos ] = finalB;
}
