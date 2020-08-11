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
    float4x4 gWorldToViewPrev;
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4 gFrustum;
    float2 gInvScreenSize;
    float2 gScreenSize;
    float2 gJitter;
    float2 gMotionVectorScale;
    float gIsOrtho;
    float gInf;
    float gDisocclusionThreshold;
    float gMaxDiffAccumulatedFrameNum;
    uint gFrameIndex;
    uint gCheckerboard;
    uint gWorldSpaceMotion;
    float gDebug;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_DiffA, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_DiffB, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_DiffA, t, 5, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_DiffB, t, 6, 0 );
NRI_RESOURCE( Texture2D<uint>, gIn_Prev_ViewZ_AccumSpeed, t, 7, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_DiffA, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_DiffB, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<uint>, gOut_InternalData, u, 2, 0 );
NRI_RESOURCE( RWTexture2D<uint>, gOut_ViewZ_AccumSpeed, u, 3, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = ( float2( pixelPos ) + 0.5 ) * gInvScreenSize;
    float2 sampleUv = pixelUv + gJitter;

    // Early out
    float viewZ = gIn_ViewZ[ pixelPos ];

    [branch]
    if ( abs( viewZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_DiffA[ pixelPos ] = 0;
        #endif
        gOut_DiffB[ pixelPos ] = NRD_INF_DIFF_B;
        gOut_InternalData[ pixelPos ] = PackDiffInternalData( MAX_ACCUM_FRAME_NUM, 0 ); // MAX_ACCUM_FRAME_NUM to skip HistoryFix on INF pixels
        gOut_ViewZ_AccumSpeed[ pixelPos ] = PackViewZAndAccumSpeed( NRD_FP16_MAX, 0 );
        return;
    }

    // Center position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );

    // Normal and roughness
    float4 normalAndRoughness = UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float roughness = normalAndRoughness.w;
    float3 N = normalAndRoughness.xyz;

    // Pseudo flat normal
    float3 Nflat = N;
    #if( USE_PSEUDO_FLAT_NORMALS ) // TODO: shared memory?
        Nflat += UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos + int2( +1,  0 ) ], false ).xyz;
        Nflat += UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos + int2( -1,  0 ) ], false ).xyz;
        Nflat += UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos + int2(  0, -1 ) ], false ).xyz;
        Nflat += UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos + int2(  0, +1 ) ], false ).xyz;
        Nflat = normalize( Nflat );
    #endif
    float3 Nvflat = STL::Geometry::RotateVectorInverse( gViewToWorld, Nflat );

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Previous viewZ and accumulation speed
    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gScreenSize );
    float2 gatherUv = ( bilinearFilterAtPrevPos.origin + 1.0 ) * gInvScreenSize;
    uint4 pack = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, gatherUv ).wzxy;
    float4 viewZprev = UnpackViewZ( pack );

    // Compute disocclusion basing on plane distance
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev = dot( Nflat, Xprev ); // = dot( Nvflatprev, Xvprev )
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) ); // = dot( Nvflatprev, Xvprev / Xvprev.z )
    float4 planeDist = abs( NoVprev * abs( viewZprev ) - NoXprev );

    float4 occlusion = step( gDisocclusionThreshold, planeDist * invDistToPoint );
    occlusion = saturate( isInScreen - occlusion );

    // Sample history
    float2 sampleUvNearestPrev = ( bilinearFilterAtPrevPos.origin + 0.5 ) * gInvScreenSize;
    float4 weights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, occlusion );

    float4 da00 = gIn_History_DiffA.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0 );
    float4 da10 = gIn_History_DiffA.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 0 ) );
    float4 da01 = gIn_History_DiffA.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 0, 1 ) );
    float4 da11 = gIn_History_DiffA.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 1 ) );
    float4 historyDiffA = STL::Filtering::ApplyBilinearCustomWeights( da00, da10, da01, da11, weights );

    float4 db00 = gIn_History_DiffB.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0 );
    float4 db10 = gIn_History_DiffB.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 0 ) );
    float4 db01 = gIn_History_DiffB.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 0, 1 ) );
    float4 db11 = gIn_History_DiffB.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 1 ) );
    float4 historyDiffB = STL::Filtering::ApplyBilinearCustomWeights( db00, db10, db01, db11, weights );

    // Accumulation speeds
    float4 accumSpeedPrev = UnpackAccumSpeed( pack );
    accumSpeedPrev = min( accumSpeedPrev + 1.0, gMaxDiffAccumulatedFrameNum );

    float accumSpeed = STL::Filtering::ApplyBilinearCustomWeights( accumSpeedPrev.x, accumSpeedPrev.y, accumSpeedPrev.z, accumSpeedPrev.w, weights );
    float2 diffAccumSpeeds = GetSpecAccumSpeed( accumSpeed, 1.0, 0.0, 0.0 );
    float diffAccumSpeed = 1.0 / ( diffAccumSpeeds.x + 1.0 );

    // Current data with reconstruction (if needed)
    float4 diffA = gIn_DiffA[ pixelPos ];
    float4 diffB = gIn_DiffB[ pixelPos ];

    #if( CHECKERBOARD_SUPPORT == 1 )
        bool isNoData = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex ) == gCheckerboard;
        if ( gCheckerboard != CHECKERBOARD_OFF && isNoData )
        {
            int3 pixelPosLR = int3( pixelPos.x - 1, pixelPos.x + 1, pixelPos.y );

            float4 diffA_left = gIn_DiffA[ pixelPosLR.xz ];
            float4 diffA_right = gIn_DiffA[ pixelPosLR.yz ];

            float4 diffB_left = gIn_DiffB[ pixelPosLR.xz ];
            float4 diffB_right = gIn_DiffB[ pixelPosLR.yz ];

            float2 zDelta = GetBilateralWeight( float2( diffB_left.w, diffB_right.w ) / NRD_FP16_VIEWZ_SCALE, viewZ );
            float2 w = zDelta * STL::Math::PositiveRcp( zDelta.x + zDelta.y );
            float w00 = saturate( 1.0 - w.x - w.y );

            // History weight
            float2 motion = pixelUvPrev - pixelUv;
            float motionLength = length( motion );
            float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, accumSpeed, motionLength );
            float historyWeight = min( temporalAccumulationParams.x, 0.5 );

            diffA = diffA_left * w.x + diffA_right * w.y + diffA * w00;
            diffA = lerp( diffA, historyDiffA, historyWeight );

            diffB = diffB_left * w.x + diffB_right * w.y + diffB * w00;
            diffB = lerp( diffB, historyDiffB, historyWeight );
        }
    #endif

    // Accumulation
    #if( SHOW_MIPS != 0 )
        diffAccumSpeed = 1.0;
    #endif

    uint data = PackDiffInternalData( float3( diffAccumSpeeds, accumSpeed ), historyDiffB.x );

    historyDiffA.xyz = lerp( historyDiffA.xyz, diffA.xyz, diffAccumSpeed );
    historyDiffA.w = lerp( historyDiffA.w, diffA.w, max( diffAccumSpeed, MIN_HITDIST_ACCUM_SPEED ) );
    historyDiffB = lerp( historyDiffB, diffB, diffAccumSpeed );

    // Add low amplitude noise to fight with imprecision problems
    STL::Rng::Initialize( pixelPos, gFrameIndex + 341 );
    float2 rnd = STL::Rng::GetFloat2( );
    float2 dither = 1.0 + ( rnd * 2.0 - 1.0 ) * DITHERING_AMPLITUDE;
    historyDiffA *= dither.x;
    historyDiffB *= dither.y;

    // Output
    float scaledViewZ = clamp( viewZ * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );

    gOut_DiffA[ pixelPos ] = historyDiffA;
    gOut_DiffB[ pixelPos ] = float4( historyDiffB.xyz, scaledViewZ );
    gOut_InternalData[ pixelPos ] = data;
    gOut_ViewZ_AccumSpeed[ pixelPos ] = PackViewZAndAccumSpeed( viewZ, accumSpeed );
}
