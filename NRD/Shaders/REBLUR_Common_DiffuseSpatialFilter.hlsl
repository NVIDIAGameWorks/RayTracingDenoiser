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

{
    float2 center = float2( STL::Color::Luminance( diff.xyz ), diff.w );

    float radius = gDiffBlurRadius;
    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float diffInternalData = REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED;
        radius *= REBLUR_PRE_BLUR_RADIUS_SCALE( 1.0 );
    #endif

    // Blur radius scale
    #if( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        float radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
        float radiusBias = error.x * gDiffBlurRadiusScale;
    #else
        float radiusScale = 1.0;
        float radiusBias = 0.0;
    #endif

    // Blur radius
    float hitDist = GetHitDist( center.y, viewZ, gDiffHitDistParams );
    float blurRadius = GetBlurRadius( radius, 1.0, hitDist, Xv, diffInternalData.x );
    blurRadius = blurRadius * ( radiusScale + radiusBias ) + radiusBias;
    float worldBlurRadius = PixelRadiusToWorld( gUnproject, gIsOrtho, blurRadius, viewZ );

    // Denoising
    float2x3 TvBv = GetKernelBasis( Xv, Nv, worldBlurRadius, edge );
    float normalWeightParams = GetNormalWeightParams( viewZ, 1.0, edge, diffInternalData.x );
    float2 hitDistanceWeightParams = GetHitDistanceWeightParams( center.y, diffInternalData.x, hitDist, Xv );
    float2 sum = 1.0;

    [unroll]
    for( uint i = 0; i < REBLUR_POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = REBLUR_POISSON_SAMPLES[ i ];

        // Sample coordinates
        float2 uv = GetKernelSampleCoordinates( gViewToClip, offset, Xv, TvBv[ 0 ], TvBv[ 1 ], rotator );

        // Handle half res input in the checkerboard mode
        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 checkerboardUv = uv;
            if( gDiffCheckerboard != 2 )
                checkerboardUv = ApplyCheckerboard( uv, gDiffCheckerboard, i, gRectSize, gInvRectSize, gFrameIndex );
        #endif

        // Fetch data
        float2 uvScaled = uv * gResolutionScale;
        float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 );

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 checkerboardUvScaled = checkerboardUv * gResolutionScale + gRectOffset;
            float4 signal = gIn_Diff.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            float z = abs( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 ) );
        #else
            float4 signal = gIn_Diff.SampleLevel( gNearestMirror, uvScaled, 0 );
            float z = gIn_ScaledViewZ.SampleLevel( gNearestMirror, uvScaled, 0 ) / NRD_FP16_VIEWZ_SCALE;
        #endif

        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );
        normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

        // Sample weight
        float w = GetGaussianWeight( offset.z );
        w *= IsInScreen( uv );
        w *= GetGeometryWeight( geometryWeightParams, Nv, samplePos );
        w *= GetNormalWeight( normalWeightParams, N, normal.xyz );

        float wh = GetHitDistanceWeight( hitDistanceWeightParams, signal.w );
        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 ww = w * lerp( REBLUR_HIT_DIST_MIN_WEIGHT * 2.0, 1.0, wh );
        #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
            float2 ww = w * lerp( float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT ), 1.0, wh );
        #else
            float2 ww = w * lerp( float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 0.5 ), 1.0, wh );
        #endif

        diff += signal * ww.xxxy;
        sum += ww;
    }

    diff /= sum.xxxy;

    // Special case for hit distance
    diff.w = lerp( diff.w, center.y, REBLUR_HIT_DIST_INPUT_MIX );

    // Estimate error
    error.x = GetColorErrorForAdaptiveRadiusScale( diff, center, diffInternalData.x );

    // Output
    gOut_Diff[ pixelPos ] = diff;
}
