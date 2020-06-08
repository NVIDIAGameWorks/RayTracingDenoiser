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
    float2 gBlueNoiseSinCos;
    float2 gInvScreenSize;
    float2 gScreenSize;
    float gIsOrtho;
    float gUnproject;
    float gMetersToUnits;
    float gBlurRadius;
    float gInf;
    uint gFrameIndex;
    uint gCheckerboard;
    float gDebug;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalA, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalB, t, 2, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalA, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalB, u, 1, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = ( float2( pixelPos ) + 0.5 ) * gInvScreenSize;
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

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( sampleUv, gFrustum, centerZ, gIsOrtho );
    float4 finalA = gIn_SignalA[ pixelPos ];
    float centerNormHitDist = finalA.w;
    centerZ = abs( centerZ );

    // Normal
    float4 normalAndRoughness = UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );

    // Blur radius
    float hitDist = GetHitDistance( finalA.w, centerZ, gScalingParams );
    float radius = DIFF_PRE_BLUR_RADIUS_SCALE * GetBlurRadius( gBlurRadius, 1.0, hitDist, centerPos, 1.0 );
    float worldRadius = radius * gUnproject * lerp( centerZ, 1.0, abs( gIsOrtho ) );

    // Tangent basis
    float3 Tv, Bv;
    GetKernelBasis( centerPos, Nv, 1.0, worldRadius, 0.75, 0, Tv, Bv );

    // Random rotation
    float4 rotator = float4( 1, 0, 0, 1 );
    #if( DIFF_PRE_BLUR_ROTATOR_MODE == FRAME )
        rotator = STL::Geometry::GetRotator( -gBlueNoiseSinCos.y, gBlueNoiseSinCos.x );
    #elif( DIFF_PRE_BLUR_ROTATOR_MODE == PIXEL )
        float angle = STL::Sequence::Bayer4x4( pixelPos, gFrameIndex );
        rotator = STL::Geometry::GetRotator( angle * STL::Math::Pi( 2.0 ) + STL::Math::Pi( 0.5 ) );
    #endif

    // Denoising
    float sum = 1.0;

    float geometryWeightParams = GetGeometryWeightParams( gMetersToUnits, centerZ );
    float2 normalWeightParams = GetNormalWeightParams( false, 1.0 );

    float checkerboardOffset = ( gFrameIndex & 0x1 ) == 0 ? -gInvScreenSize.x : gInvScreenSize.x;

    DIFF_UNROLL
    for( uint s = 0; s < DIFF_POISSON_SAMPLE_NUM; s++ )
    {
        // Sample coordinates
        float3 offset = DIFF_POISSON_SAMPLES[ s ];
        float2 uv = GetKernelSampleCoordinates( gViewToClip, gJitter, offset, centerPos, Tv, Bv, rotator );

        // Checkerboard handling - needed only in passes before temporal accumulation since signal reconstruction is there
        #if( CHECKERBOARD_SUPPORT == 1 )
            if( gCheckerboard != 0 && all( saturate( uv ) == uv ) )
            {
                uint2 pos = uint2( STL::Filtering::GetNearestFilter( uv, gScreenSize ).origin );
                bool isNotDiffuse = STL::Sequence::CheckerBoard( pos, gFrameIndex ) == 0;
                uv.x += checkerboardOffset * float( isNotDiffuse );
                checkerboardOffset = -checkerboardOffset;
            }
        #endif

        // Fetch data
        float4 sA = gIn_SignalA.SampleLevel( gNearestMirror, uv, 0.0 );
        float4 sB = gIn_SignalB.SampleLevel( gNearestMirror, uv, 0.0 );

        float z = sB.w / NRD_FP16_VIEWZ_SCALE;
        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );

        // Sample weight
        float w = GetGeometryWeight( centerPos, Nv, samplePos, geometryWeightParams );

        #if( USE_NORMAL_WEIGHT_IN_DIFF_PRE_BLUR == 1 )
            float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0.0 );
            normal = UnpackNormalAndRoughness( normal, false );
            w *= GetNormalWeight( normalWeightParams, N, normal.xyz );
        #endif

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
