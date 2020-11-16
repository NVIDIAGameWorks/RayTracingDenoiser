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

    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float2 gMotionVectorScale;
    float2 gAntilagRadianceThreshold;
    float gAntilag;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_InternalData, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_DiffHistory, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalA, t, 5, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalB, t, 6, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<uint2>, gOut_ViewZ_Normal_Roughness_AccumSpeeds, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_DiffSignal, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_DiffSignalCopy, u, 2, 0 );

groupshared float4 s_DiffSignal[ BUFFER_Y ][ BUFFER_X ];
groupshared float4 s_Normal_ViewZ[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    // TODO: use w = 0 if outside of the screen or use SampleLevel with Clamp sampler
    float4 t;
    t.xyz = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] ).xyz;
    t.w = gIn_ViewZ[ globalId ];

    float4 finalA = gIn_SignalA[ globalId ];
    float4 finalB = gIn_SignalB[ globalId ];

    s_DiffSignal[ sharedId.y ][ sharedId.x ] = _NRD_BackEnd_UnpackDiffuse( finalA, finalB, t.xyz, false );
    s_Normal_ViewZ[ sharedId.y ][ sharedId.x ] = t;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Debug
    #if ( SHOW_MIPS )
        float4 finalA = gIn_SignalA[ pixelPos ];
        float4 finalB = gIn_SignalB[ pixelPos ];
        float3 N = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] ).xyz;
        float4 s = _NRD_BackEnd_UnpackDiffuse( finalA, finalB, N, false );
        gOut_DiffSignal[ pixelPos ] = s;
        gOut_DiffSignalCopy[ pixelPos ] = s;
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
    float4 t = s_Normal_ViewZ[ pos.y ][ pos.x ];
    float centerZ = t.w;

    [branch]
    if ( abs( centerZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_DiffSignal[ pixelPos ] = 0;
            gOut_DiffSignalCopy[ pixelPos ] = 0;
        #endif
        gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( INF, 0.0, float3( 0, 0, 1 ), 1.0, 0.0 );
        return;
    }

    // Normal
    float3 N = t.xyz;

    // Internal data
    float3 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ] );
    float diffAccumSpeed = diffInternalData.z;

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );

    // Local variance
    float sum = 0;

    float4 diffM1 = 0;
    float4 diffM2 = 0;
    float4 diffInput = 0;
    float4 diffMaxInput = -INF;
    float4 diffMinInput = INF;

    float2 diffNormalParams = GetNormalWeightParamsRoughEstimate( 1.0 );

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 pos = threadId + int2( dx, dy );
            float4 signal = s_DiffSignal[ pos.y ][ pos.x ];
            t = s_Normal_ViewZ[ pos.y ][ pos.x ];

            float w = 1.0;
            if( dx == BORDER && dy == BORDER )
                diffInput = signal;
            else
            {
                w = GetBilateralWeight( t.w, centerZ );
                w *= GetNormalWeight( diffNormalParams, N, t.xyz );

                float a = float( w == 0.0 ) * INF;
                diffMaxInput = max( signal - a, diffMaxInput );
                diffMinInput = min( signal + a, diffMinInput );
            }

            diffM1 += signal * w;
            diffM2 += signal * signal * w;
            sum += w;
        }
    }

    float invSum = STL::Math::PositiveRcp( sum );
    diffM1 *= invSum;
    diffM2 *= invSum;

    float4 diffSigma = GetVariance( diffM1, diffM2 );

    // Apply RCRS
    float rcrsWeight = ( sum - 1.0 ) / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) - 1.0 );
    rcrsWeight = STL::Math::Sqrt01( rcrsWeight );
    rcrsWeight *= 1.0 - gReference;

    float4 rcrsResult = min( diffInput, diffMaxInput );
    rcrsResult = max( rcrsResult, diffMinInput );
    diffInput = lerp( diffInput, rcrsResult, rcrsWeight );

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Sample history
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float4 diffHistory = BicubicFilterNoCorners( gIn_DiffHistory, gLinearClamp, pixelPosPrev, gInvScreenSize );

    // History weight
    float motionLength = length( pixelUvPrev - pixelUv );
    float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffAccumSpeed, motionLength );

    // Antilag
    float antiLag = 1.0;
    if( gAntilag != 0.0 && USE_ANTILAG == 1 )
    {
        // TODO: if compression is used delta.x needs to be decompressed, but it doesn't affect the behavior, because heavily compressed values do not lag
        float2 delta = abs( diffHistory.xw - diffInput.xw ) - diffSigma.xw * 2.0;
        delta = STL::Math::LinearStep( float2( gAntilagRadianceThreshold.y, 0.1 ), float2( gAntilagRadianceThreshold.x, 0.01 ), delta );
        delta = STL::Math::Pow01( delta, float2( 2.0, 8.0 ) );

        float fade = diffAccumSpeed / ( 1.0 + diffAccumSpeed );
        fade *= diffTemporalAccumulationParams.x;
        fade *= rcrsWeight;

        antiLag = min( delta.x, delta.y );
        antiLag = lerp( 1.0, antiLag, fade );
    }

    // Clamp history and combine with the current frame
    float4 diffMin = diffM1 - diffSigma * diffTemporalAccumulationParams.y;
    float4 diffMax = diffM1 + diffSigma * diffTemporalAccumulationParams.y;
    diffHistory = clamp( diffHistory, diffMin, diffMax );

    float historyWeight = TS_MAX_HISTORY_WEIGHT * antiLag;
    float4 diffResult = lerp( diffInput, diffHistory, historyWeight * diffTemporalAccumulationParams.x );

    // Dither
    STL::Rng::Initialize( pixelPos, gFrameIndex + 2 );
    float2 rnd = STL::Rng::GetFloat2( );
    float dither = 1.0 + ( rnd.x * 2.0 - 1.0 ) * DITHERING_AMPLITUDE;
    diffResult *= dither;

    // Get rid of possible negative values
    diffResult.xyz = _NRD_YCoCgToLinear( diffResult.xyz );
    diffResult.w = max( diffResult.w, 0.0 );
    diffResult.xyz = _NRD_LinearToYCoCg( diffResult.xyz );

    // Output
    diffAccumSpeed *= antiLag;

    gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( centerZ, diffAccumSpeed, N, 1.0, 0.0 );
    gOut_DiffSignal[ pixelPos ] = diffResult;

    #if( SHOW_ACCUM_SPEED == 1 )
        diffResult.w = saturate( diffAccumSpeed / MAX_ACCUM_FRAME_NUM );
    #elif( SHOW_ANTILAG == 1 )
        diffResult.w = antiLag;
    #endif

    gOut_DiffSignalCopy[ pixelPos ] = diffResult;
}
