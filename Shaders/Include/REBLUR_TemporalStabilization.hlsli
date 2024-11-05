/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];
groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

#ifdef REBLUR_SH
    groupshared REBLUR_SH_TYPE s_DiffSh[ BUFFER_Y ][ BUFFER_X ];
    groupshared REBLUR_SH_TYPE s_SpecSh[ BUFFER_Y ][ BUFFER_X ];
#endif

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSizeMinusOne );

    #ifdef REBLUR_DIFFUSE
        s_Diff[ sharedPos.y ][ sharedPos.x ] = gIn_Diff[ globalPos ];
        #ifdef REBLUR_SH
            s_DiffSh[ sharedPos.y ][ sharedPos.x ] = gIn_DiffSh[ globalPos ];
        #endif
    #endif

    #ifdef REBLUR_SPECULAR
        s_Spec[ sharedPos.y ][ sharedPos.x ] = gIn_Spec[ globalPos ];
        #ifdef REBLUR_SH
            s_SpecSh[ sharedPos.y ][ sharedPos.x ] = gIn_SpecSh[ globalPos ];
        #endif
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    // Preload
    float isSky = gIn_Tiles[ pixelPos >> 4 ];
    PRELOAD_INTO_SMEM_WITH_TILE_CHECK;

    // Tile-based early out
    if( isSky != 0.0 || any( pixelPos > gRectSizeMinusOne ) )
        return;

    // Early out
    float viewZ = UnpackViewZ( gIn_ViewZ[ WithRectOrigin( pixelPos ) ] );
    if( viewZ > gDenoisingRange )
        return; // IMPORTANT: no data output, must be rejected by the "viewZ" check!

    // Local variance
    int2 smemPos = threadPos + BORDER;

    #ifdef REBLUR_DIFFUSE
        float4 diff = s_Diff[ smemPos.y ][ smemPos.x ];
        float4 diffM1 = diff;
        float4 diffM2 = diff * diff;

        #ifdef REBLUR_SH
            REBLUR_SH_TYPE diffSh = s_DiffSh[ smemPos.y ][ smemPos.x ];
            float3 diffShM1 = diffSh.xyz;
            float3 diffShM2 = diffSh.xyz * diffSh.xyz;
        #endif

        float diffMin = NRD_INF;
        float diffMax = -NRD_INF;
    #endif

    #ifdef REBLUR_SPECULAR
        float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
        float4 specM1 = spec;
        float4 specM2 = spec * spec;

        #ifdef REBLUR_SH
            REBLUR_SH_TYPE specSh = s_SpecSh[ smemPos.y ][ smemPos.x ];
            float3 specShM1 = specSh.xyz;
            float3 specShM2 = specSh.xyz * specSh.xyz;
        #endif

        float specMin = NRD_INF;
        float specMax = -NRD_INF;
    #endif

    [unroll]
    for( j = 0; j <= BORDER * 2; j++ )
    {
        [unroll]
        for( i = 0; i <= BORDER * 2; i++ )
        {
            if( i == BORDER && j == BORDER )
                continue;

            int2 pos = threadPos + int2( i, j );

            #ifdef REBLUR_DIFFUSE
                // Accumulate moments
                float4 d = s_Diff[ pos.y ][ pos.x ];
                diffM1 += d;
                diffM2 += d * d;

                #ifdef REBLUR_SH
                    float3 dh = s_DiffSh[ pos.y ][ pos.x ].xyz;
                    diffShM1 += dh;
                    diffShM2 += dh * dh;
                #endif

                // RCRS
                float diffLuma = GetLuma( d );
                diffMin = min( diffMin, diffLuma );
                diffMax = max( diffMax, diffLuma );
            #endif

            #ifdef REBLUR_SPECULAR
                // Accumulate moments
                float4 s = s_Spec[ pos.y ][ pos.x ];
                specM1 += s;
                specM2 += s * s;

                #ifdef REBLUR_SH
                    float3 sh = s_SpecSh[ pos.y ][ pos.x ].xyz;
                    specShM1 += sh;
                    specShM2 += sh * sh;
                #endif

                // RCRS
                float specLuma = GetLuma( s );
                specMin = min( specMin, specLuma );
                specMax = max( specMax, specLuma );
            #endif
        }
    }

    float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );

    #ifdef REBLUR_DIFFUSE
        // Compute sigma
        diffM1 *= invSum;
        diffM2 *= invSum;
        float4 diffSigma = GetStdDev( diffM1, diffM2 );

        #ifdef REBLUR_SH
            diffShM1 *= invSum;
            diffShM2 *= invSum;
            float3 diffShSigma = GetStdDev( diffShM1, diffShM2 );
        #endif

        // RCRS
        [flatten]
        if( gMaxBlurRadius != 0 )
        {
            float diffLuma = GetLuma( diff );
            float diffLumaClamped = clamp( diffLuma, diffMin, diffMax );

            diff = ChangeLuma( diff, diffLumaClamped );
            #ifdef REBLUR_SH
                diffSh.xyz *= GetLumaScale( length( diffSh.xyz ), diffLumaClamped );
            #endif
        }
    #endif

    #ifdef REBLUR_SPECULAR
        // Compute sigma
        specM1 *= invSum;
        specM2 *= invSum;
        float4 specSigma = GetStdDev( specM1, specM2 );

        #ifdef REBLUR_SH
            specShM1 *= invSum;
            specShM2 *= invSum;
            float3 specShSigma = GetStdDev( specShM1, specShM2 );
        #endif

        // RCRS
        [flatten]
        if( gMaxBlurRadius != 0 )
        {
            float specLuma = GetLuma( spec );
            float specLumaClamped = clamp( specLuma, specMin, specMax );

            spec = ChangeLuma( spec, specLumaClamped );
            #ifdef REBLUR_SH
                specSh.xyz *= GetLumaScale( length( specSh.xyz ), specLumaClamped );
            #endif
        }
    #endif

    // Position
    float2 pixelUv = float2( pixelPos + 0.5 ) * gRectSizeInv;
    float3 Xv = Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float3 X = Geometry::RotateVector( gViewToWorld, Xv );

    // Previous position and surface motion uv
    float4 inMv = gInOut_Mv[ WithRectOrigin( pixelPos ) ];
    float3 mv = inMv.xyz * gMvScale.xyz;
    float3 Xprev = X;
    float2 smbPixelUv = pixelUv + mv.xy;

    if( gMvScale.w == 0.0 )
    {
        if( gMvScale.z == 0.0 )
            mv.z = Geometry::AffineTransform( gWorldToViewPrev, X ).z - viewZ;

        float viewZprev = viewZ + mv.z;
        float3 Xvprevlocal = Geometry::ReconstructViewPosition( smbPixelUv, gFrustumPrev, viewZprev, gOrthoMode ); // TODO: use gOrthoModePrev

        Xprev = Geometry::RotateVectorInverse( gWorldToViewPrev, Xvprevlocal ) + gCameraDelta.xyz;
    }
    else
    {
        Xprev += mv;
        smbPixelUv = Geometry::GetScreenUv( gWorldToClipPrev, Xprev );
    }

    // Normal and roughness
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ WithRectOrigin( pixelPos ) ], materialID );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Shared data
    uint bits;
    REBLUR_DATA1_TYPE data1 = UnpackData1( gIn_Data1[ pixelPos ] );
    float2 data2 = UnpackData2( gIn_Data2[ pixelPos ], bits );

    // Surface motion footprint
    Filtering::Bilinear smbBilinearFilter = Filtering::GetBilinearFilter( smbPixelUv, gRectSizePrev );
    float4 smbOcclusion = float4( ( bits & uint4( 1, 2, 4, 8 ) ) != 0 );
    float4 smbOcclusionWeights = Filtering::GetBilinearCustomWeights( smbBilinearFilter, smbOcclusion );
    bool smbAllowCatRom = dot( smbOcclusion, 1.0 ) > 3.5 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS;
    float smbFootprintQuality = Filtering::ApplyBilinearFilter( smbOcclusion.x, smbOcclusion.y, smbOcclusion.z, smbOcclusion.w, smbBilinearFilter );
    smbFootprintQuality = Math::Sqrt01( smbFootprintQuality );

    // Diffuse
    #ifdef REBLUR_DIFFUSE
        // Sample history - surface motion
        REBLUR_TYPE smbDiffHistory;
        REBLUR_SH_TYPE smbDiffShHistory;

        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( smbPixelUv ) * gRectSizePrev, gResourceSizeInvPrev,
            smbOcclusionWeights, smbAllowCatRom,
            gHistory_DiffStabilized, smbDiffHistory
            #ifdef REBLUR_SH
                , gHistory_DiffShStabilized, smbDiffShHistory
            #endif
        );

        // Avoid negative values
        smbDiffHistory = ClampNegativeToZero( smbDiffHistory );

        // Compute antilag
        float diffAntilag = ComputeAntilag( smbDiffHistory, diffM1, diffSigma, smbFootprintQuality * data1.x );

        // Clamp history and combine with the current frame
        float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( smbFootprintQuality, data1.x );

        float diffHistoryWeight = diffTemporalAccumulationParams.x;
        diffHistoryWeight *= diffAntilag; // this is important
        diffHistoryWeight *= float( pixelUv.x >= gSplitScreen );
        diffHistoryWeight *= float( smbPixelUv.x >= gSplitScreen );

        smbDiffHistory = Color::Clamp( diffM1, diffSigma * diffTemporalAccumulationParams.y, smbDiffHistory );

        REBLUR_TYPE diffResult;
        diffResult.xyz = lerp( diff.xyz, smbDiffHistory.xyz, min( diffHistoryWeight, gStabilizationStrength ) );
        diffResult.w = lerp( diff.w, smbDiffHistory.w, min( diffHistoryWeight, gHitDistStabilizationStrength ) );

        #ifdef REBLUR_SH
            smbDiffShHistory.xyz = Color::Clamp( diffShM1, diffShSigma * diffTemporalAccumulationParams.y, smbDiffShHistory.xyz );

            REBLUR_SH_TYPE diffShResult = lerp( diffSh, smbDiffShHistory, diffHistoryWeight );
            diffShResult.w = 0; // TODO: unused
        #endif

        // Output
        gOut_Diff[ pixelPos ] = diffResult;
        #ifdef REBLUR_SH
            gOut_DiffSh[ pixelPos ] = diffShResult;
        #endif

        // Increment history length
        data1.x += 1.0;

        // Apply anti-lag
        float diffMinAccumSpeed = min( data1.x, gHistoryFixFrameNum ) * REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX;
        data1.x = lerp( diffMinAccumSpeed, data1.x, diffAntilag );
    #endif

    // Specular
    #ifdef REBLUR_SPECULAR
        float virtualHistoryAmount = data2.x;
        float curvature = data2.y;

        // Hit distance for tracking ( tests 6, 67, 155 )
        float hitDistForTracking = spec.w * _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness ); // TODO: min in 3x3 seems to be not needed here

        // Needed to preserve contact ( test 3, 8 ), but adds pixelation in some cases ( test 160 ). More fun if lobe trimming is off.
        [flatten]
        if( gSpecPrepassBlurRadius != 0.0 )
            hitDistForTracking = min( hitDistForTracking, gIn_SpecHitDistForTracking[ pixelPos ] );

        // Virtual motion
        float3 V = GetViewVector( X );
        float NoV = abs( dot( N, V ) );
        float dominantFactor = ImportanceSampling::GetSpecularDominantFactor( NoV, roughness, ML_SPECULAR_DOMINANT_DIRECTION_G2 );
        float3 Xvirtual = GetXvirtual( hitDistForTracking, curvature, X, Xprev, V, dominantFactor );

        float2 vmbPixelUv = Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );
        vmbPixelUv = materialID == gCameraAttachedReflectionMaterialID ? pixelUv : vmbPixelUv;

        // Modify MVs if requested
        if( gSpecProbabilityThresholdsForMvModification.x < 1.0 && NRD_USE_BASECOLOR_METALNESS )
        {
            float4 baseColorMetalness = gIn_BaseColor_Metalness[ WithRectOrigin( pixelPos ) ];

            float3 albedo, Rf0;
            BRDF::ConvertBaseColorMetalnessToAlbedoRf0( baseColorMetalness.xyz, baseColorMetalness.w, albedo, Rf0 );

            float3 Fenv = BRDF::EnvironmentTerm_Rtg( Rf0, NoV, roughness );

            float lumSpec = Color::Luminance( Fenv );
            float lumDiff = Color::Luminance( albedo * ( 1.0 - Fenv ) );
            float specProb = lumSpec / ( lumDiff + lumSpec + NRD_EPS );

            float f = Math::SmoothStep( gSpecProbabilityThresholdsForMvModification.x, gSpecProbabilityThresholdsForMvModification.y, specProb );
            f *= 1.0 - GetSpecMagicCurve( roughness );
            f *= 1.0 - Math::Sqrt01( abs( curvature ) );

            if( f != 0.0 )
            {
                float3 specMv = Xvirtual - X; // world-space delta fits badly into FP16! Prefer 2.5D motion!
                if( gMvScale.w == 0.0 )
                {
                    specMv.xy = vmbPixelUv - pixelUv;
                    specMv.z = Geometry::AffineTransform( gWorldToViewPrev, Xvirtual ).z - viewZ; // TODO: is it useful?
                }

                // Modify only .xy for 2D and .xyz for 2.5D and 3D MVs
                mv.xy = specMv.xy / gMvScale.xy;
                mv.z = gMvScale.z == 0.0 ? inMv.z : specMv.z / gMvScale.z;

                inMv.xyz = lerp( inMv.xyz, mv, f );

                gInOut_Mv[ WithRectOrigin( pixelPos ) ] = inMv;
            }
        }

        // Sample history - surface motion
        REBLUR_TYPE smbSpecHistory;
        REBLUR_SH_TYPE smbSpecShHistory;

        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( smbPixelUv ) * gRectSizePrev, gResourceSizeInvPrev,
            smbOcclusionWeights, smbAllowCatRom,
            gHistory_SpecStabilized, smbSpecHistory
            #ifdef REBLUR_SH
                , gHistory_SpecShStabilized, smbSpecShHistory
            #endif
        );

        // Virtual motion footprint
        Filtering::Bilinear vmbBilinearFilter = Filtering::GetBilinearFilter( vmbPixelUv, gRectSizePrev );
        float4 vmbOcclusion = float4( ( bits & uint4( 16, 32, 64, 128 ) ) != 0 );
        float4 vmbOcclusionWeights = Filtering::GetBilinearCustomWeights( vmbBilinearFilter, vmbOcclusion );
        bool vmbAllowCatRom = dot( vmbOcclusion, 1.0 ) > 3.5 && REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS;
        float vmbFootprintQuality = Filtering::ApplyBilinearFilter( vmbOcclusion.x, vmbOcclusion.y, vmbOcclusion.z, vmbOcclusion.w, vmbBilinearFilter );
        vmbFootprintQuality = Math::Sqrt01( vmbFootprintQuality );

        // Sample history - virtual motion
        REBLUR_TYPE vmbSpecHistory;
        REBLUR_SH_TYPE vmbSpecShHistory;

        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( vmbPixelUv ) * gRectSizePrev, gResourceSizeInvPrev,
            vmbOcclusionWeights, vmbAllowCatRom,
            gHistory_SpecStabilized, vmbSpecHistory
            #ifdef REBLUR_SH
                , gHistory_SpecShStabilized, vmbSpecShHistory
            #endif
        );

        // Avoid negative values
        smbSpecHistory = ClampNegativeToZero( smbSpecHistory );
        vmbSpecHistory = ClampNegativeToZero( vmbSpecHistory );

        // Combine surface and virtual motion
        REBLUR_TYPE specHistory = lerp( smbSpecHistory, vmbSpecHistory, virtualHistoryAmount );
        #ifdef REBLUR_SH
            REBLUR_SH_TYPE specShHistory = lerp( smbSpecShHistory, vmbSpecShHistory, virtualHistoryAmount );
        #endif

        // Compute antilag
        float footprintQuality = lerp( smbFootprintQuality, vmbFootprintQuality, virtualHistoryAmount );
        float specAntilag = ComputeAntilag( specHistory, specM1, specSigma, footprintQuality * data1.y );

        // Clamp history and combine with the current frame
        float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( footprintQuality, data1.y );

        // TODO: roughness should affect stabilization:
        // - use "virtualHistoryRoughnessBasedConfidence" from TA
        // - compute moments for samples with similar roughness
        float specHistoryWeight = specTemporalAccumulationParams.x;
        specHistoryWeight *= specAntilag; // this is important
        specHistoryWeight *= float( pixelUv.x >= gSplitScreen );
        specHistoryWeight *= virtualHistoryAmount != 1.0 ? float( smbPixelUv.x >= gSplitScreen ) : 1.0;
        specHistoryWeight *= virtualHistoryAmount != 0.0 ? float( vmbPixelUv.x >= gSplitScreen ) : 1.0;

        float responsiveFactor = RemapRoughnessToResponsiveFactor( roughness );
        float smc = GetSpecMagicCurve( roughness );
        float acceleration = lerp( smc, 1.0, 0.5 + responsiveFactor * 0.5 );
        specHistoryWeight *= materialID == gStrandMaterialID ? 0.5 : acceleration;

        specHistory = Color::Clamp( specM1, specSigma * specTemporalAccumulationParams.y, specHistory );

        REBLUR_TYPE specResult;
        specResult.xyz = lerp( spec.xyz, specHistory.xyz, min( specHistoryWeight, gStabilizationStrength ) );
        specResult.w = lerp( spec.w, specHistory.w, min( specHistoryWeight, gHitDistStabilizationStrength ) );

        #ifdef REBLUR_SH
            specShHistory.xyz = Color::Clamp( specShM1, specShSigma * specTemporalAccumulationParams.y, specShHistory.xyz );

            REBLUR_SH_TYPE specShResult = lerp( specSh, specShHistory, specHistoryWeight );
            specShResult.w = specSh.w; // ( Optional ) Output modified roughness to assist AA during SG resolve
        #endif

        // Output
        gOut_Spec[ pixelPos ] = specResult;
        #ifdef REBLUR_SH
            gOut_SpecSh[ pixelPos ] = specShResult;
        #endif

        // Increment history length
        data1.y += 1.0;

        // Apply anti-lag
        float specMinAccumSpeed = min( data1.y, gHistoryFixFrameNum ) * REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX;
        data1.y = lerp( specMinAccumSpeed, data1.y, specAntilag );
    #endif

    gOut_InternalData[ pixelPos ] = PackInternalData( data1.x, data1.y, materialID );
}
