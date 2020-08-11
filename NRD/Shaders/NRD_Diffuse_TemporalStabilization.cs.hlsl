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
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4 gFrustum;
    float2 gInvScreenSize;
    float2 gScreenSize;
    float2 gJitter;
    float2 gMotionVectorScale;
    float2 gAntilagRadianceThreshold;
    float gIsOrtho;
    float gInf;
    uint gFrameIndex;
    uint gWorldSpaceMotion;
    uint gAntilag;
    float gDebug;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 0, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Signal, t, 2, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<uint>, gOut_ViewZ_AccumSpeed, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Signal, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalCopy, u, 2, 0 );

groupshared float4 s_Data[ BUFFER_Y ][ BUFFER_X ];
groupshared uint s_Pack[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    s_Data[ sharedId.y ][ sharedId.x ] = gIn_Signal[ globalId ];
    s_Pack[ sharedId.y ][ sharedId.x ] = gOut_ViewZ_AccumSpeed[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    // Debug
    #if ( SHOW_MIPS )
        float4 s = gIn_Signal[ pixelPos ];
        gOut_Signal[ pixelPos ] = s;
        gOut_SignalCopy[ pixelPos ] = s;
        return;
    #endif

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

    // Early out
    int2 pos = threadId + BORDER;
    uint temp = s_Pack[ pos.y ][ pos.x ];
    float centerZ = UnpackViewZ( temp );
    float accumSpeed = UnpackAccumSpeed( temp );

    [branch]
    if ( abs( centerZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Signal[ pixelPos ] = 0;
            gOut_SignalCopy[ pixelPos ] = 0;
        #endif
        return;
    }

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );

    // Calculate color distribution
    float sum = 0;
    float4 m1 = 0;
    float4 m2 = 0;
    float4 input = 0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 pos = threadId + int2( dx, dy );
            float4 data = s_Data[ pos.y ][ pos.x ];
            float z = UnpackViewZ( s_Pack[ pos.y ][ pos.x ] );

            if ( dx == BORDER && dy == BORDER )
                input = data;

            float w = GetBilateralWeight( z, centerZ );
            sum += w;

            m1 += data * w;
            m2 += data * data * w;
        }
    }

    float invSum = STL::Math::PositiveRcp( sum );
    m1 *= invSum;
    m2 *= invSum;

    float4 sigma = sqrt( abs( m2 - m1 * m1 ) );

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );
    float2 motion = pixelUvPrev - pixelUv;
    float motionLength = length( motion );

    // History weight
    float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, accumSpeed, motionLength );

    // Mix with history
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float4 history = BicubicFilterNoCorners( gIn_History, gLinearClamp, pixelPosPrev, gInvScreenSize );
    float4 colorMin = m1 - sigma * temporalAccumulationParams.y;
    float4 colorMax = m1 + sigma * temporalAccumulationParams.y;

    float antiLag = 1.0;
    if( gAntilag && USE_ANTILAG == 1 )
    {
        float2 delta = abs( history.xw - input.xw ) - sigma.xw * 2.0;
        delta = STL::Math::LinearStep( float2( gAntilagRadianceThreshold.y, 0.1 ), float2( gAntilagRadianceThreshold.x, 0.01 ), delta );
        delta = STL::Math::Pow01( delta, float2( 2.0, 8.0 ) );

        float fade = 1.0 / ( 1.0 + accumSpeed );
        antiLag = min( delta.x, delta.y );
        antiLag = lerp( antiLag, 1.0, fade );

        gOut_ViewZ_AccumSpeed[ pixelPos ] = PackViewZAndAccumSpeed( centerZ, accumSpeed * antiLag );
    }

    history = clamp( history, colorMin, colorMax );

    float historyWeight = TS_MAX_HISTORY_WEIGHT * antiLag;
    float4 result = lerp( input, history, historyWeight * temporalAccumulationParams.x );

    // Dither
    STL::Rng::Initialize( pixelPos, gFrameIndex + 101 );
    float2 rnd = STL::Rng::GetFloat2( );
    float dither = 1.0 + ( rnd.x * 2.0 - 1.0 ) * DITHERING_AMPLITUDE;
    result *= dither;

    // Get rid of possible negative values
    float4 resultRgb;
    resultRgb.xyz = _NRD_YCoCgToLinear( result.xyz );
    resultRgb.w = max( result.w, 0.0 );

    result = resultRgb;
    result.xyz = _NRD_LinearToYCoCg( resultRgb.xyz );

    // Output
    gOut_Signal[ pixelPos ] = result;

    #if( SHOW_ACCUM_SPEED == 1 )
        resultRgb.w = saturate( accumSpeed / MAX_ACCUM_FRAME_NUM );
    #elif( SHOW_ANTILAG == 1 )
        resultRgb.w = antiLag;
    #endif

    gOut_SignalCopy[ pixelPos ] = resultRgb;
}
