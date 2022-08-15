/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float2 s_FrameNum[ BUFFER_Y ][ BUFFER_X ];

// Anti-firefly
groupshared float s_DiffLuma[ BUFFER_Y ][ BUFFER_X ];
groupshared float s_SpecLuma[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );

    float4 internalData1 = UnpackInternalData1( gIn_Data1[ globalPos ] );
    s_FrameNum[ sharedPos.y ][ sharedPos.x ] = internalData1.xz;

    // Fast history & anti-firefly
    #ifndef REBLUR_PERFORMANCE_MODE
        #ifdef REBLUR_DIFFUSE
            #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
                s_DiffLuma[ sharedPos.y ][ sharedPos.x ] = gIn_DiffFast[ globalPos ];
            #else
                s_DiffLuma[ sharedPos.y ][ sharedPos.x ] = GetLuma( gIn_Diff[ globalPos ] );
            #endif
        #endif

        #ifdef REBLUR_SPECULAR
            #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
                s_SpecLuma[ sharedPos.y ][ sharedPos.x ] = gIn_SpecFast[ globalPos ];
            #else
                s_SpecLuma[ sharedPos.y ][ sharedPos.x ] = GetLuma( gIn_Spec[ globalPos ] );
            #endif
        #endif
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    int2 pixelPosUser = gRectOrigin + pixelPos;

    // Center data
    #ifdef REBLUR_OCCLUSION
        float viewZ;

        #ifdef REBLUR_DIFFUSE
            float2 diffTemp = gIn_Diff[ pixelPos ];
            float diff = diffTemp.x;
            viewZ = diffTemp.y;
        #endif

        #ifdef REBLUR_SPECULAR
            float2 specTemp = gIn_Spec[ pixelPos ];
            float spec = specTemp.x;
            viewZ = specTemp.y;
        #endif

        viewZ = UnpackViewZ( viewZ );
    #else
        #ifdef REBLUR_DIFFUSE
            float4 diff = gIn_Diff[ pixelPos ];
            #ifdef REBLUR_SH
                float4 diffSh = gIn_DiffSh[ pixelPos ];
            #endif
        #endif

        #ifdef REBLUR_SPECULAR
            float4 spec = gIn_Spec[ pixelPos ];
            #ifdef REBLUR_SH
                float4 specSh = gIn_SpecSh[ pixelPos ];
            #endif
        #endif

        float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );
    #endif

    // Preload
    PRELOAD_INTO_SMEM;

    // Early out
    if( viewZ > gDenoisingRange )
        return;

    // Fast history & anti-firefly
    int2 smemPos = threadPos + BORDER;
    float2 frameNumUnclamped = s_FrameNum[ smemPos.y ][ smemPos.x ];

    #ifndef REBLUR_PERFORMANCE_MODE
        #ifdef REBLUR_DIFFUSE
            float diffM1 = s_DiffLuma[ smemPos.y ][ smemPos.x ];
            float diffM2 = diffM1 * diffM1;
            float diffMax = -NRD_INF;
            float diffMin = NRD_INF;
        #endif

        #ifdef REBLUR_SPECULAR
            float specM1 = s_SpecLuma[ smemPos.y ][ smemPos.x ];
            float specM2 = specM1 * specM1;
            float specMax = -NRD_INF;
            float specMin = NRD_INF;
        #endif

        [unroll]
        for( int y = 0; y <= BORDER * 2; y++ )
        {
            [unroll]
            for( int x = 0; x <= BORDER * 2; x++ )
            {
                // Skip center
                if( x == BORDER && y == BORDER )
                    continue;

                int2 pos = threadPos + int2( x, y );
                float2 o = float2( x, y ) - BORDER;

                #ifdef REBLUR_DIFFUSE
                    float d = s_DiffLuma[ pos.y ][ pos.x ];
                    diffM1 += d;
                    diffM2 += d * d;

                    if( all( abs( o ) <= 1 ) || REBLUR_USE_5X5_ANTI_FIREFLY == 1 )
                    {
                        diffMax = max( diffMax, d );
                        diffMin = min( diffMin, d );
                    }
                #endif

                #ifdef REBLUR_SPECULAR
                    float s = s_SpecLuma[ pos.y ][ pos.x ];
                    specM1 += s;
                    specM2 += s * s;

                    if( all( abs( o ) <= 1 ) || REBLUR_USE_5X5_ANTI_FIREFLY == 1 )
                    {
                        specMax = max( specMax, s );
                        specMin = min( specMin, s );
                    }
                #endif
            }
        }

        float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );
        float useFastHistory = float( gMaxAccumulatedFrameNum > gMaxFastAccumulatedFrameNum );

        #ifdef REBLUR_DIFFUSE
            float diffLuma = GetLuma( diff );

            // Fast history
            #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
                diffM1 *= invSum;
                diffM2 *= invSum;
                float diffSigma = GetStdDev( diffM1, diffM2 ) * REBLUR_COLOR_CLAMPING_SIGMA_SCALE;

                float diffLumaClamped1 = STL::Color::Clamp( diffM1, diffSigma, diffLuma );
                diffLuma = lerp( diffLumaClamped1, diffLuma, 1.0 / ( 1.0 + useFastHistory * frameNumUnclamped.x ) );
            #endif

            // Anti-firefly
            float diffLumaClamped2 = clamp( diffLuma, diffMin, diffMax );
            diffLuma = lerp( diffLumaClamped2, diffLuma, 1.0 / ( 1.0 + gAntiFirefly * frameNumUnclamped.x ) );

            // Change luma
            diff = ChangeLuma( diff, diffLuma );

            #ifdef REBLUR_SH
                diffSh.xyz *= ( diffLuma / 0.282095 ) / ( length( diffSh.xyz ) / 0.488603 + 1e-6 );
            #endif
        #endif

        #ifdef REBLUR_SPECULAR
            float specLuma = GetLuma( spec );

            // Fast history
            #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
                specM1 *= invSum;
                specM2 *= invSum;
                float specSigma = GetStdDev( specM1, specM2 ) * REBLUR_COLOR_CLAMPING_SIGMA_SCALE;

                float specLumaClamped1 = STL::Color::Clamp( specM1, specSigma, specLuma );
                specLuma = lerp( specLumaClamped1, specLuma, 1.0 / ( 1.0 + useFastHistory * frameNumUnclamped.y ) );
            #endif

            // Anti-firefly
            float specLumaClamped2 = clamp( specLuma, specMin, specMax );
            specLuma = lerp( specLumaClamped2, specLuma, 1.0 / ( 1.0 + gAntiFirefly * frameNumUnclamped.y ) );

            // Change luma
            spec = ChangeLuma( spec, specLuma );

            #ifdef REBLUR_SH
                specSh.xyz *= ( specLuma / 0.282095 ) / ( length( specSh.xyz ) / 0.488603 + 1e-6 );
            #endif
        #endif
    #endif

    // Smooth internal data // TODO: move this to TA to store smoothed accum speed in "internal data", but
    // due to SMEM preloading limitations edge pixels will have to clamp to the CTA size, but still:
    // - a corner pixel will get 3 neighbors
    // - an edge pixel will get 5 neighbors
    // - all other will get 8 neighbors
    // - it shouldn't affect IQ and even logic
    float2 frameNum = saturate( frameNumUnclamped / REBLUR_FIXED_FRAME_NUM );
    float2 c = frameNum;
    float2 sum = 1.0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            // Skip center
            if( dx == BORDER && dy == BORDER )
                continue;

            // Only 3x3 needed
            float2 o = float2( dx, dy ) - BORDER;
            if( any( abs( o ) > 1 ) )
                continue;

            int2 pos = threadPos + int2( dx, dy );

            float2 s = saturate( s_FrameNum[ pos.y ][ pos.x ] / REBLUR_FIXED_FRAME_NUM );
            float2 w = step( c, s );

            frameNum += s * w;
            sum += w;
        }
    }

    frameNum *= rcp( sum );

    // History reconstruction ( tests 20, 23, 24, 27, 28, 54, 59, 65, 66, 76, 81, 98, 112, 117, 124, 126, 128, 134 )
    float stepSize;
    float2 scale = saturate( 1.0 - frameNum );
    #if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
        stepSize = max( scale.x, scale.y );
    #elif( defined REBLUR_DIFFUSE )
        stepSize = scale.x;
    #else
        stepSize = scale.y;
    #endif

    sum = 1.0; // TODO: reduce weight?

    if( stepSize > REBLUR_HISTORY_FIX_THRESHOLD_1 ) // TODO: use REBLUR_HISTORY_FIX_THRESHOLD_2 to switch to 3x3?
    {
        // Normal and roughness
        float materialID;
        float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
        float3 N = normalAndRoughness.xyz;
        float roughness = normalAndRoughness.w;

        float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
        float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
        float3 Nv = STL::Geometry::RotateVectorInverse( gViewToWorld, N );

        float3 Vv = GetViewVector( Xv, true );
        float NoV = abs( dot( Vv, Nv ) );
        stepSize *= lerp( 1.0, 0.5, STL::BRDF::Pow5( NoV ) );
        stepSize *= REBLUR_HISTORY_FIX_STEP;
        stepSize *= gHistoryFixStrength;

        #ifdef REBLUR_DIFFUSE
            float diffNonLinearAccumSpeed = 1.0 / ( 1.0 + frameNumUnclamped.x );
            float2 diffGeometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, lerp( 1.0, REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE, diffNonLinearAccumSpeed ) );
            float diffNormalWeightParam = GetNormalWeightParams( diffNonLinearAccumSpeed, 1.0 );
        #endif

        #ifdef REBLUR_SPECULAR
            float specNonLinearAccumSpeed = 1.0 / ( 1.0 + frameNumUnclamped.y );

            float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness, 0.95 );
            #ifndef REBLUR_OCCLUSION
                uint unused;
                float3 internalData2 = UnpackInternalData2( gIn_Data2[ pixelPos ], viewZ, unused );
                float curvature = abs( internalData2.z );

                float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
                curvature *= pixelSize; // tana = pixelSize / curvatureRadius = pixelSize * curvature

                float curvatureAngleTan = stepSize * curvature;
                float curvatureAngle = STL::Math::AtanApprox( saturate( curvatureAngleTan ) );
                angle += curvatureAngle;
            #else
                float curvature = 0;
            #endif
            float specNormalWeightParam = rcp( angle * specNonLinearAccumSpeed + NRD_NORMAL_ENCODING_ERROR );

            float2 specGeometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, lerp( 1.0, REBLUR_PLANE_DIST_MIN_SENSITIVITY_SCALE, specNonLinearAccumSpeed ) );
            float2 specRoughnessWeightParams = GetRoughnessWeightParams( roughness, 1.0 );
            float2 specHitDistanceWeightParams = GetHitDistanceWeightParams( ExtractHitDist( spec ), specNonLinearAccumSpeed, roughness );

            float specGaussianWeight = 1.5 * ( 1.0 - roughness ) * ( 1.0 - STL::Math::Pow01( curvature, 0.25 ) );  // TODO: roughness at hit needed
        #endif

        #ifdef REBLUR_DIFFUSE
            diff *= sum.x;
            #ifdef REBLUR_SH
                diffSh *= sum.x;
            #endif
        #endif
        #ifdef REBLUR_SPECULAR
            spec *= sum.y;
            #ifdef REBLUR_SH
                specSh *= sum.y;
            #endif
        #endif

        // Slow path
        [unroll]
        for( int i = -2; i <= 2; i++ )
        {
            [unroll]
            for( int j = -2; j <= 2; j++ )
            {
                // Skip center
                if( i == 0 && j == 0 )
                    continue;

                // Skip corners
                if( abs( i ) + abs( j ) == 4 )
                    continue;

                // Fetch data
                float2 tap = float2( i, j );
                float2 uv = pixelUv + tap * gInvRectSize * stepSize;
                float2 uvScaled = uv * gResolutionScale;

                #ifdef REBLUR_OCCLUSION
                    float z;
                    #ifdef REBLUR_DIFFUSE
                        float2 diffTemp = gIn_Diff.SampleLevel( gNearestClamp, uvScaled, 0 );
                        float d = diffTemp.x;
                        z = diffTemp.y;
                    #endif
                    #ifdef REBLUR_SPECULAR
                        float2 specTemp = gIn_Spec.SampleLevel( gNearestClamp, uvScaled, 0 );
                        float s = specTemp.x;
                        z = specTemp.y;
                    #endif
                    z = UnpackViewZ( z );
                #else
                    #ifdef REBLUR_DIFFUSE
                        float4 d = gIn_Diff.SampleLevel( gNearestClamp, uvScaled, 0 );
                    #endif
                    #ifdef REBLUR_SPECULAR
                        float4 s = gIn_Spec.SampleLevel( gNearestClamp, uvScaled, 0 );
                    #endif
                    float z = abs( gIn_ViewZ.SampleLevel( gNearestClamp, uvScaled + gRectOffset, 0 ) );
                #endif

                float materialIDs;
                float4 Ns = gIn_Normal_Roughness.SampleLevel( gNearestClamp, uvScaled + gRectOffset, 0 );
                Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

                // Sample weight - strict
                float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gOrthoMode );
                float NoX = dot( Nv, Xvs );

                float2 w = IsInScreen( uv );
                #ifdef REBLUR_DIFFUSE
                    w.x *= _ComputeWeight( NoX, diffGeometryWeightParams.x, diffGeometryWeightParams.y );
                    w.x *= CompareMaterials( materialID, materialIDs, gDiffMaterialMask );
                #endif
                #ifdef REBLUR_SPECULAR
                    w.y *= _ComputeWeight( NoX, specGeometryWeightParams.x, specGeometryWeightParams.y );
                    w.y *= CompareMaterials( materialID, materialIDs, gSpecMaterialMask );
                #endif

                // Sample weight - exponential
                float cosa = saturate( dot( Ns.xyz, N ) );
                float angle = STL::Math::AcosApprox( cosa );

                #ifdef REBLUR_DIFFUSE
                    w.x *= _ComputeExponentialWeight( angle, diffNormalWeightParam, 0.0, NRD_EXP_WEIGHT_DEFAULT_SCALE );
                #endif
                #ifdef REBLUR_SPECULAR
                    w.y *= GetGaussianWeight( length( tap ) * specGaussianWeight );
                    w.y *= _ComputeExponentialWeight( Ns.w, specRoughnessWeightParams.x, specRoughnessWeightParams.y, NRD_EXP_WEIGHT_DEFAULT_SCALE );
                    w.y *= _ComputeExponentialWeight( angle, specNormalWeightParam, 0.0, NRD_EXP_WEIGHT_DEFAULT_SCALE );
                    w.y *= _ComputeExponentialWeight( ExtractHitDist( s ), specHitDistanceWeightParams.x, specHitDistanceWeightParams.y, NRD_EXP_WEIGHT_DEFAULT_SCALE );
                #endif

                // Accumulate
                #ifdef REBLUR_DIFFUSE
                    diff += d * w.x;
                    #ifdef REBLUR_SH
                        float4 dh = gIn_DiffSh.SampleLevel( gNearestClamp, uvScaled, 0 );
                        diffSh += dh * w.x;
                    #endif
                #endif
                #ifdef REBLUR_SPECULAR
                    spec += s * w.y;
                    #ifdef REBLUR_SH
                        float4 sh = gIn_SpecSh.SampleLevel( gNearestClamp, uvScaled, 0 );
                        specSh += sh * w.y;
                    #endif
                #endif

                sum += w;
            }
        }
    }

    sum = rcp( sum );

    // Output
    #ifdef REBLUR_DIFFUSE
        #ifdef REBLUR_OCCLUSION
            gOut_Diff[ pixelPos ] = float2( diff * sum.x, PackViewZ( viewZ ) );
        #else
            gOut_Diff[ pixelPos ] = diff * sum.x;
        #endif
        #ifdef REBLUR_SH
            gOut_DiffSh[ pixelPos ] = diffSh * sum.x;
        #endif
    #endif

    #ifdef REBLUR_SPECULAR
        #ifdef REBLUR_OCCLUSION
            gOut_Spec[ pixelPos ] = float2( spec * sum.y, PackViewZ( viewZ ) );
        #else
            gOut_Spec[ pixelPos ] = spec * sum.y;
        #endif
        #ifdef REBLUR_SH
            gOut_SpecSh[ pixelPos ] = specSh * sum.y;
        #endif
    #endif
}
