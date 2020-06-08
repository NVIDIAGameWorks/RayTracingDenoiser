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
    float3 gTrimmingParams;
    float gIsOrtho;
    float2 gJitter;
    float2 gBlueNoiseSinCos;
    float2 gInvScreenSize;
    float2 gScreenSize;
    float gMetersToUnits;
    float gBlurRadius;
    float gInf;
    float gUnproject;
    uint gFrameIndex;
    uint gCheckerboard;
    uint gAnisotropicFiltering;
    float gDebug;
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
    float2 pixelUv = ( float2( pixelPos ) + 0.5 ) * gInvScreenSize;
    float2 sampleUv = pixelUv + gJitter;

    float centerZ = gIn_ViewZ[ pixelPos ];

    // Early out
    [branch]
    if ( abs( centerZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Signal[ pixelPos ] = 0;
        #endif
        return;
    }

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( sampleUv, gFrustum, centerZ, gIsOrtho );
    float4 final = gIn_Signal[ pixelPos ];
    float centerNormHitDist = final.w;
    centerZ = abs( centerZ );

    // Normal and roughness
    float4 normalAndRoughness = UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Blur radius
    float hitDist = GetHitDistance( final.w, centerZ, gScalingParams, roughness );
    float radius = SPEC_PRE_BLUR_RADIUS_SCALE * GetBlurRadius( gBlurRadius, roughness, hitDist, centerPos, 1.0 );
    radius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gTrimmingParams );
    float worldRadius = radius * gUnproject * lerp( centerZ, 1.0, abs( gIsOrtho ) );

    // Tangent basis
    float3 Tv, Bv;
    GetKernelBasis( centerPos, Nv, roughness, worldRadius, 0.65, gAnisotropicFiltering, Tv, Bv );

    // Random rotation
    float4 rotator = float4( 1, 0, 0, 1 );
    #if( SPEC_PRE_BLUR_ROTATOR_MODE == FRAME )
        rotator = STL::Geometry::GetRotator( -gBlueNoiseSinCos.y, gBlueNoiseSinCos.x );
    #elif( SPEC_PRE_BLUR_ROTATOR_MODE == PIXEL )
        float angle = STL::Sequence::Bayer4x4( pixelPos, gFrameIndex );
        rotator = STL::Geometry::GetRotator( angle * STL::Math::Pi( 2.0 ) + STL::Math::Pi( 0.5 ) );
    #endif

    // Denoising
    float2 sum = 1.0;

    float geometryWeightParams = GetGeometryWeightParams( gMetersToUnits, centerZ );
    float2 normalWeightParams = GetNormalWeightParams( true, roughness );
    float2 roughnessWeightParams = GetRoughnessWeightParams( roughness );
    float2 hitDistanceWeightParams = GetHitDistanceWeightParams( roughness, centerNormHitDist );

    float checkerboardOffset = ( gFrameIndex & 0x1 ) == 0 ? -gInvScreenSize.x : gInvScreenSize.x;

    SPEC_UNROLL
    for( uint s = 0; s < SPEC_POISSON_SAMPLE_NUM; s++ )
    {
        // Sample coordinates
        float3 offset = SPEC_POISSON_SAMPLES[ s ];
        float2 uv = GetKernelSampleCoordinates( gViewToClip, gJitter, offset, centerPos, Tv, Bv, rotator );

        // Checkerboard handling - needed only in passes before temporal accumulation since signal reconstruction is there
        #if( CHECKERBOARD_SUPPORT == 1 )
            if( gCheckerboard != 0 && all( saturate( uv ) == uv ) )
            {
                uint2 pos = uint2( STL::Filtering::GetNearestFilter( uv, gScreenSize ).origin );
                bool isNotSpecular = STL::Sequence::CheckerBoard( pos, gFrameIndex ) != 0;
                uv.x += checkerboardOffset * float( isNotSpecular );
                checkerboardOffset = -checkerboardOffset;
            }
        #endif

        // Fetch data
        float4 s = gIn_Signal.SampleLevel( gNearestMirror, uv, 0 );
        float z = gIn_ViewZ.SampleLevel( gNearestMirror, uv, 0 );
        float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0.0 );

        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );
        normal = UnpackNormalAndRoughness( normal, false );

        // Sample weight
        float w = GetGeometryWeight( centerPos, Nv, samplePos, geometryWeightParams );
        w *= GetNormalWeight( normalWeightParams, N, normal.xyz );
        w *= GetRoughnessWeight( roughnessWeightParams, normal.w );

        float2 ww = w;
        ww.x *= GetHitDistanceWeight( hitDistanceWeightParams, s.w );

        final += s * ww.xxxy;
        sum += ww;
    }

    final /= sum.xxxy;

    // Special case for hit distance
    final.w = lerp( final.w, centerNormHitDist, HIT_DIST_INPUT_MIX );

    // Output
    gOut_Signal[ pixelPos ] = final;
}
