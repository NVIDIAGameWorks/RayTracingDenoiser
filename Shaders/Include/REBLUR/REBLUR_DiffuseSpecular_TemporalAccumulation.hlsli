/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float4 s_Normal_Roughness[ BUFFER_Y ][ BUFFER_X ];
groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );
    uint2 globalIdUser = gRectOrigin + globalPos;

    s_Normal_Roughness[ sharedPos.y ][ sharedPos.x ] = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );

    #if( defined REBLUR_SPECULAR )
        uint2 pos = gIsPrepassEnabled ? globalPos : globalIdUser;
        float4 spec = gIn_Spec[ pos ];

        s_Spec[ sharedPos.y ][ sharedPos.x ] = spec;
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
    float3 X = STL::Geometry::RotateVector( gViewToWorld, Xv );

    // Analyze neighbors
    int2 smemPos = threadPos + BORDER;

    #if( defined REBLUR_SPECULAR )
        float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
        float4 specM1 = spec;
        float4 specM2 = spec * spec;
        float roughnessM1 = roughness;
        float roughnessM2 = roughness * roughness;
        float minHitDist3x3 = spec.w != 0.0 ? spec.w : REBLUR_INVALID_HITDIST;
        float minHitDist5x5 = minHitDist3x3;
    #endif

    float3 Navg = N;
    float curvature = 0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadPos + int2( dx, dy );
            float2 o = float2( dx, dy ) - BORDER;

            #if( defined REBLUR_SPECULAR )
                float4 s = s_Spec[ pos.y ][ pos.x ];

                specM1 += s;
                specM2 += s * s;

                if( all( abs( o ) <= 1 ) ) // only in 3x3, 8 directions for curvature
                {
                    float4 n = s_Normal_Roughness[ pos.y ][ pos.x ];
                    Navg += n.xyz;

                    roughnessM1 += n.w;
                    roughnessM2 += n.w * n.w;

                    float3 xv = STL::Geometry::ReconstructViewPosition( pixelUv + o * gInvRectSize, gFrustum, 1.0, gOrthoMode );
                    float3 x = STL::Geometry::RotateVector( gViewToWorld, xv );
                    float3 v = GetViewVector( x );
                    float c = EstimateCurvature( n.xyz, v, N, X );
                    curvature += c;

                    if( s.w != 0.0 )
                        minHitDist3x3 = min( s.w, minHitDist3x3 );
                }
                else
                {
                    if( s.w != 0.0 )
                        minHitDist5x5 = min( s.w, minHitDist5x5 );
                }
            #else
                if( all( abs( o ) <= 1 ) ) // only in 3x3
                {
                    float4 n = s_Normal_Roughness[ pos.y ][ pos.x ];
                    Navg += n.xyz;
                }
            #endif
        }
    }

    Navg /= 9.0; // needs to be unnormalized!

    #if( defined REBLUR_SPECULAR )
        float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );

        float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );
        specM1 *= invSum;
        specM2 *= invSum;
        float4 specSigma = GetStdDev( specM1, specM2 );

        roughnessM1 /= 9.0;
        roughnessM2 /= 9.0;
        float roughnessSigma = GetStdDev( roughnessM1, roughnessM2 );

        curvature /= 8.0;

        minHitDist5x5 = min( minHitDist5x5, minHitDist3x3 );
    #endif

    // Previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gIsWorldSpaceMotionEnabled );
    float isInScreen = IsInScreen( pixelUvPrev );
    float3 Xprev = X + motionVector * float( gIsWorldSpaceMotionEnabled != 0 );

    // Previous data ( 4x4, surface motion )
    /*
          Gather      => CatRom12    => Bilinear
        0x 0y 1x 1y       0y 1x
        0z 0w 1z 1w    0z 0w 1z 1w       0w 1z
        2x 2y 3x 3y    2x 2y 3x 3y       2y 3x
        2z 2w 3z 3w       2w 3z

         CatRom12     => Bilinear
           0x 1x
        0y 0z 1y 1z       0z 1y
        2x 2y 3x 3y       2y 3x
           2z 3z
    */
    STL::Filtering::CatmullRom catmullRomFilterAtPrevPos = STL::Filtering::GetCatmullRomFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float2 catmullRomFilterAtPrevPosGatherOrigin = catmullRomFilterAtPrevPos.origin * gInvScreenSize;
    uint4 packedPrevViewZDiffAccumSpeed0 = gIn_Prev_ViewZ_DiffAccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
    uint4 packedPrevViewZDiffAccumSpeed1 = gIn_Prev_ViewZ_DiffAccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 packedPrevViewZDiffAccumSpeed2 = gIn_Prev_ViewZ_DiffAccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 packedPrevViewZDiffAccumSpeed3 = gIn_Prev_ViewZ_DiffAccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;

    float3 prevViewZ0 = UnpackPrevViewZ( packedPrevViewZDiffAccumSpeed0.yzw );
    float3 prevViewZ1 = UnpackPrevViewZ( packedPrevViewZDiffAccumSpeed1.xzw );
    float3 prevViewZ2 = UnpackPrevViewZ( packedPrevViewZDiffAccumSpeed2.xyw );
    float3 prevViewZ3 = UnpackPrevViewZ( packedPrevViewZDiffAccumSpeed3.xyz );

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
    float3 prevNflat = normalize( prevNormal00 + prevNormal10 + prevNormal01 + prevNormal11 );
    prevNflat = STL::Geometry::RotateVector( gWorldPrevToWorld, prevNflat );

    // Plane distance based disocclusion for surface motion
    float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
    float mvLength = length( ( pixelUvPrev - pixelUv ) * gRectSize );
    float frontFacing = lerp( -cos( STL::Math::DegToRad( 45 ) ), 0.0, saturate( mvLength / 0.25 ) );
    float disocclusionThreshold = ( isInScreen && dot( prevNflat, normalize( Navg ) ) > frontFacing ) ? gDisocclusionThreshold : -1.0; // ignore out of screen and backfacing
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float3 V = GetViewVector( X );
    float NoV = abs( dot( N, V ) );
    float NoVmod = NoV / frustumHeight; // normalize here to save ALU
    float NoVmodMulXvprevz = Xvprev.z * NoVmod;
    float3 planeDist0 = abs( prevViewZ0 * NoVmod - NoVmodMulXvprevz );
    float3 planeDist1 = abs( prevViewZ1 * NoVmod - NoVmodMulXvprevz );
    float3 planeDist2 = abs( prevViewZ2 * NoVmod - NoVmodMulXvprevz );
    float3 planeDist3 = abs( prevViewZ3 * NoVmod - NoVmodMulXvprevz );
    float3 occlusion0 = step( planeDist0, disocclusionThreshold );
    float3 occlusion1 = step( planeDist1, disocclusionThreshold );
    float3 occlusion2 = step( planeDist2, disocclusionThreshold );
    float3 occlusion3 = step( planeDist3, disocclusionThreshold );

    // Avoid "got stuck in history" effect under slow motion when only 1 sample is valid from 2x2 footprint
    float footprintQuality = STL::Filtering::ApplyBilinearFilter( occlusion0.z, occlusion1.y, occlusion2.y, occlusion3.x, bilinearFilterAtPrevPos );
    footprintQuality = STL::Math::Sqrt01( footprintQuality );

    // Avoid footprint momentary stretching due to changed viewing angle
    float3 Vprev = GetViewVectorPrev( Xprev, gCameraDelta.xyz );
    float NoVprev = abs( dot( N, Vprev ) ); // TODO: should be prevNflat, but jittering breaks logic
    float sizeQuality = ( NoVprev + 1e-3 ) / ( NoV + 1e-3 ); // this order because we need to fix stretching only, shrinking is OK
    sizeQuality *= sizeQuality;
    footprintQuality *= lerp( 0.1, 1.0, saturate( sizeQuality ) );

    float4 surfaceWeightsWithOcclusion = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, float4( occlusion0.z, occlusion1.y, occlusion2.y, occlusion3.x ) );

    // IMPORTANT: CatRom or custom bilinear work as expected when only one is in use. When mixed, a disocclusion event can introduce a switch to
    // bilinear, which can snap to a single sample according to custom weights. It can introduce a discontinuity in color. In turn, CatRom can immediately
    // react to this and increase local sharpness. Next, if another disocclusion happens, the custom bilinear can snap to the sharpened sample again...
    // This process can continue almost infinitely, blowing up the image due to over sharpening in a loop. It can be partially fixed by:
    // - using camera jittering
    // - not using CatRom on edges ( current approach )
    // - computing 4x4 normal weights
    // - doing 4x4 material checks
    bool isCatRomAllowedForSurfaceMotion = dot( occlusion0 + occlusion1 + occlusion2 + occlusion3, 1.0 ) > 11.5;
    isCatRomAllowedForSurfaceMotion = isCatRomAllowedForSurfaceMotion && ( length( Navg ) > 0.65 ); // TODO: 0.85?
    isCatRomAllowedForSurfaceMotion = isCatRomAllowedForSurfaceMotion && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA;

    // Material ID // TODO: needed in "footprintQuality"?
    float4 materialCmps = CompareMaterials( materialID, prevMaterialIDs, gDiffMaterialMask | gSpecMaterialMask );
    occlusion0.z *= materialCmps.x;
    occlusion1.y *= materialCmps.y;
    occlusion2.y *= materialCmps.z;
    occlusion3.x *= materialCmps.w;

    float4 surfaceWeightsWithOcclusionAndMaterialID = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, float4( occlusion0.z, occlusion1.y, occlusion2.y, occlusion3.x ) );

    // Bits
    float fbits = isCatRomAllowedForSurfaceMotion * 2.0;
    fbits += occlusion0.z * 4.0 + occlusion1.y * 8.0 + occlusion2.y * 16.0 + occlusion3.x * 32.0;

    // Update accumulation speeds
    // IMPORTANT: "Upper bound" is used to control accumulation, while "No confidence" artificially bumps number of accumulated
    // frames to skip "HistoryFix" pass. Maybe introduce a bit in "fbits", because current behavior negatively impacts "TS"?
    #if( defined REBLUR_DIFFUSE )
        float diffMaxAccumSpeedNoConfidence = AdvanceAccumSpeed( diffPrevAccumSpeeds, gDiffMaterialMask ? surfaceWeightsWithOcclusionAndMaterialID : surfaceWeightsWithOcclusion );
        diffMaxAccumSpeedNoConfidence *= lerp( footprintQuality, 1.0, 1.0 / ( 1.0 + diffMaxAccumSpeedNoConfidence ) );

        float diffAccumSpeedUpperBound = diffMaxAccumSpeedNoConfidence;
        #if( defined REBLUR_PROVIDED_CONFIDENCE )
            diffAccumSpeedUpperBound *= gIn_DiffConfidence[ pixelPosUser ];
        #endif
    #endif

    #if( defined REBLUR_SPECULAR )
        float specMaxAccumSpeedNoConfidence = AdvanceAccumSpeed( specPrevAccumSpeeds, gSpecMaterialMask ? surfaceWeightsWithOcclusionAndMaterialID : surfaceWeightsWithOcclusion );
        specMaxAccumSpeedNoConfidence *= lerp( footprintQuality, 1.0, 1.0 / ( 1.0 + specMaxAccumSpeedNoConfidence ) );

        float specAccumSpeedUpperBound = specMaxAccumSpeedNoConfidence;
        #if( defined REBLUR_PROVIDED_CONFIDENCE )
            specAccumSpeedUpperBound *= gIn_SpecConfidence[ pixelPosUser ];
        #endif
    #endif

    // Sample history ( surface motion )
    float4 diffHistory, specHistorySurface;
    BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
        saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize, surfaceWeightsWithOcclusion,
        gLinearClamp, isCatRomAllowedForSurfaceMotion
        #if( defined REBLUR_DIFFUSE )
            , gIn_History_Diff, diffHistory
        #endif
        #if( defined REBLUR_SPECULAR )
            , gIn_History_Spec, specHistorySurface
        #endif
    );

    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    // Diffuse
    #if( defined REBLUR_DIFFUSE )
        float4 diff = gIn_Diff[ pixelPos ];

        // Checkerboard resolve // TODO: materialID support? Invalid hitDist support?
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

        // Output
        gOut_Diff[ pixelPos ] = diffResult;

        float diffError = GetColorErrorForAdaptiveRadiusScale( diffResult, diffHistory, diffAccumSpeedNonLinear, 1.0, 0, false );
        float diffAccumSpeedFinal = min( diffMaxAccumSpeedNoConfidence, max( diffAccumSpeed, REBLUR_USE_HISTORY_FIX_WITHOUT_DISOCCLUSION ? 0 : REBLUR_FIXED_FRAME_NUM ) );

        gOut_DiffData[ pixelPos ] = diffError;
    #else
        float diffAccumSpeedFinal = 0;
    #endif

    // Specular
    #if( defined REBLUR_SPECULAR )
        float smc = GetSpecMagicCurve( roughnessModified );

        // Parallax
        float parallaxSurface = ComputeParallax( Xprev, pixelUv, pixelUvPrev, gWorldToClip, gCameraDelta.xyz, gRectSize, gUnproject, gOrthoMode );
        float parallaxInPixels = GetParallaxInPixels( parallaxSurface, gUnproject );

        // Checkerboard resolve // TODO: materialID support? Invalid hitDist support?
        bool specHasData = gSpecCheckerboard == 2 || checkerboard == gSpecCheckerboard;
        if( !specHasData && gResetHistory == 0 )
        {
            float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specAccumSpeedUpperBound, parallaxSurface, roughness );
            float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

            float4 specHistorySurfaceClamped = STL::Color::Clamp( specM1, specSigma * temporalAccumulationParams.y, specHistorySurface, REBLUR_USE_COLOR_CLAMPING_AABB ); // TODO: needed?
            specHistorySurfaceClamped = lerp( specHistorySurfaceClamped, specHistorySurface, smc );

            spec = MixHistoryAndCurrent( specHistorySurfaceClamped, spec, historyWeight, roughnessModified );
        }

        // Filtered current hit distance
        minHitDist3x3 = minHitDist3x3 == REBLUR_INVALID_HITDIST ? specHistorySurface.w : minHitDist3x3;
        minHitDist5x5 = minHitDist5x5 == REBLUR_INVALID_HITDIST ? specHistorySurface.w : minHitDist3x3;

        float hitDist = lerp( minHitDist3x3, minHitDist5x5, STL::Math::SmoothStep( 0.04, 0.08, roughnessModified ) );

        float hitDistScaleForTracking = hitDist;
        #if( REBLUR_USE_OPTIMIZED_HIT_DIST_FOR_TRACKING != 0 )
            float2 searchRadius = REBLUR_HIT_DIST_SEARCH_RADIUS * gInvScreenSize * smc * NoV;

            [unroll]
            for( int s = 0; s < 8; s++ )
            {
                float3 offset = g_Poisson8[ s ];

                float2 uv = pixelUv + STL::Geometry::RotateVector( gRotator, offset.xy ) * searchRadius;
                float2 uvScaled = uv * gResolutionScale + gRectOffset * float( !gIsPrepassEnabled );

                float hd = gIn_Spec.SampleLevel( gNearestMirror, uvScaled, 0 ).w;
                hd *= IsInScreen( uvScaled );

                if( hd != 0.0 )
                    hitDist = min( hitDist, hd );
            }
        #endif
        hitDistScaleForTracking = saturate( hitDist / ( hitDistScaleForTracking + 1e-6 ) );

        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
        hitDist *= hitDistScale;

        // Virtual motion
        float3 Xvirtual = GetXvirtual( NoV, hitDist, curvature, X, Xprev, V, roughnessModified );
        float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual, false );
        STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gRectSizePrev );

        float virtualHistoryAmount = IsInScreen( pixelUvVirtualPrev );
        virtualHistoryAmount *= 1.0 - gReference;
        virtualHistoryAmount *= STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughnessModified, STL_SPECULAR_DOMINANT_DIRECTION_G2 );

        // This scaler improves motion stability for roughness 0.4+
        float hitDistFactor = GetHitDistFactor( hitDist, frustumHeight, 0.0 );
        virtualHistoryAmount *= 1.0 - STL::Math::SmoothStep( 0.15, 0.85, roughness ) * ( 1.0 - hitDistFactor );

        // Virtual motion - surface similarity
        float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
        uint4 packedPrevViewZDiffAccumSpeedVirtual = gIn_Prev_ViewZ_DiffAccumSpeed.GatherRed( gNearestClamp, gatherUvVirtualPrev ).wzxy;
        float4 prevViewZsVirtual = UnpackPrevViewZ( packedPrevViewZDiffAccumSpeedVirtual );
        float3 Vvirtual = STL::Geometry::ReconstructViewPosition( pixelUvVirtualPrev, gFrustumPrev, 1.0 ); // unnormalized
        float3 Nvprev = STL::Geometry::RotateVector( gWorldToViewPrev, N );
        float NoXreal = dot( Nvprev, Xvprev );
        float4 NoX = ( Nvprev.x * Vvirtual.x + Nvprev.y * Vvirtual.y ) * ( gOrthoMode == 0 ? prevViewZsVirtual : gOrthoMode ) + Nvprev.z * Vvirtual.z * prevViewZsVirtual;
        float4 virtualPlaneDist = abs( NoX - NoXreal ) / frustumHeight;
        float4 virtualMotionSurfaceWeights = step( virtualPlaneDist, disocclusionThreshold );
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

        float4 specHistoryVirtual;
        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( pixelUvVirtualPrev ) * gRectSizePrev, gInvScreenSize, bilinearWeightsWithOcclusionVirtual,
            gLinearClamp, isCatRomAllowedForVirtualMotion,
            gIn_History_Spec, specHistoryVirtual
        );

        // Virtual motion - hit distance similarity // TODO: move into "prev-prev" loop?
        // Tests: 3, 6, 8, 11, 14, 100, 104, 106, 109, 110, 120, 127, 130, 131
        float specAccumSpeedSurface = GetSpecAccumSpeed( specAccumSpeedUpperBound, roughnessModified, NoV, parallaxSurface * hitDistFactor );
        float hitDistAccumSpeedNonLinear = 1.0 / ( min( specAccumSpeedSurface, gMaxAccumulatedFrameNum ) + 1.0 );
        hitDistAccumSpeedNonLinear = max( hitDistAccumSpeedNonLinear, GetMinAllowedLimitForHitDistNonLinearAccumSpeed( roughness ) );
        hitDistAccumSpeedNonLinear = lerp( 1.0, hitDistAccumSpeedNonLinear, STL::Math::SmoothStep( 0.04, 0.11, roughnessModified ) ); // see tests 114 and 138

        float hitDistSurface = lerp( specHistorySurface.w, spec.w, hitDistAccumSpeedNonLinear ) * hitDistScale;
        float hitDistVirtual = specHistoryVirtual.w * hitDistScale;
        float hitDistSurfaceCompressed = abs( ApplyThinLensEquation( NoV, hitDistSurface, curvature ) );
        float hitDistVirtualCompressed = abs( ApplyThinLensEquation( NoV, hitDistVirtual, curvature ) );
        float hitDistDelta = abs( hitDistSurfaceCompressed - hitDistVirtualCompressed ); // sigma subtraction can worsen!
        float hitDistMax = max( hitDistSurfaceCompressed, hitDistVirtualCompressed );
        float parallaxSurfaceNorm = SaturateParallax( parallaxSurface * REBLUR_PARALLAX_SCALE );
        float oneMinusParallaxSurfaceNormMod = 1.0 - parallaxSurfaceNorm * ( 1.0 - roughnessModified );
        float hitDistDeltaScale = lerp( 50.0, 0.0, roughnessModified ) * parallaxSurfaceNorm;
        float virtualMotionHitDistWeight = 1.0 - saturate( hitDistDeltaScale * hitDistDelta / ( oneMinusParallaxSurfaceNormMod * frustumHeight + hitDistMax + 1e-6 ) );

        float minConfidence = smc * saturate( gFramerateScale * 0.66 ) + 0.05 * gFramerateScale * oneMinusParallaxSurfaceNormMod;
        minConfidence = lerp( 1.0, saturate( minConfidence ), parallaxSurfaceNorm ); // TODO: parallaxSurfaceNorm => 1 - oneMinusParallaxSurfaceNormMod?
        virtualHistoryConfidence *= lerp( minConfidence, 1.0, virtualMotionHitDistWeight );

        // Normal weight ( with fixing trails if radiance on a flat surface is taken from a sloppy surface )
        float lobeHalfAngle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughnessModified );
        lobeHalfAngle += NRD_NORMAL_ENCODING_ERROR + STL::Math::DegToRad( 1.5 ); // TODO: tune better?

        float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
        float tana = pixelSize * curvature; // tana = pixelSize / curvatureRadius = pixelSize * curvature
        tana *= parallaxInPixels * 2.0 * gFramerateScale;
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

                float3 Nprev00 = UnpackPrevNormal( p.x );
                float3 Nprev10 = UnpackPrevNormal( p.y );
                float3 Nprev01 = UnpackPrevNormal( p.z );
                float3 Nprev11 = UnpackPrevNormal( p.w );

                float3 Nprev = STL::Filtering::ApplyBilinearFilter( Nprev00, Nprev10, Nprev01, Nprev11, bilinearFilterAtPrevPrevVirtualPos );
                Nprev = normalize( Nprev );
            #else
                uint p = gIn_Prev_Normal_SpecAccumSpeed[ uint2( pixelUvVirtualPrevPrev * gRectSizePrev ) ];
                float3 Nprev = UnpackPrevNormal( p );
            #endif
            Nprev = STL::Geometry::RotateVector( gWorldPrevToWorld, Nprev );

            float maxAngle = lobeHalfAngle + avgCurvatureAngle * ( 1.0 + t ); // TODO: clamp to 90 deg?
            virtualMotionNormalWeight *= IsInScreen( pixelUvVirtualPrevPrev ) ? GetEncodingAwareNormalWeight( N, Nprev, maxAngle ) : 1.0;
        }

        float virtualMotionLengthInPixels = length( virtualMotionDelta * gRectSizePrev );
        virtualMotionNormalWeight = lerp( 1.0, virtualMotionNormalWeight, saturate( virtualMotionLengthInPixels / 0.5 ) );

        virtualHistoryConfidence *= virtualMotionNormalWeight;
        virtualHistoryAmount *= lerp( 0.333, 1.0, virtualMotionNormalWeight ); // TODO: should depend on virtualHistoryAmount and accumSpeed?

        // Virtual motion - accumulation acceleration
        float responsiveAccumulationAmount = GetResponsiveAccumulationAmount( roughness );
        float specAccumSpeedVirtual = GetSpecAccumSpeed( specAccumSpeedUpperBound, lerp( 1.0, roughnessModified, responsiveAccumulationAmount ), 0.99999, 0.0 );

        float specAccumSpeedScale = lerp( saturate( gFramerateScale * gFramerateScale ), 1.0, virtualHistoryConfidence );
        specAccumSpeedScale *= lerp( 0.85, 1.0, virtualMotionHitDistWeight );
        specAccumSpeedScale *= lerp( 0.7, 1.0, virtualMotionNormalWeight );

        float specMinAccumSpeed = min( specAccumSpeedVirtual, REBLUR_FIXED_FRAME_NUM );
        specAccumSpeedVirtual = lerp( specMinAccumSpeed, specAccumSpeedVirtual, specAccumSpeedScale );

        // Virtual history clamping
        float sigmaScale = lerp( 1.0, 3.0, smc ) * ( 1.0 + 2.0 * smc * REBLUR_TS_SIGMA_AMPLITUDE * virtualHistoryConfidence );
        float unclampedVirtualHistoryAmount = lerp( virtualHistoryConfidence, 1.0, smc * STL::Math::SmoothStep( 0.2, 0.4, roughnessModified ) );
        unclampedVirtualHistoryAmount *= lerp( 1.0, smc * 0.5 + 0.5, responsiveAccumulationAmount );
        float4 specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual, REBLUR_USE_COLOR_CLAMPING_AABB );
        float4 specHistoryVirtualMixed = lerp( specHistoryVirtualClamped, specHistoryVirtual, unclampedVirtualHistoryAmount );

        // Fallback to surface motion if virtual motion doesn't go well
        virtualHistoryAmount *= saturate( ( specAccumSpeedVirtual + 1.0 ) / ( specAccumSpeedSurface * smc + 1.0 ) );

        // Final composition
        float specAccumSpeed = InterpolateAccumSpeeds( specAccumSpeedSurface, specAccumSpeedVirtual, virtualHistoryAmount );
        float specAccumSpeedNonLinear = 1.0 / ( min( specAccumSpeed, gMaxAccumulatedFrameNum ) + 1.0 );

        float4 specHistory = lerp( specHistorySurface, specHistoryVirtualMixed, virtualHistoryAmount );
        float4 specResult = MixHistoryAndCurrent( specHistory, spec, specAccumSpeedNonLinear, roughnessModified );

        // Suppress fireflies - helps to avoid false-positive antilag reactions
        float4 specResultClamped = min( specResult, specHistory * REBLUR_MAX_FIREFLY_RELATIVE_INTENSITY );
        float unclampedAmount = lerp( roughnessModified * roughnessModified, 1.0, specAccumSpeedNonLinear );
        specResult = lerp( specResultClamped, specResult, gBlurRadius != 0 ? unclampedAmount : 1.0 );

        // Output
        gOut_Spec[ pixelPos ] = specResult;

        virtualHistoryConfidence = lerp( 1.0, virtualHistoryConfidence, virtualHistoryAmount );
        virtualHistoryConfidence = lerp( virtualHistoryConfidence, 1.0, specAccumSpeedNonLinear );

        float specError = GetColorErrorForAdaptiveRadiusScale( specResult, specHistory, specAccumSpeedNonLinear, roughnessModified, 0, false );
        float specAccumSpeedFinal = min( specMaxAccumSpeedNoConfidence, max( specAccumSpeed, REBLUR_USE_HISTORY_FIX_WITHOUT_DISOCCLUSION ? 0 : REBLUR_FIXED_FRAME_NUM ) );

        gOut_SpecData[ pixelPos ] = float4( specError, virtualHistoryConfidence, virtualHistoryAmount, hitDistScaleForTracking );
    #else
        float specAccumSpeedFinal = 0;
    #endif

    // Internal data
    gOut_InternalData[ pixelPos ] = PackDiffSpecInternalData( diffAccumSpeedFinal, specAccumSpeedFinal, curvature, viewZ, fbits );
}
