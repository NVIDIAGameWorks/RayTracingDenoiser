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
    REBLUR_DIFF_SPEC_SHARED_CB_DATA;

    float4x4 gWorldToViewPrev;
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4x4 gWorldToClip;
    float4 gCameraDelta;
    float4 gSpecHitDistParams;
    float2 gMotionVectorScale;
    float gCheckerboardResolveAccumSpeed;
    float gDisocclusionThreshold;
    float gDiffMaxFastAccumulatedFrameNum;
    uint gDiffCheckerboard;
    float gSpecMaxFastAccumulatedFrameNum;
    uint gSpecCheckerboard;
};

#define NRD_CTA_8X8
#include "NRD_Common.hlsl"

#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<uint2>, gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Diff, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Spec, t, 5, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_HistoryFast_Diff, t, 6, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_HistoryFast_Spec, t, 7, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 8, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 9, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<unorm float4>, gOut_InternalData, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 2, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Fast_Diff, u, 3, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Fast_Spec, u, 4, 0 );

groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    uint2 globalIdUser = gRectOrigin + globalId;

    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
    s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Spec[ globalId ];
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
    float3 V = -X * invDistToPoint;

    // Normal and roughness
    int2 smemPos = threadId + BORDER;
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Calculate distribution of normals and signal variance
    float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
    float4 specM1 = spec;
    float4 specM2 = spec * spec;
    float normalParams = GetNormalWeightParamsRoughEstimate( roughness );
    float2 roughnessParams = GetRoughnessWeightParams( roughness );

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

            float4 s = s_Spec[ pos.y ][ pos.x ];
            specM1 += s;
            specM2 += s * s;
        }
    }

    float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );

    specM1 *= invSum;
    specM2 *= invSum;
    float4 specSigma = GetStdDev( specM1, specM2 );

    float3 Navg = Nflat * invSum;
    float edge = DetectEdge( Navg );
    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );

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
    uint4 prevPackGreen0 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy; // TODO: try to get rid of complicated roughness checks, use a single Gather like in diffuse
    uint4 prevPackGreen1 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 prevPackGreen2 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 prevPackGreen3 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;

    float4 prevViewZ0 = UnpackViewZ( prevPackRed0 );
    float4 prevViewZ1 = UnpackViewZ( prevPackRed1 );
    float4 prevViewZ2 = UnpackViewZ( prevPackRed2 );
    float4 prevViewZ3 = UnpackViewZ( prevPackRed3 );

    float4 diffPrevAccumSpeeds = UnpackDiffAccumSpeed( uint4( prevPackRed0.w, prevPackRed1.z, prevPackRed2.y, prevPackRed3.x ) );

    float4 specPrevAccumSpeeds;
    float3 prevNormal00 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen0.w, specPrevAccumSpeeds.x ).xyz;
    float3 prevNormal10 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen1.z, specPrevAccumSpeeds.y ).xyz;
    float3 prevNormal01 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen2.y, specPrevAccumSpeeds.z ).xyz;
    float3 prevNormal11 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen3.x, specPrevAccumSpeeds.w ).xyz;

    float4 prevRoughness0 = UnpackRoughness( prevPackGreen0 );
    float4 prevRoughness1 = UnpackRoughness( prevPackGreen1 );
    float4 prevRoughness2 = UnpackRoughness( prevPackGreen2 );
    float4 prevRoughness3 = UnpackRoughness( prevPackGreen3 );

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

    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float footprintAvg = STL::Filtering::ApplyBilinearFilter( planeDist2x2.x, planeDist2x2.y, planeDist2x2.z, planeDist2x2.w, bilinearFilterAtPrevPos );
    float fmin = min( bilinearFilterAtPrevPos.weights.x, bilinearFilterAtPrevPos.weights.y ) + 0.01;
    float fmax = max( bilinearFilterAtPrevPos.weights.x, bilinearFilterAtPrevPos.weights.y ) + 0.01;
    footprintAvg = lerp( footprintAvg, 1.0, STL::Math::LinearStep( 0.05, 0.5, fmin / fmax ) );

    diffPrevAccumSpeeds *= footprintAvg;
    specPrevAccumSpeeds *= footprintAvg;

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

    // Modify specular occlusion to avoid averaging of specular for different roughness
    float4 prevRoughnessWeight0 = GetRoughnessWeight( roughnessParams, prevRoughness0 );
    float4 prevRoughnessWeight1 = GetRoughnessWeight( roughnessParams, prevRoughness1 );
    float4 prevRoughnessWeight2 = GetRoughnessWeight( roughnessParams, prevRoughness2 );
    float4 prevRoughnessWeight3 = GetRoughnessWeight( roughnessParams, prevRoughness3 );
    occlusion0 *= STL::Math::LinearStep( 0.1, 0.9, prevRoughnessWeight0 );
    occlusion1 *= STL::Math::LinearStep( 0.1, 0.9, prevRoughnessWeight1 );
    occlusion2 *= STL::Math::LinearStep( 0.1, 0.9, prevRoughnessWeight2 );
    occlusion3 *= STL::Math::LinearStep( 0.1, 0.9, prevRoughnessWeight3 );

    // Sample specular history ( surface motion )
    // Averaging of values with different compression can be dangerous, especially in case of CatRom with negative lobes
    float4 s10 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 0 ) );
    float4 s20 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 0 ) );
    float4 s01 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 1 ) );
    float4 s11 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 1 ) );
    float4 s21 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 1 ) );
    float4 s31 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 1 ) );
    float4 s02 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 2 ) );
    float4 s12 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 2 ) );
    float4 s22 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 2 ) );
    float4 s32 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 2 ) );
    float4 s13 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 3 ) );
    float4 s23 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 3 ) );

    float4 specOcclusion2x2 = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );
    float4 specWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, specOcclusion2x2 );
    float4 specHistorySurface = STL::Filtering::ApplyBilinearCustomWeights( s11, s21, s12, s22, specWeights );
    float4 specHistorySurfaceCatRom = STL::Filtering::ApplyCatmullRomFilterNoCorners( catmullRomFilterAtPrevPos, s10, s20, s01, s11, s21, s31, s02, s12, s22, s32, s13, s23 );
    specHistorySurface = MixLinearAndCatmullRom( specHistorySurface, specHistorySurfaceCatRom, occlusion0, occlusion1, occlusion2, occlusion3 );

    // Accumulation speeds
    float diffMaxAccumSpeed = GetAccumSpeed( diffPrevAccumSpeeds, diffWeights, gDiffMaxAccumulatedFrameNum );
    float specMaxAccumSpeed = GetAccumSpeed( specPrevAccumSpeeds, specWeights, gSpecMaxAccumulatedFrameNum );

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

    bool specHasData = gSpecCheckerboard == 2 || checkerboard == gSpecCheckerboard;
    if( !specHasData )
    {
        float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specMaxAccumSpeed, parallax, roughnessModified );
        float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;
        float4 specHistorySurfaceClamped = STL::Color::Clamp( specM1, specSigma * temporalAccumulationParams.y, specHistorySurface ); // TODO: needed?

        spec.xyz = lerp( specHistorySurfaceClamped.xyz, spec.xyz, historyWeight );
        spec.w = lerp( specHistorySurfaceClamped.w, spec.w, max( historyWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );
    }

    // Diffuse accumulation
    float diffAccumSpeed = GetSpecAccumSpeed( diffMaxAccumSpeed, 1.0, 0.0, 0.0 );
    float diffAccumSpeedNonLinear = 1.0 / ( diffAccumSpeed + 1.0 );

    float4 diffResult;
    diffResult.xyz = lerp( diffHistory.xyz, diff.xyz, diffAccumSpeedNonLinear );
    diffResult.w = lerp( diffHistory.w, diff.w, max( diffAccumSpeedNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );

    // Output
    gOut_Diff[ pixelPos ] = diffResult;

    // Fast history
    #if( REBLUR_USE_FAST_HISTORY == 1 )
        float4 d11f = gIn_HistoryFast_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 1 ) );
        float4 d21f = gIn_HistoryFast_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 1 ) );
        float4 d12f = gIn_HistoryFast_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 2 ) );
        float4 d22f = gIn_HistoryFast_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 2 ) );
        float4 diffHistoryFast = STL::Filtering::ApplyBilinearCustomWeights( d11f, d21f, d12f, d22f, diffWeights );

        // History fix (previous state)
        float diffMinAccumSpeedFast = REBLUR_MIP_NUM - 1;
        diffHistoryFast = lerp( diffHistory, diffHistoryFast, diffAccumSpeed > diffMinAccumSpeedFast );

        float diffAccumSpeedNonLinearFast = 1.0 / ( min( diffAccumSpeed, gDiffMaxFastAccumulatedFrameNum ) + 1.0 );

        float4 diffResultFast;
        diffResultFast.xyz = lerp( diffHistoryFast.xyz, diff.xyz, diffAccumSpeedNonLinearFast );
        diffResultFast.w = lerp( diffHistoryFast.w, diff.w, max( diffAccumSpeedNonLinearFast, 2.0 * REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );

        gOut_Fast_Diff[ pixelPos ] = diffResultFast;
    #endif

    // Current specular signal ( surface motion )
    float NoV = abs( dot( N, V ) );
    float accumSpeedSurface = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, parallax );
    float accumSpeedSurfaceNonLinear = 1.0 / ( accumSpeedSurface + 1.0 );

    float hitDistNorm = lerp( specHistorySurface.w, spec.w, max( accumSpeedSurfaceNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );
    float hitDist = GetHitDist( hitDistNorm, viewZ, gSpecHitDistParams, roughness );
    float parallaxOrig = parallax;
    float hitDistToSurfaceRatio = saturate( hitDist * invDistToPoint );
    parallax *= hitDistToSurfaceRatio;

    accumSpeedSurface = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, parallax ); // TODO: add on option to use color clmaping if parallax is high (instead of accelerating the speed of accumulation)
    accumSpeedSurfaceNonLinear = 1.0 / ( accumSpeedSurface + 1.0 );

    float4 currentSurface;
    currentSurface.xyz = lerp( specHistorySurface.xyz, spec.xyz, accumSpeedSurfaceNonLinear );
    currentSurface.w = hitDistNorm;

    // Sample specular history ( virtual motion )
    float3 Xvirtual = GetXvirtual( X, Xprev, V, NoV, roughness, hitDist );
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );

    float4 specHistoryVirtual = gIn_History_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev * gRectSizePrev * gInvScreenSize, 0 );

    STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gRectSizePrev );
    float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackGreenVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, gatherUvVirtualPrev ).wzxy;

    float4 prevNormalAndRoughnessVirtual00 = UnpackNormalRoughness( prevPackGreenVirtual.x );
    float4 prevNormalAndRoughnessVirtual10 = UnpackNormalRoughness( prevPackGreenVirtual.y );
    float4 prevNormalAndRoughnessVirtual01 = UnpackNormalRoughness( prevPackGreenVirtual.z );
    float4 prevNormalAndRoughnessVirtual11 = UnpackNormalRoughness( prevPackGreenVirtual.w );

    // TODO: for IQ it's better to do all computation for each sample and then average with custom weights
    float4 prevNormalAndRoughnessVirtual = STL::Filtering::ApplyBilinearFilter( prevNormalAndRoughnessVirtual00, prevNormalAndRoughnessVirtual10, prevNormalAndRoughnessVirtual01, prevNormalAndRoughnessVirtual11, bilinearFilterAtPrevVirtualPos );
    prevNormalAndRoughnessVirtual.xyz = normalize( prevNormalAndRoughnessVirtual.xyz );

    // Virtual history confidence - out of screen
    float isInScreenVirtual = IsInScreen2x2( pixelUvVirtualPrev, gRectSizePrev );
    float virtualHistoryConfidence = isInScreenVirtual;

    // Virtual history confidence - normal
    float specNormalParams = GetNormalWeightParams( viewZ, roughnessModified, 0.0, 1.0 );

    float4 normalWeights;
    normalWeights.x = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual00.xyz );
    normalWeights.y = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual10.xyz );
    normalWeights.z = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual01.xyz );
    normalWeights.w = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual11.xyz );
    normalWeights.xy = min( normalWeights.xy, normalWeights.zw );
    float virtualNormalWeight = min( normalWeights.x, normalWeights.y );

    float fresnelFactor = STL::BRDF::Pow5( NoV );
    virtualHistoryConfidence *= lerp( virtualNormalWeight, 1.0, saturate( fresnelFactor * parallax ) );

    // Amount of virtual motion - dominant factor
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection( N, V, roughness, REBLUR_SPEC_DOMINANT_DIRECTION );
    float virtualHistoryAmount = virtualHistoryConfidence * D.w;
    virtualHistoryAmount *= 1.0 - gReference; // no virtual motion in the reference mode (it's by design, useful for integration debugging)

    // Amount of virtual motion - virtual motion correctness
    float3 R = reflect( -D.xyz, N );
    Xvirtual = X - R * hitDist * D.w;
    float2 uvVirtualExpected = STL::Geometry::GetScreenUv( gWorldToClip, Xvirtual );

    float4 Dvirtual = STL::ImportanceSampling::GetSpecularDominantDirection( prevNormalAndRoughnessVirtual.xyz, V, prevNormalAndRoughnessVirtual.w, REBLUR_SPEC_DOMINANT_DIRECTION );
    float3 Rvirtual = reflect( -Dvirtual.xyz, prevNormalAndRoughnessVirtual.xyz );
    float hitDistVirtual = GetHitDist( specHistoryVirtual.w, viewZ, gSpecHitDistParams, prevNormalAndRoughnessVirtual.w );
    Xvirtual = X - Rvirtual * hitDistVirtual * Dvirtual.w;
    float2 uvVirtualAtSample = STL::Geometry::GetScreenUv( gWorldToClip, Xvirtual );

    float thresholdMax = GetParallaxInPixels( parallaxOrig );
    float thresholdMin = thresholdMax * 0.05;
    float parallaxVirtual = length( ( uvVirtualAtSample - uvVirtualExpected ) * gRectSize );
    virtualHistoryAmount *= STL::Math::LinearStep( thresholdMax + 0.00001, thresholdMin, parallaxVirtual );

    // Virtual history confidence - roughness
    float virtualRoughnessWeight = GetRoughnessWeight( roughnessParams, prevNormalAndRoughnessVirtual.w );
    virtualHistoryConfidence *= virtualRoughnessWeight;

    // Virtual history confidence - hit distance
    float hitDistDelta = abs( hitDistVirtual - hitDist ); // no sigma substraction here - it's too noisy
    float hitDistMax = max( hitDistVirtual, hitDist );
    hitDistDelta *= STL::Math::PositiveRcp( hitDistMax + viewZ );

    thresholdMin = 0.02 * STL::Math::LinearStep( 0.2, 0.01, parallax ); // TODO: thresholdMin needs to be set to 0, but it requires very clean hit distances
    thresholdMax = lerp( 0.01, 0.25, roughness * roughness ) + thresholdMin;
    virtualHistoryConfidence *= STL::Math::LinearStep( thresholdMax, thresholdMin, hitDistDelta );

    // Clamp virtual history
    float sigmaScale = 3.0 + REBLUR_TS_SIGMA_AMPLITUDE * STL::Math::SmoothStep( 0.0, 0.5, roughness );
    float4 specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual );

    float virtualForcedConfidence = lerp( 0.75, 0.95, STL::Math::LinearStep( 0.04, 0.25, roughness ) );
    float virtualUnclampedAmount = lerp( virtualHistoryConfidence * virtualForcedConfidence, 1.0, roughness * roughness );
    specHistoryVirtual = lerp( specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount );

    // Current specular signal ( virtual motion )
    float accumSpeedVirtual = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, 0.0 ); // parallax = 0 cancels NoV too

    float minAccumSpeed = min( accumSpeedVirtual, ( REBLUR_MIP_NUM * 1.5 - 1.0 ) * STL::Math::Sqrt01( roughnessModified ) );
    accumSpeedVirtual = InterpolateAccumSpeeds( minAccumSpeed, accumSpeedVirtual, STL::Math::Sqrt01( virtualHistoryConfidence ) );

    float accumSpeedVirtualNonLinear = 1.0 / ( accumSpeedVirtual + 1.0 );

    float4 currentVirtual;
    currentVirtual.xyz = lerp( specHistoryVirtual.xyz, spec.xyz, accumSpeedVirtualNonLinear );
    currentVirtual.w = lerp( specHistoryVirtual.w, spec.w, max( accumSpeedVirtualNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

    // Final composition
    float4 specResult;
    specResult.xyz = lerp( currentSurface.xyz, currentVirtual.xyz, virtualHistoryAmount );
    specResult.w = lerp( currentSurface.w, currentVirtual.w, virtualHistoryAmount * hitDistToSurfaceRatio );

    float specAccumSpeed = InterpolateAccumSpeeds( accumSpeedSurface, accumSpeedVirtual, virtualHistoryAmount );

    // Output
    gOut_InternalData[ pixelPos ] = PackDiffSpecInternalData( diffAccumSpeed, specAccumSpeed, edge, virtualHistoryAmount );
    gOut_Spec[ pixelPos ] = specResult;

    // Fast history
    #if( REBLUR_USE_FAST_HISTORY == 1 )
        float4 s11f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 1 ) );
        float4 s21f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 1 ) );
        float4 s12f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 2 ) );
        float4 s22f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 2 ) );
        float4 specHistorySurfaceFast = STL::Filtering::ApplyBilinearCustomWeights( s11f, s21f, s12f, s22f, specWeights );

        float4 specHistoryVirtualFast = gIn_HistoryFast_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev * gRectSizePrev * gInvScreenSize, 0 );
        float4 specHistoryVirtualClampedFast = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtualFast );
        specHistoryVirtualFast = lerp( specHistoryVirtualClampedFast, specHistoryVirtualFast, virtualUnclampedAmount );

        // History fix (previous state)
        float specMinAccumSpeedFast = ( REBLUR_MIP_NUM - 1 ) * STL::Math::Sqrt01( roughness );
        specHistorySurfaceFast = lerp( specHistorySurface, specHistorySurfaceFast, specAccumSpeed > specMinAccumSpeedFast );
        specHistoryVirtualFast = lerp( specHistoryVirtual, specHistoryVirtualFast, specAccumSpeed > specMinAccumSpeedFast );

        float accumSpeedSurfaceNonLinearFast = 1.0 / ( min( accumSpeedSurface, gSpecMaxFastAccumulatedFrameNum ) + 1.0 );

        float4 currentSurfaceFast;
        currentSurfaceFast.xyz = lerp( specHistorySurfaceFast.xyz, spec.xyz, accumSpeedSurfaceNonLinearFast );
        currentSurfaceFast.w = lerp( specHistorySurfaceFast.w, spec.w, max( accumSpeedSurfaceNonLinearFast, 2.0 * REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

        float accumSpeedVirtualNonLinearFast = 1.0 / ( min( accumSpeedVirtual, gSpecMaxFastAccumulatedFrameNum ) + 1.0 );

        float4 currentVirtualFast;
        currentVirtualFast.xyz = lerp( specHistoryVirtualFast.xyz, spec.xyz, accumSpeedVirtualNonLinearFast );
        currentVirtualFast.w = lerp( specHistoryVirtualFast.w, spec.w, max( accumSpeedVirtualNonLinearFast, 2.0 * REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

        float4 specResultFast = lerp( currentSurfaceFast, currentVirtualFast, virtualHistoryAmount );

        gOut_Fast_Spec[ pixelPos ] = specResultFast;
    #endif
}
