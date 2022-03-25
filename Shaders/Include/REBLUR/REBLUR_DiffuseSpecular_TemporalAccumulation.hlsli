/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

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

    // Normal and roughness
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Early out
    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );
    float scaledViewZ = min( viewZ * NRD_FP16_VIEWZ_SCALE, NRD_FP16_MAX );

    gOut_ScaledViewZ[ pixelPos ] = scaledViewZ;

    [branch]
    if( viewZ > gDenoisingRange )
        return;

    // Current position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );

    // Calculate distribution of normals
    int2 smemPos = threadPos + BORDER;

    #if( defined REBLUR_SPECULAR )
        float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
        float4 specM1 = spec;
        float4 specM2 = spec * spec;
        float roughnessM1 = roughness;
        float roughnessM2 = roughness * roughness;
        float minHitDist3x3 = spec.w;
        float minHitDist5x5 = spec.w;
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
            float2 d = float2( dx, dy ) - BORDER;

            #if( defined REBLUR_SPECULAR )
                // TODO: using weights leads to instabilities on thin objects
                float4 s = s_Spec[ pos.y ][ pos.x ];
                specM1 += s;
                specM2 += s * s;

                if( all( abs( d ) <= 1 ) ) // only in 3x3
                {
                    float4 n = s_Normal_Roughness[ pos.y ][ pos.x ];
                    Nflat += n.xyz;

                    roughnessM1 += n.w;
                    roughnessM2 += n.w * n.w;

                    float3 xv = STL::Geometry::ReconstructViewPosition( pixelUv + d * gInvRectSize, gFrustum, 1.0, gOrthoMode );
                    float3 x = STL::Geometry::AffineTransform( gViewToWorld, xv );
                    float3 v = GetViewVector( x );
                    float c = EstimateCurvature( n.xyz, v, N, X );

                    float w = exp2( -0.5 * STL::Math::LengthSquared( d ) );
                    curvature += c * w;
                    curvatureSum += w;

                    minHitDist3x3 = min( s.w, minHitDist3x3 );
                }
                else
                    minHitDist5x5 = min( s.w, minHitDist5x5 );
            #else
                if( all( abs( d ) <= 1 ) ) // only in 3x3
                    Nflat += s_Normal_Roughness[ pos.y ][ pos.x ].xyz;
            #endif
        }
    }

    float3 Navg = Nflat / 9.0;
    Nflat = normalize( Nflat );

    #if( defined REBLUR_SPECULAR )
        float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );
        specM1 *= invSum;
        specM2 *= invSum;
        float4 specSigma = GetStdDev( specM1, specM2 );

        roughnessM1 /= 9.0;
        roughnessM2 /= 9.0;
        float roughnessSigma = GetStdDev( roughnessM1, roughnessM2 );

        minHitDist5x5 = min( minHitDist5x5, minHitDist3x3 );

        curvature /= curvatureSum;
        curvature *= STL::Math::LinearStep( 0.0, NRD_ENCODING_ERRORS.y, abs( curvature ) );

        float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );
    #endif

    // Previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser ] * gMotionVectorScale.xyy; // TODO: use nearest MV
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gIsWorldSpaceMotionEnabled );
    float isInScreen = IsInScreen( pixelUvPrev );
    float3 Xprev = X + motionVector * float( gIsWorldSpaceMotionEnabled != 0 );

    // Previous data ( 4x4, surface motion )
    STL::Filtering::CatmullRom catmullRomFilterAtPrevPos = STL::Filtering::GetCatmullRomFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float2 catmullRomFilterAtPrevPosGatherOrigin = catmullRomFilterAtPrevPos.origin * gInvScreenSize;
    uint4 packedPrevViewZDiffAccumSpeed0 = gIn_Prev_ViewZ_DiffAccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
    uint4 packedPrevViewZDiffAccumSpeed1 = gIn_Prev_ViewZ_DiffAccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 packedPrevViewZDiffAccumSpeed2 = gIn_Prev_ViewZ_DiffAccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 packedPrevViewZDiffAccumSpeed3 = gIn_Prev_ViewZ_DiffAccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;

    float4 prevViewZ0 = UnpackPrevViewZ( packedPrevViewZDiffAccumSpeed0 );
    float4 prevViewZ1 = UnpackPrevViewZ( packedPrevViewZDiffAccumSpeed1 );
    float4 prevViewZ2 = UnpackPrevViewZ( packedPrevViewZDiffAccumSpeed2 );
    float4 prevViewZ3 = UnpackPrevViewZ( packedPrevViewZDiffAccumSpeed3 );

    #if( defined REBLUR_DIFFUSE )
        float4 diffPrevAccumSpeeds = UnpackDiffAccumSpeed( uint4( packedPrevViewZDiffAccumSpeed0.w, packedPrevViewZDiffAccumSpeed1.z, packedPrevViewZDiffAccumSpeed2.y, packedPrevViewZDiffAccumSpeed3.x ) );
    #endif

    // TODO: 4x4 normals and materialID are reduced to 2x2 only
    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float2 bilinearFilterAtPrevPosGatherOrigin = ( bilinearFilterAtPrevPos.origin + 1.0 ) * gInvScreenSize;
    uint4 packedPrevNormalSpecAccumSpeed = gIn_Prev_Normal_SpecAccumSpeed.GatherRed( gNearestClamp, bilinearFilterAtPrevPosGatherOrigin ).wzxy;

    float4 prevMaterialIDs;
    float4 specPrevAccumSpeeds;
    float3 prevNormal00 = UnpackPrevNormalAccumSpeedMaterialID( packedPrevNormalSpecAccumSpeed.x, specPrevAccumSpeeds.x, prevMaterialIDs.x );
    float3 prevNormal10 = UnpackPrevNormalAccumSpeedMaterialID( packedPrevNormalSpecAccumSpeed.y, specPrevAccumSpeeds.y, prevMaterialIDs.y );
    float3 prevNormal01 = UnpackPrevNormalAccumSpeedMaterialID( packedPrevNormalSpecAccumSpeed.z, specPrevAccumSpeeds.z, prevMaterialIDs.z );
    float3 prevNormal11 = UnpackPrevNormalAccumSpeedMaterialID( packedPrevNormalSpecAccumSpeed.w, specPrevAccumSpeeds.w, prevMaterialIDs.w );

    // Plane distance based disocclusion for surface motion
    float3 V = GetViewVector( X );
    float invFrustumHeight = 1.0 / PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
    float3 prevNflatUnnormalized = prevNormal00 + prevNormal10 + prevNormal01 + prevNormal11;
    float disocclusionThreshold = GetDisocclusionThreshold( gDisocclusionThreshold, viewZ, Nflat, V );
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

    // Ignore backfacing history
    float4 cosa;
    cosa.x = dot( N, prevNormal00 );
    cosa.y = dot( N, prevNormal10 );
    cosa.z = dot( N, prevNormal01 );
    cosa.w = dot( N, prevNormal11 );

    float parallax = ComputeParallax( X, Xprev, gCameraDelta, gOrthoMode != 0 );
    float cosAngleMin = lerp( -0.95, -0.01, SaturateParallax( parallax ) );
    float mvLength = length( ( pixelUvPrev - pixelUv ) * gRectSize );
    float4 frontFacing = STL::Math::LinearStep( cosAngleMin, 0.01, cosa );
    frontFacing = lerp( 1.0, frontFacing, saturate( mvLength / 0.25 ) );

    occlusion0.w *= frontFacing.x;
    occlusion1.z *= frontFacing.y;
    occlusion2.y *= frontFacing.z;
    occlusion3.x *= frontFacing.w;

    // Avoid "got stuck in history" effect under slow motion when only 1 sample is valid from 2x2 footprint
    float footprintQuality = STL::Filtering::ApplyBilinearFilter( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x, bilinearFilterAtPrevPos );
    footprintQuality = lerp( 0.5, 1.0, footprintQuality );

    // Avoid footprint momentary stretching due to changed viewing angle
    float3 Vprev = normalize( Xprev - gCameraDelta.xyz );
    float VoNflat = abs( dot( Nflat, V ) ) + 1e-3;
    float VoNflatprev = abs( dot( Nflat, Vprev ) ) + 1e-3;
    float sizeQuality = VoNflatprev / VoNflat; // this order because we need to fix stretching only, shrinking is OK
    footprintQuality *= lerp( 0.1, 1.0, saturate( sizeQuality + abs( gOrthoMode ) ) );

    float4 surfaceWeightsWithOcclusion = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x ) );

    // Material ID // TODO: needed in "footprintQuality"?
    float4 materialCmps = CompareMaterials( materialID, prevMaterialIDs, gDiffMaterialMask | gSpecMaterialMask );
    occlusion0.w *= materialCmps.x;
    occlusion1.z *= materialCmps.y;
    occlusion2.y *= materialCmps.z;
    occlusion3.x *= materialCmps.w;

    float4 surfaceWeightsWithOcclusionAndMaterialID = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x ) );

    // IMPORTANT: CatRom or custom bilinear work as expected when only one is in use. When mixed, a disocclusion event can introduce a switch to
    // bilinear, which can snap to a single sample according to custom weights. It can introduce a discontinuity in color. In turn CatRom can immediately
    // react to this and increase local sharpness. Next, if another disocclusion happens, custom bilinear can snap to the sharpened sample again...
    // This process can continue almost infinitely, blowing up the image due to over sharpenning in a loop. It can be fixed by:
    // - using camera jittering
    // - not using CatRom on edges (current approach)
    // - computing 4x4 normal weights
    bool isCatRomAllowedForSurfaceMotion = dot( occlusion0 + occlusion1 + occlusion2 + occlusion3, 1.0 ) > 15.5;
    isCatRomAllowedForSurfaceMotion = isCatRomAllowedForSurfaceMotion & ( length( Navg ) > 0.65 ); // TODO: 0.85?
    isCatRomAllowedForSurfaceMotion = isCatRomAllowedForSurfaceMotion & REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA;

    float fbits = float( isCatRomAllowedForSurfaceMotion ) * 8.0;

    // Update accumulation speeds
    // IMPORTANT: "Upper bound" is used to control accumulation, while "No confidence" artificially bumps number of accumulated
    // frames to skip "HistoryFix" pass. Maybe introduce a bit in "fbits", because current behavior negatively impacts "TS"?
    #if( defined REBLUR_DIFFUSE )
        float diffMaxAccumSpeedNoConfidence = AdvanceAccumSpeed( diffPrevAccumSpeeds, gDiffMaterialMask ? surfaceWeightsWithOcclusionAndMaterialID : surfaceWeightsWithOcclusion );
        diffMaxAccumSpeedNoConfidence *= footprintQuality;

        float diffAccumSpeedUpperBound = diffMaxAccumSpeedNoConfidence;
        #if( defined REBLUR_PROVIDED_CONFIDENCE )
            diffAccumSpeedUpperBound *= gIn_DiffConfidence[ pixelPosUser ];
        #endif
    #endif

    #if( defined REBLUR_SPECULAR )
        float specMaxAccumSpeedNoConfidence = AdvanceAccumSpeed( specPrevAccumSpeeds, gSpecMaterialMask ? surfaceWeightsWithOcclusionAndMaterialID : surfaceWeightsWithOcclusion );
        specMaxAccumSpeedNoConfidence *= footprintQuality;

        float specAccumSpeedUpperBound = specMaxAccumSpeedNoConfidence;
        #if( defined REBLUR_PROVIDED_CONFIDENCE )
            specAccumSpeedUpperBound *= gIn_SpecConfidence[ pixelPosUser ];
        #endif
    #endif

    // Sample history ( surface motion )
    #if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
        float4 specHistorySurface;
        float4 diffHistory = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Diff, gIn_History_Spec, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            surfaceWeightsWithOcclusionAndMaterialID, isCatRomAllowedForSurfaceMotion,
            specHistorySurface
        );
    #elif( defined REBLUR_DIFFUSE )
        float4 diffHistory = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Diff, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            surfaceWeightsWithOcclusionAndMaterialID, isCatRomAllowedForSurfaceMotion
        );
    #else
        float4 specHistorySurface = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Spec, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            surfaceWeightsWithOcclusionAndMaterialID, isCatRomAllowedForSurfaceMotion
        );
    #endif

    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    // Diffuse
    #if( defined REBLUR_DIFFUSE )
        float4 diff = gIn_Diff[ pixelPos ];

        // Checkerboard resolve
        bool diffHasData = gDiffCheckerboard == 2 || checkerboard == gDiffCheckerboard;
        if( !diffHasData && gResetHistory == 0 )
        {
            float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffAccumSpeedUpperBound );
            float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

            diff = MixHistoryAndCurrent( diffHistory, diff, historyWeight );
        }

        // Accumulation
        float diffAccumSpeed = diffAccumSpeedUpperBound;
        float diffAccumSpeedNonLinear = 1.0 / ( min( diffAccumSpeed, gMaxAccumulatedFrameNum ) + 1.0 );

        float4 diffResult = MixHistoryAndCurrent( diffHistory, diff, diffAccumSpeedNonLinear );
        diffResult = Sanitize( diffResult, diff );

        // Fight with fireflies
        float diffClampedResultAmount = saturate( diffAccumSpeed / ( gMaxAccumulatedFrameNum + 0.001 ) ) * ( 1.0 - gReference );
        float4 diffResultClamped = min( diffResult, diffHistory * REBLUR_MAX_FIREFLY_RELATIVE_INTENSITY );
        diffResult = lerp( diffResult, diffResultClamped, diffClampedResultAmount );

        // Output
        gOut_Diff[ pixelPos ] = diffResult;

        float diffMipLevel = GetMipLevel( 1.0 );
        float diffError = GetColorErrorForAdaptiveRadiusScale( diffResult, diffHistory, diffAccumSpeedNonLinear, 1.0, 0 );
        float diffAccumSpeedFinal = min( diffMaxAccumSpeedNoConfidence, max( diffAccumSpeed, diffMipLevel ) );
    #else
        float diffError = 0;
        float diffAccumSpeedFinal = 0;
    #endif

    // Specular
    #if( defined REBLUR_SPECULAR )
        float smc = GetSpecMagicCurve( roughnessModified );

        // Checkerboard resolve
        bool specHasData = gSpecCheckerboard == 2 || checkerboard == gSpecCheckerboard;
        if( !specHasData && gResetHistory == 0 )
        {
            float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specAccumSpeedUpperBound, parallax, roughness );
            float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

            float4 specHistorySurfaceClamped = STL::Color::Clamp( specM1, specSigma * temporalAccumulationParams.y, specHistorySurface, REBLUR_USE_COLOR_CLAMPING_AABB ); // TODO: needed?
            specHistorySurfaceClamped = lerp( specHistorySurfaceClamped, specHistorySurface, smc );

            spec = MixHistoryAndCurrent( specHistorySurfaceClamped, spec, historyWeight, roughnessModified );
        }

        // Current hit distance
        float hitDistCurrent = lerp( minHitDist3x3, minHitDist5x5, STL::Math::SmoothStep( 0.04, 0.08, roughnessModified ) );
        hitDistCurrent = STL::Color::Clamp( specM1.w, specSigma.w * 3.0, hitDistCurrent );

        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
        hitDistCurrent *= hitDistScale;

        // Virtual motion
        float NoV = abs( dot( N, V ) );
        float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
        float realCurvature = GetRealCurvature( curvature, pixelSize, NoV );
        float3 Xvirtual = GetXvirtual( X, Xprev, V, NoV, roughnessModified, hitDistCurrent, realCurvature );
        float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual, false );
        STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gRectSizePrev );

        float virtualHistoryAmount = IsInScreen( pixelUvVirtualPrev );
        virtualHistoryAmount *= 1.0 - gReference;
        virtualHistoryAmount *= STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughnessModified, REBLUR_SPEC_DOMINANT_DIRECTION );

        // Parallax correction
        float parallaxOrig = parallax;

        float parallaxVirtual = ComputeParallax( Xvirtual, Xvirtual, gCameraDelta, gOrthoMode != 0 );
        parallax = max( parallax, parallaxVirtual * ( 1.0 - smc ) );

        float hitDistFactor = saturate( hitDistCurrent * invFrustumHeight );
        parallax *= hitDistFactor;

        // This scaler improves motion stability for roughness 0.4+
        virtualHistoryAmount *= 1.0 - STL::Math::SmoothStep( 0.15, 0.85, roughness ) * ( 1.0 - hitDistFactor );

        // Virtual motion - surface similarity // TODO: make it closer to the main disocclusion test
        float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
        uint4 packedPrevViewZDiffAccumSpeedVirtual = gIn_Prev_ViewZ_DiffAccumSpeed.GatherRed( gNearestClamp, gatherUvVirtualPrev ).wzxy;
        float4 prevViewZsVirtual = UnpackPrevViewZ( packedPrevViewZDiffAccumSpeedVirtual );
        float3 Nvprev = STL::Geometry::RotateVector( gWorldToViewPrev, Nflat );
        float4 ka;
        ka.x = dot( Nvprev, STL::Geometry::ReconstructViewPosition( pixelUvVirtualPrev, gFrustumPrev, prevViewZsVirtual.x, gOrthoMode ) );
        ka.y = dot( Nvprev, STL::Geometry::ReconstructViewPosition( pixelUvVirtualPrev, gFrustumPrev, prevViewZsVirtual.y, gOrthoMode ) );
        ka.z = dot( Nvprev, STL::Geometry::ReconstructViewPosition( pixelUvVirtualPrev, gFrustumPrev, prevViewZsVirtual.z, gOrthoMode ) );
        ka.w = dot( Nvprev, STL::Geometry::ReconstructViewPosition( pixelUvVirtualPrev, gFrustumPrev, prevViewZsVirtual.w, gOrthoMode ) );
        float kb = dot( Nvprev, Xvprev );
        float4 f = abs( ka - kb ) * invFrustumHeight;
        float4 virtualMotionSurfaceWeights = step( f, disocclusionThreshold );
        float virtualHistoryConfidence = STL::Filtering::ApplyBilinearFilter( virtualMotionSurfaceWeights.x, virtualMotionSurfaceWeights.y, virtualMotionSurfaceWeights.z, virtualMotionSurfaceWeights.w, bilinearFilterAtPrevVirtualPos );
        virtualHistoryAmount *= virtualHistoryConfidence;

        // Virtual motion - roughness similarity
        float prevRoughnessVirtual = gIn_Prev_Roughness.SampleLevel( gLinearClamp, pixelUvVirtualPrev * gRectSizePrev * gInvScreenSize, 0 );
        float2 virtualMotionRoughnessWeight = GetEncodingAwareRoughnessWeights( roughness, prevRoughnessVirtual, roughnessSigma, gRoughnessFraction );
        virtualHistoryConfidence *= virtualMotionRoughnessWeight.x;
        virtualHistoryAmount *= virtualMotionRoughnessWeight.y;

        // Sample virtual history
        float4 bilinearWeightsWithOcclusionVirtual = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevVirtualPos, max( virtualMotionSurfaceWeights, 0.001 ) );

        bool isCatRomAllowedForVirtualMotion = dot( virtualMotionSurfaceWeights, 1.0 ) > 3.5;
        isCatRomAllowedForVirtualMotion = isCatRomAllowedForVirtualMotion & isCatRomAllowedForSurfaceMotion;
        isCatRomAllowedForVirtualMotion = isCatRomAllowedForVirtualMotion & REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA;

        float4 specHistoryVirtual = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Spec, gLinearClamp,
            saturate( pixelUvVirtualPrev ) * gRectSizePrev, gInvScreenSize,
            bilinearWeightsWithOcclusionVirtual, isCatRomAllowedForVirtualMotion
        );

        // Virtual motion - hit distance similarity // TODO: move into "prev-prev" loop
        float specAccumSpeedSurface = GetSpecAccumSpeed( specAccumSpeedUpperBound, roughnessModified, NoV, parallax ); // TODO: optional scale can be applied to parallax if local curvature is very high, but it can lead to lags
        float specAccumSpeedSurfaceNonLinear = 1.0 / ( min( specAccumSpeedSurface, gMaxAccumulatedFrameNum ) + 1.0 );

        float hitDistNorm = lerp( specHistorySurface.w, spec.w, max( specAccumSpeedSurfaceNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) ); // TODO: try to use "hitDistCurrent"
        float hitDistFocused = ApplyThinLensEquation( hitDistNorm * hitDistScale, realCurvature );
        float hitDistVirtualFocused = ApplyThinLensEquation( specHistoryVirtual.w * hitDistScale, realCurvature );
        float hitDistDelta = abs( hitDistVirtualFocused - hitDistFocused ); // TODO: sigma can worsen! useful for high roughness only?
        float hitDistMax = max( abs( hitDistVirtualFocused ), abs( hitDistFocused ) );
        hitDistDelta /= hitDistMax + viewZ * 0.01 + 1e-6; // "viewZ" is already taken into account in "parallax", but use 1% to decrease sensitivity to low values
        float virtualMotionHitDistWeight = 1.0 - STL::Math::SmoothStep( 0.0, 0.25, STL::Math::Sqrt01( hitDistDelta ) * SaturateParallax( parallax * REBLUR_TS_SIGMA_AMPLITUDE ) );
        virtualHistoryConfidence *= virtualMotionHitDistWeight;

        // Normal weight ( with fixing trails if radiance on a flat surface is taken from a sloppy surface )
        float lobeHalfAngle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughnessModified );
        lobeHalfAngle += NRD_ENCODING_ERRORS.x + STL::Math::DegToRad( 1.5 ); // TODO: tune better?

        float parallaxInPixels = GetParallaxInPixels( parallaxOrig, gUnproject );
        float pixelSizeOverCurvatureRadius = curvature * NoV; // defines angle per 1 pixel
        float tana = pixelSizeOverCurvatureRadius * ( lerp( parallaxInPixels, 0.0, roughness ) + 2.0 );
        float avgCurvatureAngle = STL::Math::AtanApprox( abs( tana ) );

        float magicScale = lerp( 10.0, 1.0, saturate( parallaxInPixels / 5.0 ) );
        float2 virtualMotionDelta = pixelUvVirtualPrev - pixelUvPrev;
        virtualMotionDelta *= gOrthoMode == 0 ? magicScale : 1.0;

        float virtualMotionNormalWeight = 1.0;
        [unroll]
        for( uint i = 0; i < REBLUR_VIRTUAL_MOTION_NORMAL_WEIGHT_ITERATION_NUM; i++ )
        {
            float t = i + ( REBLUR_VIRTUAL_MOTION_NORMAL_WEIGHT_ITERATION_NUM < 3 ? 1.0 : 0.0 );
            float2 pixelUvVirtualPrevPrev = pixelUvVirtualPrev + virtualMotionDelta * t;

            #if( REBLUR_USE_BILINEAR_FOR_VIRTUAL_NORMAL_WEIGHT == 1 )
                STL::Filtering::Bilinear bilinearFilterAtPrevPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrevPrev ), gRectSizePrev );
                float2 gatherUvVirtualPrevPrev = ( bilinearFilterAtPrevPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
                uint4 p = gIn_Prev_Normal_SpecAccumSpeed.GatherRed( gNearestClamp, gatherUvVirtualPrevPrev ).wzxy;

                float3 n00 = UnpackPrevNormal( p.x );
                float3 n10 = UnpackPrevNormal( p.y );
                float3 n01 = UnpackPrevNormal( p.z );
                float3 n11 = UnpackPrevNormal( p.w );

                float3 n = STL::Filtering::ApplyBilinearFilter( n00, n10, n01, n11, bilinearFilterAtPrevPrevVirtualPos );
                n = normalize( n );
            #else
                uint p = gIn_Prev_Normal_SpecAccumSpeed[ uint2( pixelUvVirtualPrevPrev * gRectSizePrev ) ];
                float3 n = UnpackPrevNormal( p );
            #endif

            float maxAngle = lobeHalfAngle + avgCurvatureAngle * ( 1.0 + t );
            virtualMotionNormalWeight *= IsInScreen( pixelUvVirtualPrevPrev ) ? GetEncodingAwareNormalWeight( N, n, maxAngle ) : 1.0;
        }

        float virtualMotionLengthInPixels = length( virtualMotionDelta * gRectSizePrev );
        virtualMotionNormalWeight = lerp( 1.0, virtualMotionNormalWeight, saturate( virtualMotionLengthInPixels / 0.5 ) );

        virtualHistoryConfidence *= virtualMotionNormalWeight;
        virtualHistoryAmount *= lerp( 0.333, 1.0, virtualMotionNormalWeight ); // TODO: should depend on virtualHistoryAmount and accumSpeed?

        // Virtual motion - accumulation acceleration
        float responsiveAccumulationAmount = GetResponsiveAccumulationAmount( roughness );
        float specAccumSpeedVirtual = GetSpecAccumSpeed( specAccumSpeedUpperBound, lerp( 1.0, roughnessModified, responsiveAccumulationAmount ), 0.99999, 0.0 );

        float specMipLevel = GetMipLevel( roughnessModified );
        float specMinAccumSpeed = min( specAccumSpeedVirtual, specMipLevel );
        float fpsScaler = lerp( saturate( gFramerateScale * gFramerateScale ), 1.0, virtualHistoryConfidence );

        float specAccumSpeedScale = lerp( 0.7, 1.0, virtualMotionHitDistWeight );
        specAccumSpeedScale *= lerp( 0.7, 1.0, virtualMotionNormalWeight );
        specAccumSpeedScale *= fpsScaler;

        specAccumSpeedVirtual = lerp( specMinAccumSpeed, specAccumSpeedVirtual, specAccumSpeedScale );

        // Virtual history clamping
        float sigmaScale = lerp( 1.0, 3.0, smc ) * ( 1.0 + 2.0 * smc * REBLUR_TS_SIGMA_AMPLITUDE * virtualHistoryConfidence );
        float4 specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual, REBLUR_USE_COLOR_CLAMPING_AABB );
        float unclampedVirtualHistoryAmount = lerp( virtualHistoryConfidence, 1.0, smc * STL::Math::SmoothStep( 0.2, 0.4, roughnessModified ) );
        unclampedVirtualHistoryAmount *= lerp( 1.0, smc * 0.5 + 0.5, responsiveAccumulationAmount );
        float4 specHistoryVirtualMixed = lerp( specHistoryVirtualClamped, specHistoryVirtual, unclampedVirtualHistoryAmount );

        // Final composition
        float specAccumSpeed = InterpolateAccumSpeeds( specAccumSpeedSurface, specAccumSpeedVirtual, virtualHistoryAmount );
        float specAccumSpeedNonLinear = 1.0 / ( min( specAccumSpeed, gMaxAccumulatedFrameNum ) + 1.0 );

        float4 specHistory = MixSurfaceAndVirtualMotion( specHistorySurface, specHistoryVirtualMixed, virtualHistoryAmount, hitDistFactor );
        float4 specResult = MixHistoryAndCurrent( specHistory, spec, specAccumSpeedNonLinear, roughnessModified );
        specResult = Sanitize( specResult, spec );

        // Fight with fireflies
        float specClampedResultAmount = saturate( specAccumSpeed / ( gMaxAccumulatedFrameNum + 0.001 ) ) * ( 1.0 - gReference );
        float4 specResultClamped = min( specResult, specHistory * REBLUR_MAX_FIREFLY_RELATIVE_INTENSITY );
        specResult = lerp( specResult, specResultClamped, specClampedResultAmount );

        // Output
        gOut_Spec[ pixelPos ] = specResult;

        fbits += floor( specMipLevel );
        fbits += float( isCatRomAllowedForVirtualMotion ) * 16.0;

        virtualHistoryConfidence = lerp( 1.0, virtualHistoryConfidence, virtualHistoryAmount );
        virtualHistoryConfidence = lerp( virtualHistoryConfidence, 1.0, specAccumSpeedNonLinear );

        float specError = GetColorErrorForAdaptiveRadiusScale( specResult, specHistory, specAccumSpeedNonLinear, roughnessModified, 0 );
        float specAccumSpeedFinal = min( specMaxAccumSpeedNoConfidence, max( specAccumSpeed, specMipLevel ) );
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
