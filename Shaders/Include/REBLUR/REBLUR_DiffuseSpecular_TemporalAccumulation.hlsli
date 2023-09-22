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

        REBLUR_TYPE spec = gIn_Spec[ pos ];
        #ifdef REBLUR_OCCLUSION
            temp.w = ExtractHitDist( spec );
        #else
            temp.w = gSpecPrepassBlurRadius == 0.0 ? ExtractHitDist( spec ) : gIn_Spec_HitDistForTracking[ globalPos ];
        #endif
    #endif

    s_Normal_MinHitDist[ sharedPos.y ][ sharedPos.x ] = temp;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    // Preload
    float isSky = gIn_Tiles[ pixelPos >> 4 ];
    PRELOAD_INTO_SMEM_WITH_TILE_CHECK;

    // Tile-based early out
    if( isSky != 0.0 || pixelPos.x >= gRectSize.x || pixelPos.y >= gRectSize.y )
        return;

    // Early out
    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );
    if( viewZ > gDenoisingRange )
        return;

    // Current position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float3 X = STL::Geometry::RotateVector( gViewToWorld, Xv );

    // Find hit distance for tracking and averaged normal
    int2 smemPos = threadPos + BORDER;
    float4 t = s_Normal_MinHitDist[ smemPos.y ][ smemPos.x ];
    float3 Navg = t.xyz;
    float hitDistForTracking = t.w == 0.0 ? NRD_INF : t.w;

    [unroll]
    for( j = 0; j <= BORDER * 2; j++ )
    {
        [unroll]
        for( i = 0; i <= BORDER * 2; i++ )
        {
            if( i == BORDER && j == BORDER )
                continue;

            int2 pos = threadPos + int2( i, j );
            float4 t = s_Normal_MinHitDist[ pos.y ][ pos.x ];

            if( i < 2 && j < 2 )
                Navg += t.xyz;

            #ifdef REBLUR_SPECULAR
                // Min hit distance for tracking, ignoring 0 values ( which still can be produced by VNDF sampling )
                hitDistForTracking = min( hitDistForTracking, t.w == 0.0 ? NRD_INF : t.w );
            #endif
        }
    }

    Navg /= 4.0; // needs to be unnormalized!

    // Normal and roughness
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    #ifdef REBLUR_SPECULAR
        float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg ); // TODO: needed?
    #endif

    // Hit distance for tracking ( tests 8, 110, 139, e3, e9 without normal map, e24 )
    #ifdef REBLUR_SPECULAR
        hitDistForTracking = hitDistForTracking == NRD_INF ? 0.0 : hitDistForTracking;

        #ifdef REBLUR_OCCLUSION
            hitDistForTracking *= _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
        #else
            hitDistForTracking *= gSpecPrepassBlurRadius == 0.0 ? _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness ) : 1.0;
        #endif

        gOut_Spec_HitDistForTracking[ pixelPos ] = hitDistForTracking;
    #endif

    // Previous position and surface motion uv
    float3 mv = gIn_Mv[ pixelPosUser ] * gMvScale;
    float3 Xprev = X;

    float2 smbPixelUv = pixelUv + mv.xy;
    if( gIsWorldSpaceMotionEnabled )
    {
        Xprev += mv;
        smbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xprev );
    }
    else if( gMvScale.z != 0.0 )
    {
        float viewZprev = viewZ + mv.z;
        float3 Xvprevlocal = STL::Geometry::ReconstructViewPosition( smbPixelUv, gFrustumPrev, viewZprev, gOrthoMode ); // TODO: use gOrthoModePrev

        Xprev = STL::Geometry::RotateVectorInverse( gWorldToViewPrev, Xvprevlocal ) + gCameraDelta;
    }

    // Parallax
    float smbParallaxInPixels = ComputeParallaxInPixels( Xprev - gCameraDelta, gOrthoMode == 0.0 ? pixelUv : smbPixelUv, gWorldToClip, gRectSize );

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
    // IMPORTANT: bilinear filter can touch sky pixels, due to this reason "Post Blur" writes special values into sky-pixels
    STL::Filtering::Bilinear smbBilinearFilter = STL::Filtering::GetBilinearFilter( saturate( smbPixelUv ), gRectSizePrev );

    float2 smbBilinearGatherUv = ( smbBilinearFilter.origin + 1.0 ) * gInvScreenSize;
    float3 prevNavg = UnpackNormalAndRoughness( gIn_Prev_Normal_Roughness.SampleLevel( gLinearClamp, smbBilinearGatherUv, 0 ), false ).xyz;
    prevNavg = STL::Geometry::RotateVector( gWorldPrevToWorld, prevNavg );

    // Previous accum speed and materialID // TODO: 4x4 materialID footprint is reduced to 2x2 only
    uint4 smbInternalData = gIn_Prev_InternalData.GatherRed( gNearestClamp, smbBilinearGatherUv ).wzxy;

    float3 internalData00 = UnpackInternalData( smbInternalData.x );
    float3 internalData10 = UnpackInternalData( smbInternalData.y );
    float3 internalData01 = UnpackInternalData( smbInternalData.z );
    float3 internalData11 = UnpackInternalData( smbInternalData.w );

    float4 diffAccumSpeeds = float4( internalData00.x, internalData10.x, internalData01.x, internalData11.x );
    float4 specAccumSpeeds = float4( internalData00.y, internalData10.y, internalData01.y, internalData11.y );
    float4 prevMaterialIDs = float4( internalData00.z, internalData10.z, internalData01.z, internalData11.z );

    // Disocclusion threshold
    float disocclusionThresholdMulFrustumSize = gDisocclusionThreshold;
    if( gHasDisocclusionThresholdMix )
        disocclusionThresholdMulFrustumSize = lerp( gDisocclusionThreshold, gDisocclusionThresholdAlternate, gIn_DisocclusionThresholdMix[ pixelPosUser ] );

    float frustumSize = GetFrustumSize( gMinRectDimMulUnproject, gOrthoMode, viewZ );
    disocclusionThresholdMulFrustumSize *= frustumSize;

    // Surface motion - plane distance based disocclusion
    float3 V = GetViewVector( X );
    float NoV = abs( dot( N, V ) );
    float smbDisocclusionThreshold = disocclusionThresholdMulFrustumSize / lerp( 0.05 + 0.95 * NoV, 1.0, saturate( smbParallaxInPixels / 30.0 ) );

    float mvLengthFactor = STL::Math::LinearStep( 0.5, 1.0, smbParallaxInPixels );
    float frontFacing = lerp( cos( STL::Math::DegToRad( 135.0 ) ), cos( STL::Math::DegToRad( 91.0 ) ), mvLengthFactor );
    bool isInScreenAndNotBackfacing = IsInScreen( smbPixelUv ) && dot( prevNavg, Navg ) > frontFacing;
    smbDisocclusionThreshold = isInScreenAndNotBackfacing ? smbDisocclusionThreshold : -1.0;

    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float3 smbPlaneDist0 = abs( prevViewZ0 - Xvprev.z );
    float3 smbPlaneDist1 = abs( prevViewZ1 - Xvprev.z );
    float3 smbPlaneDist2 = abs( prevViewZ2 - Xvprev.z );
    float3 smbPlaneDist3 = abs( prevViewZ3 - Xvprev.z );
    float3 smbOcclusion0 = step( smbPlaneDist0, smbDisocclusionThreshold );
    float3 smbOcclusion1 = step( smbPlaneDist1, smbDisocclusionThreshold );
    float3 smbOcclusion2 = step( smbPlaneDist2, smbDisocclusionThreshold );
    float3 smbOcclusion3 = step( smbPlaneDist3, smbDisocclusionThreshold );

    float4 smbOcclusionWeights = STL::Filtering::GetBilinearCustomWeights( smbBilinearFilter, float4( smbOcclusion0.z, smbOcclusion1.y, smbOcclusion2.y, smbOcclusion3.x ) );
    bool smbAllowCatRom = dot( smbOcclusion0 + smbOcclusion1 + smbOcclusion2 + smbOcclusion3, 1.0 ) > 11.5 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA;
    #ifdef REBLUR_DIRECTIONAL_OCCLUSION
        smbAllowCatRom = false;
    #endif

    float smbFootprintQuality = STL::Filtering::ApplyBilinearFilter( smbOcclusion0.z, smbOcclusion1.y, smbOcclusion2.y, smbOcclusion3.x, smbBilinearFilter );
    smbFootprintQuality = STL::Math::Sqrt01( smbFootprintQuality );

    // Material ID check
    float4 materialCmps = CompareMaterials( materialID, prevMaterialIDs, 1 );
    smbOcclusion0.z *= materialCmps.x;
    smbOcclusion1.y *= materialCmps.y;
    smbOcclusion2.y *= materialCmps.z;
    smbOcclusion3.x *= materialCmps.w;

    float4 smbOcclusionWeightsWithMaterialID = STL::Filtering::GetBilinearCustomWeights( smbBilinearFilter, float4( smbOcclusion0.z, smbOcclusion1.y, smbOcclusion2.y, smbOcclusion3.x ) );
    bool smbAllowCatRomWithMaterialID = smbAllowCatRom && dot( materialCmps, 1.0 ) > 3.5 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA;

    float smbFootprintQualityWithMaterialID = STL::Filtering::ApplyBilinearFilter( smbOcclusion0.z, smbOcclusion1.y, smbOcclusion2.y, smbOcclusion3.x, smbBilinearFilter );
    smbFootprintQualityWithMaterialID = STL::Math::Sqrt01( smbFootprintQualityWithMaterialID );

    // Avoid footprint momentary stretching due to changed viewing angle
    float3 smbVprev = GetViewVectorPrev( Xprev, gCameraDelta );
    float NoVprev = abs( dot( N, smbVprev ) ); // TODO: should be prevNavg ( normalized? ), but jittering breaks logic
    float sizeQuality = ( NoVprev + 1e-3 ) / ( NoV + 1e-3 ); // this order because we need to fix stretching only, shrinking is OK
    sizeQuality *= sizeQuality;
    sizeQuality = lerp( 0.1, 1.0, saturate( sizeQuality ) );
    smbFootprintQuality *= sizeQuality;
    smbFootprintQualityWithMaterialID *= sizeQuality;

    // Bits
    float fbits = float( smbAllowCatRom ) * 1.0;
    fbits += smbOcclusion0.z * 2.0;
    fbits += smbOcclusion1.y * 4.0;
    fbits += smbOcclusion2.y * 8.0;
    fbits += smbOcclusion3.x * 16.0;

    // Update accumulation speeds
    #ifdef REBLUR_DIFFUSE
        float4 diffOcclusionWeights = gDiffMaterialMask ? smbOcclusionWeightsWithMaterialID : smbOcclusionWeights;
        float diffHistoryConfidence = gDiffMaterialMask ? smbFootprintQualityWithMaterialID : smbFootprintQuality;
        bool diffAllowCatRom = gDiffMaterialMask ? smbAllowCatRomWithMaterialID : smbAllowCatRom;

        if( gHasHistoryConfidence )
            diffHistoryConfidence *= gIn_Diff_Confidence[ pixelPosUser ];

        float diffAccumSpeed = STL::Filtering::ApplyBilinearCustomWeights( diffAccumSpeeds.x, diffAccumSpeeds.y, diffAccumSpeeds.z, diffAccumSpeeds.w, diffOcclusionWeights );
        diffAccumSpeed *= lerp( diffHistoryConfidence, 1.0, 1.0 / ( 1.0 + diffAccumSpeed ) );
        diffAccumSpeed = min( diffAccumSpeed, gMaxAccumulatedFrameNum );
    #endif

    #ifdef REBLUR_SPECULAR
        float4 specOcclusionWeights = gSpecMaterialMask ? smbOcclusionWeightsWithMaterialID : smbOcclusionWeights;
        float specHistoryConfidence = gSpecMaterialMask ? smbFootprintQualityWithMaterialID : smbFootprintQuality;
        bool specAllowCatRom = gSpecMaterialMask ? smbAllowCatRomWithMaterialID : smbAllowCatRom;

        if( gHasHistoryConfidence )
            specHistoryConfidence *= gIn_Spec_Confidence[ pixelPosUser ];

        float smbSpecAccumSpeed = STL::Filtering::ApplyBilinearCustomWeights( specAccumSpeeds.x, specAccumSpeeds.y, specAccumSpeeds.z, specAccumSpeeds.w, specOcclusionWeights );
        smbSpecAccumSpeed *= lerp( specHistoryConfidence, 1.0, 1.0 / ( 1.0 + smbSpecAccumSpeed ) );
        smbSpecAccumSpeed = min( smbSpecAccumSpeed, gMaxAccumulatedFrameNum );
    #endif

    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );
    #ifdef REBLUR_OCCLUSION
        int3 checkerboardPos = pixelPosUser.xyx + int3( -1, 0, 1 );
        float viewZ0 = abs( gIn_ViewZ[ checkerboardPos.xy ] );
        float viewZ1 = abs( gIn_ViewZ[ checkerboardPos.zy ] );
        float2 wc = GetBilateralWeight( float2( viewZ0, viewZ1 ), viewZ );
        wc *= STL::Math::PositiveRcp( wc.x + wc.y );
    #endif

    // Diffuse
    #ifdef REBLUR_DIFFUSE
        bool diffHasData = gDiffCheckerboard == 2 || checkerboard == gDiffCheckerboard;
        #ifdef REBLUR_OCCLUSION
            uint diffShift = gDiffCheckerboard != 2 ? 1 : 0;
            uint2 diffPos = uint2( pixelPos.x >> diffShift, pixelPos.y ) + gRectOrigin;
            diffPos.x = min( diffPos.x, gRectSize.x * ( gDiffCheckerboard != 2 ? 0.5 : 1.0 ) - 1.0 );
        #else
            uint2 diffPos = pixelPos;
        #endif

        REBLUR_TYPE diff = gIn_Diff[ diffPos ];
        #ifdef REBLUR_SH
            float4 diffSh = gIn_DiffSh[ diffPos ];
        #endif

        // Sample history - surface motion
        REBLUR_TYPE smbDiffHistory;
        REBLUR_FAST_TYPE smbDiffFastHistory;
        REBLUR_SH_TYPE smbDiffShHistory;

        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( smbPixelUv ) * gRectSizePrev, gInvScreenSize,
            diffOcclusionWeights, diffAllowCatRom,
            gIn_Diff_History, smbDiffHistory,
            gIn_DiffFast_History, smbDiffFastHistory
            #ifdef REBLUR_SH
                , gIn_DiffSh_History, smbDiffShHistory
            #endif
        );

        // Avoid negative values
        smbDiffHistory = ClampNegativeToZero( smbDiffHistory );

        // Accumulation with checkerboard resolve // TODO: materialID support?
        #ifdef REBLUR_OCCLUSION
            int3 diffCheckerboardPos = pixelPos.xyx + int3( -1, 0, 1 );
            diffCheckerboardPos.xz >>= diffShift;
            diffCheckerboardPos.x = max( diffCheckerboardPos.x, 0 );
            diffCheckerboardPos.z = min( diffCheckerboardPos.z, gRectSize.x * ( gDiffCheckerboard != 2 ? 0.5 : 1.0 ) - 1.0 );
            diffCheckerboardPos += gRectOrigin.xyx;

            float d0 = gIn_Diff[ diffCheckerboardPos.xy ];
            float d1 = gIn_Diff[ diffCheckerboardPos.zy ];

            if( !diffHasData )
            {
                diff *= saturate( 1.0 - wc.x - wc.y );
                diff += d0 * wc.x + d1 * wc.y;
            }
        #endif

        float diffNonLinearAccumSpeed = 1.0 / ( 1.0 + diffAccumSpeed );
        if( !diffHasData )
            diffNonLinearAccumSpeed *= lerp( 1.0 - gCheckerboardResolveAccumSpeed, 1.0, diffNonLinearAccumSpeed );

        REBLUR_TYPE diffResult = MixHistoryAndCurrent( smbDiffHistory, diff, diffNonLinearAccumSpeed );
        #ifdef REBLUR_SH
            float4 diffShResult = MixHistoryAndCurrent( smbDiffShHistory, diffSh, diffNonLinearAccumSpeed );
        #endif

        // Anti-firefly suppressor
        float diffAntifireflyFactor = diffAccumSpeed * gBlurRadius * REBLUR_FIREFLY_SUPPRESSOR_RADIUS_SCALE;
        diffAntifireflyFactor /= 1.0 + diffAntifireflyFactor;

        float diffHitDistResult = ExtractHitDist( diffResult );
        float diffHitDistClamped = min( diffHitDistResult, ExtractHitDist( smbDiffHistory ) * REBLUR_FIREFLY_SUPPRESSOR_MAX_RELATIVE_INTENSITY.y );
        diffHitDistClamped = lerp( diffHitDistResult, diffHitDistClamped, diffAntifireflyFactor );

        #if( defined REBLUR_OCCLUSION || defined REBLUR_DIRECTIONAL_OCCLUSION )
            diffResult = ChangeLuma( diffResult, diffHitDistClamped );
        #else
            float diffLumaResult = GetLuma( diffResult );
            float diffLumaClamped = min( diffLumaResult, GetLuma( smbDiffHistory ) * REBLUR_FIREFLY_SUPPRESSOR_MAX_RELATIVE_INTENSITY.x );
            diffLumaClamped = lerp( diffLumaResult, diffLumaClamped, diffAntifireflyFactor );

            diffResult = ChangeLuma( diffResult, diffLumaClamped );
            diffResult.w = diffHitDistClamped;

            #ifdef REBLUR_SH
                diffShResult.xyz *= GetLumaScale( length( diffShResult.xyz ), diffLumaClamped );
            #endif
        #endif

        // Output
        float diffError = GetColorErrorForAdaptiveRadiusScale( diffResult, smbDiffHistory, diffAccumSpeed );

        gOut_Diff[ pixelPos ] = diffResult;
        #ifdef REBLUR_SH
            gOut_DiffSh[ pixelPos ] = diffShResult;
        #endif

        // Fast history
        float diffFastAccumSpeed = min( diffAccumSpeed, gMaxFastAccumulatedFrameNum );
        float diffFastNonLinearAccumSpeed = 1.0 / ( 1.0 + diffFastAccumSpeed );
        if( !diffHasData )
            diffFastNonLinearAccumSpeed *= lerp( 1.0 - gCheckerboardResolveAccumSpeed, 1.0, diffFastNonLinearAccumSpeed );

        float diffFastResult = lerp( smbDiffFastHistory.x, GetLuma( diff ), diffFastNonLinearAccumSpeed );

        gOut_DiffFast[ pixelPos ] = diffFastResult;
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
            specPos.x = min( specPos.x, gRectSize.x * ( gSpecCheckerboard != 2 ? 0.5 : 1.0 ) - 1.0 );
        #else
            uint2 specPos = pixelPos;
        #endif

        REBLUR_TYPE spec = gIn_Spec[ specPos ];
        #ifdef REBLUR_SH
            float4 specSh = gIn_SpecSh[ specPos ];
        #endif

        // Checkerboard resolve // TODO: materialID support?
        #ifdef REBLUR_OCCLUSION
            int3 specCheckerboardPos = pixelPos.xyx + int3( -1, 0, 1 );
            specCheckerboardPos.xz >>= specShift;
            specCheckerboardPos.x = max( specCheckerboardPos.x, 0 );
            specCheckerboardPos.z = min( specCheckerboardPos.z, gRectSize.x * ( gSpecCheckerboard != 2 ? 0.5 : 1.0 ) - 1.0 );
            specCheckerboardPos += gRectOrigin.xyx;

            float s0 = gIn_Spec[ specCheckerboardPos.xy ];
            float s1 = gIn_Spec[ specCheckerboardPos.zy ];

            if( !specHasData )
            {
                spec *= saturate( 1.0 - wc.x - wc.y );
                spec += s0 * wc.x + s1 * wc.y;
            }
        #endif

        float Dfactor = STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughness, STL_SPECULAR_DOMINANT_DIRECTION_G2 );

        // Curvature estimation along predicted motion ( tests 15, 40, 76, 133, 146, 147, 148 )
        /*
        TODO: curvature! (╯°□°)╯︵ ┻━┻
         - by design: curvature = 0 on static objects if camera is static
         - quantization errors hurt
         - curvature on bumpy surfaces is just wrong, pulling virtual positions into a surface and introducing lags
         - bad reprojection if curvature changes signs under motion
         - code below works better on smooth curved surfaces, but not in tests: 174, 175 ( without normal map )
            BEFORE: hitDistFocused = hitDist / ( 2.0 * curvature * hitDist * NoV + 1.0 );
            AFTER:  hitDistFocused = hitDist / ( 2.0 * curvature * hitDist / NoV + 1.0 );
        */
        float curvature;
        float2 vmbDelta;
        {
            // IMPORTANT: this code allows to get non-zero parallax on objects attached to the camera
            float2 uvForZeroParallax = gOrthoMode == 0.0 ? smbPixelUv : pixelUv;
            float2 deltaUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xprev - gCameraDelta ) - uvForZeroParallax;
            float len = length( deltaUv );
            float2 motionUv = pixelUv + deltaUv * 0.99 * gInvRectSize / max( len, gInvRectSize.x / 256.0 ); // stay in SMEM

            // Construct the other edge point "x"
            float z = abs( gIn_ViewZ.SampleLevel( gLinearClamp, gRectOffset + motionUv * gResolutionScale, 0 ) );
            float3 x = STL::Geometry::ReconstructViewPosition( motionUv, gFrustum, z, gOrthoMode );
            x = STL::Geometry::RotateVector( gViewToWorld, x );

            // Interpolate normal at "x"
            STL::Filtering::Bilinear f = STL::Filtering::GetBilinearFilter( motionUv, gRectSize );

            int2 pos = threadPos + BORDER + int2( f.origin ) - pixelPos;
            pos = clamp( pos, 0, int2( BUFFER_X, BUFFER_Y ) - 2 ); // just in case?

            float3 n00 = s_Normal_MinHitDist[ pos.y ][ pos.x ].xyz;
            float3 n10 = s_Normal_MinHitDist[ pos.y ][ pos.x + 1 ].xyz;
            float3 n01 = s_Normal_MinHitDist[ pos.y + 1 ][ pos.x ].xyz;
            float3 n11 = s_Normal_MinHitDist[ pos.y + 1 ][ pos.x + 1 ].xyz;

            float3 n = normalize( STL::Filtering::ApplyBilinearFilter( n00, n10, n01, n11, f ) );

            // Estimate curvature for the edge { x; X }
            float3 edge = x - X;
            float edgeLenSq = STL::Math::LengthSquared( edge );
            curvature = dot( n - N, edge ) * STL::Math::PositiveRcp( edgeLenSq );

            // Correction #1 - values below this threshold get turned into garbage due to numerical imprecision
            float d = STL::Math::ManhattanDistance( N, n );
            float s = STL::Math::LinearStep( NRD_NORMAL_ENCODING_ERROR, 2.0 * NRD_NORMAL_ENCODING_ERROR, d );
            curvature *= s;

            // Correction #2 - very negative inconsistent with previous frame curvature blows up reprojection ( tests 164, 171 - 176 )
            float2 uv1 = STL::Geometry::GetScreenUv( gWorldToClipPrev, X - V * ApplyThinLensEquation( NoV, hitDistForTracking, curvature ) );
            float2 uv2 = STL::Geometry::GetScreenUv( gWorldToClipPrev, X );
            float a = length( ( uv1 - uv2 ) * gRectSize );
            float b = length( deltaUv * gRectSize );
            curvature *= float( a < 3.0 * b + gInvRectSize.x ); // TODO:it's a hack, incompatible with concave mirrors ( tests 22b, 23b, 25b )

            // Smooth virtual motion delta ( omitting huge values if curvature is negative and curvature radius is very small )
            float3 Xvirtual = GetXvirtual( NoV, hitDistForTracking, max( curvature, 0.0 ), X, Xprev, V, Dfactor );
            float2 vmbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );
            vmbDelta = vmbPixelUv - smbPixelUv;
        }

        // Virtual motion - coordinates
        float3 Xvirtual = GetXvirtual( NoV, hitDistForTracking, curvature, X, Xprev, V, Dfactor );
        float2 vmbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );

        float vmbPixelsTraveled = length( vmbDelta * gRectSize );
        float XvirtualLength = length( Xvirtual );

        // Virtual motion - plane distance based disocclusion
        // IMPORTANT: use "Navg" in this test to avoid false reaction on bumpy surfaces ( test 181 )
        STL::Filtering::Bilinear vmbBilinearFilter = STL::Filtering::GetBilinearFilter( saturate( vmbPixelUv ), gRectSizePrev );
        float2 vmbBilinearGatherUv = ( vmbBilinearFilter.origin + 1.0 ) * gInvScreenSize;
        float4 vmbViewZs = UnpackViewZ( gIn_Prev_ViewZ.GatherRed( gNearestClamp, vmbBilinearGatherUv ).wzxy );
        float3 vmbVv = STL::Geometry::ReconstructViewPosition( vmbPixelUv, gFrustumPrev, 1.0 ); // unnormalized, orthoMode = 0
        float3 Nvprev = STL::Geometry::RotateVector( gWorldToViewPrev, Navg );
        float NoXreal = dot( Navg, X - gCameraDelta );
        float4 NoX = ( Nvprev.x * vmbVv.x + Nvprev.y * vmbVv.y ) * ( gOrthoMode == 0 ? vmbViewZs : gOrthoMode ) + Nvprev.z * vmbVv.z * vmbViewZs;
        float4 vmbPlaneDist = abs( NoX - NoXreal );
        float4 vmbOcclusion = step( vmbPlaneDist, IsInScreen( vmbPixelUv ) ? disocclusionThresholdMulFrustumSize : -1.0 );

        bool vmbAllowCatRom = dot( vmbOcclusion, 1.0 ) > 3.5 && REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA;
        vmbAllowCatRom = vmbAllowCatRom && specAllowCatRom; // helps to reduce over-sharpening in disoccluded areas

        // Virtual motion - accumulation speed
        uint4 vmbInternalData = gIn_Prev_InternalData.GatherRed( gNearestClamp, vmbBilinearGatherUv ).wzxy;

        float3 vmbInternalData00 = UnpackInternalData( vmbInternalData.x );
        float3 vmbInternalData10 = UnpackInternalData( vmbInternalData.y );
        float3 vmbInternalData01 = UnpackInternalData( vmbInternalData.z );
        float3 vmbInternalData11 = UnpackInternalData( vmbInternalData.w );

        float4 vmbOcclusionWeights = STL::Filtering::GetBilinearCustomWeights( vmbBilinearFilter, vmbOcclusion );
        float vmbSpecAccumSpeed = STL::Filtering::ApplyBilinearCustomWeights( vmbInternalData00.y, vmbInternalData10.y, vmbInternalData01.y, vmbInternalData11.y, vmbOcclusionWeights );

        float vmbFootprintQuality = STL::Filtering::ApplyBilinearFilter( vmbOcclusion.x, vmbOcclusion.y, vmbOcclusion.z, vmbOcclusion.w, vmbBilinearFilter );
        vmbFootprintQuality = STL::Math::Sqrt01( vmbFootprintQuality );
        vmbSpecAccumSpeed *= lerp( vmbFootprintQuality, 1.0, 1.0 / ( 1.0 + vmbSpecAccumSpeed ) );

        float responsiveAccumulationAmount = GetResponsiveAccumulationAmount( roughness );
        responsiveAccumulationAmount = lerp( 1.0, GetSpecMagicCurve( roughness ), responsiveAccumulationAmount );

        float vmbMaxFrameNum = gMaxAccumulatedFrameNum * responsiveAccumulationAmount;

        // Estimate how many pixels are traveled by virtual motion - how many radians can it be?
        // If curvature angle is multiplied by path length then we can get an angle exceeding 2 * PI, what is impossible. The max
        // angle is PI ( most left and most right points on a hemisphere ), it can be achieved by using "tan" instead of angle.
        float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
        float curvatureAngleTan = pixelSize * abs( curvature ); // tana = pixelSize / curvatureRadius = pixelSize * curvature
        // TODO: *= max( gFramerateScale * vmbPixelsTraveled / max( NoV, 0.01 ), 1.0 )?
        curvatureAngleTan *= 1.0 + vmbPixelsTraveled / max( NoV, 0.01 ); // path length
        curvatureAngleTan *= gFramerateScale; // "per frame" to "per sec"

        float lobeHalfAngle = max( STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughnessModified ), NRD_NORMAL_ULP );
        float curvatureAngle = atan( curvatureAngleTan );
        float angle = lobeHalfAngle + curvatureAngle;

        // Virtual motion - normal: parallax ( test 132 )
        float4 vmbNormalAndRoughness = UnpackNormalAndRoughness( gIn_Prev_Normal_Roughness.SampleLevel( gLinearClamp, vmbPixelUv * gResolutionScalePrev, 0 ) );
        float3 vmbN = STL::Geometry::RotateVector( gWorldPrevToWorld, vmbNormalAndRoughness.xyz );

        float hitDist = ExtractHitDist( spec ) * _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
        float parallaxEstimation = smbParallaxInPixels * GetHitDistFactor( hitDist, frustumSize );
        float virtualHistoryNormalBasedConfidence = 1.0 / ( 1.0 + 0.5 * Dfactor * saturate( length( N - vmbN ) - NRD_NORMAL_ULP ) * max( parallaxEstimation, vmbPixelsTraveled ) );

        // Virtual motion - normal: lobe overlapping ( test 107 )
        float normalWeight = GetEncodingAwareNormalWeight( N, vmbN, angle );
        normalWeight = lerp( STL::Math::SmoothStep( 1.0, 0.0, vmbPixelsTraveled ), 1.0, normalWeight ); // jitter friendly
        virtualHistoryNormalBasedConfidence = min( virtualHistoryNormalBasedConfidence, normalWeight );

        // Virtual motion - normal: front-facing
        bool isFrontFace = dot( vmbN, Navg ) > 0.0;
        virtualHistoryNormalBasedConfidence *= float( isFrontFace );

        // Virtual history amount - normal confidence ( tests 9e, 65, 66, 107, 111, 132 )
        // IMPORTANT: this is currently needed for bumpy surfaces, because virtual motion gets ruined by big curvature
        float virtualHistoryAmount = virtualHistoryNormalBasedConfidence;

        // IMPORTANT: at high FPS "smb" works well, so we get enough frames in "specAccumSpeed", even if "vmb" is rejected.
        // TA doesn't behave well on roughness edges at high FPS due to significantly enlarged "sigma". As a WAR, we can
        // increase roughness sensitivity instead.
        float roughnessSensitivity = lerp( NRD_ROUGHNESS_SENSITIVITY, 0.005, STL::Math::SmoothStep( 1.0, 4.0, gFramerateScale ) );

        // Virtual motion - roughness
        float2 relaxedRoughnessWeightParams = GetRelaxedRoughnessWeightParams( roughness, roughnessModified, gRoughnessFraction, roughnessSensitivity );
        float virtualHistoryRoughnessBasedConfidence = GetRelaxedRoughnessWeight( relaxedRoughnessWeightParams, vmbNormalAndRoughness.w );
        virtualHistoryRoughnessBasedConfidence = lerp( STL::Math::SmoothStep( 1.0, 0.0, smbParallaxInPixels ), 1.0, virtualHistoryRoughnessBasedConfidence ); // jitter friendly

        // Virtual motion - virtual parallax difference
        // Tests 3, 6, 8, 11, 14, 100, 103, 104, 106, 109, 110, 114, 120, 127, 130, 131, 132, 138, 139 and 9e
        float hitDistForTrackingPrev = gIn_Prev_Spec_HitDistForTracking.SampleLevel( gLinearClamp, vmbPixelUv * gResolutionScalePrev, 0 );
        float3 XvirtualPrev = GetXvirtual( NoV, hitDistForTrackingPrev, curvature, X, Xprev, V, Dfactor );
        float XvirtualLengthPrev = length( XvirtualPrev );
        float2 vmbPixelUvPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, XvirtualPrev );

        float percentOfVolume = 0.6; // TODO: why 60%? should be smaller for high FPS?
        float lobeTanHalfAngle = STL::ImportanceSampling::GetSpecularLobeTanHalfAngle( roughness, percentOfVolume );

        #if( REBLUR_USE_MORE_STRICT_PARALLAX_BASED_CHECK == 1 )
            float unproj1 = min( hitDistForTracking, hitDistForTrackingPrev ) / PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, max( XvirtualLength, XvirtualLengthPrev ) );
            float lobeRadiusInPixels = lobeTanHalfAngle * unproj1;
        #else
            // Works better if "percentOfVolume" is 0.3-0.6
            float unproj1 = hitDistForTracking / PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, XvirtualLength );
            float unproj2 = hitDistForTrackingPrev / PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, XvirtualLengthPrev );
            float lobeRadiusInPixels = lobeTanHalfAngle * min( unproj1, unproj2 );
        #endif

        float deltaParallaxInPixels = length( ( vmbPixelUvPrev - vmbPixelUv ) * gRectSize );
        float virtualHistoryParallaxBasedConfidence = STL::Math::SmoothStep( lobeRadiusInPixels + 0.25, 0.0, deltaParallaxInPixels );
        vmbMaxFrameNum *= virtualHistoryParallaxBasedConfidence;

        // Virtual motion - normal & roughness prev-prev tests
        vmbDelta *= STL::Math::Rsqrt( STL::Math::LengthSquared( vmbDelta ) );
        vmbDelta /= gRectSizePrev;
        vmbDelta *= saturate( vmbPixelsTraveled / 0.1 ) + vmbPixelsTraveled / REBLUR_VIRTUAL_MOTION_PREV_PREV_WEIGHT_ITERATION_NUM;

        relaxedRoughnessWeightParams = GetRelaxedRoughnessWeightParams( vmbNormalAndRoughness.w, roughnessModified, gRoughnessFraction, roughnessSensitivity );

        [unroll]
        for( i = 1; i <= REBLUR_VIRTUAL_MOTION_PREV_PREV_WEIGHT_ITERATION_NUM; i++ )
        {
            float2 vmbPixelUvPrev = vmbPixelUv + vmbDelta * i;
            float4 vmbNormalAndRoughnessPrev = UnpackNormalAndRoughness( gIn_Prev_Normal_Roughness.SampleLevel( gLinearClamp, vmbPixelUvPrev * gResolutionScalePrev, 0 ) );

            float2 w;
            w.x = GetEncodingAwareNormalWeight( vmbNormalAndRoughness.xyz, vmbNormalAndRoughnessPrev.xyz, angle + curvatureAngle * i, curvatureAngle );
            w.y = GetRelaxedRoughnessWeight( relaxedRoughnessWeightParams, vmbNormalAndRoughnessPrev.w );

            w = IsInScreen( vmbPixelUvPrev ) ? w : 1.0;

            virtualHistoryNormalBasedConfidence = min( virtualHistoryNormalBasedConfidence, w.x );
            virtualHistoryRoughnessBasedConfidence = min( virtualHistoryRoughnessBasedConfidence, w.y );
        }

        vmbMaxFrameNum *= virtualHistoryNormalBasedConfidence;

        // Virtual history confidence
        float virtualHistoryConfidence = virtualHistoryNormalBasedConfidence * virtualHistoryRoughnessBasedConfidence;

        // Surface motion ( test 9, 9e )
        // IMPORTANT: needs to be responsive, because "vmb" fails on bumpy surfaces for the following reasons:
        //  - normal and prev-prev tests fail
        //  - curvature is so high that "vmb" regresses to "smb" and starts to lag
        float smc = GetSpecMagicCurve( roughnessModified );
        float smbMaxFrameNum = gMaxAccumulatedFrameNum;
        float smbSpecAccumSpeedNoBoost = 0.0;
        {
            // Main part
            float f = lerp( 6.0, 0.0, smc ); // TODO: use Dfactor somehow?
            f *= lerp( 0.5, 1.0, virtualHistoryConfidence ); // TODO: ( optional ) visually looks good, but adds temporal lag

            // IMPORTANT: we must not use any "vmb" data for parallax estimation, because curvature can be wrong.
            // Estimation below is visually close to "vmbPixelsTraveled" computed for "vmbPixelUv" produced by 0 curvature
            smbMaxFrameNum /= 1.0 + f * parallaxEstimation;

            // Eliminate trailing if parallax is out of lobe ( test 142 )
            float ta = PixelRadiusToWorld( gUnproject, gOrthoMode, vmbPixelsTraveled, viewZ ) / viewZ;
            float ca = STL::Math::Rsqrt( 1.0 + ta * ta );
            float a = STL::Math::AcosApprox( ca );

            smbMaxFrameNum *= STL::Math::SmoothStep( angle, 0.0, a );

            // Ensure that HistoryFix pass doesn't pop up without a disocclusion in critical cases
            smbSpecAccumSpeedNoBoost = smbMaxFrameNum;
            smbMaxFrameNum = max( smbMaxFrameNum, gHistoryFixFrameNum * ( 1.0 - virtualHistoryConfidence ) );
        }

        // Limit number of accumulated frames
        vmbSpecAccumSpeed = min( vmbSpecAccumSpeed, vmbMaxFrameNum );
        smbSpecAccumSpeed = min( smbSpecAccumSpeed, smbMaxFrameNum );
        smbSpecAccumSpeedNoBoost = min( smbSpecAccumSpeed, smbSpecAccumSpeedNoBoost );

        // Virtual history amount - other ( tests 65, 66, 103, 111, 132, e9, e11 )
        virtualHistoryAmount *= STL::Math::SmoothStep( 0.05, 0.95, Dfactor );
        virtualHistoryAmount *= virtualHistoryRoughnessBasedConfidence; // TODO: was virtualHistoryConfidence
        virtualHistoryAmount *= saturate( vmbSpecAccumSpeed / ( smbSpecAccumSpeed + NRD_EPS ) );

        #if( REBLUR_VIRTUAL_HISTORY_AMOUNT != 2 )
            virtualHistoryAmount = REBLUR_VIRTUAL_HISTORY_AMOUNT;
        #endif

        // Sample history
        REBLUR_TYPE smbSpecHistory;
        REBLUR_FAST_TYPE smbSpecFastHistory;
        REBLUR_SH_TYPE smbSpecShHistory;

        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( smbPixelUv ) * gRectSizePrev, gInvScreenSize,
            specOcclusionWeights, specAllowCatRom,
            gIn_Spec_History, smbSpecHistory,
            gIn_SpecFast_History, smbSpecFastHistory
            #ifdef REBLUR_SH
                , gIn_SpecSh_History, smbSpecShHistory
            #endif
        );

        REBLUR_TYPE vmbSpecHistory;
        REBLUR_FAST_TYPE vmbSpecFastHistory;
        REBLUR_SH_TYPE vmbSpecShHistory;

        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( vmbPixelUv ) * gRectSizePrev, gInvScreenSize,
            vmbOcclusionWeights, vmbAllowCatRom,
            gIn_Spec_History, vmbSpecHistory,
            gIn_SpecFast_History, vmbSpecFastHistory
            #ifdef REBLUR_SH
                , gIn_SpecSh_History, vmbSpecShHistory
            #endif
        );

        // Avoid negative values
        smbSpecHistory = ClampNegativeToZero( smbSpecHistory );
        vmbSpecHistory = ClampNegativeToZero( vmbSpecHistory );

        // Accumulation with checkerboard resolve // TODO: materialID support?
        float smbSpecNonLinearAccumSpeed = 1.0 / ( 1.0 + smbSpecAccumSpeedNoBoost );
        float vmbSpecNonLinearAccumSpeed = 1.0 / ( 1.0 + vmbSpecAccumSpeed );

        if( !specHasData )
        {
            smbSpecNonLinearAccumSpeed *= lerp( 1.0 - gCheckerboardResolveAccumSpeed, 1.0, smbSpecNonLinearAccumSpeed );
            vmbSpecNonLinearAccumSpeed *= lerp( 1.0 - gCheckerboardResolveAccumSpeed, 1.0, vmbSpecNonLinearAccumSpeed );
        }

        REBLUR_TYPE smbSpec = MixHistoryAndCurrent( smbSpecHistory, spec, smbSpecNonLinearAccumSpeed, roughnessModified );
        REBLUR_TYPE vmbSpec = MixHistoryAndCurrent( vmbSpecHistory, spec, vmbSpecNonLinearAccumSpeed, roughnessModified );

        REBLUR_TYPE specResult = lerp( smbSpec, vmbSpec, virtualHistoryAmount );

        #ifdef REBLUR_SH
            float4 smbShSpec = lerp( smbSpecShHistory, specSh, smbSpecNonLinearAccumSpeed );
            float4 vmbShSpec = lerp( vmbSpecShHistory, specSh, vmbSpecNonLinearAccumSpeed );

            float4 specShResult = lerp( smbShSpec, vmbShSpec, virtualHistoryAmount );

            // ( Optional ) Output modified roughness to assist AA during SG resolve
            specShResult.w = roughnessModified; // IMPORTANT: should not be blurred
        #endif

        float specAccumSpeed = lerp( smbSpecAccumSpeed, vmbSpecAccumSpeed, virtualHistoryAmount );
        REBLUR_TYPE specHistory = lerp( smbSpecHistory, vmbSpecHistory, virtualHistoryAmount );

        // Anti-firefly suppressor
        float specAntifireflyFactor = specAccumSpeed * gBlurRadius * REBLUR_FIREFLY_SUPPRESSOR_RADIUS_SCALE * smc;
        specAntifireflyFactor /= 1.0 + specAntifireflyFactor;

        float specHitDistResult = ExtractHitDist( specResult );
        float specHitDistClamped = min( specHitDistResult, ExtractHitDist( specHistory ) * REBLUR_FIREFLY_SUPPRESSOR_MAX_RELATIVE_INTENSITY.y );
        specHitDistClamped = lerp( specHitDistResult, specHitDistClamped, specAntifireflyFactor );

        #if( defined REBLUR_OCCLUSION || defined REBLUR_DIRECTIONAL_OCCLUSION )
            specResult = ChangeLuma( specResult, specHitDistClamped );
        #else
            float specLumaResult = GetLuma( specResult );
            float specLumaClamped = min( specLumaResult, GetLuma( specHistory ) * REBLUR_FIREFLY_SUPPRESSOR_MAX_RELATIVE_INTENSITY.x );
            specLumaClamped = lerp( specLumaResult, specLumaClamped, specAntifireflyFactor );

            specResult = ChangeLuma( specResult, specLumaClamped );
            specResult.w = specHitDistClamped;

            #ifdef REBLUR_SH
                specShResult.xyz *= GetLumaScale( length( specShResult.xyz ), specLumaClamped );
            #endif
        #endif

        // Output
        float specError = GetColorErrorForAdaptiveRadiusScale( specResult, specHistory, specAccumSpeed, roughness );

        gOut_Spec[ pixelPos ] = specResult;
        #ifdef REBLUR_SH
            gOut_SpecSh[ pixelPos ] = specShResult;
        #endif

        // Fast history
        float smbSpecFastAccumSpeed = min( smbSpecAccumSpeed, gMaxFastAccumulatedFrameNum * virtualHistoryConfidence );
        float vmbSpecFastAccumSpeed = min( vmbSpecAccumSpeed, gMaxFastAccumulatedFrameNum );

        float smbSpecFastNonLinearAccumSpeed = 1.0 / ( 1.0 + smbSpecFastAccumSpeed );
        float vmbSpecFastNonLinearAccumSpeed = 1.0 / ( 1.0 + vmbSpecFastAccumSpeed );

        if( !specHasData )
        {
            smbSpecFastNonLinearAccumSpeed *= lerp( 1.0 - gCheckerboardResolveAccumSpeed, 1.0, smbSpecFastNonLinearAccumSpeed );
            vmbSpecFastNonLinearAccumSpeed *= lerp( 1.0 - gCheckerboardResolveAccumSpeed, 1.0, vmbSpecFastNonLinearAccumSpeed );
        }

        float smbSpecFast = lerp( smbSpecFastHistory, GetLuma( spec ), smbSpecFastNonLinearAccumSpeed );
        float vmbSpecFast = lerp( vmbSpecFastHistory, GetLuma( spec ), vmbSpecFastNonLinearAccumSpeed );

        float specFastResult = lerp( smbSpecFast, vmbSpecFast, virtualHistoryAmount );

        gOut_SpecFast[ pixelPos ] = specFastResult;

        // Debug
        #if( REBLUR_SHOW == REBLUR_SHOW_CURVATURE )
            virtualHistoryAmount = abs( curvature ) * pixelSize * 30.0;
        #elif( REBLUR_SHOW == REBLUR_SHOW_CURVATURE_SIGN )
            virtualHistoryAmount = sign( curvature ) * 0.5 + 0.5;
        #elif( REBLUR_SHOW == REBLUR_SHOW_VIRTUAL_HISTORY_CONFIDENCE )
            virtualHistoryAmount = virtualHistoryConfidence;
        #elif( REBLUR_SHOW == REBLUR_SHOW_VIRTUAL_HISTORY_NORMAL_CONFIDENCE )
            virtualHistoryAmount = virtualHistoryNormalBasedConfidence;
        #elif( REBLUR_SHOW == REBLUR_SHOW_VIRTUAL_HISTORY_ROUGHNESS_CONFIDENCE )
            virtualHistoryAmount = virtualHistoryRoughnessBasedConfidence;
        #elif( REBLUR_SHOW == REBLUR_SHOW_VIRTUAL_HISTORY_PARALLAX_CONFIDENCE )
            virtualHistoryAmount = virtualHistoryParallaxBasedConfidence;
        #elif( REBLUR_SHOW == REBLUR_SHOW_HIT_DIST_FOR_TRACKING )
            virtualHistoryAmount = hitDistForTracking * lerp( 1.0, 5.0, smc ) / ( 1.0 + hitDistForTracking * lerp( 1.0, 5.0, smc ) );
        #endif
    #else
        float specAccumSpeed = 0;
        float curvature = 0;
        float virtualHistoryAmount = 0;
        float specError = 0;
    #endif

    // Output
    gOut_Data1[ pixelPos ] = PackData1( diffAccumSpeed, diffError, specAccumSpeed, specError );

    #ifndef REBLUR_OCCLUSION
        gOut_Data2[ pixelPos ] = PackData2( fbits, curvature, virtualHistoryAmount );
    #endif
}
