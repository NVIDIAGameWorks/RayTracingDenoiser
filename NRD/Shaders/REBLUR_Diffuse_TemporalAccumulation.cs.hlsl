/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "NRD.hlsl"
#include "STL.hlsl"
#include "REBLUR_Config.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    REBLUR_DIFF_SHARED_CB_DATA;

    float4x4 gWorldToViewPrev;
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4 gCameraDelta;
    float2 gMotionVectorScale;
    float gJitterDelta;
    float gCheckerboardResolveAccumSpeed;
    float gDisocclusionThreshold;
    uint gDiffCheckerboard;
};

#include "NRD_Common.hlsl"
#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<uint2>, gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Diff, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_HistoryFast_Diff, t, 5, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 6, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<unorm float2>, gOut_InternalData, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<unorm float>, gOut_Error, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 2, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Fast_Diff, u, 3, 0 );

void Preload( int2 sharedId, int2 globalId )
{
    uint2 globalIdUser = gRectOrigin + globalId;

    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );

    [branch]
    if( viewZ > gInf )
        return;

    // Center position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );

    // Normal and roughness
    int2 smemPos = threadId + BORDER;
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Calculate distribution of normals
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
            float4 n = s_Normal_Roughness[ pos.y ][ pos.x ];

            Nflat += n.xyz;
        }
    }

    float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );

    float3 Navg = Nflat * invSum;
    float edge = DetectEdge( Navg );

    Nflat = normalize( Nflat );

    // Compute previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser ] * gMotionVectorScale.xyy; // TODO: use nearest MV
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = IsInScreen2x2( pixelUvPrev, gRectSizePrev );
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );

    // Previous data ( Catmull-Rom )
    STL::Filtering::CatmullRom catmullRomFilterAtPrevPos = STL::Filtering::GetCatmullRomFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float2 catmullRomFilterAtPrevPosGatherOrigin = catmullRomFilterAtPrevPos.origin * gInvScreenSize;
    uint4 prevPackRed0 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
    uint4 prevPackRed1 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 prevPackRed2 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 prevPackRed3 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;

    float4 prevViewZ0 = UnpackViewZ( prevPackRed0 );
    float4 prevViewZ1 = UnpackViewZ( prevPackRed1 );
    float4 prevViewZ2 = UnpackViewZ( prevPackRed2 );
    float4 prevViewZ3 = UnpackViewZ( prevPackRed3 );

    float4 diffPrevAccumSpeeds = UnpackDiffAccumSpeed( uint4( prevPackRed0.w, prevPackRed1.z, prevPackRed2.y, prevPackRed3.x ) );

    // Previous normal, roughness and accum speed ( bilinear )
    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float2 bilinearFilterAtPrevPosGatherOrigin = ( bilinearFilterAtPrevPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackGreen = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, bilinearFilterAtPrevPosGatherOrigin ).wzxy;
    float3 prevNormal00 = UnpackNormalRoughness( prevPackGreen.x ).xyz;
    float3 prevNormal10 = UnpackNormalRoughness( prevPackGreen.y ).xyz;
    float3 prevNormal01 = UnpackNormalRoughness( prevPackGreen.z ).xyz;
    float3 prevNormal11 = UnpackNormalRoughness( prevPackGreen.w ).xyz;

    float3 prevNflat = prevNormal00 + prevNormal10 + prevNormal01 + prevNormal11;
    prevNflat = normalize( prevNflat );

    // Plane distance based disocclusion for surface motion
    float parallax = ComputeParallax( X, Xprev, gCameraDelta.xyz );
    float2 disocclusionThresholds = GetDisocclusionThresholds( gDisocclusionThreshold, gJitterDelta, viewZ, parallax, Nflat, X, invDistToPoint );
    disocclusionThresholds.x = lerp( -1.0, disocclusionThresholds.x, isInScreen ); // out-of-screen = occlusion
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev1 = abs( dot( Xprev, Nflat ) );
    float NoXprev2 = abs( dot( Xprev, prevNflat ) );
    float NoXprev = max( NoXprev1, NoXprev2 ) * invDistToPoint; // normalize here to save ALU
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) );
    float4 planeDist0 = abs( NoVprev * abs( prevViewZ0 ) - NoXprev );
    float4 planeDist1 = abs( NoVprev * abs( prevViewZ1 ) - NoXprev );
    float4 planeDist2 = abs( NoVprev * abs( prevViewZ2 ) - NoXprev );
    float4 planeDist3 = abs( NoVprev * abs( prevViewZ3 ) - NoXprev );
    float4 occlusion0 = step( planeDist0, disocclusionThresholds.x );
    float4 occlusion1 = step( planeDist1, disocclusionThresholds.x );
    float4 occlusion2 = step( planeDist2, disocclusionThresholds.x );
    float4 occlusion3 = step( planeDist3, disocclusionThresholds.x );

    // Avoid "got stuck in history" effect under slow motion when only 1 sample is valid from 2x2 footprint and there is a big difference between
    // foreground and background surfaces. Instead of final scalar accum speed scaling we can apply it to accum speeds from the previous frame
    float4 planeDist2x2 = float4( planeDist0.w, planeDist1.z, planeDist2.y, planeDist3.x );
    planeDist2x2 = STL::Math::LinearStep( 0.2, disocclusionThresholds.x, planeDist2x2 );

    float footprintAvg = STL::Filtering::ApplyBilinearFilter( planeDist2x2.x, planeDist2x2.y, planeDist2x2.z, planeDist2x2.w, bilinearFilterAtPrevPos );
    float fmin = min( bilinearFilterAtPrevPos.weights.x, bilinearFilterAtPrevPos.weights.y ) + 0.01;
    float fmax = max( bilinearFilterAtPrevPos.weights.x, bilinearFilterAtPrevPos.weights.y ) + 0.01;
    footprintAvg = lerp( footprintAvg, 1.0, STL::Math::LinearStep( 0.05, 0.5, fmin / fmax ) );

    diffPrevAccumSpeeds *= footprintAvg;

    // Ignore backfacing history
    float4 cosa;
    cosa.x = dot( N, prevNormal00.xyz );
    cosa.y = dot( N, prevNormal10.xyz );
    cosa.z = dot( N, prevNormal01.xyz );
    cosa.w = dot( N, prevNormal11.xyz );

    float4 frontFacing = STL::Math::LinearStep( disocclusionThresholds.y, 0.001, cosa );
    occlusion0.w *= frontFacing.x;
    occlusion1.z *= frontFacing.y;
    occlusion2.y *= frontFacing.z;
    occlusion3.x *= frontFacing.w;

    // Sample diffuse history
    float2 catmullRomFilterAtPrevPosOrigin = ( catmullRomFilterAtPrevPos.origin + 0.5 ) * gInvScreenSize;
    float4 d10 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 0 ) );
    float4 d20 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 0 ) );
    float4 d01 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 1 ) );
    float4 d11 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 1 ) );
    float4 d21 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 1 ) );
    float4 d31 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 1 ) );
    float4 d02 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 2 ) );
    float4 d12 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 2 ) );
    float4 d22 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 2 ) );
    float4 d32 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 2 ) );
    float4 d13 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 3 ) );
    float4 d23 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 3 ) );

    float4 diffOcclusion2x2 = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );
    float4 diffWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, diffOcclusion2x2 );
    float4 diffHistory = STL::Filtering::ApplyBilinearCustomWeights( d11, d21, d12, d22, diffWeights );
    float4 diffHistoryCatRom = STL::Filtering::ApplyCatmullRomFilterNoCorners( catmullRomFilterAtPrevPos, d10, d20, d01, d11, d21, d31, d02, d12, d22, d32, d13, d23 );
    diffHistory = MixLinearAndCatmullRom( diffHistory, diffHistoryCatRom, occlusion0, occlusion1, occlusion2, occlusion3 );

    // Accumulation speeds
    float diffMaxAccumSpeed = GetAccumSpeed( diffPrevAccumSpeeds, diffWeights, gDiffMaxAccumulatedFrameNum );

    // Noisy signal with reconstruction (if needed)
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    float4 diff = gIn_Diff[ pixelPos ];
    bool diffHasData = gDiffCheckerboard == 2 || checkerboard == gDiffCheckerboard;
    if( !diffHasData )
    {
        float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffMaxAccumSpeed, parallax );
        float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

        diff.xyz = lerp( diffHistory.xyz, diff.xyz, historyWeight );
        diff.w = lerp( diffHistory.w, diff.w, max( historyWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );
    }

    // Diffuse accumulation
    float diffAccumSpeed = GetSpecAccumSpeed( diffMaxAccumSpeed, 1.0, 0.0, 0.0 );
    float diffAccumSpeedNonLinear = 1.0 / ( diffAccumSpeed + 1.0 );

    float4 diffResult;
    diffResult.xyz = lerp( diffHistory.xyz, diff.xyz, diffAccumSpeedNonLinear );
    diffResult.w = lerp( diffHistory.w, diff.w, max( diffAccumSpeedNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );

    // Output
    gOut_InternalData[ pixelPos ] = PackDiffInternalData( diffAccumSpeed, edge );
    gOut_Diff[ pixelPos ] = diffResult;

    // Error
    float diffError = GetColorErrorForAdaptiveRadiusScale( diffResult, diffHistory, diffAccumSpeedNonLinear, 1.0 );
    gOut_Error[ pixelPos ] = diffError;

    // Fast history
    #if( REBLUR_USE_FAST_HISTORY == 1 )
        float4 d11f = gIn_HistoryFast_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 1 ) );
        float4 d21f = gIn_HistoryFast_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 1 ) );
        float4 d12f = gIn_HistoryFast_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 2 ) );
        float4 d22f = gIn_HistoryFast_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 2 ) );
        float4 diffHistoryFast = STL::Filtering::ApplyBilinearCustomWeights( d11f, d21f, d12f, d22f, diffWeights );

        // History fix (previous state)
        float diffMinAccumSpeedFast = REBLUR_FRAME_NUM_WITH_HISTORY_FIX - 1;
        diffHistoryFast = lerp( diffHistory, diffHistoryFast, diffAccumSpeed > diffMinAccumSpeedFast );

        float diffAccumSpeedNonLinearFast = 1.0 / ( min( diffAccumSpeed, gDiffMaxFastAccumulatedFrameNum ) + 1.0 );

        float4 diffResultFast;
        diffResultFast.xyz = lerp( diffHistoryFast.xyz, diff.xyz, diffAccumSpeedNonLinearFast );
        diffResultFast.w = lerp( diffHistoryFast.w, diff.w, max( diffAccumSpeedNonLinearFast, 2.0 * REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );

        gOut_Fast_Diff[ pixelPos ] = diffResultFast;
    #endif
}
