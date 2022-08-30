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
    float2 sum = hasData;

    if( radius != 0.0 )
    {
        float diffNonLinearAccumSpeed = REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED;
#else
        float2 sum = 1.0;

        // IMPORTANT: keep an eye on tests:
        // - 51 and 128: outlines without TAA
        // - 81 and 117: cleanness in disoccluded regions
        float boost = 1.0 - GetFadeBasedOnAccumulatedFrames( internalData1.x );
        boost *= 1.0 - STL::BRDF::Pow5( NoV );

        float diffNonLinearAccumSpeed = 1.0 / ( 1.0 + ( 1.0 - boost ) * internalData1.x );
        float radius = gBlurRadius * ( 1.0 + 2.0 * boost ) / 3.0;
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
    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        #ifdef REBLUR_HAS_DIRECTION_PDF
            lobeAngleFractionScale = 1.0;
        #endif
    #endif

        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, 1.0 );
        float hitDist = ExtractHitDist( diff ) * hitDistScale;

        // Hit distance factor ( tests 76, 95, 120 )
        // TODO: if luminance stoppers are used, blur radius should depend less on "hitDistFactor"
        float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
        float hitDistFactor = GetHitDistFactor( hitDist, frustumHeight );

        // Blur radius
        float blurRadius = radius * hitDistFactor;
    #if( REBLUR_SPATIAL_MODE != REBLUR_PRE_BLUR )
        blurRadius *= lerp( gMinConvergedStateBaseRadiusScale, 1.0, diffNonLinearAccumSpeed );
    #endif

        // Blur bias and scale
    #if( REBLUR_SPATIAL_MODE != REBLUR_PRE_BLUR )
        blurRadius += max( internalData1.y * gBlurRadiusScale, 2.0 ); // TODO: 2.0 * hitDsitFactor?
        blurRadius *= radiusScale;
        blurRadius *= float( radius != 0 );
    #endif

        // Denoising
        float2x3 TvBv = GetKernelBasis( Nv, Nv, 1.0 ); // D = N

        float worldRadius = PixelRadiusToWorld( gUnproject, gOrthoMode, blurRadius, viewZ );
        TvBv[ 0 ] *= worldRadius;
        TvBv[ 1 ] *= worldRadius;

        float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, diffNonLinearAccumSpeed );
        float normalWeightParams = GetNormalWeightParams( diffNonLinearAccumSpeed, lobeAngleFractionScale );
        float2 hitDistanceWeightParams = GetHitDistanceWeightParams( ExtractHitDist( diff ), diffNonLinearAccumSpeed );

    #ifdef REBLUR_HAS_DIRECTION_PDF
        uint2 p = pixelPos;
        if( gDiffCheckerboard != 2 )
            p.x >>= 1;

        float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_Diff_DirectionPdf[ gRectOrigin + p ] );
        float3 L = dirPdf.xyz;
        float NoL = saturate( dot( N, L ) );

        sum *= NoL / dirPdf.w;
    #endif

        float2 minHitDistWeight = REBLUR_HIT_DIST_MIN_WEIGHT * fractionScale;
    #if( REBLUR_SPATIAL_MODE != REBLUR_PRE_BLUR )
        minHitDistWeight.x = 0.0; // TODO: review
    #endif

        // Sampling
        diff *= Xxxy( sum );
    #ifdef REBLUR_SH
        diffSh *= Xxxy( sum );
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
            if( gDiffCheckerboard != 2 )
                uv = ApplyCheckerboardShift( uv, gDiffCheckerboard, n, gRectSize, gInvRectSize, gFrameIndex );
        #endif

            float2 uvScaled = uv * gResolutionScale;

            // Fetch data
        #if( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
            float zs = UnpackViewZ( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled, 0 ) );
        #else
            float zs = abs( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 ) );
        #endif

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 checkerboardUvScaled = float2( uvScaled.x * ( gDiffCheckerboard != 2 ? 0.5 : 1.0 ), uvScaled.y ) + gRectOffset;
            REBLUR_TYPE s = gIn_Diff.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            #ifdef REBLUR_SH
                float4 sh = gIn_DiffSh.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            #endif
        #else
            REBLUR_TYPE s = gIn_Diff.SampleLevel( gNearestMirror, uvScaled, 0 );
            #ifdef REBLUR_SH
                float4 sh = gIn_DiffSh.SampleLevel( gNearestMirror, uvScaled, 0 );
            #endif
        #endif

            float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, zs, gOrthoMode );

            float materialIDs;
            float4 Ns = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 );
            Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

            // Sample weight
            float w = IsInScreen( uv );
            w *= CompareMaterials( materialID, materialIDs, gDiffMaterialMask );
            w *= GetGaussianWeight( offset.z );
            w *= GetCombinedWeight( geometryWeightParams, Nv, Xvs, normalWeightParams, N, Ns );

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            #ifdef REBLUR_HAS_DIRECTION_PDF
                float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_Diff_DirectionPdf.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 ) );
            #endif
            #ifdef REBLUR_SH
                float4 dirPdf = float4( sh.xyz * rsqrt( dot( sh.xyz, sh.xyz ) + 1e-7 ), sh.w );
            #endif
            #if( defined REBLUR_HAS_DIRECTION_PDF || defined REBLUR_HAS_DIRECTION_PDF )
                float3 L = dirPdf.xyz;
                float NoL = saturate( dot( N, L ) );

                w *= NoL / dirPdf.w;
            #endif
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
            diff += s * Xxxy( ww );
        #ifdef REBLUR_SH
            diffSh += sh * Xxxy( ww );
        #endif
        }

        float2 invSum = STL::Math::PositiveRcp( sum );
        diff *= Xxxy( invSum );
    #ifdef REBLUR_SH
        diffSh *= Xxxy( invSum );
    #endif

#if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
    }

    // Checkerboard resolve ( if pre-pass failed )
    [branch]
    if( !hasData && sum.x == 0.0 )
    {
        REBLUR_TYPE s0 = gIn_Diff[ checkerboardPos.xy ];
        REBLUR_TYPE s1 = gIn_Diff[ checkerboardPos.zy ];

        diff *= saturate( 1.0 - wc.x - wc.y );
        diff += s0 * wc.x + s1 * wc.y;

    #ifdef REBLUR_SH
        float4 sh0 = gIn_DiffSh[ checkerboardPos.xy ];
        float4 sh1 = gIn_DiffSh[ checkerboardPos.zy ];

        diffSh *= saturate( 1.0 - wc.x - wc.y );
        diffSh += sh0 * wc.x + sh1 * wc.y;
    #endif
    }
#endif

    // Output
    gOut_Diff[ pixelPos ] = diff;
    #ifdef REBLUR_SH
        gOut_DiffSh[ pixelPos ] = diffSh;
    #endif
}

#undef POISSON_SAMPLE_NUM
#undef POISSON_SAMPLES
