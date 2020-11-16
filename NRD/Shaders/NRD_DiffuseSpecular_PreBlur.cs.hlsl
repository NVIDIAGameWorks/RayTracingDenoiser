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
    uint gDiffCheckerboard;
    uint gSpecCheckerboard;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalA, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalB, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Signal, t, 4, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalA, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalB, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Signal, u, 2, 0 );
NRI_RESOURCE( RWTexture2D<float>, gOut_ScaledViewZ, u, 3, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Checkerboard
    bool2 hasData = true;
    uint2 checkerboardPixelPos = pixelPos.xx;
    float2 checkerboardScale = 1.0;
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    if( gDiffCheckerboard != 2 )
    {
        hasData.x = checkerboard == gDiffCheckerboard;
        checkerboardPixelPos.x >>= 1;
        checkerboardScale.x = 0.5;
    }

    if( gSpecCheckerboard != 2 )
    {
        hasData.y = checkerboard == gSpecCheckerboard;
        checkerboardPixelPos.y >>= 1;
        checkerboardScale.y = 0.5;
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
        gOut_ScaledViewZ[ pixelPos ] = NRD_FP16_MAX;
        return;
    }

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float4 finalA = gIn_SignalA[ uint2( checkerboardPixelPos.x, pixelPos.y ) ];
    float4 finalB = gIn_SignalB[ uint2( checkerboardPixelPos.x, pixelPos.y ) ];
    float4 final = gIn_Signal[ uint2( checkerboardPixelPos.y, pixelPos.y ) ];

    #if( CHECKERBOARD_RESOLVE_MODE == SOFT )
        int4 pos = pixelPos.xyxy + int4( 0, -1, 0, 1 );
    #else
        int4 pos = pixelPos.xyxy + int4( -1, 0, 1, 0 );
    #endif

    float viewZ0 = gIn_ViewZ[ pos.xy ];
    float viewZ1 = gIn_ViewZ[ pos.zw ];

    float2 w = GetBilateralWeight( float2( viewZ0, viewZ1 ), centerZ );
    w *= STL::Math::PositiveRcp( w.x + w.y );

    pos.xz >>= 1;

    if( !hasData.x )
    {
        float4 signalB0 = gIn_SignalB[ pos.xy ];
        float4 signalA0 = gIn_SignalA[ pos.xy ];
        float4 signalB1 = gIn_SignalB[ pos.zw ];
        float4 signalA1 = gIn_SignalA[ pos.zw ];

        finalA = signalA0 * w.x + signalA1 * w.y;
        finalB = signalB0 * w.x + signalB1 * w.y;
    }

    if( !hasData.y )
    {
        float4 signal0 = gIn_Signal[ pos.xy ];
        float4 signal1 = gIn_Signal[ pos.zw ];

        final = signal0 * w.x + signal1 * w.y;
    }

    float diffCenterNormHitDist = finalA.w;
    float specCenterNormHitDist = final.w;

    // Normal and roughness
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Blur radius
    float diffHitDist = GetHitDistance( finalA.w, centerZ, gDiffScalingParams );
    float diffBlurRadius = DIFF_PRE_BLUR_RADIUS_SCALE * GetBlurRadius( gDiffBlurRadius, 1.0, diffHitDist, centerPos, 1.0 );
    float diffWorldBlurRadius = PixelRadiusToWorld( diffBlurRadius, centerZ );

    float specHitDist = GetHitDistance( final.w, centerZ, gSpecScalingParams, roughness );
    float specBlurRadius = SPEC_PRE_BLUR_RADIUS_SCALE * GetBlurRadius( gSpecBlurRadius, roughness, specHitDist, centerPos, 1.0 );
    specBlurRadius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gSpecTrimmingParams );
    float specWorldBlurRadius = PixelRadiusToWorld( specBlurRadius, centerZ );

    // Tangent basis
    float2x3 diffTvBv = GetKernelBasis( centerPos, Nv, diffWorldBlurRadius );
    float2x3 specTvBv = GetKernelBasis( centerPos, Nv, specWorldBlurRadius, roughness );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( PRE_BLUR_ROTATOR_MODE, pixelPos, gRotator );

    // Denoising
    float diffSum = 1.0;
    float2 specSum = 1.0;

    float2 geometryWeightParams = GetGeometryWeightParams( centerPos, Nv, gMetersToUnits, centerZ );
    float2 diffNormalWeightParams = GetNormalWeightParams( 1.0 );
    float2 specNormalWeightParams = GetNormalWeightParams( roughness );
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

            // Handle half res input in the checkerboard mode
            float2 checkerboardUv = uv;
            checkerboardUv.x *= checkerboardScale.x;
            checkerboardUv.x = checkerboardUv.x > checkerboardScale.x ? ( 2.0 * checkerboardScale.x - checkerboardUv.x ) : checkerboardUv.x;

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

        // Specular
        {
            // Sample coordinates
            float2 uv = GetKernelSampleCoordinates( offset, centerPos, specTvBv[ 0 ], specTvBv[ 1 ], rotator );

            // Handle half res input in the checkerboard mode
            float2 checkerboardUv = uv;
            checkerboardUv.x *= checkerboardScale.y;
            checkerboardUv.x = checkerboardUv.x > checkerboardScale.y ? ( 2.0 * checkerboardScale.y - checkerboardUv.x ) : checkerboardUv.x;

            // Fetch data
            float4 s = gIn_Signal.SampleLevel( gNearestMirror, checkerboardUv, 0 );
            float z = gIn_ViewZ.SampleLevel( gNearestMirror, uv, 0 );
            float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0 );

            float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );
            normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

            // Sample weight
            float w = GetGeometryWeight( Nv, samplePos, geometryWeightParams );
            w *= GetNormalWeight( specNormalWeightParams, N, normal.xyz );
            w *= GetRoughnessWeight( specRoughnessWeightParams, normal.w );

            float2 ww = w;
            ww.x *= GetHitDistanceWeight( specHitDistanceWeightParams, s.w );

            final += s * ww.xxxy;
            specSum += ww;
        }
    }

    float invSum = 1.0 / diffSum;
    finalA *= invSum;
    finalB.xyz *= invSum;
    final /= specSum.xxxy;

    // Special case for hit distance
    finalA.w = lerp( finalA.w, diffCenterNormHitDist, HIT_DIST_INPUT_MIX );
    final.w = lerp( final.w, specCenterNormHitDist, HIT_DIST_INPUT_MIX );

    // Output
    finalB.w = clamp( centerZ * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );

    gOut_SignalA[ pixelPos ] = finalA;
    gOut_SignalB[ pixelPos ] = finalB;
    gOut_Signal[ pixelPos ] = final;
    gOut_ScaledViewZ[ pixelPos ] = finalB.w;
}
