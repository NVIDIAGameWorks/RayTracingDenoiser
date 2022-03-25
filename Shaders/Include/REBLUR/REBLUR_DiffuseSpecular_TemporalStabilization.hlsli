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
    uint2 globalIdUser = gRectOrigin + globalPos;

    s_ViewZ[ sharedPos.y ][ sharedPos.x ] = abs( gIn_ViewZ[ globalIdUser ] );

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
    int2 smemPos = threadPos + BORDER;
    float viewZ = s_ViewZ[ smemPos.y ][ smemPos.x ];

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
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );

    // Anti-firefly ( not needed for hit distance )
    float3 diffMaxInput = -NRD_INF;
    float3 diffMinInput = NRD_INF;

    float3 specMaxInput = -NRD_INF;
    float3 specMinInput = NRD_INF;

    // Local variance
    float viewZnearest = viewZ;
    int2 offseti = int2( BORDER, BORDER );
    float sum = 1.0;

    #if( defined REBLUR_DIFFUSE )
        float4 diff = s_Diff[ smemPos.y ][ smemPos.x ];
        float4 diffM1 = diff;
        float4 diffM2 = diff * diff;
    #endif

    #if( defined REBLUR_SPECULAR )
        float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
        float4 specM1 = spec;
        float4 specM2 = spec * spec;
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
            float z = s_ViewZ[ pos.y ][ pos.x ];

            int2 t1 = int2( dx, dy ) - BORDER;
            if( ( abs( t1.x ) + abs( t1.y ) == 1 ) && z < viewZnearest )
            {
                viewZnearest = z;
                offseti = int2( dx, dy );
            }

            // Weights are needed to avoid getting 1 pixel wide outline under motion on contrast objects
            float w = GetBilateralWeight( z, viewZ );
            sum += w;

            #if( defined REBLUR_DIFFUSE )
                float4 d = s_Diff[ pos.y ][ pos.x ];
                diffM1 += d * w;
                diffM2 += d * d * w;
                diffMaxInput = max( diffMaxInput, d.xyz );
                diffMinInput = min( diffMinInput, d.xyz );
            #endif

            #if( defined REBLUR_SPECULAR )
                float4 s = s_Spec[ pos.y ][ pos.x ];
                specM1 += s * w;
                specM2 += s * s * w;
                specMaxInput = max( specMaxInput, s.xyz );
                specMinInput = min( specMinInput, s.xyz );
            #endif
        }
    }

    float invSum = 1.0 / sum;

    #if( defined REBLUR_DIFFUSE )
        diffM1 *= invSum;
        diffM2 *= invSum;
        float4 diffSigma = GetStdDev( diffM1, diffM2 );

        float3 diffClamped = clamp( diff.xyz, diffMinInput, diffMaxInput );
        diff.xyz = lerp( diff.xyz, diffClamped, 1.0 - gReference );
    #endif

    #if( defined REBLUR_SPECULAR )
        specM1 *= invSum;
        specM2 *= invSum;
        float4 specSigma = GetStdDev( specM1, specM2 );

        float3 specClamped = clamp( spec.xyz, specMinInput, specMaxInput );
        spec.xyz = lerp( spec.xyz, specClamped, GetSpecMagicCurve( roughness ) * ( 1.0 - gReference ) );
    #endif

    // Compute previous pixel position
    offseti -= BORDER;
    float2 offset = float2( offseti ) * gInvRectSize;
    float3 Xvnearest = STL::Geometry::ReconstructViewPosition( pixelUv + offset, gFrustum, viewZnearest, gOrthoMode );
    float3 Xnearest = STL::Geometry::AffineTransform( gViewToWorld, Xvnearest );
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser + offseti ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv + offset, Xnearest, gWorldToClipPrev, motionVector, gIsWorldSpaceMotionEnabled );
    pixelUvPrev -= offset;

    float isInScreen = IsInScreen( pixelUvPrev );
    float3 Xprev = X + motionVector * float( gIsWorldSpaceMotionEnabled != 0 );

    // Compute parallax
    float parallax = ComputeParallax( X, Xprev, gCameraDelta, gOrthoMode != 0 );

    // Internal data
    float curvature;
    uint bits;
    float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], curvature, bits );
    float2 diffInternalData = internalData.xy;
    float2 specInternalData = internalData.zw;

    float4 error = gIn_Error[ pixelPos ];
    float virtualHistoryAmount = error.y;
    float2 isCatRomAllowed = float2( ( bits & uint2( 8, 16 ) ) != 0 );

    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float4 bilinearWeightsWithOcclusion = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, 1.0 );

    // Sample history ( surface motion )
    #if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
        float4 specHistorySurface;
        float4 diffHistory = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_HistoryStabilized_Diff, gIn_HistoryStabilized_Spec, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            bilinearWeightsWithOcclusion, isCatRomAllowed.x == 1.0 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS,
            specHistorySurface
        );
    #elif( defined REBLUR_DIFFUSE )
        float4 diffHistory = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_HistoryStabilized_Diff, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            bilinearWeightsWithOcclusion, isCatRomAllowed.x == 1.0 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS
        );
    #else
        float4 specHistorySurface = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_HistoryStabilized_Spec, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            bilinearWeightsWithOcclusion, isCatRomAllowed.x == 1.0 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS
        );
    #endif

    // Diffuse
    #if( defined REBLUR_DIFFUSE )
        // Antilag
        float diffAntilag = ComputeAntilagScale( diffInternalData.y, diffHistory, diff, diffM1, diffSigma, gAntilagMinMaxThreshold, gAntilagSigmaScale, curvature );

        float diffMipScale = lerp( 0.0, 1.0, REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX );
        float diffMinAccumSpeed = min( diffInternalData.y, GetMipLevel( 1.0 ) * diffMipScale );
        diffInternalData.y = lerp( diffMinAccumSpeed, diffInternalData.y, diffAntilag );

        // Clamp history and combine with the current frame
        float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffInternalData.y );
        diffTemporalAccumulationParams.x *= diffAntilag;

        diffHistory = STL::Color::Clamp( diffM1, diffSigma * diffTemporalAccumulationParams.y, diffHistory );

        float diffHistoryWeight = ( gFramerateScale * REBLUR_TS_ACCUM_TIME ) / ( 1.0 + gFramerateScale * REBLUR_TS_ACCUM_TIME );
        diffHistoryWeight *= diffTemporalAccumulationParams.x;
        diffHistoryWeight *= gStabilizationStrength;
        diffHistoryWeight = 1.0 - diffHistoryWeight;
        #if( REBLUR_DEBUG_SPATIAL_DENSITY_CHECK == 1 )
            diffHistoryWeight = 1.0;
        #endif

        float4 diffResult = MixHistoryAndCurrent( diffHistory, diff, diffHistoryWeight );
        diffResult = Sanitize( diffResult, diff );

        // Output
        gOut_Diff[ pixelPos ] = diffResult;

        #if( REBLUR_DEBUG != 0 )
            // User-visible debug output
            uint diffMode = REBLUR_DEBUG;
            if( diffMode == 1 ) // Accumulated frame num
                diffResult.w = saturate( diffInternalData.y / ( gMaxAccumulatedFrameNum + 1.0 ) );
            else if( diffMode == 2 ) // Error
                diffResult.w = error.x;

            diffResult.xyz = STL::Color::ColorizeZucconi( diffResult.w );
        #endif

        gOut_DiffCopy[ pixelPos ] = diffResult;
    #endif
    gOut_ViewZ_DiffAccumSpeed[ pixelPos ] = PackViewZAccumSpeed( viewZ, diffInternalData.y );

    // Specular
    #if( defined REBLUR_SPECULAR )
        // Current hit distance
        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
        float hitDist = spec.w * hitDistScale;

        // Sample virtual history
        float3 V = GetViewVector( X );
        float NoV = abs( dot( N, V ) );
        float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
        float realCurvature = GetRealCurvature( curvature, pixelSize, NoV );
        float3 Xvirtual = GetXvirtual( X, Xprev, V, NoV, roughness, hitDist, realCurvature );
        float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual, false );

        STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gRectSizePrev );
        float4 bilinearWeightsWithOcclusionVirtual = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevVirtualPos, 1.0 );

        float4 specHistoryVirtual = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_HistoryStabilized_Spec, gLinearClamp,
            saturate( pixelUvVirtualPrev ) * gRectSizePrev, gInvScreenSize,
            bilinearWeightsWithOcclusionVirtual, isCatRomAllowed.y == 1.0 && REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS
        );

        // Parallax correction
        float parallaxVirtual = ComputeParallax( Xvirtual, Xvirtual, gCameraDelta, gOrthoMode != 0 );
        float smc = GetSpecMagicCurve( roughness );
        parallax = max( parallax, parallaxVirtual * ( 1.0 - smc ) );

        float invFrustumHeight = 1.0 / PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );;
        float hitDistFactor = saturate( hitDist * invFrustumHeight );
        parallax *= hitDistFactor;

        // Virtual motion - hit distance similarity
        float hitDistFocused = ApplyThinLensEquation( hitDist, realCurvature );
        float hitDistVirtualFocused = ApplyThinLensEquation( specHistoryVirtual.w * hitDistScale, realCurvature );
        float hitDistDelta = abs( hitDistVirtualFocused - hitDistFocused ); // TODO: sigma can worsen! useful for high roughness only?
        float hitDistMax = max( hitDistVirtualFocused, hitDistFocused );
        hitDistDelta *= STL::Math::PositiveRcp( hitDistMax + viewZ );
        float virtualHistoryHitDistConfidence = 1.0 - STL::Math::SmoothStep( 0.0, 0.25, STL::Math::Sqrt01( hitDistDelta ) * SaturateParallax( parallax * REBLUR_TS_SIGMA_AMPLITUDE ) );

        float virtualHistoryConfidence = error.w;
        virtualHistoryConfidence *= virtualHistoryHitDistConfidence;

        // Virtual history clamping
        float responsiveAccumulationAmount = GetResponsiveAccumulationAmount( roughness );
        float sigmaScale = lerp( 1.0, 3.0, smc ) + smc * gFramerateScale * virtualHistoryConfidence;
        float4 specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual );
        float unclampedVirtualHistoryAmount = lerp( virtualHistoryConfidence, 1.0, smc * STL::Math::SmoothStep( 0.2, 0.4, roughness ) );
        unclampedVirtualHistoryAmount *= lerp( 1.0, smc * 0.5 + 0.5, responsiveAccumulationAmount );
        float4 specHistoryVirtualMixed = lerp( specHistoryVirtualClamped, specHistoryVirtual, unclampedVirtualHistoryAmount );

        // Combine surface and virtual motion
        float4 specHistory = MixSurfaceAndVirtualMotion( specHistorySurface, specHistoryVirtualMixed, virtualHistoryAmount, hitDistFactor );

        // Antilag
        float specAntilag = ComputeAntilagScale( specInternalData.y, specHistory, spec, specM1, specSigma, gAntilagMinMaxThreshold, gAntilagSigmaScale, curvature, roughness );

        float specMipScale = lerp( 1.0 - virtualHistoryConfidence, 1.0, REBLUR_USE_ANTILAG_NOT_INVOKING_HISTORY_FIX );
        float specMinAccumSpeed = min( specInternalData.y, GetMipLevel( roughness ) * specMipScale );
        specInternalData.y = lerp( specMinAccumSpeed, specInternalData.y, specAntilag );

        // Clamp history and combine with the current frame
        float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specInternalData.y, parallax, roughness, virtualHistoryAmount );
        specTemporalAccumulationParams.x *= specAntilag;

        specHistory = STL::Color::Clamp( specM1, specSigma * specTemporalAccumulationParams.y, specHistory );

        float specHistoryWeight = ( gFramerateScale * REBLUR_TS_ACCUM_TIME ) / ( 1.0 + gFramerateScale * REBLUR_TS_ACCUM_TIME );
        specHistoryWeight *= specTemporalAccumulationParams.x;
        specHistoryWeight *= gStabilizationStrength;
        specHistoryWeight = 1.0 - specHistoryWeight;
        #if( REBLUR_DEBUG_SPATIAL_DENSITY_CHECK == 1 )
            specHistoryWeight = 1.0;
        #endif

        float4 specResult = MixHistoryAndCurrent( specHistory, spec, specHistoryWeight, roughness );
        specResult = Sanitize( specResult, spec );

        // Output
        gOut_Spec[ pixelPos ] = specResult;

        #if( REBLUR_DEBUG != 0 )
            // User-visible debug output
            uint specMode = REBLUR_DEBUG;
            if( specMode == 1 ) // Accumulated frame num
                specResult.w = saturate( specInternalData.y / ( gMaxAccumulatedFrameNum + 1.0 ) );
            else if( specMode == 2 ) // Error
                specResult.w = error.z;
            else if( specMode == 3 ) // Curvature magnitude
                specResult.w = abs( curvature );
            else if( specMode == 4 ) // Curvature sign
                specResult.w = curvature < 0 ? 1 : 0;
            else if( specMode == 5 ) // Virtual history amount
                specResult.w = virtualHistoryAmount;
            else if( specMode == 6 ) // Virtual history confidence
                specResult.w = error.w;
            else if( specMode == 7 ) // Parallax
                specResult.w = parallax;

            // Show how colorization represents 0-1 range on the bottom
            specResult.xyz = STL::Color::ColorizeZucconi( pixelUv.y > 0.95 ? pixelUv.x : specResult.w );
        #endif

        gOut_SpecCopy[ pixelPos ] = specResult;
    #endif
    gOut_Normal_SpecAccumSpeed[ pixelPos ] = PackNormalAccumSpeedMaterialID( N, specInternalData.y, materialID );
}
