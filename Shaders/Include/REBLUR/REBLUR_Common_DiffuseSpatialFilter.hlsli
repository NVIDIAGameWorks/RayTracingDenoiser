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
    float4 center = diff;

    #if( REBLUR_USE_COMPRESSION_FOR_DIFFUSE == 1 )
        float exposure = _NRD_GetColorCompressionExposureForSpatialPasses( 1.0 );
        diff.xyz = STL::Color::Compress( diff.xyz, exposure );
    #endif

    float radius = gBlurRadius;
    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float2 diffInternalData = REBLUR_PRE_BLUR_INTERNAL_DATA;
        radius *= REBLUR_PRE_BLUR_RADIUS_SCALE( 1.0 );
    #else
        float minAccumSpeed = GetMipLevel( 1.0 ) + 0.001;
        float boost = saturate( 1.0 - diffInternalData.y / minAccumSpeed );
        radius *= ( 1.0 + 5.0 * boost ) / 3.0;
    #endif

    float radiusScale = 1.0;
    float radiusBias = 0.0;
    float fractionScale = 1.0;

    #if( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
        radiusBias = error.x * gBlurRadiusScale + 0.00001;
        fractionScale = REBLUR_POST_BLUR_FRACTION_SCALE;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        radiusBias = error.x * gBlurRadiusScale + 0.00001;
        fractionScale = REBLUR_BLUR_FRACTION_SCALE;
    #endif

    // Blur radius
    float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
    float hitDist = REBLUR_GetHitDist( center.w, viewZ, gHitDistParams, 1.0 );
    float blurRadius = GetBlurRadius( radius, radiusBias, radiusScale, hitDist, frustumHeight, diffInternalData.x, 1.0 );
    float worldBlurRadius = PixelRadiusToWorld( gUnproject, gOrthoMode, blurRadius, viewZ );

    // Denoising
    float hitDistFactor = hitDist / ( hitDist + frustumHeight );

    float3 Vv = GetViewVector( Xv, true );
    float2x3 TvBv = GetKernelBasis( Vv, Nv, worldBlurRadius );
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, lerp( 1.0, REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE, diffInternalData.x ) );
    float normalWeightParams = GetNormalWeightParams( diffInternalData.x, frustumHeight, gLobeAngleFraction * fractionScale );
    float2 hitDistanceWeightParams = GetHitDistanceWeightParams( center.w, diffInternalData.x );
    float2 sum = 1.0;

    #ifdef REBLUR_SPATIAL_REUSE
        blurRadius *= REBLUR_PRE_BLUR_SPATIAL_REUSE_BASE_RADIUS_SCALE;

        float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( 1.0 );
        float normalWeightParamsReuse = 1.0 / max( angle, NRD_ENCODING_ERRORS.x );

        uint2 p = pixelPos;
        if( gDiffCheckerboard != 2 )
            p.x >>= 1;

        float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_DiffDirectionPdf[ gRectOrigin + p ] );
        float3 L = dirPdf.xyz;
        float NoL = saturate( dot( N, L ) );

        sum = NoL / ( dirPdf.w + REBLUR_MIN_PDF );
        #if( REBLUR_USE_SPATIAL_REUSE_FOR_HIT_DIST == 1 )
            sum.y = sum.x;
        #endif
        diff *= sum.xxxy;
    #endif

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float2 minHitDistWeight = REBLUR_HIT_DIST_MIN_WEIGHT * 2.0;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT );
    #else
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 0.5 );
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

        float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, zs, gOrthoMode );

        float materialIDs;
        Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

        // Sample weight
        float w = IsInScreen( uv );
        w *= CompareMaterials( materialID, materialIDs, gDiffMaterialMask );
        w *= GetGaussianWeight( offset.z );

        #ifdef REBLUR_SPATIAL_REUSE
            float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_DiffDirectionPdf.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 ) );
            float3 L = dirPdf.xyz;
            float NoL = saturate( dot( N, L ) );

            w *= GetGeometryWeight( geometryWeightParams, Nv, Xvs );

            float2 ww = w;
            ww.x *= NoL / ( dirPdf.w + REBLUR_MIN_PDF );
            ww.x *= GetNormalWeight( normalWeightParamsReuse, N, Ns.xyz );

            #if( REBLUR_USE_SPATIAL_REUSE_FOR_HIT_DIST == 1 )
                ww.y = ww.x;
            #else
                ww.y *= GetNormalWeight( normalWeightParams, N, Ns.xyz );
                ww.y *= lerp( minHitDistWeight.y, 1.0, GetHitDistanceWeight( hitDistanceWeightParams, s.w ) );
            #endif
        #else
            float2 ww = GetCombinedWeight( w, geometryWeightParams, Nv, Xvs, normalWeightParams, N, Ns, hitDistanceWeightParams, s.w, minHitDistWeight );
        #endif

        #if( REBLUR_DEBUG_SPATIAL_DENSITY_CHECK == 1 )
            ww = IsInScreen( uv );
        #endif

        #if( REBLUR_USE_COMPRESSION_FOR_DIFFUSE == 1 )
            s.xyz = STL::Color::Compress( s.xyz, exposure );
        #endif

        // Get rid of potentially bad values outside of the screen
        s = w ? s : 0;

        diff += s * ww.xxxy;
        sum += ww;
    }

    diff *= STL::Math::PositiveRcp( sum.xxxy );

    #if( REBLUR_USE_COMPRESSION_FOR_DIFFUSE == 1 )
        diff.xyz = STL::Color::Decompress( diff.xyz, exposure );
    #endif

    // Estimate error
    error.x = GetColorErrorForAdaptiveRadiusScale( diff, center, diffInternalData.x, 1.0, REBLUR_SPATIAL_MODE );

    // Input mix
    diff = lerp( diff, center, gInputMix * ( 1.0 - diffInternalData.x ) );

    // Output
    gOut_Diff[ pixelPos ] = diff;
}

#undef POISSON_SAMPLE_NUM
#undef POISSON_SAMPLES
