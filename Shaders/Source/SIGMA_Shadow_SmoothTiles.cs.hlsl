/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "STL.hlsli"
#include "../Include/NRD.hlsli"

#include "../Include/SIGMA/SIGMA_Config.hlsli"
#include "../Resources/SIGMA_Shadow_SmoothTiles.resources.hlsli"

#include "../Include/Common.hlsli"
#include "../Include/SIGMA/SIGMA_Common.hlsli"

groupshared float2 s_Tile[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gTilesSizeMinusOne );

    s_Tile[ sharedPos.y ][ sharedPos.x ] = gIn_Tiles[ globalPos ];
}

[numthreads( GROUP_X, GROUP_X, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    PRELOAD_INTO_SMEM;

    float blurry = 0;
    float sum = 0.0;
    float k = 1.01 / ( s_Tile[ threadPos.y + 1 ][ threadPos.x + 1 ].y + 0.01 );

    [unroll]
    for( int i = 0; i <= BORDER * 2; i++ )
    {
        [unroll]
        for( int j = 0; j <= BORDER * 2; j++ )
        {
            float x = length( float2( i, j ) - BORDER );
            float w = exp2( -k * x * x );

            blurry += s_Tile[ threadPos.y + j ][ threadPos.x + i ].x * w;
            sum += w;
        }
    }

    blurry /= sum;

    gOut_Tiles[ pixelPos ] = blurry;
}
