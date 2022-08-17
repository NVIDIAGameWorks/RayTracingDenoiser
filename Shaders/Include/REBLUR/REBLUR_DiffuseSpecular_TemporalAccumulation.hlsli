/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float4 s_Normal_MinHitDist[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );
    uint2 globalIdUser = gRectOrigin + globalPos;

    float4 temp = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );

    #ifdef REBLUR_SPECULAR
        #ifdef REBLUR_OCCLUSION
            uint shift = gSpecCheckerboard != 2 ? 1 : 0;
            uint2 pos = uint2( globalPos.x >> shift, globalPos.y ) + gRectOrigin;
        #else
            uint2 pos = globalPos + ( gIsPrepassEnabled ? 0 : gRectOrigin );
        #endif

        float minHitDist = ExtractHitDist( gIn_Spec[ pos ] );
        #ifndef REBLUR_OCCLUSION
            minHitDist = gSpecPrepassBlurRadius != 0.0 ? gIn_Spec_MinHitDist[ globalPos ] : minHitDist;
        #endif

        temp.w = minHitDist;
    #endif

    s_Normal_MinHitDist[ sharedPos.y ][ sharedPos.x ] = temp;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );

    [branch]
    if( viewZ > gDenoisingRange )
    {
        #ifdef REBLUR_OCCLUSION
            #ifdef REBLUR_DIFFUSE
                gOut_Diff[ pixelPos ] = float2( 0, PackViewZ( viewZ ) );
            #endif

            #ifdef REBLUR_SPECULAR
                gOut_Spec[ pixelPos ] = float2( 0, PackViewZ( viewZ ) );
            #endif
        #endif

        return;
    }

    // Current position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float3 X = STL::Geometry::RotateVector( gViewToWorld, Xv );

    // Analyze neighbors
    int2 smemPos = threadPos + BORDER;
    float4 Navg = s_Normal_MinHitDist[ smemPos.y ][ smemPos.x ];

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadPos + int2( dx, dy );
            float4 t = s_Normal_MinHitDist[ pos.y ][ pos.x ];

            #ifdef REBLUR_SPECULAR
                Navg.w = min( Navg.w, t.w );
            #endif

            Navg.xyz += t.xyz;
        }
    }

    Navg.xyz /= 9.0; // needs to be unnormalized!

    // Normal and roughness
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    #ifdef REBLUR_SPECULAR
        float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg.xyz ); // TODO: needed?
    #endif

    // Previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser ] * gMotionVectorScale.xyy;
    float2 smbPixelUv = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gIsWorldSpaceMotionEnabled );
    float isInScreen = IsInScreen( smbPixelUv );
    float3 Xprev = X + motionVector * float( gIsWorldSpaceMotionEnabled != 0 );

    // Curvature ( tests 15, 40, 76, 133, 146, 147 )
    float curvature = 0;
    #ifdef REBLUR_SPECULAR
        float3 cameraMotion3d = Xprev - X - gCameraDelta.xyz;
        float2 cameraMotion2d = STL::Geometry::RotateVectorInverse( gViewToWorld, cameraMotion3d ).xy;
        float mvLen = length( cameraMotion2d );
        cameraMotion2d = mvLen > 1e-7 ? cameraMotion2d / mvLen : float2( 1, 0 );
        cameraMotion2d *= 0.5 * gInvRectSize;

        [unroll]
        for( int dir = -1; dir <= 1; dir += 2 )
        {
            float2 uv = pixelUv + dir * cameraMotion2d;
            STL::Filtering::Bilinear f = STL::Filtering::GetBilinearFilter( uv, gRectSize );

            int2 pos = threadPos + BORDER + uint2( f.origin ) - pixelPos;
            float3 n00 = s_Normal_MinHitDist[ pos.y ][ pos.x ].xyz;
            float3 n10 = s_Normal_MinHitDist[ pos.y ][ pos.x + 1 ].xyz;
            float3 n01 = s_Normal_MinHitDist[ pos.y + 1 ][ pos.x ].xyz;
            float3 n11 = s_Normal_MinHitDist[ pos.y + 1 ][ pos.x + 1 ].xyz;

            float3 n = STL::Filtering::ApplyBilinearFilter( n00, n10, n01, n11, f );
            n = normalize( n );

            float3 xv = STL::Geometry::ReconstructViewPosition( uv, gFrustum, 1.0, gOrthoMode );
            float3 x = STL::Geometry::RotateVector( gViewToWorld, xv );
            float3 v = GetViewVector( x );

            curvature += EstimateCurvature( n, v, N, X );
        }

        curvature *= 0.5;
    #endif

    // Previous viewZ ( 4x4, surface motion )
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
    STL::Filtering::CatmullRom smbCatromFilter = STL::Filtering::GetCatmullRomFilter( saturate( smbPixelUv ), gRectSizePrev );
    float2 smbCatromGatherUv = smbCatromFilter.origin * gInvScreenSize;
    float4 smbViewZ0 = gIn_Prev_ViewZ.GatherRed( gNearestClamp, smbCatromGatherUv, float2( 1, 1 ) ).wzxy;
    float4 smbViewZ1 = gIn_Prev_ViewZ.GatherRed( gNearestClamp, smbCatromGatherUv, float2( 3, 1 ) ).wzxy;
    float4 smbViewZ2 = gIn_Prev_ViewZ.GatherRed( gNearestClamp, smbCatromGatherUv, float2( 1, 3 ) ).wzxy;
    float4 smbViewZ3 = gIn_Prev_ViewZ.GatherRed( gNearestClamp, smbCatromGatherUv, float2( 3, 3 ) ).wzxy;

    float3 prevViewZ0 = UnpackViewZ( smbViewZ0.yzw );
    float3 prevViewZ1 = UnpackViewZ( smbViewZ1.xzw );
    float3 prevViewZ2 = UnpackViewZ( smbViewZ2.xyw );
    float3 prevViewZ3 = UnpackViewZ( smbViewZ3.xyz );

    // Previous normal averaged for all pixels in 2x2 footprint
    STL::Filtering::Bilinear smbBilinearFilter = STL::Filtering::GetBilinearFilter( saturate( smbPixelUv ), gRectSizePrev );

    float2 smbBilinearGatherUv = ( smbBilinearFilter.origin + 1.0 ) * gInvScreenSize;
    float3 prevNflat = UnpackNormalAndRoughness( gIn_Prev_Normal_Roughness.SampleLevel( gLinearClamp, smbBilinearGatherUv, 0 ) ).xyz;
    prevNflat = STL::Geometry::RotateVector( gWorldPrevToWorld, prevNflat );

    // Previous accum speed and materialID // TODO: 4x4 materialID footprint is reduced to 2x2 only
    uint4 smbPackedAccumSpeedMaterialID = gIn_Prev_AccumSpeeds_MaterialID.GatherRed( gNearestClamp, smbBilinearGatherUv ).wzxy;

    float3 prevAccumSpeedMaterialID00 = UnpackAccumSpeedsMaterialID( smbPackedAccumSpeedMaterialID.x );
    float3 prevAccumSpeedMaterialID10 = UnpackAccumSpeedsMaterialID( smbPackedAccumSpeedMaterialID.y );
    float3 prevAccumSpeedMaterialID01 = UnpackAccumSpeedsMaterialID( smbPackedAccumSpeedMaterialID.z );
    float3 prevAccumSpeedMaterialID11 = UnpackAccumSpeedsMaterialID( smbPackedAccumSpeedMaterialID.w );

    float4 prevDiffAccumSpeeds = float4( prevAccumSpeedMaterialID00.x, prevAccumSpeedMaterialID10.x, prevAccumSpeedMaterialID01.x, prevAccumSpeedMaterialID11.x );
    float4 prevSpecAccumSpeeds = float4( prevAccumSpeedMaterialID00.y, prevAccumSpeedMaterialID10.y, prevAccumSpeedMaterialID01.y, prevAccumSpeedMaterialID11.y );
    float4 prevMaterialIDs = float4( prevAccumSpeedMaterialID00.z, prevAccumSpeedMaterialID10.z, prevAccumSpeedMaterialID01.z, prevAccumSpeedMaterialID11.z );

    // Parallax
    float smbParallax = ComputeParallax( Xprev - gCameraDelta.xyz, gOrthoMode == 0.0 ? pixelUv : smbPixelUv, gWorldToClip, gRectSize, gUnproject, gOrthoMode );
    float smbParallaxInPixels = GetParallaxInPixels( smbParallax, gUnproject );

    // Plane distance based disocclusion for surface motion
    float3 V = GetViewVector( X );
    float NoV = abs( dot( N, V ) );
    float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
    float mvLengthFactor = saturate( smbParallaxInPixels / 0.5 );
    float frontFacing = lerp( cos( STL::Math::DegToRad( 135.0 ) ), cos( STL::Math::DegToRad( 91.0 ) ), mvLengthFactor );
    float NavgLen = length( Navg.xyz );
    float disocclusionThreshold = ( isInScreen && dot( prevNflat, Navg.xyz ) > frontFacing * NavgLen ) ? gDisocclusionThreshold : -1.0; // ignore out of screen and backfacing
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoVmod = NoV / frustumHeight; // normalize here to save ALU
    float NoVmodMulXvprevz = Xvprev.z * NoVmod;
    float3 planeDist0 = abs( prevViewZ0 * NoVmod - NoVmodMulXvprevz );
    float3 planeDist1 = abs( prevViewZ1 * NoVmod - NoVmodMulXvprevz );
    float3 planeDist2 = abs( prevViewZ2 * NoVmod - NoVmodMulXvprevz );
    float3 planeDist3 = abs( prevViewZ3 * NoVmod - NoVmodMulXvprevz );
    float3 smbOcclusion0 = step( planeDist0, disocclusionThreshold );
    float3 smbOcclusion1 = step( planeDist1, disocclusionThreshold );
    float3 smbOcclusion2 = step( planeDist2, disocclusionThreshold );
    float3 smbOcclusion3 = step( planeDist3, disocclusionThreshold );

    float4 smbOcclusionWeights = STL::Filtering::GetBilinearCustomWeights( smbBilinearFilter, float4( smbOcclusion0.z, smbOcclusion1.y, smbOcclusion2.y, smbOcclusion3.x ) );
    bool smbIsCatromAllowed = dot( smbOcclusion0 + smbOcclusion1 + smbOcclusion2 + smbOcclusion3, 1.0 ) > 11.5 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA;

    float footprintQuality = STL::Filtering::ApplyBilinearFilter( smbOcclusion0.z, smbOcclusion1.y, smbOcclusion2.y, smbOcclusion3.x, smbBilinearFilter );
    footprintQuality = STL::Math::Sqrt01( footprintQuality );

    // Material ID check
    float4 materialCmps = CompareMaterials( materialID, prevMaterialIDs, 1 );
    smbOcclusion0.z *= materialCmps.x;
    smbOcclusion1.y *= materialCmps.y;
    smbOcclusion2.y *= materialCmps.z;
    smbOcclusion3.x *= materialCmps.w;

    float4 smbOcclusionWeightsWithMaterialID = STL::Filtering::GetBilinearCustomWeights( smbBilinearFilter, float4( smbOcclusion0.z, smbOcclusion1.y, smbOcclusion2.y, smbOcclusion3.x ) );
    bool smbIsCatromAllowedWithMaterialID = dot( smbOcclusion0 + smbOcclusion1 + smbOcclusion2 + smbOcclusion3, 1.0 ) > 11.5 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA;

    float footprintQualityWithMaterialID = STL::Filtering::ApplyBilinearFilter( smbOcclusion0.z, smbOcclusion1.y, smbOcclusion2.y, smbOcclusion3.x, smbBilinearFilter );
    footprintQualityWithMaterialID = STL::Math::Sqrt01( footprintQualityWithMaterialID );

    // Avoid footprint momentary stretching due to changed viewing angle
    float3 Vprev = GetViewVectorPrev( Xprev, gCameraDelta.xyz );
    float NoVprev = abs( dot( N, Vprev ) ); // TODO: should be prevNflat, but jittering breaks logic
    float sizeQuality = ( NoVprev + 1e-3 ) / ( NoV + 1e-3 ); // this order because we need to fix stretching only, shrinking is OK
    sizeQuality *= sizeQuality;
    sizeQuality = lerp( 0.1, 1.0, saturate( sizeQuality ) );
    footprintQuality *= sizeQuality;
    footprintQualityWithMaterialID *= sizeQuality;

    // Bits
    float fbits = smbIsCatromAllowed * 2.0;
    fbits += smbOcclusion0.z * 4.0 + smbOcclusion1.y * 8.0 + smbOcclusion2.y * 16.0 + smbOcclusion3.x * 32.0;

    // Update accumulation speeds
    #ifdef REBLUR_DIFFUSE
        float diffAccumSpeed = AdvanceAccumSpeed( prevDiffAccumSpeeds, gDiffMaterialMask ? smbOcclusionWeightsWithMaterialID : smbOcclusionWeights );
        diffAccumSpeed *= lerp( gDiffMaterialMask ? footprintQualityWithMaterialID : footprintQuality, 1.0, 1.0 / ( 1.0 + diffAccumSpeed ) );

        #ifdef REBLUR_HAS_CONFIDENCE
            float diffConfidence = gIn_Diff_Confidence[ pixelPosUser ];
            diffAccumSpeed *= lerp( diffConfidence, 1.0, 1.0 / ( 1.0 + diffAccumSpeed ) );
        #endif
    #endif

    #ifdef REBLUR_SPECULAR
        float specAccumSpeed = AdvanceAccumSpeed( prevSpecAccumSpeeds, gSpecMaterialMask ? smbOcclusionWeightsWithMaterialID : smbOcclusionWeights );
        specAccumSpeed *= lerp( gSpecMaterialMask ? footprintQualityWithMaterialID : footprintQuality, 1.0, 1.0 / ( 1.0 + specAccumSpeed ) );

        #ifdef REBLUR_HAS_CONFIDENCE
            float specConfidence = gIn_Spec_Confidence[ pixelPosUser ];
            specAccumSpeed *= lerp( specConfidence, 1.0, 1.0 / ( 1.0 + specAccumSpeed ) );
        #endif
    #endif

    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );
    #ifdef REBLUR_OCCLUSION
        int3 checkerboardPos = pixelPosUser.xyx + int3( -1, 0, 1 );
        float viewZ0 = gIn_ViewZ[ checkerboardPos.xy ];
        float viewZ1 = gIn_ViewZ[ checkerboardPos.zy ];
        float2 wc = GetBilateralWeight( float2( viewZ0, viewZ1 ), viewZ );
        wc *= STL::Math::PositiveRcp( wc.x + wc.y );
    #endif

    // Diffuse
    #ifdef REBLUR_DIFFUSE
        bool diffHasData = gDiffCheckerboard == 2 || checkerboard == gDiffCheckerboard;
        #ifdef REBLUR_OCCLUSION
            uint diffShift = gDiffCheckerboard != 2 ? 1 : 0;
            uint2 diffPos = uint2( pixelPos.x >> diffShift, pixelPos.y ) + gRectOrigin;
        #else
            uint2 diffPos = pixelPos;
        #endif

        REBLUR_TYPE diff = gIn_Diff[ diffPos ];
        #ifdef REBLUR_SH
            float4 diffSh = gIn_DiffSh[ diffPos ];
        #endif

        // Sample history - surface motion
        REBLUR_TYPE smbDiffHistory;
        float4 smbDiffShHistory;
        float smbDiffFastHistory;
        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( smbPixelUv ) * gRectSizePrev, gInvScreenSize,
            gDiffMaterialMask ? smbOcclusionWeightsWithMaterialID : smbOcclusionWeights, gDiffMaterialMask ? smbIsCatromAllowedWithMaterialID : smbIsCatromAllowed,
            gIn_Diff_History, smbDiffHistory
            #ifdef REBLUR_SH
                , gIn_DiffSh_History, smbDiffShHistory
            #endif
            #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
                , gIn_DiffFast_History, smbDiffFastHistory
            #endif
        );

        // Avoid negative values
        smbDiffHistory = ClampNegativeToZero( smbDiffHistory );

        // Accumulation with checkerboard resolve // TODO: materialID support?
        #ifdef REBLUR_OCCLUSION
            float d0 = gIn_Diff[ uint2( ( pixelPos.x - 1 ) >> diffShift, pixelPos.y ) + gRectOrigin ];
            float d1 = gIn_Diff[ uint2( ( pixelPos.x + 1 ) >> diffShift, pixelPos.y ) + gRectOrigin ];

            if( !diffHasData )
            {
                diff *= saturate( 1.0 - wc.x - wc.y );
                diff += d0 * wc.x + d1 * wc.y;
            }
        #endif

        float diffAccumSpeedNonLinear = 1.0 / ( min( diffAccumSpeed, gMaxAccumulatedFrameNum ) + 1.0 );
        if( !diffHasData )
            diffAccumSpeedNonLinear *= 1.0 - gCheckerboardResolveAccumSpeed * diffAccumSpeed / ( 1.0 + diffAccumSpeed );

        REBLUR_TYPE diffResult = MixHistoryAndCurrent( smbDiffHistory, diff, diffAccumSpeedNonLinear );
        #ifdef REBLUR_SH
            float4 diffShResult = MixHistoryAndCurrent( smbDiffShHistory, diffSh, diffAccumSpeedNonLinear );
        #endif

        // Internal data
        float diffError = GetColorErrorForAdaptiveRadiusScale( diffResult, smbDiffHistory, diffAccumSpeed );

        // Output
        #ifdef REBLUR_OCCLUSION
            gOut_Diff[ pixelPos ] = float2( diffResult, PackViewZ( viewZ ) );
        #else
            gOut_Diff[ pixelPos ] = diffResult;
            #ifdef REBLUR_SH
                gOut_DiffSh[ pixelPos ] = diffShResult;
            #endif
        #endif

        // Fast history
        #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
            float diffFastAccumSpeed = min( diffAccumSpeed, gMaxFastAccumulatedFrameNum );
            float diffFastAccumSpeedNonLinear = 1.0 / ( diffFastAccumSpeed + 1.0 );
            if( !diffHasData )
                diffFastAccumSpeedNonLinear *= 1.0 - gCheckerboardResolveAccumSpeed * diffAccumSpeed / ( 1.0 + diffAccumSpeed );

            smbDiffFastHistory = diffAccumSpeed < gMaxFastAccumulatedFrameNum ? GetLuma( smbDiffHistory ) : smbDiffFastHistory;

            float diffFastResult = lerp( smbDiffFastHistory, GetLuma( diff ), diffFastAccumSpeedNonLinear );

            gOut_DiffFast[ pixelPos ] = diffFastResult;
        #endif
    #else
        float diffAccumSpeed = 0;
        float diffError = 0;
    #endif

    // Specular
    #ifdef REBLUR_SPECULAR
        bool specHasData = gSpecCheckerboard == 2 || checkerboard == gSpecCheckerboard;
        #ifdef REBLUR_OCCLUSION
            uint specShift = gSpecCheckerboard != 2 ? 1 : 0;
            uint2 specPos = uint2( pixelPos.x >> specShift, pixelPos.y ) + gRectOrigin;
        #else
            uint2 specPos = pixelPos;
        #endif

        REBLUR_TYPE spec = gIn_Spec[ specPos ];
        #ifdef REBLUR_SH
            float4 specSh = gIn_SpecSh[ specPos ];
        #endif

        // Checkerboard resolve // TODO: materialID support?
        #ifdef REBLUR_OCCLUSION
            float s0 = gIn_Spec[ uint2( ( pixelPos.x - 1 ) >> specShift, pixelPos.y ) + gRectOrigin ];
            float s1 = gIn_Spec[ uint2( ( pixelPos.x + 1 ) >> specShift, pixelPos.y ) + gRectOrigin ];

            if( !specHasData )
            {
                spec *= saturate( 1.0 - wc.x - wc.y );
                spec += s0 * wc.x + s1 * wc.y;
            }
        #endif

        // Hit distance for tracking ( tests 8, 110, 139 )
        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
        float hitDistForTracking = Navg.w * hitDistScale;

        // Virtual motion
        float dominantFactor = STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughness, STL_SPECULAR_DOMINANT_DIRECTION_G2 );
        float3 Xvirtual = GetXvirtual( NoV, hitDistForTracking, curvature, X, Xprev, V, dominantFactor );
        float2 vmbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual, false );

        // Adjust curvature if curvature sign oscillation is forseen
        float pixelsBetweenSurfaceAndVirtualMotion = length( ( vmbPixelUv - smbPixelUv ) * gRectSize );
        float curvatureCorrectionThreshold = smbParallaxInPixels + gInvRectSize.x;
        float curvatureCorrection = STL::Math::SmoothStep( 2.0 * curvatureCorrectionThreshold, curvatureCorrectionThreshold, pixelsBetweenSurfaceAndVirtualMotion );
        curvature *= curvatureCorrection;

        Xvirtual = GetXvirtual( NoV, hitDistForTracking, curvature, X, Xprev, V, dominantFactor );
        vmbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual, false );

        // Virtual history amount - base
        float virtualHistoryAmount = IsInScreen( vmbPixelUv );
        virtualHistoryAmount *= 1.0 - gReference;
        virtualHistoryAmount *= dominantFactor;

        // Virtual motion amount - surface
        STL::Filtering::Bilinear vmbBilinearFilter = STL::Filtering::GetBilinearFilter( saturate( vmbPixelUv ), gRectSizePrev );
        float2 vmbBilinearGatherUv = ( vmbBilinearFilter.origin + 1.0 ) * gInvScreenSize;
        float4 vmbViewZs = UnpackViewZ( gIn_Prev_ViewZ.GatherRed( gNearestClamp, vmbBilinearGatherUv ).wzxy );
        float3 vmbV = STL::Geometry::ReconstructViewPosition( vmbPixelUv, gFrustumPrev, 1.0 ); // unnormalized
        float3 Nvprev = STL::Geometry::RotateVector( gWorldToViewPrev, N );
        float NoXreal = dot( Nvprev, Xvprev );
        float4 NoX = ( Nvprev.x * vmbV.x + Nvprev.y * vmbV.y ) * ( gOrthoMode == 0 ? vmbViewZs : gOrthoMode ) + Nvprev.z * vmbV.z * vmbViewZs;
        float4 planeDist = abs( NoX - NoXreal ) / frustumHeight;
        float4 vmbOcclusion = step( planeDist, disocclusionThreshold );
        float virtualMotionSurfaceWeight = STL::Filtering::ApplyBilinearFilter( vmbOcclusion.x, vmbOcclusion.y, vmbOcclusion.z, vmbOcclusion.w, vmbBilinearFilter );
        virtualHistoryAmount *= virtualMotionSurfaceWeight;

        // Virtual motion amount - normal
        float lobeHalfAngle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
        lobeHalfAngle += NRD_NORMAL_ENCODING_ERROR + STL::Math::DegToRad( 1.5 ); // TODO: tune better?

        float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
        float curvatureAngleTan = pixelSize * abs( curvature ); // tana = pixelSize / curvatureRadius = pixelSize * curvature
        curvatureAngleTan *= 1.0 + smbParallaxInPixels; // path length
        curvatureAngleTan *= gFramerateScale; // "pixels / frame" to "pixels / sec"
        float curvatureAngle = STL::Math::AtanApprox( saturate( curvatureAngleTan ) );

        float4 vmbNormalAndRoughness = UnpackNormalAndRoughness( gIn_Prev_Normal_Roughness.SampleLevel( gLinearClamp, vmbPixelUv * gRectSizePrev * gInvScreenSize, 0 ) );
        float3 vmbN = STL::Geometry::RotateVector( gWorldPrevToWorld, vmbNormalAndRoughness.xyz );

        float angle = lobeHalfAngle + curvatureAngle;
        float virtualMotionNormalWeight = GetEncodingAwareNormalWeight( N, vmbN, angle );
        virtualHistoryAmount *= virtualMotionNormalWeight;

        // Virtual motion amount - roughness
        float vmbRoughness = vmbNormalAndRoughness.w;
        float roughnessFraction = lerp( 0.2, 1.0, STL::BRDF::Pow5( NoV ) );
        float virtualMotionRoughnessWeight = GetEncodingAwareRoughnessWeights( roughness, vmbRoughness, roughnessFraction );
        virtualHistoryAmount *= virtualMotionRoughnessWeight;

        // Sample history  surface motion
        REBLUR_TYPE smbSpecHistory;
        float4 smbSpecShHistory;
        float smbSpecFastHistory;
        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( smbPixelUv ) * gRectSizePrev, gInvScreenSize,
            gSpecMaterialMask ? smbOcclusionWeightsWithMaterialID : smbOcclusionWeights, gSpecMaterialMask ? smbIsCatromAllowedWithMaterialID : smbIsCatromAllowed,
            gIn_Spec_History, smbSpecHistory
            #ifdef REBLUR_SH
                , gIn_SpecSh_History, smbSpecShHistory
            #endif
            #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
                , gIn_SpecFast_History, smbSpecFastHistory
            #endif
        );

        // Sample history - virtual motion
        float4 vmbOcclusionWeights = STL::Filtering::GetBilinearCustomWeights( vmbBilinearFilter, vmbOcclusion );

        bool vmbIsCatromAllowed = dot( vmbOcclusion, 1.0 ) > 3.5;
        vmbIsCatromAllowed = vmbIsCatromAllowed & smbIsCatromAllowed;
        vmbIsCatromAllowed = vmbIsCatromAllowed & REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA;

        REBLUR_TYPE vmbSpecHistory;
        float4 vmbSpecShHistory;
        float vmbSpecFastHistory;
        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( vmbPixelUv ) * gRectSizePrev, gInvScreenSize,
            vmbOcclusionWeights, vmbIsCatromAllowed,
            gIn_Spec_History, vmbSpecHistory
            #ifdef REBLUR_SH
                , gIn_SpecSh_History, vmbSpecShHistory
            #endif
            #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
                , gIn_SpecFast_History, vmbSpecFastHistory
            #endif
        );

        // Avoid negative values
        smbSpecHistory = ClampNegativeToZero( smbSpecHistory );
        vmbSpecHistory = ClampNegativeToZero( vmbSpecHistory );

        // Virtual motion confidence - virtual position ( tests 3, 6, 8, 11, 14, 100, 103, 104, 106, 109, 110, 114, 120, 127, 130, 131, 138, 139 )
        float smbHitDist = ExtractHitDist( lerp( spec, smbSpecHistory, STL::Math::SmoothStep( 0.04, 0.11, roughnessModified ) ) ); // see tests 114 and 138
        smbHitDist *= hitDistScale;

        float vmbHitDist = ExtractHitDist( vmbSpecHistory );
        vmbHitDist *= _REBLUR_GetHitDistanceNormalization( dot( vmbViewZs, 0.25 ), gHitDistParams, vmbRoughness );

        float3 smbXvirtual = GetXvirtual( NoV, smbHitDist, curvature, X, Xprev, V, dominantFactor );
        float2 uv1 = STL::Geometry::GetScreenUv( gWorldToClipPrev, smbXvirtual, false );
        float3 vmbXvirtual = GetXvirtual( NoV, vmbHitDist, curvature, X, Xprev, V, dominantFactor );
        float2 uv2 = STL::Geometry::GetScreenUv( gWorldToClipPrev, vmbXvirtual, false );

        float hitDistDeltaScale = min( specAccumSpeed, 10.0 ) * lerp( 1.0, 0.5, saturate( gSpecPrepassBlurRadius / 5.0 ) ); // TODO: is it possible to tune better?
        float deltaParallaxInPixels = length( ( uv1 - uv2 ) * gRectSize );
        float virtualMotionHitDistWeight = saturate( 1.0 - hitDistDeltaScale * deltaParallaxInPixels );

        // Virtual motion confidence - fixing trails if radiance on a flat surface is taken from a sloppy surface
        float2 virtualMotionDelta = vmbPixelUv - smbPixelUv;
        virtualMotionDelta *= STL::Math::Rsqrt( STL::Math::LengthSquared( virtualMotionDelta ) );
        virtualMotionDelta /= gRectSizePrev;
        virtualMotionDelta *= saturate( smbParallaxInPixels / 0.1 ) + smbParallaxInPixels;

        float virtualMotionPrevPrevWeight = 1.0;
        [unroll]
        for( uint i = 1; i <= REBLUR_VIRTUAL_MOTION_NORMAL_WEIGHT_ITERATION_NUM; i++ )
        {
            float2 vmbPixelUvPrev = vmbPixelUv + virtualMotionDelta * i;

            float4 vmbNormalAndRoughnessPrev = UnpackNormalAndRoughness( gIn_Prev_Normal_Roughness.SampleLevel( gLinearClamp, vmbPixelUvPrev * gRectSizePrev * gInvScreenSize, 0 ) );
            float3 vmbNprev = STL::Geometry::RotateVector( gWorldPrevToWorld, vmbNormalAndRoughnessPrev.xyz );

            float w = GetEncodingAwareNormalWeight( N, vmbNprev, angle + curvatureAngle * i );

            float wr = GetEncodingAwareRoughnessWeights( roughness, vmbNormalAndRoughnessPrev.w, roughnessFraction );
            w *= lerp( 0.25 * i, 1.0, wr );

            // Ignore pixels from distant surfaces
            // TODO: with this addition test 3e shows a leak from the bright wall
            float vmbZprev = UnpackViewZ( gIn_Prev_ViewZ.SampleLevel( gLinearClamp, vmbPixelUvPrev * gRectSizePrev * gInvScreenSize, 0 ) );
            float wz = GetBilateralWeight( vmbZprev, Xvprev.z );
            w = lerp( 1.0, w, wz );

            virtualMotionPrevPrevWeight *= IsInScreen( vmbPixelUvPrev ) ? w : 1.0;
            virtualMotionRoughnessWeight *= wr;
        }

        // Virtual motion - accumulation acceleration
        float responsiveAccumulationAmount = GetResponsiveAccumulationAmount( roughness );
        float vmbSpecAccumSpeed = GetSpecAccumSpeed( specAccumSpeed, lerp( 1.0, roughnessModified, responsiveAccumulationAmount ), 0.99999, 0.0, 0.0, 1.0 );

        float smc = GetSpecMagicCurve2( roughnessModified, 0.97 );
        vmbSpecAccumSpeed *= lerp( smc, 1.0, virtualMotionHitDistWeight );
        vmbSpecAccumSpeed *= virtualMotionPrevPrevWeight;

        // Surface motion - allow more accumulation in regions with low virtual motion confidence ( test 9 )
        float roughnessBoost = ( 0.1 + 0.3 * roughnessModified ) * ( 1.0 - roughnessModified );
        float roughnessBoostAmount = virtualHistoryAmount * ( 1.0 - virtualMotionRoughnessWeight );
        float roughnessBoosted = roughnessModified + roughnessBoost * roughnessBoostAmount;
        float smbSpecAccumSpeed = GetSpecAccumSpeed( specAccumSpeed, roughnessBoosted, NoV, smbParallax, curvature, viewZ );

        // Fallback to surface motion if virtual motion doesn't go well ( tests 103, 111, 132, e9, e11 )
        virtualHistoryAmount *= saturate( ( vmbSpecAccumSpeed + 0.1 ) / ( smbSpecAccumSpeed + 0.1 ) );

        // Accumulation with checkerboard resolve // TODO: materialID support?
        specAccumSpeed = InterpolateAccumSpeeds( smbSpecAccumSpeed, vmbSpecAccumSpeed, virtualHistoryAmount );

        float specAccumSpeedNonLinear = 1.0 / ( min( specAccumSpeed, gMaxAccumulatedFrameNum ) + 1.0 );
        if( !specHasData )
            specAccumSpeedNonLinear *= 1.0 - gCheckerboardResolveAccumSpeed * specAccumSpeed / ( 1.0 + specAccumSpeed );

        REBLUR_TYPE specHistory = lerp( smbSpecHistory, vmbSpecHistory, virtualHistoryAmount );
        REBLUR_TYPE specResult = MixHistoryAndCurrent( specHistory, spec, specAccumSpeedNonLinear, roughnessModified );
        #ifdef REBLUR_SH
            float4 specShHistory = lerp( smbSpecShHistory, vmbSpecShHistory, virtualHistoryAmount );
            float4 specShResult = MixHistoryAndCurrent( specShHistory, specSh, specAccumSpeedNonLinear, roughnessModified );
        #endif

        // Anti-firefly suppressor
        float antifireflyFactor = lerp( roughnessModified * roughnessModified, 1.0, gBlurRadius != 0 ? specAccumSpeedNonLinear : 1.0 );
        #ifdef REBLUR_OCCLUSION
            float lumaResultClamped = min( specResult, specHistory * REBLUR_MAX_FIREFLY_RELATIVE_INTENSITY.y );
            specResult = lerp( lumaResultClamped, specResult, antifireflyFactor );
        #else
            float lumaResultClamped = min( specResult.w, specHistory.w * REBLUR_MAX_FIREFLY_RELATIVE_INTENSITY.y );
            specResult.w = lerp( lumaResultClamped, specResult.w, antifireflyFactor );

            float lumaResult = GetLuma( specResult );
            float lumaHistory = GetLuma( specHistory );
            lumaResultClamped = min( lumaResult, lumaHistory * REBLUR_MAX_FIREFLY_RELATIVE_INTENSITY.x );
            specResult = ChangeLuma( specResult, lerp( lumaResultClamped, lumaResult, antifireflyFactor ) );
        #endif

        // Internal data
        #ifdef REBLUR_OCCLUSION
            float hitDistScaleForTracking = 1.0;
        #else
            float hitDistScaleForTracking = saturate( hitDistForTracking / ( specResult.w * hitDistScale + pixelSize ) );
        #endif

        float specError = GetColorErrorForAdaptiveRadiusScale( specResult, specHistory, specAccumSpeed, roughness );

        // Output
        #ifdef REBLUR_OCCLUSION
            gOut_Spec[ pixelPos ] = float2( specResult, PackViewZ( viewZ ) );
        #else
            gOut_Spec[ pixelPos ] = specResult;
            #ifdef REBLUR_SH
                gOut_SpecSh[ pixelPos ] = specShResult;
            #endif
        #endif

        // Fast history
        #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
            float specFastAccumSpeed = min( specAccumSpeed, gMaxFastAccumulatedFrameNum );
            float specFastAccumSpeedNonLinear = 1.0 / ( specFastAccumSpeed + 1.0 );
            if( !specHasData )
                specFastAccumSpeedNonLinear *= 1.0 - gCheckerboardResolveAccumSpeed * specAccumSpeed / ( 1.0 + specAccumSpeed );

            float specFastHistory = lerp( smbSpecFastHistory, vmbSpecFastHistory, virtualHistoryAmount );
            specFastHistory = specAccumSpeed < gMaxFastAccumulatedFrameNum ? GetLuma( specHistory ) : specFastHistory;

            float specFastResult = lerp( specFastHistory, GetLuma( spec ), specFastAccumSpeedNonLinear );

            gOut_SpecFast[ pixelPos ] = specFastResult;
        #endif
    #else
        float specAccumSpeed = 0;
        float specError = 0;
        float virtualHistoryAmount = 0;
        float hitDistScaleForTracking = 0;
    #endif

    // Internal data
    gOut_Data1[ pixelPos ] = PackInternalData1( diffAccumSpeed, diffError, specAccumSpeed, specError );

    #ifndef REBLUR_OCCLUSION
        gOut_Data2[ pixelPos ] = PackInternalData2( fbits, curvature, virtualHistoryAmount, hitDistScaleForTracking, viewZ );
    #endif
}
