/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "STL.hlsl"

//===============================================================
// DENOISER PART
//===============================================================

#include "..\..\NRD\Shaders\NRD.hlsl"

//===============================================================
// RESOURCES
//===============================================================

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    float4x4 gWorldToView;
    float4x4 gViewToWorld;
    float4x4 gViewToClip;
    float4x4 gWorldToClipPrev;
    float4x4 gWorldToClip;
    float4 gCameraFrustum;
    float3 gSunDirection;
    float gExposure;
    float3 gWorldOrigin;
    float gMipBias;
    float3 gTrimmingParams;
    float gEmissionIntensity;
    float2 gScreenSize;
    float2 gInvScreenSize;
    float2 gJitter;
    float gAmbient;
    float gSeparator;
    float gNearZ;
    float gRoughnessOverride;
    float gMetalnessOverride;
    float gDiffHitDistScale;
    float gSpecHitDistScale;
    float gUnitsToMetersMultiplier;
    float gIndirectDiffuse;
    float gIndirectSpecular;
    float gTanSunAngularDiameter;
    float gPixelAngularDiameter;
    float gSunAngularDiameter;
    float gUseMipmapping;
    float gIsOrtho;
    float gDebug;
    float gDiffSecondBounce;
    float gTransparent;
    uint gOnScreen;
    uint gFrameIndex;
    uint gForcedMaterial;
    uint gPrimaryFullBrdf;
    uint gIndirectFullBrdf;
    uint gUseNormalMap;
    uint gWorldSpaceMotion;
    uint gUseBlueNoise;
    uint gDither;
};

NRI_RESOURCE( SamplerState, gLinearMipmapLinearSampler, s, 1, 0 );
NRI_RESOURCE( SamplerState, gNearestMipmapNearestSampler, s, 2, 0 );
NRI_RESOURCE( SamplerState, gLinearSampler, s, 3, 0 );

//=============================================================================================
// SETTINGS
//=============================================================================================

// Constants
#define SHOW_FINAL                          0
#define SHOW_AMBIENT_OCCLUSION              1
#define SHOW_SPECULAR_OCCLUSION             2
#define SHOW_SHADOW                         3
#define SHOW_BASE_COLOR                     4
#define SHOW_NORMAL                         5
#define SHOW_ROUGHNESS                      6
#define SHOW_METALNESS                      7
#define SHOW_WORLD_UNITS                    8
#define SHOW_BARY                           9
#define SHOW_MESH                           10
#define SHOW_MIP_PRIMARY                    11
#define SHOW_MIP_SPECULAR                   12

#define FP16_MAX                            65504.0
#define INF                                 1e5

#define MAT_GYPSUM                          1
#define MAT_COBALT                          2

#define SKY_MARK                            0.0

// Settings
#define USE_BEST_FIT_NORMALS                1
#define USE_SIMPLE_MIP_SELECTION            1
#define USE_SIMPLIFIED_BRDF_MODEL           0
#define USE_SQRT_ROUGHNESS                  1

#define GEOMETRY_OFFSET                     ( 0.0001 / gUnitsToMetersMultiplier ) // m
#define SHADOW_OFFSET                       ( 0.002 / gUnitsToMetersMultiplier ) // m
#define TAA_HISTORY_SHARPNESS               0.5 // [0; 1], 0.5 matches Catmull-Rom
#define TAA_MOTION_MAX_REUSE                ( 210.0 / 1920.0 )
#define TAA_FAST_MOTION_LENGTH              ( 30.0 / 1920.0 )
#define TAA_MIN_HISTORY_WEIGHT              0.1
#define TAA_MAX_HISTORY_WEIGHT              0.9
#define MAX_MIP_LEVEL                       11.0
#define EMISSION_TEXTURE_MIP_BIAS           5.0
#define HIT_DISTANCE_LINEAR_SCALE           0.1
#define MAX_MONTE_CARLO_VIRTUAL_SAMPLE_NUM  16 // number of steps to replace useless rays "being cast inside the surface" with valid rays (1 - better perf)

//=============================================================================================
// SHARED
//=============================================================================================

void ModifyMaterial( inout float3 baseColor, inout float metalness, inout float roughness )
{
    if ( gForcedMaterial == MAT_GYPSUM )
    {
        roughness = 1.0;
        baseColor = 0.5;
        metalness = 0.0;
    }
    else if ( gForcedMaterial == MAT_COBALT )
    {
        roughness = pow( saturate( baseColor.x * baseColor.y * baseColor.z ), 0.33333 );
        baseColor = float3( 0.672411, 0.637331, 0.585456 );
        metalness = 1.0;
    }

    metalness = gMetalnessOverride == 0.0 ? metalness : gMetalnessOverride;
    roughness = gRoughnessOverride == 0.0 ? roughness : gRoughnessOverride;
}

float4 PackNormalAndRoughness( float3 N, float roughness )
{
    #if( USE_BEST_FIT_NORMALS == 1 )
        float m = max( abs( N.x ), max( abs( N.y ), abs( N.z ) ) );
        N *= STL::Math::PositiveRcp( m );
    #endif

    #if( USE_SQRT_ROUGHNESS == 1 )
        roughness = STL::Math::Sqrt01( roughness );
    #endif

    return float4( N * 0.5 + 0.5, roughness );
}

float4 UnpackNormalAndRoughness( float4 p )
{
    p.xyz = p.xyz * 2.0 - 1.0;
    p.xyz *= STL::Math::Rsqrt( STL::Math::LengthSquared( p.xyz ) );

    #if( USE_SQRT_ROUGHNESS == 1 )
        p.w *= p.w;
    #endif

    return p;
}

float GetTrimmingFactor( float roughness )
{
    float trimmingFactor = gTrimmingParams.x * STL::Math::SmoothStep( gTrimmingParams.y, gTrimmingParams.z, roughness );

    return trimmingFactor;
}

//=============================================================================================
// VERY SIMPLE SKY MODEL
//=============================================================================================

float3 GetSkyColor( float3 v, float3 vSun )
{
    float atmosphere = sqrt( 1.0 - saturate( v.z ) );

    float scatter = pow( saturate( vSun.z ), 1.0 / 15.0 );
    scatter = 1.0 - clamp( scatter, 0.8, 1.0 );

    float3 scatterColor = lerp( float3( 1.0, 1.0, 1.0 ), float3( 1.0, 0.3, 0.0 ) * 1.5, scatter );
    float3 skyColor = lerp( float3( 0.2, 0.4, 0.8 ), float3( scatterColor ), atmosphere / 1.3 );
    skyColor *= saturate( 1.0 + vSun.z );

    return STL::Color::GammaToLinear( saturate( skyColor ) );

}

float3 GetSunColor( float3 v, float3 vSun, float angularDiameter )
{
    float b = dot( v, vSun );
    float d = length( v - vSun * b );

    float glow = saturate( 1.015 - d );
    glow *= b * 0.5 + 0.5;
    glow *= 0.6;

    float angularRadius = angularDiameter * 0.5;
    float a = sqrt( 2.0 ) * sqrt( saturate( 1.0 - b ) ); // acos approx
    float sun = 1.0 - smoothstep( angularRadius * 0.9, angularRadius * 1.66, a );
    sun *= 1.0 - pow( saturate( 1.0 - v.z ), 4.85 );
    sun *= smoothstep( 0.0, 0.1, vSun.z );
    sun += glow;

    float3 sunColor = lerp( float3( 1.0, 0.6, 0.3 ), float3( 1.0, 0.9, 0.7 ), sqrt( saturate( vSun.z ) ) );
    sunColor *= saturate( sun );

    float fade = saturate( 1.0 + vSun.z );
    sunColor *= fade * fade;

    return STL::Color::GammaToLinear( sunColor );
}

float3 GetSkyIntensity( float3 v, float3 vSun, float angularDiameter = 0.5 )
{
    float sunIntensity = 80000.0;
    float skyIntensity = 10000.0;

    return sunIntensity * GetSunColor( v, vSun, angularDiameter ) + skyIntensity * GetSkyColor( v, vSun );
}

