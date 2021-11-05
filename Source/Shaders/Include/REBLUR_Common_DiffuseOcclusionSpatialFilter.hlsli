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
    float center = diff;

    float radius = gDiffBlurRadius;
    float minAccumSpeed = GetMipLevel( 1.0, gDiffMaxFastAccumulatedFrameNum ) + 0.001;
    float boost = saturate( 1.0 - diffInternalData.y / minAccumSpeed );
    radius *= ( 1.0 + 2.0 * boost ) / 3.0;

    float radiusScale = 1.0;
    float radiusBias = 0.0; // see GetBlurRadius()
    float strictness = 1.0;

    #if( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
        radiusBias = error.x * gDiffBlurRadiusScale + 0.00001;
        strictness = REBLUR_POST_BLUR_STRICTNESS;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        radiusBias = error.x * gDiffBlurRadiusScale + 0.00001;
        strictness = REBLUR_BLUR_NORMAL_WEIGHT_RELAXATION;
    #endif

    // Blur radius
    float hitDist = GetHitDist( center, viewZ, gDiffHitDistParams, 1.0 );
    float blurRadius = GetBlurRadius( radius, hitDist, viewZ, diffInternalData.x, radiusBias, radiusScale );
    float worldBlurRadius = PixelRadiusToWorld( gUnproject, gIsOrtho, blurRadius, viewZ );

    // Denoising
    float2x3 TvBv = GetKernelBasis( Xv, Nv, worldBlurRadius );
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, gMeterToUnitsMultiplier, Xv, Nv, lerp( 1.0, REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE, diffInternalData.x ) );
    float normalWeightParams = GetNormalWeightParams( diffInternalData.x, curvature, viewZ, 1.0, gNormalWeightStrictness * strictness );
    float2 hitDistanceWeightParams = GetHitDistanceWeightParams( center, diffInternalData.x );
    float sum = 1.0;

    #if( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float2 minwh = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT );
    #else
        float2 minwh = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 0.5 );
    #endif

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

        float2 s = gIn_Diff.SampleLevel( gNearestMirror, uvScaled, 0 );
        float zs = s.y / NRD_FP16_VIEWZ_SCALE;

        float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, zs, gIsOrtho );

        float materialIDs;
        Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

        // Sample weight
        float w = IsInScreen( uv );
        w *= float( materialIDs == materialID );
        w *= GetGaussianWeight( offset.z );

        float ww = GetCombinedWeight( w, geometryWeightParams, Nv, Xvs, normalWeightParams, N, Ns, hitDistanceWeightParams, s.x, minwh ).y;

        diff += s.x * ww;
        sum += ww;
    }

    diff *= STL::Math::PositiveRcp( sum );

    // Estimate error
    error.x = GetColorErrorForAdaptiveRadiusScale( diff.xxxx, center.xxxx, diffInternalData.x, 1.0, true );

    // Input mix
    diff = lerp( diff, center, REBLUR_INPUT_MIX.y );

    // Output
    #if( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        gOut_Diff[ pixelPos ] = float2( diff, viewZ * NRD_FP16_VIEWZ_SCALE );
    #else
        gOut_Diff[ pixelPos ] = diff;
    #endif
}

#undef POISSON_SAMPLE_NUM
#undef POISSON_SAMPLES
