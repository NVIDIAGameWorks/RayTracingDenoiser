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
    float2 gJitter;
    float2 gInvScreenSize;
    float gIsOrtho;
    float gBlurRadius;
    float gMetersToUnits;
    float gInf;
    float gUnproject;
    uint gFrameIndex;
    float gDebug;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_Signal, t, 1, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float3>, gOut_Signal, u, 0, 0 );

groupshared float2 s_Data[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    float2 data = gIn_Signal[ globalId ];
    data.x = ( data.x == NRD_FP16_MAX ) ? NRD_FP16_MAX : ( data.x / NRD_FP16_VIEWZ_SCALE );
    data.y = data.y / NRD_FP16_VIEWZ_SCALE;

    s_Data[ sharedId.y ][ sharedId.x ] = data;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = ( float2( pixelPos ) + 0.5 ) * gInvScreenSize;
    float2 sampleUv = pixelUv + gJitter;

    // Rename the 16x16 group into a 18x14 group + some idle threads in the end
    float linearId = ( threadIndex + 0.5 ) / BUFFER_X;
    int2 newId = int2( frac( linearId ) * BUFFER_X, linearId );
    int2 groupBase = pixelPos - threadId - BORDER;

    // Preload into shared memory
    if ( newId.y < RENAMED_GROUP_Y )
        Preload( newId, groupBase + newId );

    newId.y += RENAMED_GROUP_Y;

    if ( newId.y < BUFFER_Y )
        Preload( newId, groupBase + newId );

    GroupMemoryBarrierWithGroupSync( );

    // Center data
    int2 pos = threadId + BORDER;
    float2 centerData = s_Data[ pos.y ][ pos.x ];
    float centerShadow = float( centerData.x == NRD_FP16_MAX );
    float centerZ = centerData.y;

    // Early out
    [branch]
    if( abs( centerZ ) > gInf || centerData.x == 0.0 )
    {
        gOut_Signal[ pixelPos ] = float3( centerShadow, 0.0, centerZ * NRD_FP16_VIEWZ_SCALE );
        return;
    }

    // Position
    float3 centerPos = STL::Geometry::ReconstructViewPosition( sampleUv, gFrustum, centerZ, gIsOrtho );
    centerZ = abs( centerZ );

    // Normal
    float4 normalAndRoughness = UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );

    // Estimate average distance to occluder
    float2 final = float2( centerShadow, centerData.x * float( centerShadow != 1.0 ) + 0.001 );
    float sum = 1.0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadId + int2( dx, dy );
            float2 data = s_Data[ pos.y ][ pos.x ];

            float shadow = float( data.x == NRD_FP16_MAX );
            float hitDist = data.x;
            float z = data.y;

            float w = GetBilateralWeight( z, centerPos.z );

            final += float2( shadow, hitDist * float( shadow != 1.0 ) + 0.001 ) * w;
            sum += w;
        }
    }

    final *= STL::Math::PositiveRcp( sum );

    // Blur radius
    float innerShadowFix = lerp( 0.5, 1.0, final.x );
    float worldRadius = final.y * gBlurRadius * innerShadowFix;

    float unprojectZ = gUnproject * lerp( centerZ, 1.0, abs( gIsOrtho ) );
    float pixelRadius = worldRadius * STL::Math::PositiveRcp( unprojectZ );
    pixelRadius = min( pixelRadius, SHADOW_MAX_PIXEL_RADIUS );
    worldRadius = pixelRadius * unprojectZ;

    #if( USE_SHADOW_BLUR_RADIUS_FIX == 1 )
        worldRadius += 5.0 * unprojectZ * final.x;
    #endif

    // Tangent basis
    float3x3 mWorldToLocal = STL::Geometry::GetBasis( Nv );
    float3 Tv = mWorldToLocal[ 0 ] * worldRadius;
    float3 Bv = mWorldToLocal[ 1 ] * worldRadius;

    // Random rotation (yes, it's needed here!)
    float angle = STL::Sequence::Bayer4x4( pixelPos, gFrameIndex );
    float4 rotator = STL::Geometry::GetRotator( angle * STL::Math::Pi( 2.0 ) );

    // Denoising
    sum = 1.0;

    float centerWeight = STL::Math::LinearStep( 1.0, 0.9, final.x );
    float geometryWeightParams = SHADOW_PLANE_DISTANCE_SCALE * GetGeometryWeightParams( gMetersToUnits, centerZ );

    SHADOW_UNROLL
    for( uint s = 0; s < SHADOW_POISSON_SAMPLE_NUM; s++ )
    {
        // Sample coordinates
        float3 offset = SHADOW_POISSON_SAMPLES[ s ];
        offset.xy = STL::Geometry::RotateVector( rotator, offset.xy );

        float3 p = centerPos + Tv * offset.x + Bv * offset.y;
        float3 clip = mul( gViewToClip, float4( p, 1.0 ) ).xyw;
        clip.xy /= clip.z; // TODO: potentially dangerous!
        clip.y = -clip.y;
        float2 uv = clip.xy * 0.5 + 0.5 - gJitter;

        // Fetch data
        float2 s = gIn_Signal.SampleLevel( gNearestMirror, uv, 0.0 );

        float shadow = float( s.x == NRD_FP16_MAX );
        float hitDist = s.x / NRD_FP16_VIEWZ_SCALE;
        float z = s.y / NRD_FP16_VIEWZ_SCALE;
        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );

        // Sample weight
        float w = GetGeometryWeight( centerPos, Nv, samplePos, geometryWeightParams );

        #if( USE_SHADOW_BLUR_RADIUS_FIX == 1 )
            w *= lerp( shadow, 1.0, centerWeight );
        #endif

        final += float2( shadow, hitDist * float( shadow != 1.0 ) + 0.001 ) * w;
        sum += w;
    }

    final *= STL::Math::PositiveRcp( sum );
    final.y *= NRD_FP16_VIEWZ_SCALE;

    // Output
    gOut_Signal[ pixelPos ] = float3( final, centerPos.z * NRD_FP16_VIEWZ_SCALE );
}
