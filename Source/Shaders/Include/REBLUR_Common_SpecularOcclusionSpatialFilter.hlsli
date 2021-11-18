/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

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

    float radius = gSpecBlurRadius;
    float minAccumSpeed = GetMipLevel( roughness ) + 0.001;
    float boost = saturate( 1.0 - specInternalData.y / minAccumSpeed );
    radius *= ( 1.0 + 2.0 * boost ) / 3.0;
    radius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gSpecTrimmingParams.xyz );

    float radiusScale = 1.0;
    float radiusBias = 0.0; // see GetBlurRadius()
    float strictness = 1.0;

    #if( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
        radiusBias = lerp( REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE, 1.0, error.w ) * error.z * gSpecBlurRadiusScale + 0.00001;
        strictness = REBLUR_POST_BLUR_STRICTNESS;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        radiusBias = lerp( REBLUR_RADIUS_BIAS_CONFIDENCE_BASED_SCALE, 1.0, error.w ) * error.z * gSpecBlurRadiusScale + 0.00001;
        strictness = REBLUR_BLUR_NORMAL_WEIGHT_RELAXATION;
    #endif

    // Blur radius
    float hitDist = GetHitDist( center, viewZ, gSpecHitDistParams, roughness );
    float blurRadius = GetBlurRadius( radius, hitDist, viewZ, specInternalData.x, radiusBias, radiusScale, roughness );
    float worldBlurRadius = PixelRadiusToWorld( gUnproject, gIsOrtho, blurRadius, viewZ );

    // Denoising
    float anisoFade = lerp( curvature, 1.0, specInternalData.x );
    float3 Vv = GetViewVector( Xv, true );
    float2x3 TvBv = GetKernelBasis( Vv, Nv, worldBlurRadius, roughness, anisoFade );
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, gMeterToUnitsMultiplier, Xv, Nv, lerp( 1.0, REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE, specInternalData.x ) );
    float normalWeightParams = GetNormalWeightParams( specInternalData.x, curvature, viewZ, roughness, gNormalWeightStrictness * strictness );
    float2 hitDistanceWeightParams = GetHitDistanceWeightParams( center, specInternalData.x, roughness );
    float2 roughnessWeightParams = GetRoughnessWeightParams( roughness );
    float sum = 1.0;

    #if( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float2 minwh = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT );
    #else
        float2 minwh = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 0.5 );
    #endif
    minwh = lerp( 1.0, minwh, error.w );

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

        float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, zs, gIsOrtho );

        float materialIDs;
        Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

        // Sample weight
        float w = IsInScreen( uv );
        w *= float( materialIDs == materialID );
        w *= GetGaussianWeight( offset.z );
        w *= GetRoughnessWeight( roughnessWeightParams, Ns.w );

        float ww = GetCombinedWeight( w, geometryWeightParams, Nv, Xvs, normalWeightParams, N, Ns, hitDistanceWeightParams, s.x, minwh ).y;

        spec += s.x * ww;
        sum += ww;
    }

    spec *= STL::Math::PositiveRcp( sum );

    // Estimate error
    error.z = GetColorErrorForAdaptiveRadiusScale( spec.xxxx, center.xxxx, specInternalData.x, roughness, true );

    // Input mix
    spec = lerp( spec, center, REBLUR_INPUT_MIX.y );

    // Output
    #if( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        gOut_Spec[ pixelPos ] = float2( spec, viewZ * NRD_FP16_VIEWZ_SCALE );
    #else
        gOut_Spec[ pixelPos ] = spec;
    #endif
}

#undef POISSON_SAMPLE_NUM
#undef POISSON_SAMPLES
