/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"
#include "NRD_MipGeneration_Float4_Float4_Float.resources.hlsli"

NRD_DECLARE_CONSTANTS

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 s_TempA[ 17 ][ 17 ];
groupshared float4 s_TempB[ 17 ][ 17 ];
groupshared float s_TempZ[ 17 ][ 17 ];

#define DO_REDUCTION \
{ \
    float4 w = float4( abs( float4( z00, z10, z01, z11 ) / NRD_FP16_VIEWZ_SCALE ) < gInf ); \
    float sum = dot( w, 1.0 ); \
    float invSum = STL::Math::PositiveRcp( sum ); \
    a = a00 * w.x + a10 * w.y + a01 * w.z + a11 * w.w; \
    a *= invSum; \
    b = b00 * w.x + b10 * w.y + b01 * w.z + b11 * w.w; \
    b *= invSum; \
    z = z00 * w.x + z10 * w.y + z01 * w.z + z11 * w.w; \
    z *= invSum; \
    z = sum == 0.0 ? NRD_FP16_MAX : z; \
}

[numthreads( 16, 16, 1 )]
NRD_EXPORT void NRD_CS_MAIN( uint2 groupID : SV_GroupId, uint threadID : SV_GroupIndex )
{
    uint2 localID = uint2( threadID & 15, threadID >> 4 );
    uint2 globalID = groupID * 16 + localID;

    uint2 coord = globalID << 1;
    uint4 coords = min( coord.xyxy + uint4( 0, 0, 1, 1 ), gRectSize.xyxy - 1 );

    float4 a00 = gIn_A[ coords.xy ];
    float4 a10 = gIn_A[ coords.zy ];
    float4 a01 = gIn_A[ coords.xw ];
    float4 a11 = gIn_A[ coords.zw ];

    float4 b00 = gIn_B[ coords.xy ];
    float4 b10 = gIn_B[ coords.zy ];
    float4 b01 = gIn_B[ coords.xw ];
    float4 b11 = gIn_B[ coords.zw ];

    float z00 = gIn_ScaledViewZ[ coords.xy ];
    float z10 = gIn_ScaledViewZ[ coords.zy ];
    float z01 = gIn_ScaledViewZ[ coords.xw ];
    float z11 = gIn_ScaledViewZ[ coords.zw ];

    float4 a;
    float4 b;
    float z;
    DO_REDUCTION;

    s_TempA[ localID.y ][ localID.x ] = a;
    s_TempB[ localID.y ][ localID.x ] = b;
    s_TempZ[ localID.y ][ localID.x ] = z;

    GroupMemoryBarrierWithGroupSync( );

    gOut_A_x2[ globalID ] = a;
    gOut_B_x2[ globalID ] = b;
    gOut_ScaledViewZ_x2[ globalID ] = z;

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

        z00 = s_TempZ[ id.y ][ id.x ];
        z10 = s_TempZ[ id.y ][ id1.x ];
        z01 = s_TempZ[ id1.y ] [id.x ];
        z11 = s_TempZ[ id1.y ][ id1.x ];

        DO_REDUCTION;

        localID <<= 1;
        s_TempA[ localID.y ][ localID.x ] = a;
        s_TempB[ localID.y ][ localID.x ] = b;
        s_TempZ[ localID.y ][ localID.x ] = z;

        gOut_A_x4[ globalID ] = a;
        gOut_B_x4[ globalID ] = b;
        gOut_ScaledViewZ_x4[ globalID ] = z;
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

        z00 = s_TempZ[ id.y ][ id.x ];
        z10 = s_TempZ[ id.y ][ id1.x ];
        z01 = s_TempZ[ id1.y ][ id.x ];
        z11 = s_TempZ[ id1.y ][ id1.x ];

        DO_REDUCTION;

        localID <<= 2;
        s_TempA[ localID.y ][ localID.x ] = a;
        s_TempB[ localID.y ][ localID.x ] = b;
        s_TempZ[ localID.y ][ localID.x ] = z;

        gOut_A_x8[ globalID ] = a;
        gOut_B_x8[ globalID ] = b;
        gOut_ScaledViewZ_x8[ globalID ] = z;
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

        z00 = s_TempZ[ id.y ][ id.x ];
        z10 = s_TempZ[ id.y ][ id1.x ];
        z01 = s_TempZ[ id1.y ][ id.x ];
        z11 = s_TempZ[ id1.y ][ id1.x ];

        DO_REDUCTION;

        gOut_A_x16[ globalID ] = a;
        gOut_B_x16[ globalID ] = b;
        gOut_ScaledViewZ_x16[ globalID ] = z;
    }
}
