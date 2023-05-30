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

    // Preload
    float isSky = gIn_Tiles[ pixelPos >> 4 ];
    PRELOAD_INTO_SMEM_WITH_TILE_CHECK;

    // Tile-based early out
    if( isSky != 0.0 || pixelPos.x >= gRectSize.x || pixelPos.y >= gRectSize.y )
        return;

    // Early out
    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );
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
                    float w = IsInScreen( uv );
                    w *= CompareMaterials( materialID, materialIDs, gDiffMaterialMask );
                    w *= _ComputeWeight( NoX, diffGeometryWeightParams.x, diffGeometryWeightParams.y );
                    w *= _ComputeExponentialWeight( angle, diffNormalWeightParam, 0.0 );

                    REBLUR_TYPE s = gIn_Diff.SampleLevel( gNearestClamp, uvScaled, 0 );

                    // Get rid of potentially bad values outside of the screen
                    w = IsInScreen( uv ) ? w : 0.0; // no "!isnan" because "s" is not used for "w" calculations
                    s = w != 0.0 ? s : 0.0;

                    diff += s * w;
                    sumd += w;

                    #ifdef REBLUR_SH
                        float4 sh = gIn_DiffSh.SampleLevel( gNearestClamp, uvScaled, 0 );
                        diffSh += sh * w;
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

                float d = s_DiffLuma[ pos.y ][ pos.x ];
                diffM1 += d;
                diffM2 += d * d;
            }
        }

        float diffLuma = GetLuma( diff );

        // Anti-firefly
        if( gAntiFirefly && REBLUR_USE_ANTIFIREFLY == 1 )
        {
            float m1 = 0;
            float m2 = 0;

            [unroll]
            for( j = -REBLUR_ANTI_FIREFLY_FILTER_RADIUS; j <= REBLUR_ANTI_FIREFLY_FILTER_RADIUS; j++ )
            {
                [unroll]
                for( i = -REBLUR_ANTI_FIREFLY_FILTER_RADIUS; i <= REBLUR_ANTI_FIREFLY_FILTER_RADIUS; i++ )
                {
                    // Skip central 3x3 area
                    if( abs( i ) <= 1 && abs( j ) <= 1 )
                        continue;

                    float d = gIn_DiffFast.SampleLevel( gNearestClamp, pixelUv + int2( i, j ) * gInvRectSize, 0 ).x;
                    m1 += d;
                    m2 += d * d;
                }
            }

            float invNorm = 1.0 / ( ( REBLUR_ANTI_FIREFLY_FILTER_RADIUS * 2 + 1 ) * ( REBLUR_ANTI_FIREFLY_FILTER_RADIUS * 2 + 1 ) - 3 * 3 );
            m1 *= invNorm;
            m2 *= invNorm;

            float sigma = GetStdDev( m1, m2 ) * REBLUR_ANTI_FIREFLY_SIGMA_SCALE;
            diffLuma = clamp( diffLuma, m1 - sigma, m1 + sigma );
        }

        // Fast history
        diffM1 /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 );
        diffM2 /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 );
        float diffSigma = GetStdDev( diffM1, diffM2 ) * REBLUR_COLOR_CLAMPING_SIGMA_SCALE;

        // Seems that extending clamping range by the center helps to minimize potential bias
        float diffMin = min( diffM1 - diffSigma, diffCenter );
        float diffMax = max( diffM1 + diffSigma, diffCenter );

        float diffLumaClamped = clamp( diffLuma, diffMin, diffMax );
        diffLuma = lerp( diffLumaClamped, diffLuma, 1.0 / ( 1.0 + float( gMaxFastAccumulatedFrameNum < gMaxAccumulatedFrameNum ) * frameNumUnclamped.x ) );

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
            float2 specRoughnessWeightParamsSq = GetRoughnessWeightParamsSq( roughness, gRoughnessFraction );

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
                    float w = IsInScreen( uv );
                    w *= CompareMaterials( materialID, materialIDs, gSpecMaterialMask );
                    w *= _ComputeWeight( NoX, specGeometryWeightParams.x, specGeometryWeightParams.y );
                    w *= _ComputeExponentialWeight( angle, specNormalWeightParam, 0.0 );
                    w *= _ComputeExponentialWeight( Ns.w * Ns.w, specRoughnessWeightParamsSq.x, specRoughnessWeightParamsSq.y );

                    REBLUR_TYPE s = gIn_Spec.SampleLevel( gNearestClamp, uvScaled, 0 );

                    // TODO: ideally "diffuseness at hit" needed...
                    // TODO: for roughness closer to REBLUR_HISTORY_FIX_BUMPED_ROUGHNESS "saturate( hitDistNormAtCenter - ExtractHitDist( s ) )" could be used.
                    // It allows bleeding of background to foreground, but not vice versa ( doesn't suit for 0 roughness )
                    w *= saturate( 1.0 - hitDistWeightScale * abs( ExtractHitDist( s ) - hitDistNormAtCenter ) / ( max( ExtractHitDist( s ), hitDistNormAtCenter ) + NRD_EPS ) );

                    // Get rid of potentially bad values outside of the screen
                    w = ( IsInScreen( uv ) && !isnan( w ) ) ? w : 0.0;
                    s = w != 0.0 ? s : 0.0;

                    spec += s * w;
                    sums += w;

                    #ifdef REBLUR_SH
                        float4 sh = gIn_SpecSh.SampleLevel( gNearestClamp, uvScaled, 0 );
                        specSh += sh * w;
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

                float s = s_SpecLuma[ pos.y ][ pos.x ];
                specM1 += s;
                specM2 += s * s;
            }
        }

        float specLuma = GetLuma( spec );

        // Anti-firefly
        if( gAntiFirefly && REBLUR_USE_ANTIFIREFLY == 1 )
        {
            float m1 = 0;
            float m2 = 0;

            [unroll]
            for( j = -REBLUR_ANTI_FIREFLY_FILTER_RADIUS; j <= REBLUR_ANTI_FIREFLY_FILTER_RADIUS; j++ )
            {
                [unroll]
                for( i = -REBLUR_ANTI_FIREFLY_FILTER_RADIUS; i <= REBLUR_ANTI_FIREFLY_FILTER_RADIUS; i++ )
                {
                    // Skip central 3x3 area
                    if( abs( i ) <= 1 && abs( j ) <= 1 )
                        continue;

                    float s = gIn_SpecFast.SampleLevel( gNearestClamp, pixelUv + int2( i, j ) * gInvRectSize, 0 ).x;
                    m1 += s;
                    m2 += s * s;
                }
            }

            float invNorm = 1.0 / ( ( REBLUR_ANTI_FIREFLY_FILTER_RADIUS * 2 + 1 ) * ( REBLUR_ANTI_FIREFLY_FILTER_RADIUS * 2 + 1 ) - 3 * 3 );
            m1 *= invNorm;
            m2 *= invNorm;

            float sigma = GetStdDev( m1, m2 ) * REBLUR_ANTI_FIREFLY_SIGMA_SCALE;
            specLuma = clamp( specLuma, m1 - sigma, m1 + sigma );
        }

        // Fast history
        specM1 /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 );
        specM2 /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 );
        float specSigma = GetStdDev( specM1, specM2 ) * REBLUR_COLOR_CLAMPING_SIGMA_SCALE;

        // Seems that extending clamping range by the center helps to minimize potential bias
        float specMin = min( specM1 - specSigma, specCenter );
        float specMax = max( specM1 + specSigma, specCenter );

        float specLumaClamped = clamp( specLuma, specMin, specMax );
        specLuma = lerp( specLumaClamped, specLuma, 1.0 / ( 1.0 + float( gMaxFastAccumulatedFrameNum < gMaxAccumulatedFrameNum ) * frameNumUnclamped.y ) );

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
