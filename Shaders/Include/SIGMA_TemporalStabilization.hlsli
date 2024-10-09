/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float2 s_Penumbra_ViewZ[ BUFFER_Y ][ BUFFER_X ];
groupshared SIGMA_TYPE s_Shadow_Translucency[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSizeMinusOne );

    float2 data;
    data.x = gIn_Penumbra[ globalPos ];
    data.y = UnpackViewZ( gIn_ViewZ[ WithRectOrigin( globalPos ) ] );

    s_Penumbra_ViewZ[ sharedPos.y ][ sharedPos.x ] = data;

    SIGMA_TYPE s = gIn_Shadow_Translucency[ globalPos ];
    s = SIGMA_BackEnd_UnpackShadow( s );

    s_Shadow_Translucency[ sharedPos.y ][ sharedPos.x ] = s;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    // Preload
    float isSky = gIn_Tiles[ pixelPos >> 4 ].y;
    PRELOAD_INTO_SMEM_WITH_TILE_CHECK;

    // Tile-based early out
    if( isSky != 0.0 || any( pixelPos > gRectSizeMinusOne ) )
        return;

    // Center data
    int2 smemPos = threadPos + BORDER;
    float2 centerData = s_Penumbra_ViewZ[ smemPos.y ][ smemPos.x ];
    float centerPenumbra = centerData.x;
    float centerSignNoL = float( centerData.x != 0.0 );
    float viewZ = centerData.y;

    // Early out
    if( viewZ > gDenoisingRange )
        return;

    // Tile-based early out ( potentially )
    float2 pixelUv = float2( pixelPos + 0.5 ) * gRectSizeInv;
    float tileValue = TextureCubic( gIn_Tiles, pixelUv * gResolutionScale );
    bool isHardShadow = ( ( tileValue == 0.0 && NRD_USE_TILE_CHECK ) || centerPenumbra == 0.0 ) && SIGMA_USE_EARLY_OUT_IN_TS;

    if( isHardShadow && SIGMA_SHOW == 0 )
    {
        gOut_Shadow_Translucency[ pixelPos ] = PackShadow( s_Shadow_Translucency[ smemPos.y ][ smemPos.x ] );

        return;
    }

    // Local variance
    float sum = 0.0;
    SIGMA_TYPE m1 = 0;
    SIGMA_TYPE m2 = 0;
    SIGMA_TYPE input = 0;

    float viewZnearest = viewZ;
    int2 offseti = int2( BORDER, BORDER );

    [unroll]
    for( j = 0; j <= BORDER * 2; j++ )
    {
        [unroll]
        for( i = 0; i <= BORDER * 2; i++ )
        {
            int2 pos = threadPos + int2( i, j );
            float2 data = s_Penumbra_ViewZ[ pos.y ][ pos.x ];

            SIGMA_TYPE s = s_Shadow_Translucency[ pos.y ][ pos.x ];
            float penum = data.x;
            float z = data.y;
            float signNoL = float( penum != 0.0 );

            float w = 1.0;
            if( i == BORDER && j == BORDER )
                input = s;
            else
            {
                w = abs( z - viewZ ) / max( z, viewZ ) < 0.02; // TODO: slope scale?
                w *= IsLit( penum ) == IsLit( centerPenumbra ); // no-harm on a flat surface due to wide spatials, needed to prevent bleeding from one surface to another
                w *= float( z < gDenoisingRange ); // ignore sky
                w *= float( centerSignNoL == signNoL ); // ignore samples with different NoL signs

                if( z < viewZnearest )
                {
                    viewZnearest = z;
                    offseti = int2( i, j );
                }
            }

            m1 += s * w;
            m2 += s * s * w;
            sum += w;
        }
    }

    float invSum = Math::PositiveRcp( sum );
    m1 *= invSum;
    m2 *= invSum;

    SIGMA_TYPE sigma = GetStdDev( m1, m2 );

    // Compute previous pixel position
    float3 Xv = Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZnearest, gOrthoMode );
    float3 X = Geometry::RotateVectorInverse( gWorldToView, Xv );
    float3 mv = gIn_Mv[ WithRectOrigin( pixelPos ) + offseti - BORDER ] * gMvScale.xyz;
    float2 pixelUvPrev = pixelUv + mv.xy;

    if( gMvScale.w != 0.0 )
        pixelUvPrev = Geometry::GetScreenUv( gWorldToClipPrev, X + mv );

    // Sample history
    SIGMA_TYPE history;
    BicubicFilterNoCorners( saturate( pixelUvPrev ) * gRectSizePrev, gResourceSizeInvPrev, SIGMA_USE_CATROM, gIn_History, history );

    history = saturate( history );
    history = SIGMA_BackEnd_UnpackShadow( history );

    // Clamp history
    SIGMA_TYPE inputMin = m1 - sigma * SIGMA_TS_SIGMA_SCALE;
    SIGMA_TYPE inputMax = m1 + sigma * SIGMA_TS_SIGMA_SCALE;
    SIGMA_TYPE historyClamped = clamp( history, inputMin, inputMax );

    // Antilag
    float antilag = abs( historyClamped.x - history.x );
    antilag = Math::Pow01( antilag, SIGMA_TS_ANTILAG_POWER );
    antilag = 1.0 - antilag;

    // Dark magic ( helps to smooth out "penumbra to 1" regions )
    historyClamped = lerp( historyClamped, history, 0.5 );

    // History weight
    float historyWeight = SIGMA_TS_MAX_HISTORY_WEIGHT;
    historyWeight *= IsInScreenNearest( pixelUvPrev );
    historyWeight *= antilag;
    historyWeight *= gStabilizationStrength;

    // Combine with current frame
    SIGMA_TYPE result = lerp( input, historyClamped, historyWeight );

    // Debug
    #if( SIGMA_SHOW == 1 )
        tileValue = gIn_Tiles[ pixelPos >> 4 ].x;
        tileValue = float( tileValue != 0.0 ); // optional, just to show fully discarded tiles

        #ifdef SIGMA_TRANSLUCENT
            result = lerp( float4( 0, 0, 1, 0 ), result, tileValue );
        #else
            result = tileValue;
        #endif

        // Show grid ( works badly with TAA )
        result *= all( ( pixelPos & 15 ) != 0 );
    #elif( SIGMA_SHOW == 2 )
        // .x - is used in antilag computations!
        #ifdef SIGMA_TRANSLUCENT
            historyWeight *= float( !isHardShadow );
            result.yzw = historyWeight;
        #endif
    #elif( SIGMA_SHOW == 3 )
        float unprojectZ = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
        float penumbraInPixels = centerPenumbra / unprojectZ;
        result = saturate( penumbraInPixels / 10.0 );
    #endif

    // Output
    gOut_Shadow_Translucency[ pixelPos ] = PackShadow( result );
}
