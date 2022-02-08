/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"
#include "REBLUR_DiffuseSpecular_TemporalAccumulation.resources.hlsli"

NRD_DECLARE_CONSTANTS

#if( defined REBLUR_SPECULAR )
    #define NRD_CTA_8X8
    #define NRD_USE_BORDER_2
#endif

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS

#include "REBLUR_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );
    uint2 globalIdUser = gRectOrigin + globalPos;

    s_Normal_Roughness[ sharedPos.y ][ sharedPos.x ] = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );

    #if( defined REBLUR_SPECULAR )
        s_Spec[ sharedPos.y ][ sharedPos.x ] = gIn_Spec[ gPreblurEnabled ? globalPos : globalIdUser ];
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );
    float scaledViewZ = min( viewZ * NRD_FP16_VIEWZ_SCALE, NRD_FP16_MAX );

    gOut_ScaledViewZ[ pixelPos ] = scaledViewZ;

    [branch]
    if( viewZ > gInf )
        return;

    // Normal and roughness
    int2 smemPos = threadPos + BORDER;
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Current position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );

    // Calculate distribution of normals
    #if( defined REBLUR_SPECULAR )
        float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
        float4 specM1 = spec;
        float4 specM2 = spec * spec;
        float minHitDist3x3 = spec.w;
        float minHitDist5x5 = spec.w;
    #else
        float minHitDist3x3 = 0;
        float minHitDist5x5 = 0;
    #endif

    float3 Nflat = N;
    float curvature = 0;
    float curvatureSum = 0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadPos + int2( dx, dy );

            #if( defined REBLUR_SPECULAR )
                // TODO: using weights leads to instabilities on thin objects
                float4 s = s_Spec[ pos.y ][ pos.x ];
                specM1 += s;
                specM2 += s * s;
            #else
                float4 s = 0;
            #endif

            float2 d = float2( dx, dy ) - BORDER;
            if( all( abs( d ) <= 1 ) ) // only in 3x3
            {
                float3 n = s_Normal_Roughness[ pos.y ][ pos.x ].xyz;
                Nflat += n;

                float3 pv = STL::Geometry::ReconstructViewPosition( pixelUv + d * gInvRectSize, gFrustum, 1.0, gIsOrtho );
                float3 v = STL::Geometry::RotateVector( gViewToWorld, normalize( -pv ) );
                float c = EstimateCurvature( n, v, N, X );

                float w = exp2( -0.5 * STL::Math::LengthSquared( d ) );
                curvature += c * w;
                curvatureSum += w;

                minHitDist3x3 = min( s.w, minHitDist3x3 );
            }
            else
                minHitDist5x5 = min( s.w, minHitDist5x5 );
        }
    }

    float3 Navg = Nflat / 9.0;
    float flatness = STL::Math::SmoothStep( 0.985, 1.0, length( Navg ) );

    Nflat = normalize( Nflat );

    #if( defined REBLUR_SPECULAR )
        float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );
        specM1 *= invSum;
        specM2 *= invSum;
        float4 specSigma = GetStdDev( specM1, specM2 );

        minHitDist5x5 = min( minHitDist5x5, minHitDist3x3 );
        curvature /= curvatureSum;

        // Mitigate imprecision problems
        curvature *= STL::Math::LinearStep( 0.0, NRD_ENCODING_ERRORS.y + 1e-5, abs( curvature ) );

        float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );
    #endif

    // Previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser ] * gMotionVectorScale.xyy; // TODO: use nearest MV
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = IsInScreen2x2( pixelUvPrev, gRectSizePrev );
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );

    // Previous data ( 4x4, surface motion )
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

    // TODO: 4x4 normal checks are reduced to 2x2 footprint only ( missed samples are covered by an additional 3x3 edge check )
    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float2 bilinearFilterAtPrevPosGatherOrigin = ( bilinearFilterAtPrevPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackGreen = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, bilinearFilterAtPrevPosGatherOrigin ).wzxy;

    float4 specPrevAccumSpeeds;
    float3 prevNormal00 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen.x, specPrevAccumSpeeds.x ).xyz;
    float3 prevNormal10 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen.y, specPrevAccumSpeeds.y ).xyz;
    float3 prevNormal01 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen.z, specPrevAccumSpeeds.z ).xyz;
    float3 prevNormal11 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen.w, specPrevAccumSpeeds.w ).xyz;

    #if( defined REBLUR_DIFFUSE )
        float4 diffPrevAccumSpeeds = UnpackDiffAccumSpeed( uint4( prevPackRed0.w, prevPackRed1.z, prevPackRed2.y, prevPackRed3.x ) );
    #endif

    // Plane distance based disocclusion for surface motion
    float3 V = GetViewVector( X );
    float invFrustumHeight = STL::Math::PositiveRcp( PixelRadiusToWorld( gUnproject, gIsOrtho, gRectSize.y, viewZ ) );
    float3 prevNflatUnnormalized = prevNormal00 + prevNormal10 + prevNormal01 + prevNormal11;
    float disocclusionThreshold = GetDisocclusionThreshold( gDisocclusionThreshold, gJitterDelta, viewZ, Nflat, V );
    disocclusionThreshold = lerp( -1.0, disocclusionThreshold, isInScreen ); // out-of-screen = occlusion
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev1 = abs( dot( Xprev, Nflat ) );
    float NoXprev2 = abs( dot( Xprev, prevNflatUnnormalized ) ) * STL::Math::Rsqrt( STL::Math::LengthSquared( prevNflatUnnormalized ) );
    float NoXprev = max( NoXprev1, NoXprev2 ) * invFrustumHeight; // normalize here to save ALU
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) );
    float4 planeDist0 = abs( NoVprev * abs( prevViewZ0 ) - NoXprev );
    float4 planeDist1 = abs( NoVprev * abs( prevViewZ1 ) - NoXprev );
    float4 planeDist2 = abs( NoVprev * abs( prevViewZ2 ) - NoXprev );
    float4 planeDist3 = abs( NoVprev * abs( prevViewZ3 ) - NoXprev );
    float4 occlusion0 = step( planeDist0, disocclusionThreshold );
    float4 occlusion1 = step( planeDist1, disocclusionThreshold );
    float4 occlusion2 = step( planeDist2, disocclusionThreshold );
    float4 occlusion3 = step( planeDist3, disocclusionThreshold );

    // Avoid "got stuck in history" effect under slow motion when only 1 sample is valid from 2x2 footprint and there is a big difference between
    // foreground and background surfaces. Instead of final scalar accum speed scaling we can apply it to accum speeds from the previous frame
    float4 planeDist2x2 = float4( planeDist0.w, planeDist1.z, planeDist2.y, planeDist3.x );
    planeDist2x2 = STL::Math::LinearStep( 0.2, disocclusionThreshold, planeDist2x2 );

    float footprintAvg = STL::Filtering::ApplyBilinearFilter( planeDist2x2.x, planeDist2x2.y, planeDist2x2.z, planeDist2x2.w, bilinearFilterAtPrevPos );
    float fmin = min( bilinearFilterAtPrevPos.weights.x, bilinearFilterAtPrevPos.weights.y ) + 0.01;
    float fmax = max( bilinearFilterAtPrevPos.weights.x, bilinearFilterAtPrevPos.weights.y ) + 0.01;
    footprintAvg = lerp( footprintAvg, 1.0, STL::Math::LinearStep( 0.05, 0.5, fmin / fmax ) );

    #if( defined REBLUR_DIFFUSE )
        diffPrevAccumSpeeds *= footprintAvg;
    #endif

    #if( defined REBLUR_SPECULAR )
        specPrevAccumSpeeds *= footprintAvg;
    #endif

    // Ignore backfacing history
    float4 cosa;
    cosa.x = dot( N, prevNormal00 );
    cosa.y = dot( N, prevNormal10 );
    cosa.z = dot( N, prevNormal01 );
    cosa.w = dot( N, prevNormal11 );

    float parallax = ComputeParallax( X, Xprev, gCameraDelta );
    float cosAngleMin = lerp( -0.95, -0.01, SaturateParallax( parallax ) );
    float4 frontFacing = STL::Math::LinearStep( cosAngleMin, 0.01, cosa );
    occlusion0.w *= frontFacing.x;
    occlusion1.z *= frontFacing.y;
    occlusion2.y *= frontFacing.z;
    occlusion3.x *= frontFacing.w;

    float surfaceOcclusionAvg = step( 15.5, dot( occlusion0 + occlusion1 + occlusion2 + occlusion3, 1.0 ) ) * REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA;

    // IMPORTANT: CatRom or custom bilinear work as expected when only one is in use. When mixed, a disocclusion event can introduce a switch to
    // bilinear, which can snap to a single sample according to custom weights. It can introduce a discontinuity in color. In turn CatRom can immediately
    // react to this and increase local sharpness. Next, if another disocclusion happens, custom bilinear can snap to the sharpened sample again...
    // This process can continue almost infinitely, blowing up the image due to over sharpenning in a loop. It can be fixed by:
    // - using camera jittering
    // - not using CatRom on edges
    // - computing 4x4 normal weights
    surfaceOcclusionAvg *= float( length( Navg ) > 0.65 ); // TODO: 0.85?

    float4 surfaceOcclusion2x2 = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );
    float4 surfaceWeightsWithOcclusion = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, surfaceOcclusion2x2 );

    float fbits = surfaceOcclusionAvg * 8.0;

    // Update accumulation speeds
    #if( defined REBLUR_DIFFUSE )
        float diffMaxAccumSpeed = AdvanceAccumSpeed( diffPrevAccumSpeeds, surfaceWeightsWithOcclusion );

        float diffHistoryConfidence = 1.0;
        #if( defined REBLUR_PROVIDED_CONFIDENCE )
            diffHistoryConfidence = gIn_DiffConfidence[ pixelPosUser ];
        #endif
    #endif

    #if( defined REBLUR_SPECULAR )
        float specMaxAccumSpeed = AdvanceAccumSpeed( specPrevAccumSpeeds, surfaceWeightsWithOcclusion );

        float specHistoryConfidence = 1.0;
        #if( defined REBLUR_PROVIDED_CONFIDENCE )
            specHistoryConfidence = gIn_SpecConfidence[ pixelPosUser ];
        #endif
    #endif

    // Sample history ( surface motion )
    #if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
        float4 specHistorySurface;
        float4 diffHistory = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Diff, gIn_History_Spec, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            surfaceWeightsWithOcclusion, surfaceOcclusionAvg == 1.0,
            specHistorySurface
        );
    #elif( defined REBLUR_DIFFUSE )
        float4 diffHistory = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Diff, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            surfaceWeightsWithOcclusion, surfaceOcclusionAvg == 1.0
        );
    #else
        float4 specHistorySurface = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Spec, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            surfaceWeightsWithOcclusion, surfaceOcclusionAvg == 1.0
        );
    #endif

    // Noisy signal with checkerboard reconstruction
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    #if( defined REBLUR_DIFFUSE )
        float4 diff = gIn_Diff[ pixelPos ];
        bool diffHasData = gDiffCheckerboard == 2 || checkerboard == gDiffCheckerboard;
        if( !diffHasData && gResetHistory == 0 )
        {
            float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffMaxAccumSpeed * diffHistoryConfidence );
            float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

            diff = MixHistoryAndCurrent( diffHistory, diff, historyWeight );
        }
    #endif

    #if( defined REBLUR_SPECULAR )
        bool specHasData = gSpecCheckerboard == 2 || checkerboard == gSpecCheckerboard;
        if( !specHasData && gResetHistory == 0 )
        {
            float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specMaxAccumSpeed * specHistoryConfidence, parallax, roughness );
            float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

            float4 specHistorySurfaceClamped = STL::Color::Clamp( specM1, specSigma * temporalAccumulationParams.y, specHistorySurface, REBLUR_USE_COLOR_CLAMPING_AABB ); // TODO: needed?
            specHistorySurfaceClamped = lerp( specHistorySurfaceClamped, specHistorySurface, roughnessModified * roughnessModified );

            spec = MixHistoryAndCurrent( specHistorySurfaceClamped, spec, historyWeight, roughnessModified );
        }
    #endif

    // Diffuse
    #if( defined REBLUR_DIFFUSE )
        // Accumulation
        float diffAccumSpeed = diffMaxAccumSpeed * diffHistoryConfidence;
        float diffAccumSpeedNonLinear = 1.0 / ( min( diffAccumSpeed, gDiffMaxAccumulatedFrameNum ) + 1.0 );

        float4 diffResult = MixHistoryAndCurrent( diffHistory, diff, diffAccumSpeedNonLinear );
        diffResult = Sanitize( diffResult, diff );

        gOut_Diff[ pixelPos ] = diffResult;

        float diffError = GetColorErrorForAdaptiveRadiusScale( diffResult, diffHistory, diffAccumSpeedNonLinear, 1.0, 0 );
        float diffAccumSpeedFinal = min( diffMaxAccumSpeed, max( diffAccumSpeed, GetMipLevel( 1.0 ) ) );
    #else
        float diffError = 0;
        float diffAccumSpeedFinal = 0;
    #endif

    // Specular
    #if( defined REBLUR_SPECULAR )
        // Current hit distance
        float hitDistCurrent = lerp( minHitDist3x3, minHitDist5x5, STL::Math::SmoothStep( 0.04, 0.08, roughnessModified ) );
        hitDistCurrent = STL::Color::Clamp( specM1.w, specSigma.w * 3.0, hitDistCurrent );

        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gSpecHitDistParams, roughness );
        hitDistCurrent *= hitDistScale;

        // Parallax correction
        float hitDistFactor = saturate( hitDistCurrent * invFrustumHeight );
        parallax *= hitDistFactor;

        // Virtual motion
        float NoV = abs( dot( N, V ) );
        float4 Xvirtual = GetXvirtual( X, V, NoV, roughnessModified, hitDistCurrent, viewZ, curvature );
        float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual.xyz, false );
        STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gRectSizePrev );

        float virtualHistoryAmount = IsInScreen2x2( pixelUvVirtualPrev, gRectSizePrev );
        virtualHistoryAmount *= flatness;
        virtualHistoryAmount *= 1.0 - gReference;
        virtualHistoryAmount *= STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughnessModified, REBLUR_SPEC_DOMINANT_DIRECTION );

        // This scaler improves motion stability for roughness 0.4+
        virtualHistoryAmount *= 1.0 - STL::Math::SmoothStep( 0.15, 0.85, roughness ) * ( 1.0 - hitDistFactor );

        // Virtual motion - surface similarity // TODO: make it closer to the main disocclusion test
        float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
        uint4 prevPackRedVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, gatherUvVirtualPrev ).wzxy;
        float4 prevViewZsVirtual = UnpackViewZ( prevPackRedVirtual );
        float3 Nvprev = STL::Geometry::RotateVector( gWorldToViewPrev, Nflat );
        float4 ka;
        ka.x = dot( Nvprev, STL::Geometry::ReconstructViewPosition( pixelUvVirtualPrev, gFrustumPrev, prevViewZsVirtual.x, gIsOrtho ) );
        ka.y = dot( Nvprev, STL::Geometry::ReconstructViewPosition( pixelUvVirtualPrev, gFrustumPrev, prevViewZsVirtual.y, gIsOrtho ) );
        ka.z = dot( Nvprev, STL::Geometry::ReconstructViewPosition( pixelUvVirtualPrev, gFrustumPrev, prevViewZsVirtual.z, gIsOrtho ) );
        ka.w = dot( Nvprev, STL::Geometry::ReconstructViewPosition( pixelUvVirtualPrev, gFrustumPrev, prevViewZsVirtual.w, gIsOrtho ) );
        float kb = dot( Nvprev, Xvprev );
        float4 f = abs( ka - kb ) * invFrustumHeight;
        float4 virtualMotionSurfaceWeights = STL::Math::LinearStep( 0.015, 0.005, f );
        float4 virtualMotionFootprintWeights = virtualMotionSurfaceWeights;

        virtualHistoryAmount *= dot( virtualMotionSurfaceWeights, 0.25 );

        // Virtual motion - normal similarity
        uint4 prevPackGreenVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, gatherUvVirtualPrev ).wzxy;
        float4 prevNormalAndRoughnessVirtual00 = UnpackNormalRoughness( prevPackGreenVirtual.x );
        float4 prevNormalAndRoughnessVirtual10 = UnpackNormalRoughness( prevPackGreenVirtual.y );
        float4 prevNormalAndRoughnessVirtual01 = UnpackNormalRoughness( prevPackGreenVirtual.z );
        float4 prevNormalAndRoughnessVirtual11 = UnpackNormalRoughness( prevPackGreenVirtual.w );

        float lobeHalfAngle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughnessModified );
        float avgCurvatureAngle = STL::Math::AtanApprox( abs( curvature ) ) + NRD_ENCODING_ERRORS.x;
        float specNormalParams = 1.0 / min( lobeHalfAngle + avgCurvatureAngle, STL::Math::DegToRad( 89.0 ) );
        float4 virtualMotionNormalWeights = GetNormalWeight4( specNormalParams, N, prevNormalAndRoughnessVirtual00.xyz, prevNormalAndRoughnessVirtual10.xyz, prevNormalAndRoughnessVirtual01.xyz, prevNormalAndRoughnessVirtual11.xyz );
        virtualMotionNormalWeights = lerp( 1.0, virtualMotionNormalWeights, Xvirtual.w );
        virtualMotionFootprintWeights *= virtualMotionNormalWeights;

        // Virtual motion - roughness similarity
        float4 prevRoughnessVirtual = float4( prevNormalAndRoughnessVirtual00.w, prevNormalAndRoughnessVirtual10.w, prevNormalAndRoughnessVirtual01.w, prevNormalAndRoughnessVirtual11.w );
        float2 specRoughnessParams = GetRoughnessWeightParams( roughness, 0.1 );
        float4 virtualMotionRoughnessWeights = GetRoughnessWeight( specRoughnessParams, prevRoughnessVirtual );
        virtualMotionFootprintWeights *= virtualMotionRoughnessWeights;

        float interpolatedRoughnessWeight = STL::Filtering::ApplyBilinearFilter( virtualMotionRoughnessWeights.x, virtualMotionRoughnessWeights.y, virtualMotionRoughnessWeights.z, virtualMotionRoughnessWeights.w, bilinearFilterAtPrevVirtualPos );
        float NoVflat = abs( dot( Nflat, V ) );
        float fresnelFactor = STL::BRDF::Pow5( NoVflat );
        float virtualHistoryAmountRoughnessAdjustment = lerp( fresnelFactor, 1.0, SaturateParallax( parallax * 0.5 ) ) * 0.5;
        virtualHistoryAmount *= lerp( virtualHistoryAmountRoughnessAdjustment, 1.0, interpolatedRoughnessWeight );

        // Sample virtual history
        float4 bilinearWeightsWithOcclusionVirtual = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevVirtualPos, max( virtualMotionSurfaceWeights, 0.001 ) );

        float virtualOcclusionAvg = step( 3.5, dot( virtualMotionSurfaceWeights, 1.0 ) ) * REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA;
        virtualOcclusionAvg *= surfaceOcclusionAvg;

        float4 specHistoryVirtual = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Spec, gLinearClamp,
            saturate( pixelUvVirtualPrev ) * gRectSizePrev, gInvScreenSize,
            bilinearWeightsWithOcclusionVirtual, virtualOcclusionAvg == 1.0
        );

        // Virtual motion - hit distance similarity
        float parallaxScale = lerp( 0.125 + flatness * 0.125, 1.0, gReference );
        float specAccumSpeedSurface = GetSpecAccumSpeed( specMaxAccumSpeed * specHistoryConfidence, roughnessModified, NoV, parallax * parallaxScale );
        float specAccumSpeedSurfaceNonLinear = 1.0 / ( min( specAccumSpeedSurface, gSpecMaxAccumulatedFrameNum ) + 1.0 );

        float hitDistNorm = lerp( specHistorySurface.w, spec.w, max( specAccumSpeedSurfaceNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) ); // TODO: try to use "hitDistCurrent"
        float hitDist = hitDistNorm * hitDistScale;
        float hitDistVirtual = specHistoryVirtual.w * hitDistScale;
        float hitDistDelta = abs( hitDistVirtual - hitDist ); // TODO: sigma can worsen! useful for high roughness only?
        float hitDistMax = max( hitDistVirtual, hitDist );
        hitDistDelta /= hitDistMax + viewZ + 1e-6; // TODO:  + roughnessModified?
        float virtualMotionHitDistWeights = 1.0 - STL::Math::SmoothStep( 0.0, 0.25, STL::Math::Sqrt01( hitDistDelta ) * SaturateParallax( parallax * REBLUR_TS_SIGMA_AMPLITUDE ) );
        virtualMotionHitDistWeights = lerp( 1.0, virtualMotionHitDistWeights, Xvirtual.w );
        virtualMotionFootprintWeights *= virtualMotionHitDistWeights;

        // Accumulation acceleration
        float specAccumSpeedVirtual = specMaxAccumSpeed * specHistoryConfidence;
        float specMinAccumSpeed = min( specAccumSpeedVirtual, GetMipLevel( roughnessModified ) );
        float specAccumSpeedScale = 1.0 - STL::Math::SmoothStep01( dot( hitDistDelta, 0.25 ) * SaturateParallax( parallax * REBLUR_TS_SIGMA_AMPLITUDE ) );
        specAccumSpeedVirtual = lerp( specMinAccumSpeed, specAccumSpeedVirtual, specAccumSpeedScale );

        // Fix trails if radiance on a flat surface is taken from a sloppy surface
        float2 pixelUvDelta = pixelUvVirtualPrev - pixelUvPrev;
        float trailConfidence = 1.0;

        [unroll]
        for( uint i = 0; i < 2; i++ )
        {
            float2 pixelUvVirtualPrevPrev = pixelUvVirtualPrev + pixelUvDelta * ( 2.0 + i );

            uint p = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds[ uint2( pixelUvVirtualPrevPrev * gRectSizePrev ) ].y;
            float3 n = UnpackNormalRoughness( p ).xyz;

            float cosa = dot( N, n );
            float angle = STL::Math::AcosApprox( saturate( cosa ) );

            trailConfidence *= _ComputeWeight( float2( specNormalParams, -0.001 ), angle );
        }

        trailConfidence = lerp( 1.0, trailConfidence, Xvirtual.w );

        // Virtual history clamping
        float virtualHistoryConfidence = STL::Filtering::ApplyBilinearFilter( virtualMotionFootprintWeights.x, virtualMotionFootprintWeights.y, virtualMotionFootprintWeights.z, virtualMotionFootprintWeights.w, bilinearFilterAtPrevVirtualPos );
        virtualHistoryConfidence *= trailConfidence;

        float smc = GetSpecMagicCurve( roughnessModified );
        float sigmaScale = lerp( 1.0, 3.0, smc ) * ( 1.0 + 2.0 * smc * REBLUR_TS_SIGMA_AMPLITUDE * virtualHistoryConfidence );
        float4 specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual, REBLUR_USE_COLOR_CLAMPING_AABB );
        float unclampedVirtualHistoryAmount = lerp( virtualHistoryConfidence, 1.0, smc * STL::Math::SmoothStep( 0.2, 0.4, roughnessModified ) );
        float4 specHistoryVirtualMixed = lerp( specHistoryVirtualClamped, specHistoryVirtual, unclampedVirtualHistoryAmount );

        // Final composition
        float specAccumSpeed = InterpolateAccumSpeeds( specAccumSpeedSurface, specAccumSpeedVirtual, virtualHistoryAmount );
        float specAccumSpeedNonLinear = 1.0 / ( min( specAccumSpeed, gSpecMaxAccumulatedFrameNum ) + 1.0 );

        float4 specHistory = MixSurfaceAndVirtualMotion( specHistorySurface, specHistoryVirtualMixed, virtualHistoryAmount, hitDistFactor );
        float4 specResult = MixHistoryAndCurrent( specHistory, spec, specAccumSpeedNonLinear, roughnessModified );
        specResult = Sanitize( specResult, spec );

        gOut_Spec[ pixelPos ] = specResult;

        fbits += floor( GetMipLevel( roughnessModified ) );
        fbits += virtualOcclusionAvg * 16.0;

        virtualHistoryConfidence = lerp( 1.0, virtualHistoryConfidence, virtualHistoryAmount );

        float specError = GetColorErrorForAdaptiveRadiusScale( specResult, specHistory, specAccumSpeedNonLinear, roughnessModified, 0 );
        float specAccumSpeedFinal = min( specMaxAccumSpeed, max( specAccumSpeed, GetMipLevel( roughnessModified ) ) );
    #else
        float virtualHistoryAmount = 0;
        float virtualHistoryConfidence = 0;
        float specError = 0;
        float specAccumSpeedFinal = 0;
    #endif

    // Internal data
    gOut_InternalData[ pixelPos ] = PackDiffSpecInternalData( diffAccumSpeedFinal, specAccumSpeedFinal, curvature, fbits );
    gOut_Error[ pixelPos ] = float4( diffError, virtualHistoryAmount, specError, virtualHistoryConfidence );
}
