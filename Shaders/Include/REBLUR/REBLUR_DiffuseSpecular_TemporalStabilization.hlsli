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

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );

    #if( defined REBLUR_DIFFUSE )
        s_Diff[ sharedPos.y ][ sharedPos.x ] = gIn_Diff[ globalPos ];
    #endif

    #if( defined REBLUR_SPECULAR )
        s_Spec[ sharedPos.y ][ sharedPos.x ] = gIn_Spec[ globalPos ];
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

    [branch]
    if( viewZ > gDenoisingRange )
    {
        gOut_ViewZ_DiffAccumSpeed[ pixelPos ] = PackViewZAccumSpeed( NRD_INF, 0.0 );
        gOut_Normal_SpecAccumSpeed[ pixelPos ] = PackNormalAccumSpeedMaterialID( float3( 0, 0, 1 ), 0.0, 0 );

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

    #if( defined REBLUR_DIFFUSE )
        float4 diff = s_Diff[ smemPos.y ][ smemPos.x ];
        float4 diffM1 = diff;
        float4 diffM2 = diff * diff;
        float4 diffMin = NRD_INF;
        float4 diffMax = -NRD_INF;
    #endif

    #if( defined REBLUR_SPECULAR )
        float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
        float4 specM1 = spec;
        float4 specM2 = spec * spec;
        float4 specMin = NRD_INF;
        float4 specMax = -NRD_INF;
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

            #if( defined REBLUR_DIFFUSE )
                float4 d = s_Diff[ pos.y ][ pos.x ];
                diffM1 += d;
                diffM2 += d * d;
                diffMin = min( diffMin, d );
                diffMax = max( diffMax, d );
            #endif

            #if( defined REBLUR_SPECULAR )
                float4 s = s_Spec[ pos.y ][ pos.x ];
                specM1 += s;
                specM2 += s * s;
                specMin = min( specMin, s );
                specMax = max( specMax, s );
            #endif
        }
    }

    float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );

    #if( defined REBLUR_DIFFUSE )
        diffM1 *= invSum;
        diffM2 *= invSum;
        float4 diffSigma = GetStdDev( diffM1, diffM2 );

        float4 diffClamped = clamp( diff, diffMin, diffMax );
        diff = lerp( diff, diffClamped, gStabilizationStrength * float( gBlurRadius != 0 ) );
    #endif

    #if( defined REBLUR_SPECULAR )
        specM1 *= invSum;
        specM2 *= invSum;
        float4 specSigma = GetStdDev( specM1, specM2 );

        float4 specClamped = clamp( spec, specMin, specMax );
        spec = lerp( spec, specClamped, gStabilizationStrength * float( gBlurRadius != 0 ) * GetSpecMagicCurve( roughness ) );
    #endif

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gIsWorldSpaceMotionEnabled );
    float isInScreen = IsInScreen( pixelUvPrev );
    float3 Xprev = X + motionVector * float( gIsWorldSpaceMotionEnabled != 0 );

    // Internal data
    float curvature;
    uint bits;
    float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], viewZ, curvature, bits );
    float2 diffInternalData = internalData.xy;
    float2 specInternalData = internalData.zw;
    float4 occlusion = float4( ( bits & uint4( 4, 8, 16, 32 ) ) != 0 );

    float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );

    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float footprintQuality = STL::Filtering::ApplyBilinearFilter( occlusion.x, occlusion.y, occlusion.z, occlusion.w, bilinearFilterAtPrevPos );
    isInScreen *= STL::Math::Sqrt01( footprintQuality );

    // Sample history ( surface motion )
    float4 surfaceWeightsWithOcclusion = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, occlusion );

    bool isCatRomAllowedForSurfaceMotion = ( bits & 2 ) != 0 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS;
    float4 diffHistory, specHistorySurface;
    BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
        saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize, surfaceWeightsWithOcclusion,
        gLinearClamp, isCatRomAllowedForSurfaceMotion
        #if( defined REBLUR_DIFFUSE )
            , gIn_HistoryStabilized_Diff, diffHistory
        #endif
        #if( defined REBLUR_SPECULAR )
            , gIn_HistoryStabilized_Spec, specHistorySurface
        #endif
    );

    // Diffuse
    #if( defined REBLUR_DIFFUSE )
        // Antilag
        float diffAntilag = ComputeAntilagScale(
            diffHistory, diff, diffM1, diffSigma,
            gAntilagMinMaxThreshold, gAntilagSigmaScale, gStabilizationStrength,
            curvature * pixelSize, diffInternalData.x
        );

        // TODO: diffAntilag = lerp( diffAntilag, 1.0, diffData.x )?

        float diffMinAccumSpeed = min( diffInternalData.y, REBLUR_FIXED_FRAME_NUM * REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX );
        diffInternalData.y = lerp( diffMinAccumSpeed, diffInternalData.y, diffAntilag );

        // Clamp history and combine with the current frame
        float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffInternalData.y );
        diffTemporalAccumulationParams.x *= diffAntilag;

        diffHistory = STL::Color::Clamp( diffM1, diffSigma * diffTemporalAccumulationParams.y, diffHistory, REBLUR_USE_COLOR_CLAMPING_AABB );

        float diffHistoryWeight = ( gFramerateScale * REBLUR_TS_ACCUM_TIME ) / ( 1.0 + gFramerateScale * REBLUR_TS_ACCUM_TIME );
        diffHistoryWeight *= diffTemporalAccumulationParams.x;
        diffHistoryWeight *= gStabilizationStrength;
        diffHistoryWeight = 1.0 - diffHistoryWeight;

        float4 diffResult = MixHistoryAndCurrent( diffHistory, diff, diffHistoryWeight );

        // Output
        gOut_Diff[ pixelPos ] = diffResult;

        #if( REBLUR_DEBUG != 0 )
            // User-visible debug output
            uint diffMode = REBLUR_DEBUG;
            if( diffMode == 1 ) // Accumulated frame num
                diffResult.w = 1.0 - saturate( diffInternalData.y / ( gMaxAccumulatedFrameNum + 1.0 ) ); // map history reset to red
            else if( diffMode == 2 ) // Antilag
                diffResult.w = diffAntilag;

            // Show how colorization represents 0-1 range on the bottom
            diffResult.xyz = STL::Color::ColorizeZucconi( pixelUv.y > 0.96 ? pixelUv.x : diffResult.w );
            diffResult.xyz = pixelUv.y > 0.98 ? pixelUv.x : diffResult.xyz;
        #endif

        gOut_DiffCopy[ pixelPos ] = diffResult;
    #endif
    gOut_ViewZ_DiffAccumSpeed[ pixelPos ] = PackViewZAccumSpeed( viewZ, diffInternalData.y );

    // Specular
    #if( defined REBLUR_SPECULAR )
        float4 specData = gIn_SpecData[ pixelPos ];
        float virtualHistoryConfidence = specData.y;
        float virtualHistoryAmount = specData.z;
        float hitDistScaleForTracking = specData.w;

        // Current hit distance
        float hitDist = min( specMin.w, spec.w );
        hitDist *= hitDistScaleForTracking;

        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
        hitDist *= hitDistScale;

        // Sample virtual history
        float3 V = GetViewVector( X );
        float NoV = abs( dot( N, V ) );
        float3 Xvirtual = GetXvirtual( NoV, hitDist, curvature, X, Xprev, V, roughness );
        float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual, false );

        float4 specHistoryVirtual;
        BicubicFilterNoCorners(
            saturate( pixelUvVirtualPrev ) * gRectSizePrev, gInvScreenSize,
            gLinearClamp, REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS,
            gIn_HistoryStabilized_Spec, specHistoryVirtual
        );

        // Parallax
        float parallaxSurface = ComputeParallax( Xprev, pixelUv, pixelUvPrev, gWorldToClip, gCameraDelta.xyz, gRectSize, gUnproject, gOrthoMode );

        float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
        float hitDistFactor = GetHitDistFactor( spec.w * hitDistScale, frustumHeight, 0.0 ); // hitDist is for tracking only
        parallaxSurface *= hitDistFactor;

        // Combine surface and virtual motion
        float4 specHistory = lerp( specHistorySurface, specHistoryVirtual, virtualHistoryAmount );

        // Antilag
        float specAntilag = ComputeAntilagScale(
            specHistory, spec, specM1, specSigma,
            gAntilagMinMaxThreshold, gAntilagSigmaScale, gStabilizationStrength,
            curvature * pixelSize, specInternalData.x, roughness );

        specAntilag = lerp( 1.0, specAntilag, virtualHistoryConfidence * virtualHistoryConfidence );
        specAntilag = lerp( specAntilag, 1.0, specData.x );

        float specMinAccumSpeed = min( specInternalData.y, REBLUR_FIXED_FRAME_NUM * REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX );
        specInternalData.y = lerp( specMinAccumSpeed, specInternalData.y, specAntilag );

        // Clamp history and combine with the current frame
        float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specInternalData.y, parallaxSurface, roughness, virtualHistoryAmount );
        specTemporalAccumulationParams.x *= specAntilag;

        specHistory = STL::Color::Clamp( specM1, specSigma * specTemporalAccumulationParams.y, specHistory, REBLUR_USE_COLOR_CLAMPING_AABB );

        float specHistoryWeight = ( gFramerateScale * REBLUR_TS_ACCUM_TIME ) / ( 1.0 + gFramerateScale * REBLUR_TS_ACCUM_TIME );
        specHistoryWeight *= specTemporalAccumulationParams.x;
        specHistoryWeight *= gStabilizationStrength;
        specHistoryWeight = 1.0 - specHistoryWeight;

        float4 specResult = MixHistoryAndCurrent( specHistory, spec, specHistoryWeight, roughness );

        // Output
        gOut_Spec[ pixelPos ] = specResult;

        #if( REBLUR_DEBUG != 0 )
            // User-visible debug output
            uint specMode = REBLUR_DEBUG;
            if( specMode == 1 ) // Accumulated frame num
                specResult.w = 1.0 - saturate( specInternalData.y / ( gMaxAccumulatedFrameNum + 1.0 ) ); // map history reset to red
            else if( specMode == 2 ) // Antilag
                specResult.w = specAntilag;
            else if( specMode == 3 ) // Error
                specResult.w = specData.x;
            else if( specMode == 4 ) // Curvature magnitude
                specResult.w = abs( curvature * pixelSize );
            else if( specMode == 5 ) // Curvature sign
                specResult.w = curvature * pixelSize < 0 ? 1 : 0;
            else if( specMode == 6 ) // Virtual history amount
                specResult.w = virtualHistoryAmount;
            else if( specMode == 7 ) // Hit dist scale for tracking
                specResult.w = hitDistScaleForTracking;
            else if( specMode == 8 ) // Virtual history confidence
                specResult.w = 1.0 - virtualHistoryConfidence; // map zero confidence to red
            else if( specMode == 9 ) // Parallax
                specResult.w = parallaxSurface;

            // Show how colorization represents 0-1 range on the bottom
            specResult.xyz = STL::Color::ColorizeZucconi( pixelUv.y > 0.96 ? pixelUv.x : specResult.w );
            specResult.xyz = pixelUv.y > 0.98 ? pixelUv.x : specResult.xyz;
        #endif

        gOut_SpecCopy[ pixelPos ] = specResult;
    #endif
    gOut_Normal_SpecAccumSpeed[ pixelPos ] = PackNormalAccumSpeedMaterialID( N, specInternalData.y, materialID );
}
