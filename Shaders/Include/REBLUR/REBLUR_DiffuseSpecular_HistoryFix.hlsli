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

    float4 data1 = UnpackData1( gIn_Data1[ globalPos ] );
    s_FrameNum[ sharedPos.y ][ sharedPos.x ] = data1.xz;

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
    #ifdef REBLUR_DIFFUSE
        REBLUR_TYPE diff = gIn_Diff[ pixelPos ];
        #ifdef REBLUR_SH
            float4 diffSh = gIn_DiffSh[ pixelPos ];
        #endif
    #endif

    #ifdef REBLUR_SPECULAR
        REBLUR_TYPE spec = gIn_Spec[ pixelPos ];
        #ifdef REBLUR_SH
            float4 specSh = gIn_SpecSh[ pixelPos ];
        #endif
    #endif

    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );

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
            float diffCenter = s_DiffLuma[ smemPos.y ][ smemPos.x ];
            float diffM1 = diffCenter;
            float diffM2 = diffM1 * diffM1;
            float diffMax = -NRD_INF;
            float diffMin = NRD_INF;
        #endif

        #ifdef REBLUR_SPECULAR
            float specCenter = s_SpecLuma[ smemPos.y ][ smemPos.x ];
            float specM1 = specCenter;
            float specM2 = specM1 * specM1;
            float specMax = -NRD_INF;
            float specMin = NRD_INF;
        #endif

        [unroll]
        for( j = 0; j <= BORDER * 2; j++ )
        {
            [unroll]
            for( i = 0; i <= BORDER * 2; i++ )
            {
                // Skip center
                if( i == BORDER && j == BORDER )
                    continue;

                int2 pos = threadPos + int2( i, j );
                float2 o = float2( i, j ) - BORDER;

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

            // Anti-firefly
            float diffLumaClamped1 = clamp( diffLuma, diffMin, diffMax );
            diffLuma = gAntiFirefly ? diffLumaClamped1 : diffLuma;

            // Fast history
            #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
                diffM1 *= invSum;
                diffM2 *= invSum;
                float diffSigma = GetStdDev( diffM1, diffM2 ) * REBLUR_COLOR_CLAMPING_SIGMA_SCALE;

                // Seems that extending clamping range by the center helps to minimize potential bias
                diffMin = min( diffM1 - diffSigma, diffCenter );
                diffMax = max( diffM1 + diffSigma, diffCenter );

                float diffLumaClamped2 = clamp( diffLuma, diffMin, diffMax );
                diffLuma = lerp( diffLumaClamped2, diffLuma, 1.0 / ( 1.0 + useFastHistory * frameNumUnclamped.x ) );
            #endif

            // Change luma
            diff = ChangeLuma( diff, diffLuma );

            #ifdef REBLUR_SH
                diffSh.xyz *= ( diffLuma + NRD_EPS ) / ( length( diffSh.xyz ) + NRD_EPS );
            #endif
        #endif

        #ifdef REBLUR_SPECULAR
            float specLuma = GetLuma( spec );

            // Anti-firefly
            float specLumaClamped1 = clamp( specLuma, specMin, specMax );
            specLuma = gAntiFirefly ? specLumaClamped1 : specLuma;

            // Fast history
            #if( REBLUR_USE_FAST_HISTORY == 1 && !defined( REBLUR_OCCLUSION ) )
                specM1 *= invSum;
                specM2 *= invSum;
                float specSigma = GetStdDev( specM1, specM2 ) * REBLUR_COLOR_CLAMPING_SIGMA_SCALE;

                // Seems that extending clamping range by the center helps to minimize potential bias
                specMin = min( specM1 - specSigma, specCenter );
                specMax = max( specM1 + specSigma, specCenter );

                float specLumaClamped2 = clamp( specLuma, specMin, specMax );
                specLuma = lerp( specLumaClamped2, specLuma, 1.0 / ( 1.0 + useFastHistory * frameNumUnclamped.y ) );
            #endif

            // Change luma
            spec = ChangeLuma( spec, specLuma );

            #ifdef REBLUR_SH
                specSh.xyz *= ( specLuma + NRD_EPS ) / ( length( specSh.xyz ) + NRD_EPS );
            #endif
        #endif
    #endif

    // Smooth
    float2 frameNum = saturate( frameNumUnclamped / gHistoryFixFrameNum );
    float2 c = frameNum;
    float2 sum = 1.0;

    [unroll]
    for( j = 0; j <= BORDER * 2; j++ )
    {
        [unroll]
        for( i = 0; i <= BORDER * 2; i++ )
        {
            // Skip center
            if( i == BORDER && j == BORDER )
                continue;

            // Only 3x3 needed
            float2 o = float2( i, j ) - BORDER;
            if( any( abs( o ) > 1 ) )
                continue;

            int2 pos = threadPos + int2( i, j );

            float2 s = saturate( s_FrameNum[ pos.y ][ pos.x ] / gHistoryFixFrameNum );
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

    if( stepSize > REBLUR_HISTORY_FIX_THRESHOLD_1 ) // TODO: use REBLUR_HISTORY_FIX_THRESHOLD_2 to switch to 3x3?
    {
        float materialID;
        float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
        float3 N = normalAndRoughness.xyz;
        float roughness = normalAndRoughness.w;

        float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
        float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
        float3 Nv = STL::Geometry::RotateVectorInverse( gViewToWorld, N );

        stepSize *= gHistoryFixStrideBetweenSamples;

        #ifdef REBLUR_DIFFUSE
            float sumd = 1.0;
            float diffNonLinearAccumSpeed = 1.0 / ( 1.0 + frameNumUnclamped.x );

            float diffNormalWeightParam = GetNormalWeightParams( diffNonLinearAccumSpeed, 1.0 );
            float2 diffGeometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, diffNonLinearAccumSpeed );
        #endif

        #ifdef REBLUR_SPECULAR
            float2 sums = 1.0;
            float specNonLinearAccumSpeed = 1.0 / ( 1.0 + frameNumUnclamped.y );

            float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness, 0.95 );
            #ifndef REBLUR_OCCLUSION
                uint unused;
                float3 data2 = UnpackData2( gIn_Data2[ pixelPos ], viewZ, unused );
                float curvature = abs( data2.z );

                float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
                curvature *= pixelSize; // tana = pixelSize / curvatureRadius = pixelSize * curvature

                float curvatureAngleTan = stepSize * curvature;
                float curvatureAngle = STL::Math::AtanApprox( saturate( curvatureAngleTan ) );
                angle += curvatureAngle;
            #else
                float curvature = 0;
            #endif
            float specNormalWeightParam = rcp( angle * specNonLinearAccumSpeed + NRD_NORMAL_ENCODING_ERROR );
            float2 specGeometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, specNonLinearAccumSpeed );
            float2 specRoughnessWeightParams = GetRoughnessWeightParams( roughness, 1.0 );
            float2 specHitDistanceWeightParams = GetHitDistanceWeightParams( ExtractHitDist( spec ), specNonLinearAccumSpeed, roughness );

            float2 specGaussianWeight = 1.5 * ( 1.0 - roughness ) * ( 1.0 - STL::Math::Pow01( curvature, 0.25 ) ); // TODO: roughness at hit needed

            // Hit distances are clean for very low roughness, sparse reconstruction can confuse temporal accumulation on the next frame
            float smc = GetSpecMagicCurve2( roughness );
            specGaussianWeight.y *= lerp( 10.0, 1.0, smc );
        #endif

        [unroll]
        for( j = -2; j <= 2; j++ )
        {
            [unroll]
            for( i = -2; i <= 2; i++ )
            {
                // Skip center
                if( i == 0 && j == 0 )
                    continue;

                // Skip corners
                if( abs( i ) + abs( j ) == 4 )
                    continue;

                float2 tap = float2( i, j );
                float2 uv = pixelUv + STL::Geometry::RotateVector( gRotator, tap ) * gInvRectSize * stepSize;
                float2 uvScaled = uv * gResolutionScale;

                float z = abs( gIn_ViewZ.SampleLevel( gNearestClamp, uvScaled + gRectOffset, 0 ) );

                float materialIDs;
                float4 Ns = gIn_Normal_Roughness.SampleLevel( gNearestClamp, uvScaled + gRectOffset, 0 );
                Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

                float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gOrthoMode );
                float NoX = dot( Nv, Xvs );

                float cosa = saturate( dot( Ns.xyz, N ) );
                float angle = STL::Math::AcosApprox( cosa );

                // Accumulate
                #ifdef REBLUR_DIFFUSE
                    float wd = IsInScreen( uv );
                    wd *= _ComputeWeight( NoX, diffGeometryWeightParams.x, diffGeometryWeightParams.y );
                    wd *= CompareMaterials( materialID, materialIDs, gDiffMaterialMask );
                    wd *= _ComputeExponentialWeight( angle, diffNormalWeightParam, 0.0 );

                    REBLUR_TYPE d = gIn_Diff.SampleLevel( gNearestClamp, uvScaled, 0 );

                    diff += d * wd;
                    sumd += wd;

                    #ifdef REBLUR_SH
                        float4 dh = gIn_DiffSh.SampleLevel( gNearestClamp, uvScaled, 0 );
                        diffSh += dh * wd;
                    #endif
                #endif

                #ifdef REBLUR_SPECULAR
                    float ws = IsInScreen( uv );
                    ws *= _ComputeWeight( NoX, specGeometryWeightParams.x, specGeometryWeightParams.y );
                    ws *= CompareMaterials( materialID, materialIDs, gSpecMaterialMask );
                    ws *= _ComputeExponentialWeight( Ns.w, specRoughnessWeightParams.x, specRoughnessWeightParams.y );
                    ws *= _ComputeExponentialWeight( angle, specNormalWeightParam, 0.0 );

                    REBLUR_TYPE s = gIn_Spec.SampleLevel( gNearestClamp, uvScaled, 0 );
                    ws *= _ComputeExponentialWeight( ExtractHitDist( s ), specHitDistanceWeightParams.x, specHitDistanceWeightParams.y );

                    float2 ww = ws;
                    ww.x *= GetGaussianWeight( length( tap ) * specGaussianWeight.x );
                    ww.y *= GetGaussianWeight( length( tap ) * specGaussianWeight.y );

                    spec += s * Xxxy( ww );
                    sums += ww;

                    #ifdef REBLUR_SH
                        float4 sh = gIn_SpecSh.SampleLevel( gNearestClamp, uvScaled, 0 );
                        specSh += sh * Xxxy( ww );
                    #endif
                #endif
            }
        }

        #ifdef REBLUR_DIFFUSE
            sumd = STL::Math::PositiveRcp( sumd );
            diff *= sumd;
            #ifdef REBLUR_SH
                diffSh *= sumd;
            #endif
        #endif

        #ifdef REBLUR_SPECULAR
            sums = STL::Math::PositiveRcp( sums );
            spec *= Xxxy( sums );
            #ifdef REBLUR_SH
                specSh *= Xxxy( sums );
            #endif
        #endif
    }

    // Output
    #ifdef REBLUR_DIFFUSE
        gOut_Diff[ pixelPos ] = diff;
        #ifdef REBLUR_SH
            gOut_DiffSh[ pixelPos ] = diffSh;
        #endif
    #endif

    #ifdef REBLUR_SPECULAR
        gOut_Spec[ pixelPos ] = spec;
        #ifdef REBLUR_SH
            gOut_SpecSh[ pixelPos ] = specSh;
        #endif
    #endif
}
