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
    float4 gSpecScalingParams;
    float3 gSpecTrimmingParams;
    float gDiffBlurRadius;
    float gSpecBlurRadius;
    float gDiffBlurRadiusScale;
    float gSpecBlurRadiusScale;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<uint>, gIn_InternalData, t, 1, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalA, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalB, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_DiffTemporalAccumulationOutput, t, 5, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Signal, t, 6, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SpecTemporalAccumulationOutput, t, 7, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalA, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalB, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Signal, u, 2, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    #if ( SHOW_MIPS )
        float4 tempA = gIn_SignalA[ pixelPos ];
        float4 tempB = gIn_SignalB[ pixelPos ];
        float3 tempN = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] ).xyz;
        gOut_SignalA[ pixelPos ] = tempA;
        gOut_SignalB[ pixelPos ] = tempB;
        gOut_Signal[ pixelPos ] = gIn_Signal[ pixelPos ];
        return;
    #endif

    // Early out
    float4 finalB = gIn_SignalB[ pixelPos ];
    float centerZ = finalB.w / NRD_FP16_VIEWZ_SCALE;

    [branch]
    if ( abs( centerZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_SignalA[ pixelPos ] = 0;
            gOut_SignalB[ pixelPos ] = float4( 0, 0, 0, finalB.w );
            gOut_Signal[ pixelPos ] = 0;
        #endif
        return;
    }

    // Normal
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Accumulations speeds
    float2x3 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], roughness );

    float3 diffInternalData = internalData[ 0 ];
    float diffNormAccumSpeed = saturate( diffInternalData.x * STL::Math::PositiveRcp( diffInternalData.y ) );
    float diffNonLinearAccumSpeed = 1.0 / ( 1.0 + diffInternalData.x );

    float3 specInternalData = internalData[ 1 ];
    float specNormAccumSpeed = saturate( specInternalData.x * STL::Math::PositiveRcp( specInternalData.y ) );
    float specNonLinearAccumSpeed = 1.0 / ( 1.0 + specInternalData.x );

    // Specular specific - want to use wide blur radius
    specNonLinearAccumSpeed = lerp( 0.02, 1.0, specNonLinearAccumSpeed );

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float4 finalA = gIn_SignalA[ pixelPos ];
    float4 final = gIn_Signal[ pixelPos ];
    float diffCenterNormHitDist = finalA.w;
    float specCenterNormHitDist = final.w;

    // Blur radius
    float diffHitDist = GetHitDistance( finalA.w, centerZ, gDiffScalingParams );
    float diffBlurRadius = GetBlurRadius( gDiffBlurRadius, 1.0, diffHitDist, centerPos, diffNonLinearAccumSpeed );
    diffBlurRadius *= DIFF_POST_BLUR_RADIUS_SCALE;

    float diffWorldBlurRadius = PixelRadiusToWorld( diffBlurRadius, centerZ );

    float specHitDist = GetHitDistance( final.w, centerZ, gSpecScalingParams, roughness );
    float specBlurRadius = GetBlurRadius( gSpecBlurRadius, roughness, specHitDist, centerPos, specNonLinearAccumSpeed );
    specBlurRadius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gSpecTrimmingParams );
    specBlurRadius *= SPEC_POST_BLUR_RADIUS_SCALE;

    float specWorldBlurRadius = PixelRadiusToWorld( specBlurRadius, centerZ );

    // Blur radius scale
    float luma = finalB.x;
    float lumaPrev = gIn_DiffTemporalAccumulationOutput[ pixelPos ].x;
    float error = abs( luma - lumaPrev ) * STL::Math::PositiveRcp( min( luma, lumaPrev ) );
    error = STL::Math::SmoothStep( 0.0, 0.1, error );
    error *= 1.0 - diffNonLinearAccumSpeed;
    float diffBlurRadiusScale = 1.0 + error * gDiffBlurRadiusScale / DIFF_POST_BLUR_RADIUS_SCALE;
    diffWorldBlurRadius *= diffBlurRadiusScale;

    luma = final.x;
    lumaPrev = gIn_SpecTemporalAccumulationOutput[ pixelPos ].x;
    error = abs( luma - lumaPrev ) * STL::Math::PositiveRcp( max( luma, lumaPrev ) );
    error = STL::Math::SmoothStep( 0.0, 0.1, error );
    error *= STL::Math::LinearStep( 0.04, 0.15, roughness );
    error *= 1.0 - specNonLinearAccumSpeed;
    float specBlurRadiusScale = 1.0 + error * gSpecBlurRadiusScale / SPEC_POST_BLUR_RADIUS_SCALE;
    specWorldBlurRadius *= specBlurRadiusScale;

    // Tangent basis
    float2x3 diffTvBv = GetKernelBasis( centerPos, Nv, diffWorldBlurRadius, diffNormAccumSpeed );
    float2x3 specTvBv = GetKernelBasis( centerPos, Nv, specWorldBlurRadius, specNormAccumSpeed, roughness );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( POST_BLUR_ROTATOR_MODE, pixelPos, gRotator );

    // Denoising
    float diffSum = 1.0;
    float specSum = 1.0; // yes, apply hit distance weight to SO in this pass

    float2 geometryWeightParams = GetGeometryWeightParams( centerPos, Nv, gMetersToUnits, centerZ );
    float2 diffNormalWeightParams = GetNormalWeightParams( 1.0, diffInternalData.z, diffNormAccumSpeed );
    float2 specNormalWeightParams = GetNormalWeightParams( roughness, specInternalData.z, specNormAccumSpeed );
    float2 specRoughnessWeightParams = GetRoughnessWeightParams( roughness );
    float2 specHitDistanceWeightParams = GetHitDistanceWeightParams( roughness, specCenterNormHitDist );

    UNROLL
    for( uint i = 0; i < POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = POISSON_SAMPLES[ i ];

        // Diffuse
        {
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

        // Specular
        {
            // Sample coordinates
            float2 uv = GetKernelSampleCoordinates( offset, centerPos, specTvBv[ 0 ], specTvBv[ 1 ], rotator );

            // Fetch data
            float4 s = gIn_Signal.SampleLevel( gNearestMirror, uv, 0 );
            float scaledViewZ = gIn_ScaledViewZ.SampleLevel( gNearestMirror, uv, 0 );
            float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0 );

            float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, scaledViewZ / NRD_FP16_VIEWZ_SCALE, gIsOrtho );
            normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

            // Sample weight
            float w = GetGeometryWeight( Nv, samplePos, geometryWeightParams );
            w *= GetNormalWeight( specNormalWeightParams, N, normal.xyz );
            w *= GetRoughnessWeight( specRoughnessWeightParams, normal.w );
            w *= GetHitDistanceWeight( specHitDistanceWeightParams, s.w );

            final += s * w;
            specSum += w;
        }
    }

    float invSum = 1.0 / diffSum;
    finalA *= invSum;
    finalB.xyz *= invSum;
    final /= specSum;

    // Special case for hit distance
    finalA.w = lerp( finalA.w, diffCenterNormHitDist, HIT_DIST_INPUT_MIX );
    final.w = lerp( final.w, specCenterNormHitDist, HIT_DIST_INPUT_MIX );

    // Output
    gOut_SignalA[ pixelPos ] = finalA;
    gOut_SignalB[ pixelPos ] = finalB;
    gOut_Signal[ pixelPos ] = final;
}
