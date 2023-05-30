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
    float sum = hasData;

    if( gSpecPrepassBlurRadius != 0.0 )
    {
        float specNonLinearAccumSpeed = REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED;
#else
        float sum = 1.0;
#endif

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
        float frustumSize = GetFrustumSize( gMinRectDimMulUnproject, gOrthoMode, viewZ );
        float hitDistFactor = GetHitDistFactor( hitDist * NoD, frustumSize );

        // Blur radius
    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        // Add viewZ-dependent threshold to avoid extreme small values in the special test
        hitDist = max( hitDist, frustumSize * max( gInvRectSize.x, gInvRectSize.y ) );

        // Blur radius - main
        float blurRadius = gSpecPrepassBlurRadius;
        blurRadius *= hitDistFactor * smc;
        blurRadius = min( blurRadius, minBlurRadius );
    #else
        // Tests 144, 145, 150, 153, 23e
        hitDistFactor = lerp( hitDistFactor, 1.0, data1.w );

        // IMPORTANT: keep an eye on tests:
        // - 51 and 128: outlines without TAA
        // - 81 and 117: cleanness in disoccluded regions
        float boost = 1.0 - GetFadeBasedOnAccumulatedFrames( data1.z );
        boost *= 1.0 - STL::BRDF::Pow5( NoV );
        boost *= smc;

        float specNonLinearAccumSpeed = 1.0 / ( 1.0 + ( 1.0 - boost ) * data1.z );

        float relaxedHitDistFactor = lerp( 1.0, hitDistFactor, roughness );
        hitDistFactor = lerp( hitDistFactor, relaxedHitDistFactor, specNonLinearAccumSpeed );

        // Blur radius - main
        float blurRadius = gBlurRadius * ( 1.0 + 2.0 * boost ) / 3.0;
        blurRadius *= hitDistFactor * smc;
        blurRadius = min( blurRadius, minBlurRadius );

        // Blur radius - addition to avoid underblurring
        blurRadius += smc;

        // Blur radius - scaling
        blurRadius *= radiusScale;
        blurRadius *= float( gBlurRadius != 0 );
    #endif

        // Weights
        float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumSize, Xv, Nv, specNonLinearAccumSpeed );
        float normalWeightParams = GetNormalWeightParams( specNonLinearAccumSpeed, lobeAngleFractionScale, roughness );
        float2 hitDistanceWeightParams = GetHitDistanceWeightParams( ExtractHitDist( spec ), specNonLinearAccumSpeed, roughness );
        float2 roughnessWeightParams = GetRoughnessWeightParams( roughness, roughnessFractionScale );
        float minHitDistWeight = REBLUR_HIT_DIST_MIN_WEIGHT * fractionScale;

        // Sampling
        float minHitDist = ExtractHitDist( spec );
        float minHitDistLuma = GetLuma( spec );

        spec *= sum;
    #ifdef REBLUR_SH
        specSh *= sum;
    #endif

        float2x3 TvBv = GetKernelBasis( Dv.xyz, Nv, NoD, roughness, specNonLinearAccumSpeed );

        float worldRadius = PixelRadiusToWorld( gUnproject, gOrthoMode, blurRadius, viewZ );
        TvBv[ 0 ] *= worldRadius;
        TvBv[ 1 ] *= worldRadius;

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
            float zs = UnpackViewZ( gIn_ViewZ.SampleLevel( gNearestClamp, uvScaled, 0 ) );
        #else
            float zs = abs( gIn_ViewZ.SampleLevel( gNearestClamp, uvScaled + gRectOffset, 0 ) );
        #endif

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 checkerboardUvScaled = float2( uvScaled.x * ( gSpecCheckerboard != 2 ? 0.5 : 1.0 ), uvScaled.y ) + gRectOffset;
        #else
            float2 checkerboardUvScaled = uvScaled;
        #endif

            REBLUR_TYPE s = gIn_Spec.SampleLevel( gNearestClamp, checkerboardUvScaled, 0 );

            float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, zs, gOrthoMode );

            float materialIDs;
            float4 Ns = gIn_Normal_Roughness.SampleLevel( gNearestClamp, uvScaled + gRectOffset, 0 );
            Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

            // Sample weights
            float w = CompareMaterials( materialID, materialIDs, gSpecMaterialMask );
            w *= GetGaussianWeight( offset.z );
            w *= GetCombinedWeight( geometryWeightParams, Nv, Xvs, normalWeightParams, N, Ns, roughnessWeightParams );
            w *= lerp( minHitDistWeight, 1.0, GetHitDistanceWeight( hitDistanceWeightParams, ExtractHitDist( s ) ) );

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            // Decrease weight for samples that most likely are very close to reflection contact which should not be blurred
            float d = length( Xvs - Xv );
            float h = ExtractHitDist( s ) * hitDistScale; // roughness weight will handle the rest
            float t = h / ( hitDist + d );
            w *= lerp( saturate( t ), 1.0, STL::Math::LinearStep( 0.5, 1.0, roughness ) );

            // Adjust blur radius "on the fly" if taps have short hit distances
            #if( REBLUR_USE_ADJUSTED_ON_THE_FLY_BLUR_RADIUS_IN_PRE_BLUR == 1 )
                float hitDistFactorAtSample = GetHitDistFactor( h * NoD, frustumSize );
                float blurRadiusScale = lerp( hitDistFactorAtSample, 1.0, NoD );
                blurRadius *= lerp( 1.0, blurRadiusScale, n / ( 1.0 + n ) );
            #endif
        #endif

            // Get rid of potentially bad values outside of the screen
            w = ( IsInScreen( uv ) && !isnan( w ) ) ? w : 0.0;
            s = w != 0.0 ? s : 0.0;

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            // Min hit distance for tracking
            [flatten]
            if( w != 0.0 )
                minHitDist = min( minHitDist, ExtractHitDist( s ) );
        #endif

            // Accumulate
            sum += w;
            spec += s * w;
        #ifdef REBLUR_SH
            float4 sh = gIn_SpecSh.SampleLevel( gNearestClamp, checkerboardUvScaled, 0 );
            specSh.xyz += sh.xyz * w;
        #endif
        }

        float invSum = STL::Math::PositiveRcp( sum );
        spec *= invSum;
    #ifdef REBLUR_SH
        specSh.xyz *= invSum;
    #endif

#if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        // Output
        gOut_Spec_HitDistForTracking[ pixelPos ] = minHitDist; // TODO: lerp to ExtractHitDist( spec ) based on NoV or NoD?
    }

    // Checkerboard resolve ( if pre-pass failed )
    [branch]
    if( !hasData && sum == 0.0 )
    {
        REBLUR_TYPE s0 = gIn_Spec[ checkerboardPos.xy ];
        REBLUR_TYPE s1 = gIn_Spec[ checkerboardPos.zy ];

        spec = s0 * wc.x + s1 * wc.y;

    #ifdef REBLUR_SH
        float4 sh0 = gIn_SpecSh[ checkerboardPos.xy ];
        float4 sh1 = gIn_SpecSh[ checkerboardPos.zy ];

        specSh = sh0 * wc.x + sh1 * wc.y;
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
