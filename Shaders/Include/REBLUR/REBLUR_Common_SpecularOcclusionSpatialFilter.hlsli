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

#define POISSON_SAMPLE_NUM      REBLUR_POISSON_SAMPLE_NUM
#define POISSON_SAMPLES( i )    REBLUR_POISSON_SAMPLES( i )

{
    float center = spec;

    float radius = gBlurRadius;
    float minAccumSpeed = GetMipLevel( roughness ) + 0.001;
    float boost = saturate( 1.0 - specInternalData.y / minAccumSpeed );
    radius *= ( 1.0 + 5.0 * roughness * boost ) / 3.0;
    radius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gSpecLobeTrimmingParams.xyz );

    float radiusScale = 1.0;
    float radiusBias = 0.0;
    float fractionScale = 1.0;

    #if( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
        radiusBias = lerp( 1.0 / REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE, 1.0, error.w ) * error.z * gBlurRadiusScale + 0.00001;
        radius *= lerp( REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE, 1.0, error.w );
        fractionScale = REBLUR_POST_BLUR_FRACTION_SCALE;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        radiusBias = lerp( 1.0 / REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE, 1.0, error.w ) * error.z * gBlurRadiusScale + 0.00001;
        radius *= lerp( REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE, 1.0, error.w );
        fractionScale = REBLUR_BLUR_FRACTION_SCALE;
    #endif

    // Blur radius
    float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
    float hitDist = REBLUR_GetHitDist( center, viewZ, gHitDistParams, roughness );
    float blurRadius = GetBlurRadius( radius, radiusBias, radiusScale, hitDist, frustumHeight, specInternalData.x, error.w, roughness );
    float worldBlurRadius = PixelRadiusToWorld( gUnproject, gOrthoMode, blurRadius, viewZ );

    // Denoising
    float hitDistFactor = hitDist / ( hitDist + frustumHeight );
    float anisoFade = lerp( abs( curvature ), 1.0, specInternalData.x );

    float3 Vv = GetViewVector( Xv, true );
    float2x3 TvBv = GetKernelBasis( Vv, Nv, worldBlurRadius, roughness, anisoFade );
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, lerp( 1.0, REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE, specInternalData.x ) );
    float normalWeightParams = GetNormalWeightParams( specInternalData.x, frustumHeight, gLobeAngleFraction * fractionScale, roughness );
    float2 hitDistanceWeightParams = GetHitDistanceWeightParams( center, specInternalData.x );
    float2 roughnessWeightParams = GetRoughnessWeightParams( roughness, gRoughnessFraction * fractionScale );
    float sum = 1.0;

    #if( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT );
    #else
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 0.5 );
    #endif
    float2 minHitDistWeightLowConfidence = lerp( minHitDistWeight, 1.0, hitDistFactor );
    minHitDistWeight = lerp( minHitDistWeightLowConfidence, minHitDistWeight, error.w );

    [unroll]
    for( uint i = 0; i < POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = POISSON_SAMPLES( i );

        // Sample coordinates
        #if( REBLUR_USE_SCREEN_SPACE_SAMPLING == 1 )
            float2 uv = pixelUv + STL::Geometry::RotateVector( rotator, offset.xy ) * gInvScreenSize * blurRadius;
        #else
            float2 uv = GetKernelSampleCoordinates( gViewToClip, offset, Xv, TvBv[ 0 ], TvBv[ 1 ], rotator );
        #endif

        // Fetch data
        float2 uvScaled = uv * gResolutionScale;
        float4 Ns = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 );

        float2 s = gIn_Spec.SampleLevel( gNearestMirror, uvScaled, 0 );
        float zs = s.y / NRD_FP16_VIEWZ_SCALE;

        float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, zs, gOrthoMode );

        float materialIDs;
        Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

        // Sample weight
        float w = IsInScreen( uv );
        w *= CompareMaterials( materialID, materialIDs, gSpecMaterialMask );
        w *= GetGaussianWeight( offset.z );

        float ww = GetCombinedWeight( w, geometryWeightParams, Nv, Xvs, normalWeightParams, N, Ns, hitDistanceWeightParams, s.x, minHitDistWeight, roughnessWeightParams ).y;

        #if( REBLUR_DEBUG_SPATIAL_DENSITY_CHECK == 1 )
            ww = IsInScreen( uv );
        #endif

        // Get rid of potentially bad values outside of the screen
        s = w ? s : 0;

        spec += s.x * ww;
        sum += ww;
    }

    spec *= STL::Math::PositiveRcp( sum );

    // Estimate error
    error.z = GetColorErrorForAdaptiveRadiusScale( spec, center, specInternalData.x, roughness, REBLUR_SPATIAL_MODE );

    // Input mix
    spec = lerp( spec, center, gInputMix * ( 1.0 - specInternalData.x ) );

    // Output
    #if( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        gOut_Spec[ pixelPos ] = float2( spec, viewZ * NRD_FP16_VIEWZ_SCALE );
    #else
        gOut_Spec[ pixelPos ] = spec;
    #endif
}

#undef POISSON_SAMPLE_NUM
#undef POISSON_SAMPLES
