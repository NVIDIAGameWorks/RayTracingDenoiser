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
    float4 center = diff;

    float exposure = _NRD_GetColorCompressionExposureForSpatialPasses( 1.0 );
    diff.xyz = STL::Color::Compress( diff.xyz, exposure );

    float radius = gDiffBlurRadius;
    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float2 diffInternalData = REBLUR_PRE_BLUR_INTERNAL_DATA;
        radius *= REBLUR_PRE_BLUR_RADIUS_SCALE( 1.0 );
    #else
        float minAccumSpeed = GetMipLevel( 1.0, gDiffMaxFastAccumulatedFrameNum ) + 0.001;
        float boost = saturate( 1.0 - diffInternalData.y / minAccumSpeed );
        radius *= ( 1.0 + 2.0 * boost ) / 3.0;
    #endif

    // Blur radius scale
    #if( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        float radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
    #else
        float radiusScale = 1.0;
    #endif

    // TODO: on thin objects adaptive scale can lead to sampling from "not contributing" surfaces. Mips for viewZ can be computed
    // and used to estimate "local planarity". If abs( z_mip0 - z_mip3 ) is big reduce adaptive radius scale accordingly.
    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float radiusBias = 0.0;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float radiusBias = gDiffBlurRadiusScale + 0.00001; // TODO: using error.x introduces bias
    #else
        float radiusBias = error.x * gDiffBlurRadiusScale + 0.00001;
    #endif

    // Blur radius
    float hitDist = GetHitDist( center.w, viewZ, gDiffHitDistParams, 1.0 );
    float blurRadius = GetBlurRadius( radius, 1.0, hitDist, viewZ, diffInternalData.x, 1.0, error.x, radiusBias, radiusScale );
    float worldBlurRadius = PixelRadiusToWorld( gUnproject, gIsOrtho, blurRadius, viewZ );

    // Denoising
    float2x3 TvBv = GetKernelBasis( Xv, Nv, worldBlurRadius );
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, Xv, Nv, lerp( 1.0, 0.05, diffInternalData.x ) );
    float normalWeightParams = GetNormalWeightParams( diffInternalData.x, edge, error.x, 1.0, gNormalWeightStrictness );
    float2 hitDistanceWeightParams = GetHitDistanceWeightParams( center.w, diffInternalData.x );
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
        float4 Ns = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 );

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 checkerboardUvScaled = checkerboardUv * gResolutionScale + gRectOffset;
            float4 s = gIn_Diff.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            float zs = abs( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 ) );
        #else
            float4 s = gIn_Diff.SampleLevel( gNearestMirror, uvScaled, 0 );
            float zs = gIn_ScaledViewZ.SampleLevel( gNearestMirror, uvScaled, 0 ) / NRD_FP16_VIEWZ_SCALE;
        #endif

        float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, zs, gIsOrtho );
        Ns = _NRD_FrontEnd_UnpackNormalAndRoughness( Ns );

        // Sample weight
        float w = GetGeometryWeight( geometryWeightParams, Nv, Xvs );
        w *= IsInScreen( uv );
        w *= GetGaussianWeight( offset.z );
        w *= GetNormalWeight( normalWeightParams, N, Ns.xyz );

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 minwh = REBLUR_HIT_DIST_MIN_WEIGHT * 2.0;
        #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
            float2 minwh = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT );
        #else
            float2 minwh = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 0.5 );
        #endif

        float wh = GetHitDistanceWeight( hitDistanceWeightParams, s.w );
        float2 ww = w * lerp( minwh, 1.0, wh );

        s.xyz = STL::Color::Compress( s.xyz, exposure );

        diff += s * ww.xxxy;
        sum += ww;
    }

    diff /= sum.xxxy;

    diff.xyz = STL::Color::Decompress( diff.xyz, exposure );

    // Estimate error
    error.x = GetColorErrorForAdaptiveRadiusScale( diff, center, diffInternalData.x, 1.0 );

    // Input mix
    diff = lerp( diff, center, REBLUR_INPUT_MIX.xxxy );

    // Output
    gOut_Diff[ pixelPos ] = diff;
}
