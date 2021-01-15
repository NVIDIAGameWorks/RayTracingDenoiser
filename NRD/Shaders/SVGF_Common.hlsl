/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "STL.hlsl"
#include "NRD.hlsl"

NRI_RESOURCE( SamplerState, gNearestClamp, s, 0, 0 );
NRI_RESOURCE( SamplerState, gNearestMirror, s, 1, 0 );
NRI_RESOURCE( SamplerState, gLinearClamp, s, 2, 0 );
NRI_RESOURCE( SamplerState, gLinearMirror, s, 3, 0 );

// Debug

#define SHOW_ACCUM_SPEED                        0

// Booleans

#define BLACK_OUT_INF_PIXELS                    0

// Settings

#define FP16_MAX                                65504.0
#define FP16_VIEWZ_SCALE                        0.0125
#define LOBE_STRICTNESS_FACTOR                  0.333 // 0.3 - 1.0 (to avoid overblurring)
#define MAX_FRAME_NUM_WITH_HISTORY_FIX          4
#define MAX_FRAME_NUM_WITH_VARIANCE_BOOST       8
#define VARIANCE_ESTIMATION_RADIUS              3 // 1-3
#define ATROUS_RADIUS                           2 // 1-2
#define NORMAL_BANDING_FIX                      STL::Math::DegToRad( 2.5 ) // mitigate banding introduced by normals stored in RGB8 format
#define NORMAL_ROUGHNESS_BITS			        11, 11, 10

// CTA size

#define BORDER                                  1 // max radius of blur used for shared memory
#define GROUP_X                                 16
#define GROUP_Y                                 16
#define BUFFER_X                                ( GROUP_X + BORDER * 2 )
#define BUFFER_Y                                ( GROUP_Y + BORDER * 2 )
#define RENAMED_GROUP_Y                         ( ( GROUP_X * GROUP_Y ) / BUFFER_X )

// Misc

uint2 PackViewZNormalRoughness( float viewZ, float3 N, float roughness )
{
    float3 t;
    t.xy = STL::Packing::EncodeUnitVector( N );
    t.z = roughness;

    uint2 p;
    p.x = asuint( viewZ );
    p.y = STL::Packing::RgbaToUint( t.xyzz, NORMAL_ROUGHNESS_BITS );

    return p;
}

float4 UnpackNormalRoughness( uint p )
{
    float3 t = STL::Packing::UintToRgba( p, NORMAL_ROUGHNESS_BITS ).xyz;
    float3 N = STL::Packing::DecodeUnitVector( t.xy );

    return float4( N, t.z );
}

float PackHistoryLength( float p )
{
    return saturate( p / 255.0 );
}

float UnpackHistoryLength( float p )
{
    return p * 255.0;
}

float GetNormalWeight( float2 params0, float3 n0, float3 n )
{
    float cosa = saturate( dot( n0, n ) );
    float a = STL::Math::AcosApprox( cosa );
    a = 1.0 - STL::Math::SmoothStep( 0.0, params0.x, a );

    return saturate( a * params0.y + 1.0 - params0.y );
}

float2 ComputeWeight( float zc, float z, float zWeight, float3 nc, float3 n, float2 nWeightParams, float2 lc, float2 l, float2 lWeight, float fade )
{
    // Normal weight (taken from old REBLUR)
    float wn = GetNormalWeight( nWeightParams, nc, n );
    wn = lerp( wn, 1.0, fade );

    // ViewZ weight
    float wz = abs( zc - z ) * STL::Math::PositiveRcp( min( abs( zc ), abs( z ) ) );

    // Color weight
    float2 wl = abs( lc - l );
    wl = lerp( wl, 0.0, fade );

    // Total weight
    float2 w = exp( -wl * lWeight - wz * zWeight ) * wn;

    return w;
}
