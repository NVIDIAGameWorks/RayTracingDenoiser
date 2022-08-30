/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef REBLUR_SPATIAL_MODE
    #error REBLUR_SPATIAL_MODE must be defined!
#endif

#if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
    #define POISSON_SAMPLE_NUM      REBLUR_PRE_BLUR_POISSON_SAMPLE_NUM
    #define POISSON_SAMPLES( i )    REBLUR_PRE_BLUR_POISSON_SAMPLES( i )
#else
    #define POISSON_SAMPLE_NUM      REBLUR_POISSON_SAMPLE_NUM
    #define POISSON_SAMPLES( i )    REBLUR_POISSON_SAMPLES( i )
#endif

{
    float smc = GetSpecMagicCurve2( roughness );

#if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
    float2 sum = hasData;

    if( radius != 0.0 )
    {
        float specNonLinearAccumSpeed = REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED;
#else
        float2 sum = 1.0;

        // IMPORTANT: keep an eye on tests:
        // - 51 and 128: outlines without TAA
        // - 81 and 117: cleanness in disoccluded regions
        float boost = 1.0 - GetFadeBasedOnAccumulatedFrames( internalData1.z );
        boost *= 1.0 - STL::BRDF::Pow5( NoV );
        boost *= smc;

        float specNonLinearAccumSpeed = 1.0 / ( 1.0 + ( 1.0 - boost ) * internalData1.z );
        float radius = gBlurRadius * ( 1.0 + 2.0 * boost ) / 3.0;
#endif
        radius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gSpecLobeTrimmingParams.xyz );

        float fractionScale = 1.0;
    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        fractionScale = REBLUR_PRE_BLUR_FRACTION_SCALE;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float radiusScale = 1.0;
        fractionScale = REBLUR_BLUR_FRACTION_SCALE;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        float radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
        fractionScale = REBLUR_POST_BLUR_FRACTION_SCALE;
    #endif

        float lobeAngleFractionScale = gLobeAngleFraction * fractionScale;
        float roughnessFractionScale = gRoughnessFraction * fractionScale;
    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        #ifdef REBLUR_HAS_DIRECTION_PDF
            lobeAngleFractionScale = 1.0;
            roughnessFractionScale = 1.0;
        #endif
    #endif

        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
        float hitDist = ExtractHitDist( spec ) * hitDistScale;

        // Min blur radius
        float4 Dv = STL::ImportanceSampling::GetSpecularDominantDirection( Nv, Vv, roughness, STL_SPECULAR_DOMINANT_DIRECTION_G2 );
        float NoD = abs( dot( Nv, Dv.xyz ) );
        float lobeTanHalfAngle = STL::ImportanceSampling::GetSpecularLobeTanHalfAngle( roughness );
        float lobeRadius = hitDist * NoD * lobeTanHalfAngle;
        float minBlurRadius = lobeRadius / PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ + hitDist * Dv.w );

        // Hit distance factor ( tests 76, 95, 120 )
        // TODO: reduce "hitDistFactor" influence if reprojection confidence is low?
        // TODO: if luminance stoppers are used, blur radius should depend less on "hitDistFactor"
        float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float hitDistFactor = GetHitDistFactor( hitDist * NoD, frustumHeight ); // this version beter follows the lobe...
    #else
        float hitDistFactor = GetHitDistFactor( hitDist, frustumHeight ); // TODO: ...but this one is needed to avoid under-blurring

        float relaxedHitDistFactor = lerp( 1.0, hitDistFactor, roughness );
        hitDistFactor = lerp( hitDistFactor, relaxedHitDistFactor, specNonLinearAccumSpeed );
    #endif

        // Blur radius
        float blurRadius = radius * hitDistFactor * smc;
    #if( REBLUR_SPATIAL_MODE != REBLUR_PRE_BLUR )
        blurRadius *= lerp( gMinConvergedStateBaseRadiusScale, 1.0, specNonLinearAccumSpeed );
    #endif
        blurRadius = min( blurRadius, minBlurRadius );

        // Blur bias and scale
    #if( REBLUR_SPATIAL_MODE != REBLUR_PRE_BLUR )
        blurRadius += max( internalData1.w * gBlurRadiusScale, 2.0 * roughness ) * smc; // TODO: 2.0 * roughness * hitDistFactor?
        blurRadius *= radiusScale;
        blurRadius *= float( radius != 0 );
    #endif

        // Denoising
        float2x3 TvBv = GetKernelBasis( Dv.xyz, Nv, NoD, roughness, specNonLinearAccumSpeed );

        float worldRadius = PixelRadiusToWorld( gUnproject, gOrthoMode, blurRadius, viewZ );
        TvBv[ 0 ] *= worldRadius;
        TvBv[ 1 ] *= worldRadius;

        float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, specNonLinearAccumSpeed );
        float normalWeightParams = GetNormalWeightParams( specNonLinearAccumSpeed, lobeAngleFractionScale, roughness );
        float2 hitDistanceWeightParams = GetHitDistanceWeightParams( ExtractHitDist( spec ), specNonLinearAccumSpeed, roughness );
        float2 roughnessWeightParams = GetRoughnessWeightParams( roughness, roughnessFractionScale );

    #ifdef REBLUR_HAS_DIRECTION_PDF
        uint2 p = pixelPos;
        if( gSpecCheckerboard != 2 )
            p.x >>= 1;

        float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_Spec_DirectionPdf[ gRectOrigin + p ] );
        float3 L = dirPdf.xyz;
        float3 V = STL::Geometry::RotateVector( gViewToWorld, Vv );
        float3 H = normalize( L + V );
        float NoL = saturate( dot( N, L ) );
        float NoH = saturate( dot( N, H ) );
        float VoH = saturate( dot( V, H ) );
        float D = STL::BRDF::DistributionTerm( roughness, NoH );
        float G = STL::BRDF::GeometryTermMod_SmithUncorrelated( roughness, NoL, NoV, VoH, NoH );

        // IMPORTANT: no F at least because we don't know Rf0, PDF should not include it too
        sum *= D * G * NoL / dirPdf.w;
    #endif

        float2 minHitDistWeight = REBLUR_HIT_DIST_MIN_WEIGHT * fractionScale;
    #if( REBLUR_SPATIAL_MODE != REBLUR_PRE_BLUR )
        minHitDistWeight.x = 0.0; // TODO: review
    #endif

        // TODO: previosuly "minHitDistWeight" was adjusted by specular reprojection confidence
        //minHitDistWeight = lerp( lerp( minHitDistWeight, 1.0, hitDistFactor ), minHitDistWeight, confidence );

        // Sampling
        float minHitDist = ExtractHitDist( spec );

        spec *= Xxxy( sum );
    #ifdef REBLUR_SH
        specSh *= Xxxy( sum );
    #endif

        [unroll]
        for( uint n = 0; n < POISSON_SAMPLE_NUM; n++ )
        {
            float3 offset = POISSON_SAMPLES( n );

            // Sample coordinates
        #if( REBLUR_USE_SCREEN_SPACE_SAMPLING == 1 || REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 uv = pixelUv + STL::Geometry::RotateVector( rotator, offset.xy ) * gInvScreenSize * blurRadius;
        #else
            float2 uv = GetKernelSampleCoordinates( gViewToClip, offset, Xv, TvBv[ 0 ], TvBv[ 1 ], rotator );
        #endif

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            if( gSpecCheckerboard != 2 )
                uv = ApplyCheckerboardShift( uv, gSpecCheckerboard, n, gRectSize, gInvRectSize, gFrameIndex );
        #endif

            float2 uvScaled = uv * gResolutionScale;

            // Fetch data
        #if( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
            float zs = UnpackViewZ( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled, 0 ) );
        #else
            float zs = abs( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 ) );
        #endif

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 checkerboardUvScaled = float2( uvScaled.x * ( gSpecCheckerboard != 2 ? 0.5 : 1.0 ), uvScaled.y ) + gRectOffset;
            REBLUR_TYPE s = gIn_Spec.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            #ifdef REBLUR_SH
                float4 sh = gIn_SpecSh.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            #endif
        #else
            REBLUR_TYPE s = gIn_Spec.SampleLevel( gNearestMirror, uvScaled, 0 );
            #ifdef REBLUR_SH
                float4 sh = gIn_SpecSh.SampleLevel( gNearestMirror, uvScaled, 0 );
            #endif
        #endif

            float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, zs, gOrthoMode );

            float materialIDs;
            float4 Ns = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 );
            Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

            // Sample weight
            float w = IsInScreen( uv );
            w *= CompareMaterials( materialID, materialIDs, gSpecMaterialMask );
            w *= GetGaussianWeight( offset.z );
            w *= GetCombinedWeight( geometryWeightParams, Nv, Xvs, normalWeightParams, N, Ns, roughnessWeightParams );

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            #ifdef REBLUR_HAS_DIRECTION_PDF
                float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_Spec_DirectionPdf.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 ) );
            #endif
            #ifdef REBLUR_SH
                float4 dirPdf = float4( sh.xyz * rsqrt( dot( sh.xyz, sh.xyz ) + 1e-7 ), sh.w );
            #endif
            #if( defined REBLUR_HAS_DIRECTION_PDF || defined REBLUR_HAS_DIRECTION_PDF )
                float3 L = dirPdf.xyz;
                float3 H = normalize( L + V );
                float NoL = saturate( dot( N, L ) );
                float NoH = saturate( dot( N, H ) );
                float VoH = saturate( dot( V, H ) );
                float D = STL::BRDF::DistributionTerm( roughness, NoH );
                float G = STL::BRDF::GeometryTermMod_SmithUncorrelated( roughness, NoL, NoV, VoH, NoH );

                // IMPORTANT: no F at least because we don't know Rf0, PDF should not include it too
                w *= D * G * NoL / dirPdf.w;
            #endif

            // Decrease weight for samples that most likely are very close to reflection contact which should not be blurred
            float d = length( Xvs - Xv );
            float h = ExtractHitDist( s ) * hitDistScale; // roughness weight will handle the rest
            float t = h / ( hitDist + d + frustumHeight * gInvRectSize.y );
            w *= lerp( saturate( t ), 1.0, STL::Math::LinearStep( 0.5, 1.0, roughness ) );

            // Adjust blur radius "on the fly" if taps have short hit distances
            float hitDistFactorAtSample = GetHitDistFactor( h, frustumHeight );
            float blurRadiusScale = lerp( hitDistFactorAtSample, 1.0, NoD );
            blurRadius *= lerp( 1.0, blurRadiusScale, n / ( 1.0 + n ) );

            // Min hit distance for tracking
            if( w != 0.0 )
                minHitDist = min( ExtractHitDist( s ), minHitDist ); // TODO: is there something better than "min"?
        #endif

            // Weight separation - radiance and hitDist
            float2 ww = w;
        #ifndef REBLUR_HAS_DIRECTION_PDF
            ww *= lerp( minHitDistWeight, 1.0, GetHitDistanceWeight( hitDistanceWeightParams, ExtractHitDist( s ) ) );
        #endif

            // Get rid of potentially bad values outside of the screen
            s = w ? s : 0;

            // Accumulate
            sum += ww;
            spec += s * Xxxy( ww );
        #ifdef REBLUR_SH
            specSh += sh * Xxxy( ww );
        #endif
        }

        float2 invSum = STL::Math::PositiveRcp( sum );
        spec *= Xxxy( invSum );
    #ifdef REBLUR_SH
        specSh *= Xxxy( invSum );
    #endif

#if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        // Min hit distance modification
        minHitDist = lerp( minHitDist, ExtractHitDist( spec ), STL::BRDF::Pow5( NoD ) );

        // Output
        gOut_Spec_MinHitDist[ pixelPos ] = minHitDist;
    }

    // Checkerboard resolve ( if pre-pass failed )
    [branch]
    if( !hasData && sum.x == 0.0 )
    {
        REBLUR_TYPE s0 = gIn_Spec[ checkerboardPos.xy ];
        REBLUR_TYPE s1 = gIn_Spec[ checkerboardPos.zy ];

        spec *= saturate( 1.0 - wc.x - wc.y );
        spec += s0 * wc.x + s1 * wc.y;

    #ifdef REBLUR_SH
        float4 sh0 = gIn_SpecSh[ checkerboardPos.xy ];
        float4 sh1 = gIn_SpecSh[ checkerboardPos.zy ];

        specSh *= saturate( 1.0 - wc.x - wc.y );
        specSh += sh0 * wc.x + sh1 * wc.y;
    #endif
    }
#endif

    // Output
    gOut_Spec[ pixelPos ] = spec;
    #ifdef REBLUR_SH
        gOut_SpecSh[ pixelPos ] = specSh;
    #endif
}

#undef POISSON_SAMPLE_NUM
#undef POISSON_SAMPLES
