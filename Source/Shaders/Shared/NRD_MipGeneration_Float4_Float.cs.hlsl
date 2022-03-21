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
#include "Shared/NRD_MipGeneration_Float4_Float.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 s_TempA[ 17 ][ 17 ];
groupshared float s_TempZ[ 17 ][ 17 ];

#define DO_REDUCTION \
{ \
    float4 w = float4( abs( float4( z00, z10, z01, z11 ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE ); \
    float sum = dot( w, 1.0 ); \
    w *= rcp( sum + 1e-7 ); \
    a = a00 * w.x + a10 * w.y + a01 * w.z + a11 * w.w; \
    z = z00 * w.x + z10 * w.y + z01 * w.z + z11 * w.w; \
    z = sum == 0.0 ? NRD_FP16_MAX : z; \
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( uint2 groupID : SV_GroupId, uint threadID : SV_GroupIndex )
{
    uint2 DIMS = uint2( GROUP_X, GROUP_Y );
    uint2 localID = uint2( threadID % DIMS.x, threadID / DIMS.x );
    uint2 globalID = groupID * DIMS + localID;

    uint2 coord = globalID << 1;
    uint4 coords = min( coord.xyxy + uint4( 0, 0, 1, 1 ), gRectSize.xyxy - 1 );

    float4 a00 = gIn_A[ coords.xy ];
    float4 a10 = gIn_A[ coords.zy ];
    float4 a01 = gIn_A[ coords.xw ];
    float4 a11 = gIn_A[ coords.zw ];

    float z00 = gIn_ScaledViewZ[ coords.xy ];
    float z10 = gIn_ScaledViewZ[ coords.zy ];
    float z01 = gIn_ScaledViewZ[ coords.xw ];
    float z11 = gIn_ScaledViewZ[ coords.zw ];

    float4 a;
    float z;
    DO_REDUCTION;

    s_TempA[ localID.y ][ localID.x ] = a;
    s_TempZ[ localID.y ][ localID.x ] = z;

    GroupMemoryBarrierWithGroupSync( );

    gOut_A_x2[ globalID ] = a;
    gOut_ScaledViewZ_x2[ globalID ] = z;

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

        z00 = s_TempZ[ id.y ][ id.x ];
        z10 = s_TempZ[ id.y ][ id1.x ];
        z01 = s_TempZ[ id1.y ] [id.x ];
        z11 = s_TempZ[ id1.y ][ id1.x ];

        DO_REDUCTION;

        localID <<= 1;
        s_TempA[ localID.y ][ localID.x ] = a;
        s_TempZ[ localID.y ][ localID.x ] = z;

        gOut_A_x4[ globalID ] = a;
        gOut_ScaledViewZ_x4[ globalID ] = z;
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

        z00 = s_TempZ[ id.y ][ id.x ];
        z10 = s_TempZ[ id.y ][ id1.x ];
        z01 = s_TempZ[ id1.y ][ id.x ];
        z11 = s_TempZ[ id1.y ][ id1.x ];

        DO_REDUCTION;

        localID <<= 2;
        s_TempA[ localID.y ][ localID.x ] = a;
        s_TempZ[ localID.y ][ localID.x ] = z;

        gOut_A_x8[ globalID ] = a;
        gOut_ScaledViewZ_x8[ globalID ] = z;
    }
}
