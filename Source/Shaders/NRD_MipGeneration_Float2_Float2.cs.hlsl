/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"
#include "NRD_MipGeneration_Float2_Float2.resources.hlsli"

NRD_DECLARE_CONSTANTS

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float2 s_TempA[ 17 ][ 17 ];
groupshared float2 s_TempB[ 17 ][ 17 ];

#define DO_REDUCTION \
{ \
    float4 w = float4( abs( float4( a00.y, a10.y, a01.y, a11.y ) / NRD_FP16_VIEWZ_SCALE ) < gInf ); \
    float sum = dot( w, 1.0 ); \
    float invSum = STL::Math::PositiveRcp( sum ); \
    a = a00 * w.x + a10 * w.y + a01 * w.z + a11 * w.w; \
    a *= invSum; \
    a.y = sum == 0.0 ? NRD_FP16_MAX : a.y; \
    b = b00 * w.x + b10 * w.y + b01 * w.z + b11 * w.w; \
    b *= invSum; \
    b.y = sum == 0.0 ? NRD_FP16_MAX : b.y; \
}

[numthreads( 16, 16, 1 )]
NRD_EXPORT void NRD_CS_MAIN( uint2 groupID : SV_GroupId, uint threadID : SV_GroupIndex )
{
    uint2 localID = uint2( threadID & 15, threadID >> 4 );
    uint2 globalID = groupID * 16 + localID;
    uint3 coord = uint3( globalID << 1, 0 );

    float2 a00 = gIn_A.Load( coord, int2( 0, 0 ) );
    float2 a10 = gIn_A.Load( coord, int2( 1, 0 ) );
    float2 a01 = gIn_A.Load( coord, int2( 0, 1 ) );
    float2 a11 = gIn_A.Load( coord, int2( 1, 1 ) );

    float2 b00 = gIn_B.Load( coord, int2( 0, 0 ) );
    float2 b10 = gIn_B.Load( coord, int2( 1, 0 ) );
    float2 b01 = gIn_B.Load( coord, int2( 0, 1 ) );
    float2 b11 = gIn_B.Load( coord, int2( 1, 1 ) );

    float2 a;
    float2 b;
    DO_REDUCTION;

    s_TempA[ localID.y ][ localID.x ] = a;
    s_TempB[ localID.y ][ localID.x ] = b;

    GroupMemoryBarrierWithGroupSync( );

    gOut_A_x2[ globalID ] = a;
    gOut_B_x2[ globalID ] = b;

    if( threadID < 64 )
    {
        localID = uint2( threadID & 7, threadID >> 3 );
        globalID = groupID * 8 + localID;

        uint2 id = localID << 1;
        uint2 id1 = id + 1;

        a00 = s_TempA[ id.y ][ id.x ];
        a10 = s_TempA[ id.y ][ id1.x ];
        a01 = s_TempA[ id1.y ][ id.x ];
        a11 = s_TempA[ id1.y ][ id1.x ];

        b00 = s_TempB[ id.y ][ id.x ];
        b10 = s_TempB[ id.y ][ id1.x ];
        b01 = s_TempB[ id1.y ] [id.x ];
        b11 = s_TempB[ id1.y ][ id1.x ];

        DO_REDUCTION;

        gOut_A_x4[ globalID ] = a;
        gOut_B_x4[ globalID ] = b;

        localID <<= 1;
        s_TempA[ localID.y ][ localID.x ] = a;
        s_TempB[ localID.y ][ localID.x ] = b;
    }

    GroupMemoryBarrierWithGroupSync( );

    if( threadID < 16 )
    {
        localID = uint2( threadID & 3, threadID >> 2 );
        globalID = groupID * 4 + localID;

        uint2 id = localID << 2;
        uint2 id1 = id + 2;

        a00 = s_TempA[ id.y ][ id.x ];
        a10 = s_TempA[ id.y ][ id1.x ];
        a01 = s_TempA[ id1.y ][ id.x ];
        a11 = s_TempA[ id1.y ][ id1.x ];

        b00 = s_TempB[ id.y ][ id.x ];
        b10 = s_TempB[ id.y ][ id1.x ];
        b01 = s_TempB[ id1.y ][ id.x ];
        b11 = s_TempB[ id1.y ][ id1.x ];

        DO_REDUCTION;

        gOut_A_x8[ globalID ] = a;
        gOut_B_x8[ globalID ] = b;

        localID <<= 2;
        s_TempA[ localID.y ][ localID.x ] = a;
        s_TempB[ localID.y ][ localID.x ] = b;
    }

    GroupMemoryBarrier( );

    if( threadID < 4 )
    {
        localID = uint2( threadID & 1, threadID >> 1 );
        globalID = groupID * 2 + localID;

        uint2 id = localID << 3;
        uint2 id1 = id + 4;

        a00 = s_TempA[ id.y ][ id.x ];
        a10 = s_TempA[ id.y ][ id1.x ];
        a01 = s_TempA[ id1.y ][ id.x ];
        a11 = s_TempA[ id1.y ][ id1.x ];

        b00 = s_TempB[ id.y ][ id.x ];
        b10 = s_TempB[ id.y ][ id1.x ];
        b01 = s_TempB[ id1.y ][ id.x ];
        b11 = s_TempB[ id1.y ][ id1.x ];

        DO_REDUCTION;

        gOut_A_x16[ globalID ] = a;
        gOut_B_x16[ globalID ] = b;
    }
}
