/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsl"
#include "STL.hlsl"
#include "REBLUR_DiffuseSpecular_TemporalAccumulation.resources.hlsl"

NRD_DECLARE_CONSTANTS

#define NRD_CTA_8X8
#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS

#include "REBLUR_Common.hlsl"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    uint2 globalIdUser = gRectOrigin + globalId;

    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
    s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Spec[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
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

            // TODO: using weights leads to instabilities on thin objects
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
    uint4 prevPackGreen0 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
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
    cosa.x = dot( N, prevNormal00 );
    cosa.y = dot( N, prevNormal10 );
    cosa.z = dot( N, prevNormal01 );
    cosa.w = dot( N, prevNormal11 );

    float4 frontFacing = STL::Math::LinearStep( disocclusionThresholds.y, 0.001, cosa );
    occlusion0.w *= frontFacing.x;
    occlusion1.z *= frontFacing.y;
    occlusion2.y *= frontFacing.z;
    occlusion3.x *= frontFacing.w;

    float4 occlusion2x2 = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );

    // For CatRom
    float4 occlusionSum = occlusion0 + occlusion1 + occlusion2 + occlusion3;
    float occlusionAvg = dot( occlusionSum, 1.0 / 16.0 );

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

    float4 diffWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, occlusion2x2 );
    float4 diffHistory = STL::Filtering::ApplyBilinearCustomWeights( d11, d21, d12, d22, diffWeights );
    float4 diffHistoryCatRom = STL::Filtering::ApplyCatmullRomFilterNoCorners( catmullRomFilterAtPrevPos, d10, d20, d01, d11, d21, d31, d02, d12, d22, d32, d13, d23 );
    diffHistory = MixLinearAndCatmullRom( diffHistory, diffHistoryCatRom, occlusionAvg );

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

    float4 specWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, occlusion2x2 );
    float4 specHistorySurface = STL::Filtering::ApplyBilinearCustomWeights( s11, s21, s12, s22, specWeights );
    float4 specHistorySurfaceCatRom = STL::Filtering::ApplyCatmullRomFilterNoCorners( catmullRomFilterAtPrevPos, s10, s20, s01, s11, s21, s31, s02, s12, s22, s32, s13, s23 );
    specHistorySurface = MixLinearAndCatmullRom( specHistorySurface, specHistorySurfaceCatRom, occlusionAvg );

    // Accumulation speeds
    float diffMaxAccumSpeed = GetAccumSpeed( diffPrevAccumSpeeds, diffWeights, gDiffMaxAccumulatedFrameNum );
    float specMaxAccumSpeed = GetAccumSpeed( specPrevAccumSpeeds, specWeights, gSpecMaxAccumulatedFrameNum );

    // Noisy signal with reconstruction (if needed)
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    float4 diff = gIn_Diff[ pixelPos ];
    bool diffHasData = gDiffCheckerboard == 2 || checkerboard == gDiffCheckerboard;
    if( !diffHasData && gFrameIndex != 0 )
    {
        float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffMaxAccumSpeed, parallax );
        float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

        diff.xyz = lerp( diffHistory.xyz, diff.xyz, historyWeight );
        diff.w = lerp( diffHistory.w, diff.w, max( historyWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );
    }

    bool specHasData = gSpecCheckerboard == 2 || checkerboard == gSpecCheckerboard;
    if( !specHasData && gFrameIndex != 0 )
    {
        float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specMaxAccumSpeed, parallax, roughnessModified );
        float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;
        float4 specHistorySurfaceClamped = STL::Color::Clamp( specM1, specSigma * temporalAccumulationParams.y, specHistorySurface, REBLUR_USE_COLOR_CLAMPING_AABB ); // TODO: needed?

        spec.xyz = lerp( specHistorySurfaceClamped.xyz, spec.xyz, historyWeight );
        spec.w = lerp( specHistorySurfaceClamped.w, spec.w, max( historyWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );
    }

    // Diffuse accumulation
    float diffAccumSpeed = GetSpecAccumSpeed( diffMaxAccumSpeed, 1.0, 0.0, 0.0 );
    float diffAccumSpeedNonLinear = 1.0 / ( diffAccumSpeed + 1.0 );

    float4 diffResult;
    diffResult.xyz = lerp( diffHistory.xyz, diff.xyz, diffAccumSpeedNonLinear );
    diffResult.w = lerp( diffHistory.w, diff.w, max( diffAccumSpeedNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );
    diffResult = Sanitize( diffResult, diff );

    float diffError = GetColorErrorForAdaptiveRadiusScale( diffResult, diffHistory, diffAccumSpeedNonLinear, 1.0 );

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
        float diffMinAccumSpeedFast = GetMipLevel( 1.0, gDiffMaxFastAccumulatedFrameNum );
        diffHistoryFast = lerp( diffHistory, diffHistoryFast, diffAccumSpeed > diffMinAccumSpeedFast );

        float diffAccumSpeedNonLinearFast = 1.0 / ( min( diffAccumSpeed, gDiffMaxFastAccumulatedFrameNum ) + 1.0 );

        float4 diffResultFast;
        diffResultFast.xyz = lerp( diffHistoryFast.xyz, diff.xyz, diffAccumSpeedNonLinearFast );
        diffResultFast.w = lerp( diffHistoryFast.w, diff.w, max( diffAccumSpeedNonLinearFast, 2.0 * REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );
        diffResultFast = Sanitize( diffResultFast, diff );

        gOut_Fast_Diff[ pixelPos ] = diffResultFast;
    #endif

    // Current specular signal ( surface motion )
    float NoV = abs( dot( N, V ) );
    float accumSpeedSurface = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, parallax );
    float accumSpeedSurfaceNonLinear = 1.0 / ( accumSpeedSurface + 1.0 );

    float hitDistNorm = lerp( specHistorySurface.w, spec.w, max( accumSpeedSurfaceNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

    #if( REBLUR_USE_CURRENT_HIT_DIST_FOR_VIRTUAL_MOTION == 1 )
        // TODO: return at least bilateral weight in specM1 calculation and use specM1.w for glossy+ roughness?
        float hitDist = GetHitDist( spec.w, viewZ, gSpecHitDistParams, roughness );
    #else
        float hitDist = GetHitDist( hitDistNorm, viewZ, gSpecHitDistParams, roughness );
    #endif

    float parallaxOrig = parallax;
    float hitDistToSurfaceRatio = saturate( hitDist * invDistToPoint );
    parallax *= hitDistToSurfaceRatio;

    accumSpeedSurface = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, parallax ); // TODO: add on option to use color clamping if parallax is high (instead of accelerating the speed of accumulation)

    // Sample specular history ( virtual motion )
    float3 Xvirtual = GetXvirtual( X, Xprev, V, NoV, roughness, hitDist );
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );
    float4 specHistoryVirtual = gIn_History_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev * gRectSizePrev * gInvScreenSize, 0 );

    STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gRectSizePrev );
    float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackRedVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, gatherUvVirtualPrev ).wzxy;
    uint4 prevPackGreenVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, gatherUvVirtualPrev ).wzxy;

    // Amount of virtual motion - out of screen & reference
    float virtualHistoryAmount = IsInScreen2x2( pixelUvVirtualPrev, gRectSizePrev );
    virtualHistoryAmount *= 1.0 - gReference; // no virtual motion in the reference mode (it's by design, useful for integration debugging)

    // Amount of virtual motion - surface
    float4 prevViewZsVirtual = UnpackViewZ( prevPackRedVirtual );
    float prevViewZVirtual = STL::Filtering::ApplyBilinearFilter( prevViewZsVirtual.x, prevViewZsVirtual.y, prevViewZsVirtual.z, prevViewZsVirtual.w, bilinearFilterAtPrevVirtualPos );

    float virtualZocclusion = abs( prevViewZVirtual - Xvprev.z ) / ( max( prevViewZVirtual, Xvprev.z ) + 0.001 );
    float zThresholdMin = 0.01 / max( NoV, 0.03 );
    virtualHistoryAmount *= STL::Math::LinearStep( zThresholdMin * 3.0, zThresholdMin, virtualZocclusion );

    // Amount of virtual motion - normal
    float fresnelFactor = STL::BRDF::Pow5( NoV );
    float virtualLobeScale = lerp( 0.5, 1.0, fresnelFactor );
    float specNormalParams = GetNormalWeightParams( virtualLobeScale, 0.0, 1.0, roughnessModified );

    float4 prevNormalAndRoughnessVirtual00 = UnpackNormalRoughness( prevPackGreenVirtual.x );
    float4 prevNormalAndRoughnessVirtual10 = UnpackNormalRoughness( prevPackGreenVirtual.y );
    float4 prevNormalAndRoughnessVirtual01 = UnpackNormalRoughness( prevPackGreenVirtual.z );
    float4 prevNormalAndRoughnessVirtual11 = UnpackNormalRoughness( prevPackGreenVirtual.w );

    float4 normalWeights;
    normalWeights.x = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual00.xyz );
    normalWeights.y = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual10.xyz );
    normalWeights.z = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual01.xyz );
    normalWeights.w = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual11.xyz );

    float4 prevNormalAndRoughnessVirtual = STL::Filtering::ApplyBilinearFilter( prevNormalAndRoughnessVirtual00, prevNormalAndRoughnessVirtual10, prevNormalAndRoughnessVirtual01, prevNormalAndRoughnessVirtual11, bilinearFilterAtPrevVirtualPos );
    prevNormalAndRoughnessVirtual.xyz = normalize( prevNormalAndRoughnessVirtual.xyz );

    float2 tempMax = max( normalWeights.xy, normalWeights.zw );
    float2 tempMin = min( normalWeights.xy, normalWeights.zw );
    float normalWeightMax = max( tempMax.x, tempMax.y );
    float normalWeightMin = min( tempMin.x, tempMin.y );
    float virtualNormalWeight = lerp( normalWeightMin, normalWeightMax, fresnelFactor * ( 1.0 - edge ) );

    float renorm = lerp( 0.9, 1.0, STL::Math::LinearStep( 0.0, 0.17, roughnessModified ) );
    virtualNormalWeight = saturate( virtualNormalWeight / renorm ); // mitigate imprecision problems introduced by normals encoded with different precision
    virtualHistoryAmount *= virtualNormalWeight;

    // Amount of virtual motion - dominant factor
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection( N, V, roughness, REBLUR_SPEC_DOMINANT_DIRECTION );
    virtualHistoryAmount *= D.w;

    // Amount of virtual motion - virtual motion correctness
    float3 R = reflect( -D.xyz, N );
    Xvirtual = X - R * hitDist * D.w;
    float2 uvVirtualExpected = STL::Geometry::GetScreenUv( gWorldToClip, Xvirtual );

    float4 Dvirtual = STL::ImportanceSampling::GetSpecularDominantDirection( prevNormalAndRoughnessVirtual.xyz, V, prevNormalAndRoughnessVirtual.w, REBLUR_SPEC_DOMINANT_DIRECTION );
    float3 Rvirtual = reflect( -Dvirtual.xyz, prevNormalAndRoughnessVirtual.xyz );
    float hitDistVirtual = GetHitDist( specHistoryVirtual.w, prevViewZVirtual, gSpecHitDistParams, prevNormalAndRoughnessVirtual.w );
    Xvirtual = X - Rvirtual * hitDistVirtual * Dvirtual.w;
    float2 uvVirtualAtSample = STL::Geometry::GetScreenUv( gWorldToClip, Xvirtual );

    float parallaxInPixels = GetParallaxInPixels( parallaxOrig );
    float parallaxVirtual = length( ( uvVirtualAtSample - uvVirtualExpected ) * gRectSize );
    virtualHistoryAmount *= STL::Math::LinearStep( parallaxInPixels * 1.1 + 0.01, parallaxInPixels * 0.5, parallaxVirtual );

    // Virtual history confidence - normal
    float virtualHistoryConfidence = normalWeightMin;

    // Virtual history confidence - roughness
    float2 roughnessParams = GetRoughnessWeightParams( roughness );
    float prevRoughnessVirtualCorrected = lerp( roughness, prevNormalAndRoughnessVirtual.w, saturate( parallaxInPixels ) ); // TODO: just roughness can't be used due to jittering. Compute average?
    float roughnessWeight = GetRoughnessWeight( roughnessParams, prevRoughnessVirtualCorrected );
    virtualHistoryConfidence *= roughnessWeight;

    // Virtual history confidence - hit distance
    // TODO: since hit distances are normalized many values can be represented as 1. It may break this test...
    #if( REBLUR_USE_CURRENT_HIT_DIST_FOR_VIRTUAL_MOTION == 1 )
        hitDist = GetHitDist( hitDistNorm, viewZ, gSpecHitDistParams, roughness );
    #endif

    float hitDistDelta = abs( hitDistVirtual - hitDist ); // no sigma substraction
    float hitDistMax = max( hitDistVirtual, hitDist );
    hitDistDelta *= STL::Math::PositiveRcp( hitDistMax + viewZ );

    float thresholdMin = lerp( 0.005, 0.03, STL::Math::Sqrt01( roughnessModified ) );
    float thresholdMax = 0.25 * roughnessModified * roughnessModified;
    float virtualHistoryHitDistConfidence = STL::Math::LinearStep( thresholdMin + thresholdMax, thresholdMin, hitDistDelta * parallaxOrig );
    float virtualHistoryHitDistConfidenceNoParallax = STL::Math::LinearStep( 0.005 + thresholdMax, 0.005, hitDistDelta );

    // Clamp virtual history
    float sigmaScale = lerp( lerp( 1.0, 3.0, roughnessModified ), 3.0, virtualHistoryConfidence ) + REBLUR_TS_SIGMA_AMPLITUDE * GetSpecMagicCurve( roughnessModified );
    float4 specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual, REBLUR_USE_COLOR_CLAMPING_AABB );

    float virtualUnclampedAmount = lerp( virtualHistoryConfidence * virtualHistoryHitDistConfidenceNoParallax, 1.0, roughness * roughness );
    specHistoryVirtual = lerp( specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount );

    // Final composition
    float4 specHistory = InterpolateSurfaceAndVirtualMotion( specHistorySurface, specHistoryVirtual, virtualHistoryAmount, hitDistToSurfaceRatio );

    float specAccumSpeed = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, parallax * ( 1.0 - virtualHistoryAmount ) );

    float specMinAccumSpeed = min( specAccumSpeed, GetMipLevel( roughnessModified, gSpecMaxFastAccumulatedFrameNum ) );
    float specAccumSpeedScale = lerp( 1.0, virtualHistoryHitDistConfidence, virtualHistoryAmount );
    specAccumSpeed = InterpolateAccumSpeeds( specMinAccumSpeed, specAccumSpeed, specAccumSpeedScale );

    float accumSpeedNonLinear = 1.0 / ( specAccumSpeed + 1.0 );

    float4 specResult;
    specResult.xyz = lerp( specHistory.xyz, spec.xyz, accumSpeedNonLinear );
    specResult.w = lerp( specHistory.w, spec.w, max( accumSpeedNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

    specResult = Sanitize( specResult, spec );

    // Output
    #if( REBLUR_DEBUG == REBLUR_SHOW_VIRTUAL_HISTORY_CONFIDENCE )
        virtualHistoryAmount = virtualHistoryConfidence;
    #endif

    gOut_InternalData[ pixelPos ] = PackDiffSpecInternalData( diffAccumSpeed, specAccumSpeed, edge, virtualHistoryAmount );
    gOut_Spec[ pixelPos ] = specResult;

    // Error
    float bestAccumulatedFrameNum = GetSpecAccumulatedFrameNum( roughnessModified, 1.0 );
    float boost = saturate( 1.0 - ( specAccumSpeed + 0.1 ) / ( min( bestAccumulatedFrameNum, gSpecMaxAccumulatedFrameNum ) + 0.1 ) );
    boost *= saturate( parallaxOrig / 0.25 );

    float specError = GetColorErrorForAdaptiveRadiusScale( specResult, specHistory, 1.0 / ( 1.0 + specAccumSpeed ), lerp( 1.0, roughness, saturate( parallax ) ) );

    gOut_Error[ pixelPos ] = float4( diffError, specError, boost, virtualHistoryConfidence );

    // Fast history
    #if( REBLUR_USE_FAST_HISTORY == 1 )
        float4 s11f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 1 ) );
        float4 s21f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 1 ) );
        float4 s12f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 2 ) );
        float4 s22f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 2 ) );
        float4 specHistorySurfaceFast = STL::Filtering::ApplyBilinearCustomWeights( s11f, s21f, s12f, s22f, specWeights );

        float4 specHistoryVirtualFast = gIn_HistoryFast_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev * gRectSizePrev * gInvScreenSize, 0 );
        float4 specHistoryVirtualClampedFast = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtualFast, REBLUR_USE_COLOR_CLAMPING_AABB );
        specHistoryVirtualFast = lerp( specHistoryVirtualClampedFast, specHistoryVirtualFast, virtualUnclampedAmount );

        float4 specHistoryFast = InterpolateSurfaceAndVirtualMotion( specHistorySurfaceFast, specHistoryVirtualFast, virtualHistoryAmount, hitDistToSurfaceRatio );

        float specMinAccumSpeedFast = GetMipLevel( roughnessModified, gSpecMaxFastAccumulatedFrameNum );
        specHistoryFast = lerp( specHistory, specHistoryFast, specAccumSpeed > specMinAccumSpeedFast );

        float maxFastAccumSpeedRoughnessAdjusted = gSpecMaxFastAccumulatedFrameNum * GetSpecMagicCurve( roughnessModified );
        float accumSpeedNonLinearFast = 1.0 / ( min( specAccumSpeed, maxFastAccumSpeedRoughnessAdjusted ) + 1.0 );

        float4 specResultFast;
        specResultFast.xyz = lerp( specHistoryFast.xyz, spec.xyz, accumSpeedNonLinearFast );
        specResultFast.w = lerp( specHistoryFast.w, spec.w, max( accumSpeedNonLinearFast, 2.0 * REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

        specResultFast = Sanitize( specResultFast, spec );

        gOut_Fast_Spec[ pixelPos ] = specResultFast;
    #endif
}
