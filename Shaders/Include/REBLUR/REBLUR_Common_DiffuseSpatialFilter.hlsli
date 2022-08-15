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
    float2 sum = hasData.x;

    float2 diffInternalData = REBLUR_PRE_BLUR_INTERNAL_DATA;

    if( radius != 0.0 )
    {
#else
        float2 diffInternalData = float2( 1.0 / ( 1.0 + internalData1.x ), internalData1.x );
        float2 sum = 1.0;
        float boost = saturate( 1.0 - diffInternalData.y / REBLUR_FIXED_FRAME_NUM );
        float radius = gBlurRadius;
        radius *= ( 1.0 + 2.0 * boost ) / 3.0;
#endif

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        #ifdef REBLUR_HAS_DIRECTION_PDF
            float lobeAngleFractionScale = 1.0;
        #else
            float lobeAngleFractionScale = gLobeAngleFraction;
        #endif
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float radiusScale = 1.0;
        float lobeAngleFractionScale = gLobeAngleFraction * REBLUR_BLUR_FRACTION_SCALE;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        float radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
        float lobeAngleFractionScale = gLobeAngleFraction * REBLUR_POST_BLUR_FRACTION_SCALE;
    #endif

        // Blur radius
        float3 Vv = GetViewVector( Xv, true );

        float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
        float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, 1.0 );
        float hitDist = diff.w * hitDistScale;

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR ) // TODO: explore using unnnormalized (real) hit distance
        float hitDistFactor = GetHitDistFactor( hitDist, frustumHeight ); // NoD = 1
        float blurRadius = radius * hitDistFactor;
    #else
        float radiusBias = internalData1.y * gBlurRadiusScale;
        float hitDistFactor = GetHitDistFactor( hitDist, frustumHeight );
        float blurRadius = GetBlurRadius( radius, radiusBias, radiusScale, hitDistFactor, diffInternalData.x );
    #endif

        float worldBlurRadius = PixelRadiusToWorld( gUnproject, gOrthoMode, blurRadius, viewZ );

        // Denoising
        float2x3 TvBv = GetKernelBasis( Nv, Nv, 1.0, worldBlurRadius ); // D = N
        float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, lerp( 1.0, REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE, diffInternalData.x ) );
        float normalWeightParams = GetNormalWeightParams( diffInternalData.x, lobeAngleFractionScale );
        float2 hitDistanceWeightParams = GetHitDistanceWeightParams( diff.w, diffInternalData.x );

    #ifdef REBLUR_HAS_DIRECTION_PDF
        uint2 p = pixelPos;
        if( gDiffCheckerboard != 2 )
            p.x >>= 1;

        float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_Diff_DirectionPdf[ gRectOrigin + p ] );
        float3 L = dirPdf.xyz;
        float NoL = saturate( dot( N, L ) );

        sum *= NoL / ( dirPdf.w + REBLUR_MIN_PDF );
    #endif

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float2 minHitDistWeight = REBLUR_HIT_DIST_MIN_WEIGHT * 2.0;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT );
    #else
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 0.5 );
    #endif

        // Sampling
        diff *= sum.xxxy;
    #ifdef REBLUR_SH
        diffSh *= sum.xxxy;
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

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 checkerboardUvScaled = checkerboardUv * gResolutionScale + gRectOffset;
            float zs = abs( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 ) );
            float4 s = gIn_Diff.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            #ifdef REBLUR_SH
                float4 sh = gIn_DiffSh.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            #endif
        #else
            #ifdef REBLUR_OCCLUSION
                float2 data = gIn_Diff.SampleLevel( gNearestMirror, uvScaled, 0 );
                float zs = data.y;
                float4 s = data.x;
            #else
                float zs = gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled, 0 );
                float4 s = gIn_Diff.SampleLevel( gNearestMirror, uvScaled, 0 );
                #ifdef REBLUR_SH
                    float4 sh = gIn_DiffSh.SampleLevel( gNearestMirror, uvScaled, 0 );
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
            w *= CompareMaterials( materialID, materialIDs, gDiffMaterialMask );
            w *= GetGaussianWeight( offset.z );
            w *= GetCombinedWeight( geometryWeightParams, Nv, Xvs, normalWeightParams, N, Ns );

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            #ifdef REBLUR_HAS_DIRECTION_PDF
                float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_Diff_DirectionPdf.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 ) );
                float3 L = dirPdf.xyz;
                float NoL = saturate( dot( N, L ) );

                w *= NoL / ( dirPdf.w + REBLUR_MIN_PDF );
            #endif
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
            diff += s * ww.xxxy;
        #ifdef REBLUR_SH
            diffSh += sh * ww.xxxy;
        #endif
        }

        float2 invSum = STL::Math::PositiveRcp( sum );
        diff *= invSum.xxxy;
    #ifdef REBLUR_SH
        diffSh *= invSum.xxxy;
    #endif

#if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
    }

    // Checkerboard resolve ( if pre-pass failed )
    [branch]
    if( !hasData.x && sum.x == 0.0 )
    {
        float4 s0 = gIn_Diff[ checkerboardPos.xy ];
        float4 s1 = gIn_Diff[ checkerboardPos.zy ];

        diff *= saturate( 1.0 - wc.x - wc.y );
        diff += s0 * wc.x + s1 * wc.y;

    #ifdef REBLUR_SH
        float4 sh0 = gIn_Diff[ checkerboardPos.xy ];
        float4 sh1 = gIn_Diff[ checkerboardPos.zy ];

        diffSh *= saturate( 1.0 - wc.x - wc.y );
        diffSh += sh0 * wc.x + sh1 * wc.y;
    #endif
    }
#endif

    // Output
#ifdef REBLUR_OCCLUSION
    #if( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        gOut_Diff[ pixelPos ] = float2( diff.w, PackViewZ( viewZ ) );
    #else
        gOut_Diff[ pixelPos ] = diff.w;
    #endif
#else
    gOut_Diff[ pixelPos ] = diff;
    #ifdef REBLUR_SH
        gOut_DiffSh[ pixelPos ] = diffSh;
    #endif
#endif
}

#undef POISSON_SAMPLE_NUM
#undef POISSON_SAMPLES
