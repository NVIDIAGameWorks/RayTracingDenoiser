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
        float2 specInternalData = REBLUR_PRE_BLUR_INTERNAL_DATA;

        if( radius != 0.0 )
        {
    #else
        float boost = saturate( 1.0 - specInternalData.y / REBLUR_FIXED_FRAME_NUM );
        float radius = gBlurRadius;
        radius *= ( 1.0 + 2.0 * boost * GetSpecMagicCurve( roughness ) ) / 3.0;
    #endif
    radius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gSpecLobeTrimmingParams.xyz );

    float radiusScale = 1.0;
    float radiusBias = 0.0;
    float fractionScale = 1.0;

    #if( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
        radiusBias = lerp( 1.0 / REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE, 1.0, specData.y ) * specData.x * gBlurRadiusScale + 0.00001;
        radius *= lerp( REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE, 1.0, specData.y );
        fractionScale = REBLUR_POST_BLUR_FRACTION_SCALE;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        radiusBias = lerp( 1.0 / REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE, 1.0, specData.y ) * specData.x * gBlurRadiusScale + 0.00001;
        radius *= lerp( REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE, 1.0, specData.y );
        fractionScale = REBLUR_BLUR_FRACTION_SCALE;
    #endif

    if( spec.w == 0.0 )
        specInternalData.x = 0.5;

    // Blur radius
    float3 Vv = GetViewVector( Xv, true );
    float4 Dv = STL::ImportanceSampling::GetSpecularDominantDirection( Nv, Vv, roughness, STL_SPECULAR_DOMINANT_DIRECTION_G2 );
    float NoD = abs( dot( Nv, Dv.xyz ) );

    float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
    float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
    float hitDist = ( spec.w == 0.0 ? 1.0 : spec.w ) * hitDistScale;

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float hitDistFactor = GetHitDistFactor( hitDist * NoD, frustumHeight );

        float blurRadius = radius * hitDistFactor * GetSpecMagicCurve( roughness );
        float lobeTanHalfAngle = STL::ImportanceSampling::GetSpecularLobeTanHalfAngle( roughness );
        float lobeRadius = hitDist * NoD * lobeTanHalfAngle;
        float minBlurRadius = lobeRadius / PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ + hitDist * Dv.w );

        blurRadius = min( blurRadius, minBlurRadius );
    #else
        float hitDistFactor = GetHitDistFactor( hitDist, frustumHeight );
        float blurRadius = GetBlurRadius( radius, radiusBias, radiusScale, hitDistFactor, frustumHeight, specInternalData.x, roughness );
    #endif

    if( spec.w == 0.0 )
        blurRadius = max( blurRadius, 1.0 );

    float worldBlurRadius = PixelRadiusToWorld( gUnproject, gOrthoMode, blurRadius, viewZ );

    // Denoising
    float2x3 TvBv = GetKernelBasis( Dv.xyz, Nv, NoD, worldBlurRadius, roughness, specInternalData.x );
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, lerp( 1.0, REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE, specInternalData.x ) );
    float normalWeightParams = GetNormalWeightParams( specInternalData.x, frustumHeight, gLobeAngleFraction * fractionScale, roughness );
    float2 hitDistanceWeightParams = spec.w == 0.0 ? 0.0 : GetHitDistanceWeightParams( spec.w, specInternalData.x );

    float2 roughnessWeightParams = GetRoughnessWeightParams( roughness, gRoughnessFraction * fractionScale );
    float2 sum = 1.0;

    #ifdef REBLUR_SPATIAL_REUSE
        float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
        normalWeightParams = 1.0 / max( angle, NRD_NORMAL_ENCODING_ERROR );

        uint2 p = pixelPos;
        if( gSpecCheckerboard != 2 )
            p.x >>= 1;

        float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_SpecDirectionPdf[ gRectOrigin + p ] );
        float3 L = dirPdf.xyz;
        float3 V = STL::Geometry::RotateVector( gViewToWorld, Vv );
        float3 H = normalize( L + V );
        float NoV = abs( dot( N, V ) );
        float NoL = saturate( dot( N, L ) );
        float NoH = saturate( dot( N, H ) );
        float VoH = saturate( dot( V, H ) );
        float D = STL::BRDF::DistributionTerm( roughness, NoH );
        float G = STL::BRDF::GeometryTermMod_SmithUncorrelated( roughness, NoL, NoV, VoH, NoH );

        sum = D * G * NoL / ( dirPdf.w + REBLUR_MIN_PDF );
    #endif

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float2 minHitDistWeight = REBLUR_HIT_DIST_MIN_WEIGHT * 2.0;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT );
    #else
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 0.5 );
    #endif

    float2 minHitDistWeightLowConfidence = lerp( minHitDistWeight, 1.0, hitDistFactor );
    minHitDistWeight = lerp( minHitDistWeightLowConfidence, minHitDistWeight, specData.y );

    // Ignore "no data" hit distances
    sum.y *= float( spec.w != 0.0 );

    // Sampling
    float4 center = spec;
    spec *= sum.xxxy;

    [unroll]
    for( uint i = 0; i < POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = POISSON_SAMPLES( i );

        // Sample coordinates
        #if( REBLUR_USE_SCREEN_SPACE_SAMPLING == 1 || REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR ) // TODO: screen space is OK for diffuse in pre-pass, but is it OK for glossy specular?
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
            float4 s = gIn_Spec.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            float zs = abs( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 ) );
        #else
            #ifdef REBLUR_OCCLUSION
                float2 data = gIn_Spec.SampleLevel( gNearestMirror, uvScaled, 0 );
                float4 s = data.x;
                float zs = data.y / NRD_FP16_VIEWZ_SCALE;
            #else
                float4 s = gIn_Spec.SampleLevel( gNearestMirror, uvScaled, 0 );
                float zs = gIn_ScaledViewZ.SampleLevel( gNearestMirror, uvScaled, 0 ) / NRD_FP16_VIEWZ_SCALE;
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
            #ifdef REBLUR_SPATIAL_REUSE
                float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_SpecDirectionPdf.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 ) );
                float3 L = dirPdf.xyz;
                float3 H = normalize( L + V );
                float NoL = saturate( dot( N, L ) );
                float NoH = saturate( dot( N, H ) );
                float VoH = saturate( dot( V, H ) );
                float D = STL::BRDF::DistributionTerm( roughness, NoH );
                float G = STL::BRDF::GeometryTermMod_SmithUncorrelated( roughness, NoL, NoV, VoH, NoH );

                w *= D * G * NoL / ( dirPdf.w + REBLUR_MIN_PDF );
            #endif

            // Decrease weight for samples that most likely are very close to reflection contact which should not be blurred
            float d = length( Xvs - Xv );
            float h = s.w * hitDistScale; // roughness weight will handle the rest
            float t = h * rcp( hitDist + d + 1e-6 );
            w *= lerp( saturate( t ), 1.0, STL::Math::LinearStep( 0.5, 1.0, roughness ) );
        #endif

        // Weight separation - radiance and hitDist
        float2 ww = w;
        #ifndef REBLUR_SPATIAL_REUSE
            ww *= lerp( minHitDistWeight, 1.0, GetHitDistanceWeight( hitDistanceWeightParams, s.w ) );
        #endif

        // Ignore "no data" hit distances
        ww.y *= float( s.w != 0.0 );

        // Get rid of potentially bad values outside of the screen
        s = w ? s : 0;

        // Accumulate
        spec += s * ww.xxxy;
        sum += ww;
    }

    spec *= STL::Math::PositiveRcp( sum.xxxy );

    // Input mix
    #if( REBLUR_SPATIAL_MODE != REBLUR_PRE_BLUR )
        spec = lerp( spec, center, gInputMix * ( 1.0 - specInternalData.x ) );
    #else
        }
    #endif

    // Output
    #ifdef REBLUR_OCCLUSION
        #if( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
            gOut_Spec[ pixelPos ] = float2( spec.w, viewZ * NRD_FP16_VIEWZ_SCALE );
        #else
            gOut_Spec[ pixelPos ] = spec.w;
        #endif
    #else
        gOut_Spec[ pixelPos ] = spec;
    #endif
}

#undef POISSON_SAMPLE_NUM
#undef POISSON_SAMPLES
