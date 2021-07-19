/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsl"
#include "STL.hlsl"
#include "SIGMA_Shadow_SmoothTiles.resources.hlsl"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS
#include "SIGMA_Common.hlsl"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float2 s_Tile[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    s_Tile[ sharedId.y ][ sharedId.x ] = gIn_Tiles[ clamp( globalId, 0, gTilesSizeMinusOne ) ]; // TODO: do similar clamping in all Preload functions!
}

[numthreads( GROUP_X, GROUP_X, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    PRELOAD_INTO_SMEM;

    float blurry = 0;
    float sum = 0.0;
    float k = 1.01 / ( s_Tile[ threadId.y + 1 ][ threadId.x + 1 ].y + 0.01 );

    [unroll]
    for( int i = 0; i <= BORDER * 2; i++ )
    {
        [unroll]
        for( int j = 0; j <= BORDER * 2; j++ )
        {
            float x = length( float2( i, j ) - BORDER );
            float w = exp2( -k * x * x );

            blurry += s_Tile[ threadId.y + j ][ threadId.x + i ].x * w;
            sum += w;
        }
    }

    blurry /= sum;

    gOut_Tiles[ pixelPos ] = blurry;
}
