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
    float gFramerateScale;

    float4x4 gWorldToView;
    float4 gRotator;
    float4 gDiffScalingParams;
    float gDiffBlurRadius;
    float gDiffBlurRadiusScale;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_InternalData, t, 1, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_TemporalAccumulationOutput, t, 4, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 0, 0 );

void Preload( int2 sharedId, int2 globalId )
{
    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = gIn_ScaledViewZ[ globalId ] / NRD_FP16_VIEWZ_SCALE;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    PRELOAD_INTO_SMEM;

    // Debug
    #if( SHOW_MIPS )
        gOut_Diff[ pixelPos ] = gIn_Diff[ pixelPos ];
        return;
    #endif

    // Early out
    int2 smemPos = threadId + BORDER;
    float centerZ = s_ViewZ[ smemPos.y ][ smemPos.x ];

    [branch]
    if( abs( centerZ ) > abs( gInf ) )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Diff[ pixelPos ] = 0;
        #endif
        return;
    }

    // Normal and roughness
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Accumulations speeds
    float3 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ] );
    float diffNormAccumSpeed = saturate( diffInternalData.x * STL::Math::PositiveRcp( diffInternalData.y ) );
    float diffNonLinearAccumSpeed = 1.0 / ( 1.0 + diffInternalData.x );

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float4 diff = gIn_Diff[ pixelPos ];
    float diffCenterNormHitDist = diff.w;

    // Blur radius
    float diffHitDist = GetHitDistance( diff.w, centerZ, gDiffScalingParams );
    float diffBlurRadius = GetBlurRadius( gDiffBlurRadius, 1.0, diffHitDist, centerPos, diffNonLinearAccumSpeed );
    float diffWorldBlurRadius = PixelRadiusToWorld( diffBlurRadius, centerZ );

    // Blur radius scale
    float luma = diff.x;
    float lumaPrev = gIn_TemporalAccumulationOutput[ pixelPos ].x;
    float error = GetColorErrorForAdaptiveRadiusScale( luma, lumaPrev, diffNonLinearAccumSpeed );
    float diffBlurRadiusScale = POST_BLUR_RADIUS_SCALE + error * gDiffBlurRadiusScale;
    diffWorldBlurRadius *= diffBlurRadiusScale;

    // Tangent basis
    float2x3 diffTvBv = GetKernelBasis( centerPos, Nv, diffWorldBlurRadius, diffNormAccumSpeed );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( POST_BLUR_ROTATOR_MODE, pixelPos, gRotator );

    // Edge detection
    float edge = DetectEdge( N, smemPos );

    // Denoising
    float diffSum = 1.0;

    float2 geometryWeightParams = GetGeometryWeightParams( centerPos, Nv, centerZ );
    float diffNormalWeightParams = GetNormalWeightParams( 1.0, edge, diffNormAccumSpeed );
    float2 diffHitDistanceWeightParams = GetHitDistanceWeightParams( diffCenterNormHitDist, diffNormAccumSpeed, diffHitDist, centerPos );

    UNROLL
    for( uint i = 0; i < POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = POISSON_SAMPLES[ i ];

        // Sample coordinates
        float2 uv = GetKernelSampleCoordinates( offset, centerPos, diffTvBv[ 0 ], diffTvBv[ 1 ], rotator );

        // Fetch data
        float4 d = gIn_Diff.SampleLevel( gNearestMirror, uv, 0 );
        float scaledViewZ = gIn_ScaledViewZ.SampleLevel( gNearestMirror, uv, 0 );
        float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0 );

        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, scaledViewZ / NRD_FP16_VIEWZ_SCALE, gIsOrtho );
        normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

        // Sample weight
        float w = GetGeometryWeight( geometryWeightParams, Nv, samplePos );
        w *= GetNormalWeight( diffNormalWeightParams, N, normal.xyz );
        w *= GetHitDistanceWeight( diffHitDistanceWeightParams, d.w ); // yes, apply hit distance weight to hit distance in this pass

        diff += d * w;
        diffSum += w;
    }

    diff *= STL::Math::PositiveRcp( diffSum );

    // Special case for hit distance
    diff.w = lerp( diff.w, diffCenterNormHitDist, HIT_DIST_INPUT_MIX );

    // Output
    gOut_Diff[ pixelPos ] = diff;
}
