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
#if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
    float2 sum = hasData.y;

    float2 specInternalData = REBLUR_PRE_BLUR_INTERNAL_DATA;
    float minHitDist = spec.w;

    if( radius != 0.0 )
    {
#else
        float2 specInternalData = float2( 1.0 / ( 1.0 + internalData1.z ), internalData1.z );
        float2 sum = 1.0;
        float boost = saturate( 1.0 - specInternalData.y / REBLUR_FIXED_FRAME_NUM );
        float radius = gBlurRadius;
        radius *= ( 1.0 + 2.0 * boost * GetSpecMagicCurve2( roughness ) ) / 3.0;
#endif
        radius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gSpecLobeTrimmingParams.xyz );

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        #ifdef REBLUR_HAS_DIRECTION_PDF
            float lobeAngleFractionScale = 1.0;
            float roughnessFractionScale = 1.0;
        #else
            float lobeAngleFractionScale = lerp( 1.0, gLobeAngleFraction, roughness );
            float roughnessFractionScale = 0.333;
        #endif
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float radiusScale = 1.0;
        float lobeAngleFractionScale = gLobeAngleFraction * REBLUR_BLUR_FRACTION_SCALE;
        float roughnessFractionScale = gRoughnessFraction * REBLUR_BLUR_FRACTION_SCALE;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        float radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
        float lobeAngleFractionScale = gLobeAngleFraction * REBLUR_POST_BLUR_FRACTION_SCALE;
        float roughnessFractionScale = gRoughnessFraction * REBLUR_POST_BLUR_FRACTION_SCALE;
    #endif

        // Blur radius
        float3 Vv = GetViewVector( Xv, true );
        float4 Dv = STL::ImportanceSampling::GetSpecularDominantDirection( Nv, Vv, roughness, STL_SPECULAR_DOMINANT_DIRECTION_G2 );
        float NoD = abs( dot( Nv, Dv.xyz ) );

        float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
        float hitDist = spec.w * hitDistScale;

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR ) // TODO: explore using unnnormalized (real) hit distance
        float hitDistFactor = GetHitDistFactor( hitDist * NoD, frustumHeight );

        float blurRadius = radius * hitDistFactor * GetSpecMagicCurve2( roughness );
        float lobeTanHalfAngle = STL::ImportanceSampling::GetSpecularLobeTanHalfAngle( roughness );
        float lobeRadius = hitDist * NoD * lobeTanHalfAngle;
        float minBlurRadius = lobeRadius / PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ + hitDist * Dv.w );

        blurRadius = min( blurRadius, minBlurRadius );
    #else
        float radiusBias = internalData1.w * gBlurRadiusScale;
        float hitDistFactor = GetHitDistFactor( hitDist, frustumHeight );
        float blurRadius = GetBlurRadius( radius, radiusBias, radiusScale, hitDistFactor, specInternalData.x, roughness );
    #endif

        float worldBlurRadius = PixelRadiusToWorld( gUnproject, gOrthoMode, blurRadius, viewZ );

        // Denoising
        float2x3 TvBv = GetKernelBasis( Dv.xyz, Nv, NoD, worldBlurRadius, roughness, specInternalData.x );
        float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, lerp( 1.0, REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE, specInternalData.x ) );
        float normalWeightParams = GetNormalWeightParams( specInternalData.x, lobeAngleFractionScale, roughness );
        float2 hitDistanceWeightParams = GetHitDistanceWeightParams( spec.w, specInternalData.x, roughness );
        float2 roughnessWeightParams = GetRoughnessWeightParams( roughness, roughnessFractionScale );

    #ifdef REBLUR_HAS_DIRECTION_PDF
        uint2 p = pixelPos;
        if( gSpecCheckerboard != 2 )
            p.x >>= 1;

        float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_Spec_DirectionPdf[ gRectOrigin + p ] );
        float3 L = dirPdf.xyz;
        float3 V = STL::Geometry::RotateVector( gViewToWorld, Vv );
        float3 H = normalize( L + V );
        float NoV = abs( dot( N, V ) );
        float NoL = saturate( dot( N, L ) );
        float NoH = saturate( dot( N, H ) );
        float VoH = saturate( dot( V, H ) );
        float D = STL::BRDF::DistributionTerm( roughness, NoH );
        float G = STL::BRDF::GeometryTermMod_SmithUncorrelated( roughness, NoL, NoV, VoH, NoH );

        // IMPORTANT: no F at least because we don't know Rf0, PDF should not include it too
        sum *= D * G * NoL / ( dirPdf.w + REBLUR_MIN_PDF );
    #endif

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float2 minHitDistWeight = REBLUR_HIT_DIST_MIN_WEIGHT * 2.0;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT );
    #else
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 0.5 );
    #endif

        // TODO: previosuly "minHitDistWeight" was adjusted by specular reprojection confidence
        //minHitDistWeight = lerp( lerp( minHitDistWeight, 1.0, hitDistFactor ), minHitDistWeight, confidence );

        // Sampling
        spec *= sum.xxxy;
    #ifdef REBLUR_SH
        specSh *= sum.xxxy;
    #endif

        [unroll]
        for( uint i = 0; i < POISSON_SAMPLE_NUM; i++ )
        {
            float3 offset = POISSON_SAMPLES( i );

            // Sample coordinates
        #if( REBLUR_USE_SCREEN_SPACE_SAMPLING == 1 || REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 uv = pixelUv + STL::Geometry::RotateVector( rotator, offset.xy ) * gInvScreenSize * blurRadius;
        #else
            float2 uv = GetKernelSampleCoordinates( gViewToClip, offset, Xv, TvBv[ 0 ], TvBv[ 1 ], rotator );
        #endif

            // Handle half res input in the checkerboard mode
        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 checkerboardUv = uv;
            if( gSpecCheckerboard != 2 )
                checkerboardUv = ApplyCheckerboard( uv, gSpecCheckerboard, i, gRectSize, gInvRectSize, gFrameIndex );
        #endif

            // Fetch data
            float2 uvScaled = uv * gResolutionScale;

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 checkerboardUvScaled = checkerboardUv * gResolutionScale + gRectOffset;
            float zs = abs( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 ) );
            float4 s = gIn_Spec.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            #ifdef REBLUR_SH
                float4 sh = gIn_SpecSh.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            #endif
        #else
            #ifdef REBLUR_OCCLUSION
                float2 data = gIn_Spec.SampleLevel( gNearestMirror, uvScaled, 0 );
                float zs = data.y;
                float4 s = data.x;
            #else
                float zs = gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled, 0 );
                float4 s = gIn_Spec.SampleLevel( gNearestMirror, uvScaled, 0 );
                #ifdef REBLUR_SH
                    float4 sh = gIn_SpecSh.SampleLevel( gNearestMirror, uvScaled, 0 );
                #endif
            #endif
        #endif
        #if( defined( REBLUR_OCCLUSION ) || REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
            zs = UnpackViewZ( zs );
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
                float3 L = dirPdf.xyz;
                float3 H = normalize( L + V );
                float NoL = saturate( dot( N, L ) );
                float NoH = saturate( dot( N, H ) );
                float VoH = saturate( dot( V, H ) );
                float D = STL::BRDF::DistributionTerm( roughness, NoH );
                float G = STL::BRDF::GeometryTermMod_SmithUncorrelated( roughness, NoL, NoV, VoH, NoH );

                // IMPORTANT: no F at least because we don't know Rf0, PDF should not include it too
                w *= D * G * NoL / ( dirPdf.w + REBLUR_MIN_PDF );
            #endif

            // Decrease weight for samples that most likely are very close to reflection contact which should not be blurred
            float d = length( Xvs - Xv );
            float h = s.w * hitDistScale; // roughness weight will handle the rest
            float t = h * rcp( hitDist + d + 1e-6 );
            w *= lerp( saturate( t ), 1.0, STL::Math::LinearStep( 0.5, 1.0, roughness ) );

            // Adjust blur radius "on the fly" if taps have short hit distances
            float hitDistFactorAtSample = GetHitDistFactor( h, frustumHeight );
            float blurRadiusScale = lerp( hitDistFactorAtSample, 1.0, NoD );
            blurRadius *= lerp( 1.0, blurRadiusScale, i / ( 1.0 + i ) );

            // Min hit distance for tracking
            if( w != 0.0 )
                minHitDist = min( s.w, minHitDist ); // TODO: use weighted sum with weight = w * ( 1.0 - s.w )?
        #endif

            // Weight separation - radiance and hitDist
            float2 ww = w;
        #ifndef REBLUR_HAS_DIRECTION_PDF
            ww *= lerp( minHitDistWeight, 1.0, GetHitDistanceWeight( hitDistanceWeightParams, s.w ) );
        #endif

            // Get rid of potentially bad values outside of the screen
            s = w ? s : 0;

            // Accumulate
            sum += ww;
            spec += s * ww.xxxy;
        #ifdef REBLUR_SH
            specSh += sh * ww.xxxy;
        #endif
        }

        float2 invSum = STL::Math::PositiveRcp( sum );
        spec *= invSum.xxxy;
    #ifdef REBLUR_SH
        specSh *= invSum.xxxy;
    #endif

#if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        // Min hit distance modification
        minHitDist = lerp( minHitDist, spec.w, STL::BRDF::Pow5( NoD ) );

        // Output
        gOut_Spec_MinHitDist[ pixelPos ] = minHitDist;
    }

    // Checkerboard resolve ( if pre-pass failed )
    [branch]
    if( !hasData.y && sum.x == 0.0 )
    {
        float4 s0 = gIn_Spec[ checkerboardPos.xy ];
        float4 s1 = gIn_Spec[ checkerboardPos.zy ];

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
#ifdef REBLUR_OCCLUSION
    #if( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        gOut_Spec[ pixelPos ] = float2( spec.w, PackViewZ( viewZ ) );
    #else
        gOut_Spec[ pixelPos ] = spec.w;
    #endif
#else
    gOut_Spec[ pixelPos ] = spec;
    #ifdef REBLUR_SH
        gOut_SpecSh[ pixelPos ] = specSh;
    #endif
#endif
}

#undef POISSON_SAMPLE_NUM
#undef POISSON_SAMPLES
