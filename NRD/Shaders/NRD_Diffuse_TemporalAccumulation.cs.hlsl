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

    float4x4 gWorldToViewPrev;
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4x4 gWorldToClip;
    float3 gCameraDelta;
    float gJitterDelta;
    float2 gMotionVectorScale;
    float gCheckerboardResolveAccumSpeed;
    float gDisocclusionThreshold;
    float gDiffMaxAccumulatedFrameNum;
    uint gDiffCheckerboard;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_SignalA, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_SignalB, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalA, t, 5, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SignalB, t, 6, 0 );
NRI_RESOURCE( Texture2D<uint2>, gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds, t, 7, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalA, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_SignalB, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float2>, gOut_InternalData, u, 2, 0 );

groupshared float4 s_Normal_ViewZ[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    // TODO: use w = 0 if outside of the screen or use SampleLevel with Clamp sampler
    float4 t;
    t.xyz = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] ).xyz;
    t.w = gIn_ViewZ[ globalId ];

    s_Normal_ViewZ[ sharedId.y ][ sharedId.x ] = t;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

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
    int2 centerId = threadId + BORDER;
    float4 centerData = s_Normal_ViewZ[ centerId.y ][ centerId.x ];
    float viewZ = centerData.w;

    [branch]
    if ( abs( viewZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_SignalA[ pixelPos ] = 0;
        #endif
        gOut_SignalB[ pixelPos ] = NRD_INF_DIFF_B;
        gOut_InternalData[ pixelPos ] = PackDiffInternalData( MAX_ACCUM_FRAME_NUM ); // MAX_ACCUM_FRAME_NUM to skip HistoryFix on INF pixels
        return;
    }

    // Center position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );

    // Normal and roughness
    float3 N = centerData.xyz;

    // Flat normal
    float3 Nflat = N;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadId + int2( dx, dy );
            float4 normalAndViewZ = s_Normal_ViewZ[ pos.y ][ pos.x ];

            Nflat += normalAndViewZ.xyz; // yes, no weight // TODO: all 9? or 5 samples like it was before?
        }
    }

    Nflat = normalize( Nflat );

    // Compute previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) ); // TODO: ideally, isInScreen must be per pixel in 2x2 or 4x4 footprint
    float2 motion = pixelUvPrev - pixelUv;
    float motionLength = length( motion );
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );

    // Previous viewZ, normal, roughness and accum speed ( bilinear )
    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gScreenSize );
    float2 bilinearFilterAtPrevPosGatherOrigin = ( bilinearFilterAtPrevPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackRed = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, bilinearFilterAtPrevPosGatherOrigin ).wzxy;
    uint4 prevPackGreen = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, bilinearFilterAtPrevPosGatherOrigin ).wzxy;
    float4 prevViewZ = UnpackViewZ( prevPackRed );
    float4 prevNormalAndRoughness00 = UnpackNormalRoughnessAccumSpeed( prevPackGreen.x );
    float4 prevNormalAndRoughness10 = UnpackNormalRoughnessAccumSpeed( prevPackGreen.y );
    float4 prevNormalAndRoughness01 = UnpackNormalRoughnessAccumSpeed( prevPackGreen.z );
    float4 prevNormalAndRoughness11 = UnpackNormalRoughnessAccumSpeed( prevPackGreen.w );
    float4 prevAccumSpeeds = UnpackAccumSpeed( prevPackRed );

    float3 prevNflat = prevNormalAndRoughness00.xyz + prevNormalAndRoughness10.xyz + prevNormalAndRoughness01.xyz + prevNormalAndRoughness11.xyz;
    prevNflat = normalize( prevNflat );

    // Plane distance based disocclusion for surface motion
    float parallax = ComputeParallax( pixelUv, 1.0, Xprev, gCameraDelta, gWorldToClip );
    float2 disocclusionThresholds = GetDisocclusionThresholds( gDisocclusionThreshold, gJitterDelta, viewZ, parallax, Nflat, X, invDistToPoint );
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev1 = abs( dot( Nflat, Xprev ) ); // = dot( Nvflatprev, Xvprev ), "abs" is needed here only to get "max" absolute value in the next line
    float NoXprev2 = abs( dot( prevNflat, Xprev ) );
    float NoXprev = max( NoXprev1, NoXprev2 ) * invDistToPoint;
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) ); // = dot( Nvflatprev, Xvprev / Xvprev.z )
    float4 planeDist = abs( NoVprev * abs( prevViewZ ) - NoXprev );
    float4 occlusion = saturate( isInScreen - step( disocclusionThresholds.x, planeDist ) );

    // Ignore backfacing history
    float4 cosa;
    cosa.x = dot( N, prevNormalAndRoughness00.xyz );
    cosa.y = dot( N, prevNormalAndRoughness10.xyz );
    cosa.z = dot( N, prevNormalAndRoughness01.xyz );
    cosa.w = dot( N, prevNormalAndRoughness11.xyz );
    occlusion *= STL::Math::LinearStep( disocclusionThresholds.y, 0.001, cosa );

    // Sample history
    float2 sampleUvNearestPrev = ( bilinearFilterAtPrevPos.origin + 0.5 ) * gInvScreenSize;
    float4 weights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, occlusion );

    float4 da00 = gIn_History_SignalA.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0 );
    float4 da10 = gIn_History_SignalA.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 0 ) );
    float4 da01 = gIn_History_SignalA.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 0, 1 ) );
    float4 da11 = gIn_History_SignalA.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 1 ) );
    float4 historyDiffA = STL::Filtering::ApplyBilinearCustomWeights( da00, da10, da01, da11, weights );

    float4 db00 = gIn_History_SignalB.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0 );
    float4 db10 = gIn_History_SignalB.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 0 ) );
    float4 db01 = gIn_History_SignalB.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 0, 1 ) );
    float4 db11 = gIn_History_SignalB.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 1 ) );
    float4 historyDiffB = STL::Filtering::ApplyBilinearCustomWeights( db00, db10, db01, db11, weights );

    // Diffuse accumulation speeds
    prevAccumSpeeds = min( prevAccumSpeeds + 1.0, gDiffMaxAccumulatedFrameNum );
    float diffAccumSpeed = STL::Filtering::ApplyBilinearCustomWeights( prevAccumSpeeds.x, prevAccumSpeeds.y, prevAccumSpeeds.z, prevAccumSpeeds.w, weights );

    // Diffuse noisy signal with reconstruction (if needed)
    float4 diffA = gIn_SignalA[ pixelPos ];
    float4 diffB = gIn_SignalB[ pixelPos ];

    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );
    bool diffHasData = gDiffCheckerboard == 2 || checkerboard == gDiffCheckerboard;

    if( !diffHasData )
    {
        #if( CHECKERBOARD_RESOLVE_MODE == SOFT )
            int3 pos = int3( pixelPos.x - 1, pixelPos.x + 1, pixelPos.y );

            float4 diffA0 = gIn_SignalA[ pos.xz ];
            float4 diffA1 = gIn_SignalA[ pos.yz ];

            float4 diffB0 = gIn_SignalB[ pos.xz ];
            float4 diffB1 = gIn_SignalB[ pos.yz ];

            float2 w = GetBilateralWeight( float2( diffB0.w, diffB1.w ) / NRD_FP16_VIEWZ_SCALE, viewZ );
            w *= CHECKERBOARD_SIDE_WEIGHT * 0.5;

            float invSum = STL::Math::PositiveRcp( w.x + w.y + 1.0 - CHECKERBOARD_SIDE_WEIGHT );

            diffA = diffA0 * w.x + diffA1 * w.y + diffA * ( 1.0 - CHECKERBOARD_SIDE_WEIGHT );
            diffA *= invSum;

            diffB = diffB0 * w.x + diffB1 * w.y + diffB * ( 1.0 - CHECKERBOARD_SIDE_WEIGHT );
            diffB *= invSum;
        #endif

        // Mix with history ( optional )
        float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffAccumSpeed, motionLength );
        float historyWeight = gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

        diffA = lerp( diffA, historyDiffA, historyWeight );
        diffB = lerp( diffB, historyDiffB, historyWeight );
    }

    // Diffuse accumulation
    float2 diffAccumSpeeds = GetSpecAccumSpeed( diffAccumSpeed, 1.0, 0.0, 0.0 );
    float diffHistoryAmount = 1.0 / ( diffAccumSpeeds.x + 1.0 );

    float4 resultA;
    resultA.xyz = lerp( historyDiffA.xyz, diffA.xyz, diffHistoryAmount );
    resultA.w = lerp( historyDiffA.w, diffA.w, max( diffHistoryAmount, MIN_HITDIST_ACCUM_SPEED ) );
    float4 resultB = lerp( historyDiffB, diffB, diffHistoryAmount );

    // Add low amplitude noise to fight with imprecision problems
    STL::Rng::Initialize( pixelPos, gFrameIndex + 1 );
    float2 rnd = STL::Rng::GetFloat2( );
    float2 dither = 1.0 + ( rnd * 2.0 - 1.0 ) * DITHERING_AMPLITUDE;
    resultA *= dither.x;
    resultB *= dither.y;

    // Output
    float scaledViewZ = clamp( viewZ * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );

    gOut_SignalA[ pixelPos ] = resultA;
    gOut_SignalB[ pixelPos ] = float4( resultB.xyz, scaledViewZ );
    gOut_InternalData[ pixelPos ] = PackDiffInternalData( float3( diffAccumSpeeds, diffAccumSpeed ) );
}
