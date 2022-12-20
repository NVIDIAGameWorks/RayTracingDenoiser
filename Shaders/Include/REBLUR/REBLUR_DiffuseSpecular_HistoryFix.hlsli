/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float2 s_FrameNum[ BUFFER_Y ][ BUFFER_X ];
groupshared float s_DiffLuma[ BUFFER_Y ][ BUFFER_X ];
groupshared float s_SpecLuma[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );

    s_FrameNum[ sharedPos.y ][ sharedPos.x ] = UnpackData1( gIn_Data1[ globalPos ] ).xz;

    #ifdef REBLUR_DIFFUSE
        s_DiffLuma[ sharedPos.y ][ sharedPos.x ] = gIn_DiffFast[ globalPos ].x;
    #endif

    #ifdef REBLUR_SPECULAR
        s_SpecLuma[ sharedPos.y ][ sharedPos.x ] = gIn_SpecFast[ globalPos ].x;
    #endif
}

// Tests 20, 23, 24, 27, 28, 54, 59, 65, 66, 76, 81, 98, 112, 117, 124, 126, 128, 134
// TODO: potentially do color clamping after reconstruction in a separate pass

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    int2 pixelPosUser = gRectOrigin + pixelPos;

    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );

    // Preload
    PRELOAD_INTO_SMEM;

    // Early out
    if( viewZ > gDenoisingRange )
        return;

    // Center data
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    float frustumSize = GetFrustumSize( gMinRectDimMulUnproject, gOrthoMode, viewZ );
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float3 Nv = STL::Geometry::RotateVectorInverse( gViewToWorld, N );

    // Smooth number of accumulated frames
    int2 smemPos = threadPos + BORDER;
    float2 frameNumUnclamped = s_FrameNum[ smemPos.y ][ smemPos.x ];
    float invHistoryFixFrameNum = STL::Math::PositiveRcp( gHistoryFixFrameNum );
    float2 normFrameNum = saturate( frameNumUnclamped * invHistoryFixFrameNum );
    float2 c = normFrameNum;
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

            float2 s = saturate( s_FrameNum[ pos.y ][ pos.x ] * invHistoryFixFrameNum );
            float2 w = step( c, s );

            normFrameNum += s * w;
            sum += w;
        }
    }

    normFrameNum *= rcp( sum );

    float2 scale = saturate( 1.0 - normFrameNum ) * float( gHistoryFixFrameNum != 0.0 );
    float2 frameNum = normFrameNum * gHistoryFixFrameNum;

    // Diffuse
    #ifdef REBLUR_DIFFUSE
        REBLUR_TYPE diff = gIn_Diff[ pixelPos ];
        #ifdef REBLUR_SH
            float4 diffSh = gIn_DiffSh[ pixelPos ];
        #endif

        // History reconstruction
        if( scale.x > REBLUR_HISTORY_FIX_THRESHOLD_1 ) // TODO: use REBLUR_HISTORY_FIX_THRESHOLD_2 to switch to 3x3?
        {
            scale.x = gHistoryFixStrideBetweenSamples / ( 2.0 + frameNum.x ); // TODO: 2 to match RELAX logic, where HistoryFix uses "1 / ( 1 + "N + 1" )"

            // Parameters
            float diffNonLinearAccumSpeed = 1.0 / ( 1.0 + frameNum.x );

            float diffNormalWeightParam = GetNormalWeightParams( diffNonLinearAccumSpeed, 1.0 );
            float2 diffGeometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumSize, Xv, Nv, diffNonLinearAccumSpeed );

            float sumd = 1.0;

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

                    // Sample uv
                    float2 uv = pixelUv + float2( i, j ) * gInvRectSize * scale.x;
                    float2 uvScaled = uv * gResolutionScale;

                    // Fetch data
                    float z = abs( gIn_ViewZ.SampleLevel( gNearestClamp, uvScaled + gRectOffset, 0 ) );

                    float materialIDs;
                    float4 Ns = gIn_Normal_Roughness.SampleLevel( gNearestClamp, uvScaled + gRectOffset, 0 );
                    Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

                    float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gOrthoMode );
                    float NoX = dot( Nv, Xvs );

                    float cosa = saturate( dot( Ns.xyz, N ) );
                    float angle = STL::Math::AcosApprox( cosa );

                    // Accumulate
                    float wd = IsInScreen( uv );
                    wd *= CompareMaterials( materialID, materialIDs, gDiffMaterialMask );
                    wd *= _ComputeWeight( NoX, diffGeometryWeightParams.x, diffGeometryWeightParams.y );
                    wd *= _ComputeExponentialWeight( angle, diffNormalWeightParam, 0.0 );

                    REBLUR_TYPE d = gIn_Diff.SampleLevel( gNearestClamp, uvScaled, 0 );

                    diff += d * wd;
                    sumd += wd;

                    #ifdef REBLUR_SH
                        float4 dh = gIn_DiffSh.SampleLevel( gNearestClamp, uvScaled, 0 );
                        diffSh += dh * wd;
                    #endif
                }
            }

            sumd = STL::Math::PositiveRcp( sumd );
            diff *= sumd;
            #ifdef REBLUR_SH
                diffSh *= sumd;
            #endif
        }

        // Local variance
        float diffCenter = s_DiffLuma[ smemPos.y ][ smemPos.x ];
        float diffM1 = diffCenter;
        float diffM2 = diffM1 * diffM1;
        float diffMax = -NRD_INF;
        float diffMin = NRD_INF;

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

                float d = s_DiffLuma[ pos.y ][ pos.x ];
                diffM1 += d;
                diffM2 += d * d;

                #if( REBLUR_USE_ANTIFIREFLY == 1 )
                    if( all( abs( o ) <= 1 ) || REBLUR_USE_5X5_ANTI_FIREFLY == 1 )
                    {
                        diffMax = max( diffMax, d );
                        diffMin = min( diffMin, d );
                    }
                #endif
            }
        }

        float diffLuma = GetLuma( diff );

        // Anti-firefly
        #if( REBLUR_USE_ANTIFIREFLY == 1 )
            float diffLumaClamped1 = clamp( diffLuma, diffMin, diffMax );
            diffLuma = gAntiFirefly ? diffLumaClamped1 : diffLuma;
        #endif

        // Fast history
        diffM1 /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 );
        diffM2 /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 );
        float diffSigma = GetStdDev( diffM1, diffM2 ) * REBLUR_COLOR_CLAMPING_SIGMA_SCALE;

        // Seems that extending clamping range by the center helps to minimize potential bias
        diffMin = min( diffM1 - diffSigma, diffCenter );
        diffMax = max( diffM1 + diffSigma, diffCenter );

        float diffLumaClamped2 = clamp( diffLuma, diffMin, diffMax );
        diffLuma = lerp( diffLumaClamped2, diffLuma, 1.0 / ( 1.0 + float( gMaxFastAccumulatedFrameNum < gMaxAccumulatedFrameNum ) * frameNumUnclamped.x ) );

        // Change luma
        diff = ChangeLuma( diff, diffLuma );
        #ifdef REBLUR_SH
            diffSh.xyz *= GetLumaScale( length( diffSh.xyz ), diffLuma );
        #endif

        // Output
        gOut_Diff[ pixelPos ] = diff;
        #ifdef REBLUR_SH
            gOut_DiffSh[ pixelPos ] = diffSh;
        #endif
    #endif

    // Specular
    #ifdef REBLUR_SPECULAR
        REBLUR_TYPE spec = gIn_Spec[ pixelPos ];
        #ifdef REBLUR_SH
            float4 specSh = gIn_SpecSh[ pixelPos ];
        #endif

        // History reconstruction
        if( scale.y > REBLUR_HISTORY_FIX_THRESHOLD_1 ) // TODO: use REBLUR_HISTORY_FIX_THRESHOLD_2 to switch to 3x3?
        {
            scale.y = gHistoryFixStrideBetweenSamples / ( 2.0 + frameNum.y ); // TODO: 2 to match RELAX logic, where HistoryFix uses "1 / ( 1 + "N + 1" )"

            // TODO: introduce IN_SECONDARY_ROUGHNESS:
            //  - to allow blur on diffuse-like surfaces in reflection
            //  - use "hitDistanceWeight" only for very low primary roughness to avoid color bleeding from one surface to another

            // Adjust scale to respect the specular lobe
            float hitDistScale = _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, roughness );
            float hitDist = ExtractHitDist( spec ) * hitDistScale;
            float3 Vv = GetViewVector( Xv, true );
            float4 Dv = STL::ImportanceSampling::GetSpecularDominantDirection( Nv, Vv, roughness, STL_SPECULAR_DOMINANT_DIRECTION_G2 );
            float NoD = abs( dot( Nv, Dv.xyz ) );
            float lobeTanHalfAngle = STL::ImportanceSampling::GetSpecularLobeTanHalfAngle( max( REBLUR_HISTORY_FIX_BUMPED_ROUGHNESS, roughness ), 0.65 ); // TODO: 65% energy to better follow the lobe?
            float lobeRadius = hitDist * NoD * lobeTanHalfAngle;
            float minBlurRadius = lobeRadius / PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ + hitDist * Dv.w );
            scale.y = min( scale.y, minBlurRadius / 2.0 );

            // Parameters
            float specNonLinearAccumSpeed = 1.0 / ( 1.0 + frameNum.y );

            float lobeEnergy = lerp( 0.75, 0.85, specNonLinearAccumSpeed );
            float lobeHalfAngle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness, lobeEnergy ); // up to 85% energy to depend less on normal weight
            lobeHalfAngle *= specNonLinearAccumSpeed;

            float specNormalWeightParam = 1.0 / max( lobeHalfAngle, REBLUR_NORMAL_ULP );
            float2 specGeometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumSize, Xv, Nv, specNonLinearAccumSpeed );
            float2 specRoughnessWeightParams = GetRoughnessWeightParams( roughness, gRoughnessFraction );

            float hitDistNormAtCenter = ExtractHitDist( spec );
            float smc = GetSpecMagicCurve( roughness );
            float hitDistWeightScale = 20.0 * STL::Math::LinearStep( REBLUR_HISTORY_FIX_BUMPED_ROUGHNESS, 0.0, roughness );

            float sums = 1.0;

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

                    // Sample uv
                    float2 uv = pixelUv + float2( i, j ) * gInvRectSize * scale.y;
                    float2 uvScaled = uv * gResolutionScale;

                    // Fetch data
                    float z = abs( gIn_ViewZ.SampleLevel( gNearestClamp, uvScaled + gRectOffset, 0 ) );

                    float materialIDs;
                    float4 Ns = gIn_Normal_Roughness.SampleLevel( gNearestClamp, uvScaled + gRectOffset, 0 );
                    Ns = NRD_FrontEnd_UnpackNormalAndRoughness( Ns, materialIDs );

                    float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gOrthoMode );
                    float NoX = dot( Nv, Xvs );

                    float cosa = saturate( dot( Ns.xyz, N ) );
                    float angle = STL::Math::AcosApprox( cosa );

                    // Accumulate
                    float ws = IsInScreen( uv );
                    ws *= CompareMaterials( materialID, materialIDs, gSpecMaterialMask );
                    ws *= _ComputeWeight( NoX, specGeometryWeightParams.x, specGeometryWeightParams.y );
                    ws *= _ComputeExponentialWeight( angle, specNormalWeightParam, 0.0 );
                    ws *= _ComputeExponentialWeight( Ns.w, specRoughnessWeightParams.x, specRoughnessWeightParams.y );

                    REBLUR_TYPE s = gIn_Spec.SampleLevel( gNearestClamp, uvScaled, 0 );

                    // TODO: ideally "diffuseness at hit" needed...
                    // TODO: for roughness closer to REBLUR_HISTORY_FIX_BUMPED_ROUGHNESS "saturate( hitDistNormAtCenter - ExtractHitDist( s ) )" could be used.
                    // It allows bleeding of background to foreground, but not vice versa ( doesn't suit for 0 roughness )
                    ws *= saturate( 1.0 - hitDistWeightScale * abs( ExtractHitDist( s ) - hitDistNormAtCenter ) / ( max( ExtractHitDist( s ), hitDistNormAtCenter ) + NRD_EPS ) );

                    spec += s * ws;
                    sums += ws;

                    #ifdef REBLUR_SH
                        float4 sh = gIn_SpecSh.SampleLevel( gNearestClamp, uvScaled, 0 );
                        specSh += sh * ws;
                    #endif
                }
            }

            sums = STL::Math::PositiveRcp( sums );
            spec *= sums;
            #ifdef REBLUR_SH
                specSh *= sums;
            #endif
        }

        // Local variance
        float specCenter = s_SpecLuma[ smemPos.y ][ smemPos.x ];
        float specM1 = specCenter;
        float specM2 = specM1 * specM1;
        float specMax = -NRD_INF;
        float specMin = NRD_INF;

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

                float s = s_SpecLuma[ pos.y ][ pos.x ];
                specM1 += s;
                specM2 += s * s;

                if( all( abs( o ) <= 1 ) || REBLUR_USE_5X5_ANTI_FIREFLY == 1 )
                {
                    specMax = max( specMax, s );
                    specMin = min( specMin, s );
                }
            }
        }

        float specLuma = GetLuma( spec );

        // Anti-firefly
        float specLumaClamped1 = clamp( specLuma, specMin, specMax );
        specLuma = gAntiFirefly ? specLumaClamped1 : specLuma;

        // Fast history
        specM1 /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 );
        specM2 /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 );
        float specSigma = GetStdDev( specM1, specM2 ) * REBLUR_COLOR_CLAMPING_SIGMA_SCALE;

        // Seems that extending clamping range by the center helps to minimize potential bias
        specMin = min( specM1 - specSigma, specCenter );
        specMax = max( specM1 + specSigma, specCenter );

        float specLumaClamped2 = clamp( specLuma, specMin, specMax );
        specLuma = lerp( specLumaClamped2, specLuma, 1.0 / ( 1.0 + float( gMaxFastAccumulatedFrameNum < gMaxAccumulatedFrameNum ) * frameNumUnclamped.y ) );

        // Change luma
        spec = ChangeLuma( spec, specLuma );
        #ifdef REBLUR_SH
            specSh.xyz *= GetLumaScale( length( specSh.xyz ), specLuma );
        #endif

        // Output
        gOut_Spec[ pixelPos ] = spec;
        #ifdef REBLUR_SH
            gOut_SpecSh[ pixelPos ] = specSh;
        #endif
    #endif
}
