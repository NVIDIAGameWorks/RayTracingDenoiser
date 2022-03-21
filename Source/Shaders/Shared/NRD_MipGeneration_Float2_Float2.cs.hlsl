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
#include "Shared/NRD_MipGeneration_Float2_Float2.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float2 s_TempA[ 17 ][ 17 ];
groupshared float2 s_TempB[ 17 ][ 17 ];

#define DO_REDUCTION \
{ \
    float4 w = float4( abs( float4( a00.y, a10.y, a01.y, a11.y ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE ); \
    float sum = dot( w, 1.0 ); \
    w *= rcp( sum + 1e-7 ); \
    a = a00 * w.x + a10 * w.y + a01 * w.z + a11 * w.w; \
    a.y = sum == 0.0 ? NRD_FP16_MAX : a.y; \
    b = b00 * w.x + b10 * w.y + b01 * w.z + b11 * w.w; \
    b.y = sum == 0.0 ? NRD_FP16_MAX : b.y; \
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( uint2 groupID : SV_GroupId, uint threadID : SV_GroupIndex )
{
    uint2 DIMS = uint2( GROUP_X, GROUP_Y );
    uint2 localID = uint2( threadID % DIMS.x, threadID / DIMS.x );
    uint2 globalID = groupID * DIMS + localID;

    uint2 coord = globalID << 1;
    uint4 coords = min( coord.xyxy + uint4( 0, 0, 1, 1 ), gRectSize.xyxy - 1 );

    float2 a00 = gIn_A[ coords.xy ];
    float2 a10 = gIn_A[ coords.zy ];
    float2 a01 = gIn_A[ coords.xw ];
    float2 a11 = gIn_A[ coords.zw ];

    float2 b00 = gIn_B[ coords.xy ];
    float2 b10 = gIn_B[ coords.zy ];
    float2 b01 = gIn_B[ coords.xw ];
    float2 b11 = gIn_B[ coords.zw ];

    float2 a;
    float2 b;
    DO_REDUCTION;

    s_TempA[ localID.y ][ localID.x ] = a;
    s_TempB[ localID.y ][ localID.x ] = b;

    GroupMemoryBarrierWithGroupSync( );

    gOut_A_x2[ globalID ] = a;
    gOut_B_x2[ globalID ] = b;

    DIMS >>= 1;
    if( threadID < DIMS.x * DIMS.y )
    {
        localID = uint2( threadID % DIMS.x, threadID / DIMS.x );
        globalID = groupID * DIMS + localID;

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

    DIMS >>= 1;
    if( threadID < DIMS.x * DIMS.y )
    {
        localID = uint2( threadID % DIMS.x, threadID / DIMS.x );
        globalID = groupID * DIMS + localID;

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
}
