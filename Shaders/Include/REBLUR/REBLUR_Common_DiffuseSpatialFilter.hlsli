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
        float2 diffInternalData = REBLUR_PRE_BLUR_INTERNAL_DATA;

        if( radius != 0.0 )
        {
    #else
        float boost = saturate( 1.0 - diffInternalData.y / REBLUR_FIXED_FRAME_NUM );
        float radius = gBlurRadius;
        radius *= ( 1.0 + 2.0 * boost ) / 3.0;
    #endif

    float radiusScale = 1.0;
    float radiusBias = 0.0;
    float fractionScale = 1.0;

    #if( REBLUR_SPATIAL_MODE == REBLUR_POST_BLUR )
        radiusScale = REBLUR_POST_BLUR_RADIUS_SCALE;
        radiusBias = diffData.x * gBlurRadiusScale + 0.00001;
        fractionScale = REBLUR_POST_BLUR_FRACTION_SCALE;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        radiusBias = diffData.x * gBlurRadiusScale + 0.00001;
        fractionScale = REBLUR_BLUR_FRACTION_SCALE;
    #endif

    if( diff.w == 0.0 )
        diffInternalData.x = 0.5;

    // Blur radius
    float3 Vv = GetViewVector( Xv, true );

    float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
    float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, 1.0 );
    float hitDist = ( diff.w == 0.0 ? 1.0 : diff.w ) * hitDistScale;

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float hitDistFactor = GetHitDistFactor( hitDist, frustumHeight ); // NoD = 1
        float blurRadius = radius * hitDistFactor;
    #else
        float hitDistFactor = GetHitDistFactor( hitDist, frustumHeight );
        float blurRadius = GetBlurRadius( radius, radiusBias, radiusScale, hitDistFactor, frustumHeight, diffInternalData.x );
    #endif

    if( diff.w == 0.0 )
        blurRadius = max( blurRadius, 1.0 );

    float worldBlurRadius = PixelRadiusToWorld( gUnproject, gOrthoMode, blurRadius, viewZ );

    // Denoising
    float2x3 TvBv = GetKernelBasis( Nv, Nv, 1.0, worldBlurRadius ); // D = N
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, lerp( 1.0, REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE, diffInternalData.x ) );
    float normalWeightParams = GetNormalWeightParams( diffInternalData.x, frustumHeight, gLobeAngleFraction * fractionScale );
    float2 hitDistanceWeightParams = diff.w == 0.0 ? 0.0 : GetHitDistanceWeightParams( diff.w, diffInternalData.x );
    float2 sum = 1.0;

    #ifdef REBLUR_SPATIAL_REUSE
        float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( 1.0 );
        normalWeightParams = 1.0 / max( angle, NRD_NORMAL_ENCODING_ERROR );

        uint2 p = pixelPos;
        if( gDiffCheckerboard != 2 )
            p.x >>= 1;

        float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_DiffDirectionPdf[ gRectOrigin + p ] );
        float3 L = dirPdf.xyz;
        float NoL = saturate( dot( N, L ) );

        sum = NoL / ( dirPdf.w + REBLUR_MIN_PDF );
    #endif

    #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
        float2 minHitDistWeight = REBLUR_HIT_DIST_MIN_WEIGHT * 2.0;
    #elif( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT );
    #else
        float2 minHitDistWeight = float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 0.5 );
    #endif

    // Ignore "no data" hit distances
    sum.y *= float( diff.w != 0.0 );

    // Sampling
    float4 center = diff;
    diff *= sum.xxxy;

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
            if( gDiffCheckerboard != 2 )
                checkerboardUv = ApplyCheckerboard( uv, gDiffCheckerboard, i, gRectSize, gInvRectSize, gFrameIndex );
        #endif

        // Fetch data
        float2 uvScaled = uv * gResolutionScale;

        #if( REBLUR_SPATIAL_MODE == REBLUR_PRE_BLUR )
            float2 checkerboardUvScaled = checkerboardUv * gResolutionScale + gRectOffset;
            float4 s = gIn_Diff.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            float zs = abs( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 ) );
        #else
            #ifdef REBLUR_OCCLUSION
                float2 data = gIn_Diff.SampleLevel( gNearestMirror, uvScaled, 0 );
                float4 s = data.x;
                float zs = data.y / NRD_FP16_VIEWZ_SCALE;
            #else
                float4 s = gIn_Diff.SampleLevel( gNearestMirror, uvScaled, 0 );
                float zs = gIn_ScaledViewZ.SampleLevel( gNearestMirror, uvScaled, 0 ) / NRD_FP16_VIEWZ_SCALE;
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
            #ifdef REBLUR_SPATIAL_REUSE
                float4 dirPdf = NRD_FrontEnd_UnpackDirectionAndPdf( gIn_DiffDirectionPdf.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 ) );
                float3 L = dirPdf.xyz;
                float NoL = saturate( dot( N, L ) );

                w *= NoL / ( dirPdf.w + REBLUR_MIN_PDF );
            #endif
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
        diff += s * ww.xxxy;
        sum += ww;
    }

    diff *= STL::Math::PositiveRcp( sum.xxxy );

    // Input mix
    #if( REBLUR_SPATIAL_MODE != REBLUR_PRE_BLUR )
        diff = lerp( diff, center, gInputMix * ( 1.0 - diffInternalData.x ) );
    #else
        }
    #endif

    // Output
    #ifdef REBLUR_OCCLUSION
        #if( REBLUR_SPATIAL_MODE == REBLUR_BLUR )
            gOut_Diff[ pixelPos ] = float2( diff.w, viewZ * NRD_FP16_VIEWZ_SCALE );
        #else
            gOut_Diff[ pixelPos ] = diff.w;
        #endif
    #else
        gOut_Diff[ pixelPos ] = diff;
    #endif
}

#undef POISSON_SAMPLE_NUM
#undef POISSON_SAMPLES
