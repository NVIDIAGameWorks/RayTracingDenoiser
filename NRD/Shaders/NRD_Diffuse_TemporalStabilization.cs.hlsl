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
    float2 padding;
    float gMetersToUnits;
    float gIsOrtho;
    float gUnproject;
    float gDebug;
    float gInf;
    uint gCheckerboard;
    uint gFrameIndex;
    uint gWorldSpaceMotion;

    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float2 gScreenSize;
    float2 gMotionVectorScale;
    float2 gAntilagRadianceThreshold;
    uint gAntilag;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<uint>, gIn_InternalData, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Signal, t, 5, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<uint>, gOut_ViewZ_AccumSpeed, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Signal, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalCopy, u, 2, 0 );

groupshared float4 s_Signal[ BUFFER_Y ][ BUFFER_X ];
groupshared float4 s_Data[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    // TODO: use w = 0 if outside of the screen or use SampleLevel with Clamp sampler
    float4 t;
    t.xyz = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] ).xyz;
    t.w = gIn_ViewZ[ globalId ];

    s_Signal[ sharedId.y ][ sharedId.x ] = gIn_Signal[ globalId ];
    s_Data[ sharedId.y ][ sharedId.x ] = t;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Debug
    #if ( SHOW_MIPS )
        float4 s = gIn_Signal[ pixelPos ];
        gOut_Signal[ pixelPos ] = s;
        gOut_SignalCopy[ pixelPos ] = s;
        return;
    #endif

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
    float4 t = s_Data[ pos.y ][ pos.x ];
    float3 N = t.xyz;
    float centerZ = t.w;

    [branch]
    if ( abs( centerZ ) > gInf )
    {
        #if( DIFF_BLACK_OUT_INF_PIXELS == 1 )
            gOut_Signal[ pixelPos ] = 0;
            gOut_SignalCopy[ pixelPos ] = 0;
        #endif
        gOut_ViewZ_AccumSpeed[ pixelPos ] = PackViewZAndAccumSpeed( INF, 0 );
        return;
    }

    float2 normalParams;
    normalParams.x = STL::ImportanceSampling::GetSpecularLobeHalfAngle( 1.0 );
    normalParams.x += SPEC_NORMAL_BANDING_FIX;
    //normalParams.x *= LOBE_STRICTNESS_FACTOR * 1.5; // TODO: is it needed? can be dangeroud in this place... use full angle instead? or add "fade" a bit? use GetSpecularLobeHalfAngle with 0.8-0.95 factor?
    normalParams.y = 1.0;

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );

    // Local variance
    float sum = 0;
    float4 m1 = 0;
    float4 m2 = 0;
    float4 input = 0;

    float4 maxInput = -INF;
    float4 minInput = INF;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 pos = threadId + int2( dx, dy );
            float4 signal = s_Signal[ pos.y ][ pos.x ];
            float4 data = s_Data[ pos.y ][ pos.x ];
            float3 normal = data.xyz;
            float z = data.w;

            float w = 1.0;
            if( dx == BORDER && dy == BORDER )
                input = signal;
            else
            {
                w = GetBilateralWeight( z, centerZ );
                w *= GetNormalWeight( normalParams, N, normal );

                maxInput = max( signal - float( w == 0.0 ) * INF, maxInput );
                minInput = min( signal + float( w == 0.0 ) * INF, minInput );
            }

            m1 += signal * w;
            m2 += signal * signal * w;
            sum += w;
        }
    }

    float invSum = STL::Math::PositiveRcp( sum );
    m1 *= invSum;
    m2 *= invSum;

    float4 sigma = GetVariance( m1, m2 );

    // Apply RCRS
    float rcrsWeight = ( sum - 1.0 ) / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) - 1.0 );
    rcrsWeight = STL::Math::Sqrt01( rcrsWeight );

    float4 rcrsResult = min( input, maxInput );
    rcrsResult = max( rcrsResult, minInput );

    input = lerp( input, rcrsResult, rcrsWeight );

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Sample history
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float4 history = BicubicFilterNoCorners( gIn_History, gLinearClamp, pixelPosPrev, gInvScreenSize );

    // History weight
    float4 internalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ] );
    float accumSpeed = internalData.z;
    float motionLength = length( pixelUvPrev - pixelUv );
    float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, accumSpeed, motionLength );

    // Antilag
    float antiLag = 1.0;
    if( gAntilag && USE_ANTILAG == 1 )
    {
        float2 delta = abs( history.xw - input.xw ) - sigma.xw * 2.0;
        delta = STL::Math::LinearStep( float2( gAntilagRadianceThreshold.y, 0.1 ), float2( gAntilagRadianceThreshold.x, 0.01 ), delta );
        delta = STL::Math::Pow01( delta, float2( 2.0, 8.0 ) );

        float fade = accumSpeed / ( 1.0 + accumSpeed );
        fade *= temporalAccumulationParams.x;
        fade *= rcrsWeight;

        antiLag = min( delta.x, delta.y );
        antiLag = lerp( 1.0, antiLag, fade );
    }

    // Clamp history and combine with the current frame
    float4 colorMin = m1 - sigma * temporalAccumulationParams.y;
    float4 colorMax = m1 + sigma * temporalAccumulationParams.y;
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
    gOut_ViewZ_AccumSpeed[ pixelPos ] = PackViewZAndAccumSpeed( centerZ, accumSpeed * antiLag );
    gOut_Signal[ pixelPos ] = result;

    #if( SHOW_ACCUM_SPEED == 1 )
        resultRgb.w = saturate( accumSpeed / MAX_ACCUM_FRAME_NUM );
    #elif( SHOW_ANTILAG == 1 )
        resultRgb.w = antiLag;
    #endif

    gOut_SignalCopy[ pixelPos ] = resultRgb;
}
