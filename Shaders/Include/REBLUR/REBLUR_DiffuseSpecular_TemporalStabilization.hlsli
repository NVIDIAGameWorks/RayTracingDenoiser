/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// TODO: add REBLUR_OCCLUSION support to TemporalStabilization?

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];
groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

#ifdef REBLUR_SH
    groupshared float4 s_DiffSh[ BUFFER_Y ][ BUFFER_X ];
    groupshared float4 s_SpecSh[ BUFFER_Y ][ BUFFER_X ];
#endif

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );

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
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    float viewZ = UnpackViewZ( gIn_ViewZ[ pixelPosUser ] );

    [branch]
    if( viewZ > gDenoisingRange )
    {
        // IMPORTANT: no data output, must be rejected by the "viewZ" check!
        return;
    }

    // Normal and roughness
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float3 X = STL::Geometry::RotateVector( gViewToWorld, Xv );

    // Local variance
    int2 smemPos = threadPos + BORDER;

    #ifdef REBLUR_DIFFUSE
        float4 diff = s_Diff[ smemPos.y ][ smemPos.x ];
        float4 diffM1 = diff;
        float4 diffM2 = diff * diff;

        #ifdef REBLUR_SH
            float4 diffSh = s_DiffSh[ smemPos.y ][ smemPos.x ];
            float4 diffShM1 = diffSh;
            float4 diffShM2 = diffSh * diffSh;
        #endif

        float2 diffMin = NRD_INF;
        float2 diffMax = -NRD_INF;
    #endif

    #ifdef REBLUR_SPECULAR
        float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
        float4 specM1 = spec;
        float4 specM2 = spec * spec;

        #ifdef REBLUR_SH
            float4 specSh = s_SpecSh[ smemPos.y ][ smemPos.x ];
            float4 specShM1 = specSh;
            float4 specShM2 = specSh * specSh;
        #endif

        float2 specMin = NRD_INF;
        float2 specMax = -NRD_INF;

        float hitDistForTracking = spec.w;
    #endif

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadPos + int2( dx, dy );

            #ifdef REBLUR_DIFFUSE
                // Accumulate moments
                float4 d = s_Diff[ pos.y ][ pos.x ];
                diffM1 += d;
                diffM2 += d * d;

                #ifdef REBLUR_SH
                    float4 dh = s_DiffSh[ pos.y ][ pos.x ];
                    diffShM1 += dh;
                    diffShM2 += dh * dh;
                #endif

                // RCRS
                float diffLuma = GetLuma( d );
                diffMin = min( diffMin, float2( diffLuma, d.w ) );
                diffMax = max( diffMax, float2( diffLuma, d.w ) );
            #endif

            #ifdef REBLUR_SPECULAR
                // Accumulate moments
                float4 s = s_Spec[ pos.y ][ pos.x ];
                specM1 += s;
                specM2 += s * s;

                #ifdef REBLUR_SH
                    float4 sh = s_SpecSh[ pos.y ][ pos.x ];
                    specShM1 += sh;
                    specShM2 += sh * sh;
                #endif

                // RCRS
                float specLuma = GetLuma( s );
                specMin = min( specMin, float2( specLuma, s.w ) );
                specMax = max( specMax, float2( specLuma, s.w ) );

                // Find optimal hitDist for tracking
                hitDistForTracking = min( ExtractHitDist( s ), hitDistForTracking );
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
            float4 diffShSigma = GetStdDev( diffShM1, diffShM2 );
        #endif

        // RCRS
        float diffLuma = GetLuma( diff );
        float diffLumaClamped = clamp( diffLuma, diffMin.x, diffMax.x );

        [flatten]
        if( gBlurRadius != 0 )
        {
            diff = ChangeLuma( diff, diffLumaClamped );
            diff.w = clamp( diff.w, diffMin.y, diffMax.y );
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
            float4 specShSigma = GetStdDev( specShM1, specShM2 );
        #endif

        // RCRS
        float specLuma = GetLuma( spec );
        float specLumaClamped = clamp( specLuma, specMin.x, specMax.x );

        [flatten]
        if( gBlurRadius != 0 )
        {
            spec = ChangeLuma( spec, specLumaClamped );
            spec.w = clamp( spec.w, specMin.y, specMax.y );
        }
    #endif

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser ] * gMotionVectorScale.xyy;
    float2 smbPixelUv = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gIsWorldSpaceMotionEnabled );
    float isInScreen = IsInScreen( smbPixelUv );
    float3 Xprev = X + motionVector * float( gIsWorldSpaceMotionEnabled != 0 );

    // Internal data
    uint bits;
    float4 internalData1 = UnpackInternalData1( gIn_Data1[ pixelPos ] );
    float3 internalData2 = UnpackInternalData2( gIn_Data2[ pixelPos ], viewZ, bits );

    float virtualHistoryAmount = internalData2.x;
    float hitDistScaleForTracking = internalData2.y;
    float curvature = internalData2.z;
    float4 smbOcclusion = float4( ( bits & uint4( 4, 8, 16, 32 ) ) != 0 );

    float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );

    STL::Filtering::Bilinear smbBilinearFilter = STL::Filtering::GetBilinearFilter( saturate( smbPixelUv ), gRectSizePrev );

    float4 smbOcclusionWeights = STL::Filtering::GetBilinearCustomWeights( smbBilinearFilter, smbOcclusion ); // TODO: only for "WithMaterialID" even if test is disabled
    bool smbIsCatromAllowed = ( bits & 2 ) != 0 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS; // TODO: only for "WithMaterialID" even if test is disabled

    float footprintQuality = STL::Filtering::ApplyBilinearFilter( smbOcclusion.x, smbOcclusion.y, smbOcclusion.z, smbOcclusion.w, smbBilinearFilter );
    footprintQuality = STL::Math::Sqrt01( footprintQuality );

    // Diffuse
    #ifdef REBLUR_DIFFUSE
        // Sample history - surface motion
        float4 smbDiffHistory;
        float4 smbDiffShHistory;
        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( smbPixelUv ) * gRectSizePrev, gInvScreenSize,
            smbOcclusionWeights, smbIsCatromAllowed,
            gIn_Diff_StabilizedHistory, smbDiffHistory
            #ifdef REBLUR_SH
                , gIn_DiffSh_StabilizedHistory, smbDiffShHistory
            #endif
        );

        // Avoid negative values
        smbDiffHistory = ClampNegativeToZero( smbDiffHistory );

        // Compute antilag
        float diffAntilag = ComputeAntilagScale(
            smbDiffHistory, diff, diffM1, diffSigma,
            gAntilagMinMaxThreshold, gAntilagSigmaScale, gStabilizationStrength,
            curvature * pixelSize, internalData1.xy
        );

        // Clamp history and combine with the current frame
        float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, internalData1.x );

        smbDiffHistory = STL::Color::Clamp( diffM1, diffSigma * diffTemporalAccumulationParams.y, smbDiffHistory );
        #ifdef REBLUR_SH
            smbDiffShHistory = STL::Color::Clamp( diffShM1, diffShSigma * diffTemporalAccumulationParams.y, smbDiffShHistory );
        #endif

        float diffHistoryWeight = REBLUR_TS_ACCUM_TIME / ( 1.0 + REBLUR_TS_ACCUM_TIME );
        diffHistoryWeight *= diffTemporalAccumulationParams.x;
        diffHistoryWeight *= footprintQuality;
        diffHistoryWeight *= diffAntilag;
        diffHistoryWeight *= gStabilizationStrength;
        diffHistoryWeight = 1.0 - diffHistoryWeight;

        float4 diffResult = MixHistoryAndCurrent( smbDiffHistory, diff, diffHistoryWeight );
        #ifdef REBLUR_SH
            float4 diffShResult = MixHistoryAndCurrent( smbDiffShHistory, diffSh, diffHistoryWeight );
        #endif

        // Debug output
        #if( REBLUR_DEBUG != 0 )
            uint diffMode = REBLUR_DEBUG;
            if( diffMode == 1 ) // Accumulated frame num
                diffResult.w = 1.0 - saturate( internalData1.x / ( 1.0 + gMaxAccumulatedFrameNum ) ); // map history reset to red
            else if( diffMode == 2 ) // Error
                diffResult.w = internalData1.y;

            // Show how colorization represents 0-1 range on the bottom
            diffResult.xyz = STL::Color::ColorizeZucconi( pixelUv.y > 0.96 ? pixelUv.x : diffResult.w );
            diffResult.xyz = pixelUv.y > 0.98 ? pixelUv.x : diffResult.xyz;

            #if( REBLUR_USE_YCOCG == 1 )
                diffResult.xyz = _NRD_LinearToYCoCg( diffResult.xyz );
            #endif
        #endif

        // Output
        gOut_Diff[ pixelPos ] = diffResult;
        #ifdef REBLUR_SH
            gOut_DiffSh[ pixelPos ] = diffShResult;
        #endif

        // Increment history length
        internalData1.x += 1.0;

        // Apply anti-lag
        float diffMinAccumSpeed = min( internalData1.x, REBLUR_FIXED_FRAME_NUM ) * REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX;
        internalData1.x = lerp( diffMinAccumSpeed, internalData1.x, diffAntilag );
    #endif

    // Specular
    #ifdef REBLUR_SPECULAR
        // Current hit distance
        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
        hitDistForTracking *= hitDistScale;
        hitDistForTracking *= hitDistScaleForTracking;

        // Sample history - surface motion
        float4 smbSpecHistory;
        float4 smbSpecShHistory;
        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            saturate( smbPixelUv ) * gRectSizePrev, gInvScreenSize,
            smbOcclusionWeights, smbIsCatromAllowed,
            gIn_Spec_StabilizedHistory, smbSpecHistory
            #ifdef REBLUR_SH
                , gIn_SpecSh_StabilizedHistory, smbSpecShHistory
            #endif
        );

        // Sample history - virtual motion
        float3 V = GetViewVector( X );
        float NoV = abs( dot( N, V ) );
        float dominantFactor = STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughness, STL_SPECULAR_DOMINANT_DIRECTION_G2 );
        float3 Xvirtual = GetXvirtual( NoV, hitDistForTracking, curvature, X, Xprev, V, dominantFactor );
        float2 vmbPixelUv = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual, false );

        float4 vmbSpecHistory;
        #if( REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS == 1 )
            BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
                saturate( vmbPixelUv ) * gRectSizePrev, gInvScreenSize,
                0, true,
                gIn_Spec_StabilizedHistory, vmbSpecHistory
            );
        #else
            vmbSpecHistory = gIn_Spec_StabilizedHistory.SampleLevel( gLinearClamp, vmbPixelUv, 0 );
        #endif

        #ifdef REBLUR_SH
            float4 vmbSpecShHistory = gIn_SpecSh_StabilizedHistory.SampleLevel( gLinearClamp, vmbPixelUv, 0 );
        #endif

        // Avoid negative values
        smbSpecHistory = ClampNegativeToZero( smbSpecHistory );
        vmbSpecHistory = ClampNegativeToZero( vmbSpecHistory );

        // Combine surface and virtual motion
        float4 specHistory = lerp( smbSpecHistory, vmbSpecHistory, virtualHistoryAmount );
        #ifdef REBLUR_SH
            float4 specShHistory = lerp( smbSpecShHistory, vmbSpecShHistory, virtualHistoryAmount );
        #endif

        // Compute antilag
        float specAntilag = ComputeAntilagScale(
            specHistory, spec, specM1, specSigma,
            gAntilagMinMaxThreshold, gAntilagSigmaScale, gStabilizationStrength,
            curvature * pixelSize, internalData1.zw, roughness );

        // Clamp history and combine with the current frame
        float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, internalData1.z );

        specHistory = STL::Color::Clamp( specM1, specSigma * specTemporalAccumulationParams.y, specHistory );
        #ifdef REBLUR_SH
            specShHistory = STL::Color::Clamp( specShM1, specShSigma * specTemporalAccumulationParams.y, specShHistory );
        #endif

        float specHistoryWeight = REBLUR_TS_ACCUM_TIME / ( 1.0 + REBLUR_TS_ACCUM_TIME );
        specHistoryWeight *= specTemporalAccumulationParams.x;
        specHistoryWeight *= footprintQuality;
        specHistoryWeight *= specAntilag; // this is important
        specHistoryWeight *= gStabilizationStrength;
        specHistoryWeight = 1.0 - specHistoryWeight;

        float4 specResult = MixHistoryAndCurrent( specHistory, spec, specHistoryWeight, roughness );
        #ifdef REBLUR_SH
            float4 specShResult = MixHistoryAndCurrent( specShHistory, specSh, specHistoryWeight, roughness );
        #endif

        // Debug output
        #if( REBLUR_DEBUG != 0 )
            uint specMode = REBLUR_DEBUG;
            if( specMode == 1 ) // Accumulated frame num
                specResult.w = 1.0 - saturate( internalData1.z / ( 1.0 + gMaxAccumulatedFrameNum ) ); // map history reset to red
            else if( specMode == 2 ) // Error
                specResult.w = internalData1.w; // can be > 1.0
            else if( specMode == 3 ) // Curvature magnitude
                specResult.w = abs( curvature * pixelSize );
            else if( specMode == 4 ) // Curvature sign
                specResult.w = curvature * pixelSize < 0 ? 1 : 0;
            else if( specMode == 5 ) // Virtual history amount
                specResult.w = virtualHistoryAmount;
            else if( specMode == 6 ) // Hit dist scale for tracking
                specResult.w = hitDistScaleForTracking;
            else if( specMode == 7 ) // Parallax
            {
                // or
                specResult.w = ComputeParallax( Xprev - gCameraDelta.xyz, gOrthoMode == 0.0 ? pixelUv : smbPixelUv, gWorldToClip, gRectSize, gUnproject, gOrthoMode );

                // or
                //specResult.w = ComputeParallax( Xprev + gCameraDelta.xyz, gOrthoMode == 0.0 ? smbPixelUv : pixelUv, gWorldToClipPrev, gRectSize, gUnproject, gOrthoMode );
            }

            // Show how colorization represents 0-1 range on the bottom
            specResult.xyz = STL::Color::ColorizeZucconi( pixelUv.y > 0.96 ? pixelUv.x : specResult.w );
            specResult.xyz = pixelUv.y > 0.98 ? pixelUv.x : specResult.xyz;

            #if( REBLUR_USE_YCOCG == 1 )
                specResult.xyz = _NRD_LinearToYCoCg( specResult.xyz );
            #endif
        #endif

        // Output
        gOut_Spec[ pixelPos ] = specResult;
        #ifdef REBLUR_SH
            gOut_SpecSh[ pixelPos ] = specShResult;
        #endif

        // Increment history length
        internalData1.z += 1.0;

        // Apply anti-lag
        float specMinAccumSpeed = min( internalData1.z, REBLUR_FIXED_FRAME_NUM ) * REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX;
        internalData1.z = lerp( specMinAccumSpeed, internalData1.z, specAntilag );
    #endif

    gOut_AccumSpeeds_MaterialID[ pixelPos ] = PackAccumSpeedsMaterialID( internalData1.x, internalData1.z, materialID );
}
