/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    float4x4 gViewToClip;
    float4 gFrustum;
    float2 gInvScreenSize;
    float2 gScreenSize;
    float gMetersToUnits;
    float gIsOrtho;
    float gUnproject;
    float gDebug;
    float gInf;
    float gReference;
    uint gFrameIndex;
    uint gWorldSpaceMotion;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_A, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 1, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_A_x2, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float>, gOut_ScaledViewZ_x2, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_A_x4, u, 2, 0 );
NRI_RESOURCE( RWTexture2D<float>, gOut_ScaledViewZ_x4, u, 3, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_A_x8, u, 4, 0 );
NRI_RESOURCE( RWTexture2D<float>, gOut_ScaledViewZ_x8, u, 5, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_A_x16, u, 6, 0 );
NRI_RESOURCE( RWTexture2D<float>, gOut_ScaledViewZ_x16, u, 7, 0 );

groupshared float4 s_TempA[ 17 ][ 17 ];
groupshared float s_TempB[ 17 ][ 17 ];

// TODO: Since specular values are compressed it would be better to take compression into account, otherwise there is a risk to add high energy ( from
// high roughness ) to highly compressed value ( for low roughness )... which will be later decompressed ( and will get even more energy on top )
#define DO_REDUCTION \
{ \
    float4 w = float4( abs( float4( b00, b10, b01, b11 ) / NRD_FP16_VIEWZ_SCALE ) < gInf ); \
    a = a00 * w.x + a10 * w.y + a01 * w.z + a11 * w.w; \
    b = b00 * w.x + b10 * w.y + b01 * w.z + b11 * w.w; \
    float sum = dot( w, 1.0 ); \
    float invSum = STL::Math::PositiveRcp( sum ); \
    a *= invSum; \
    b *= invSum; \
    b = sum == 0.0 ? NRD_FP16_MAX : b; \
}

[numthreads( 16, 16, 1 )]
void main( uint2 groupID : SV_GroupID, uint threadID : SV_GroupIndex )
{
    uint2 localID = uint2( threadID & 15, threadID >> 4 );
    uint2 globalID = groupID * 16 + localID;
    uint3 coord = uint3( globalID << 1, 0 );

    float4 a00 = gIn_A.Load( coord, int2( 0, 0 ) );
    float4 a10 = gIn_A.Load( coord, int2( 1, 0 ) );
    float4 a01 = gIn_A.Load( coord, int2( 0, 1 ) );
    float4 a11 = gIn_A.Load( coord, int2( 1, 1 ) );

    float b00 = gIn_ScaledViewZ.Load( coord, int2( 0, 0 ) );
    float b10 = gIn_ScaledViewZ.Load( coord, int2( 1, 0 ) );
    float b01 = gIn_ScaledViewZ.Load( coord, int2( 0, 1 ) );
    float b11 = gIn_ScaledViewZ.Load( coord, int2( 1, 1 ) );

    float4 a;
    float b;
    DO_REDUCTION;

    s_TempA[ localID.y ][ localID.x ] = a;
    s_TempB[ localID.y ][ localID.x ] = b;

    GroupMemoryBarrierWithGroupSync( );

    gOut_A_x2[ globalID ] = a;
    gOut_ScaledViewZ_x2[ globalID ] = b;

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
        gOut_ScaledViewZ_x4[ globalID ] = b;

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
        gOut_ScaledViewZ_x8[ globalID ] = b;

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
        gOut_ScaledViewZ_x16[ globalID ] = b;
    }
}
