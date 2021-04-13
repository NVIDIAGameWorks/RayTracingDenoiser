/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "STL.hlsl"

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
    float4 gDiffHitDistParams;
    float4 gSpecHitDistParams;
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
    float2 gRectSize;
    float2 gInvRectSize;
    float2 gRectSizePrev;
    float gNearZ;
    float gAmbient;
    float gAmbientInComposition;
    float gSeparator;
    float gRoughnessOverride;
    float gMetalnessOverride;
    float gUnitsToMetersMultiplier;
    float gIndirectDiffuse;
    float gIndirectSpecular;
    float gTanSunAngularRadius;
    float gPixelAngularDiameter;
    float gSunAngularDiameter;
    float gUseMipmapping;
    float gIsOrtho;
    float gDebug;
    float gDiffSecondBounce;
    float gTransparent;
    uint gDenoiserType;
    uint gDisableShadowsAndEnableImportanceSampling;
    uint gOnScreen;
    uint gFrameIndex;
    uint gForcedMaterial;
    uint gPrimaryFullBrdf;
    uint gIndirectFullBrdf;
    uint gUseNormalMap;
    uint gWorldSpaceMotion;
};

NRI_RESOURCE( SamplerState, gLinearMipmapLinearSampler, s, 1, 0 );
NRI_RESOURCE( SamplerState, gNearestMipmapNearestSampler, s, 2, 0 );
NRI_RESOURCE( SamplerState, gLinearSampler, s, 3, 0 );

//=============================================================================================
// DENOISER PART
//=============================================================================================

#include "..\..\NRD\Shaders\NRD.hlsl"

//=============================================================================================
// SETTINGS
//=============================================================================================

// Constants
#define REBLUR                              0
#define RELAX                               1

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
#define USE_SQRT_ROUGHNESS                  0
#define USE_OCT_PACKED_NORMALS              0
#define USE_BEST_FIT_NORMALS                1

#define USE_SIMPLE_MIP_SELECTION            1
#define USE_SIMPLIFIED_BRDF_MODEL           0
#define USE_IMPORTANCE_SAMPLING             2 // 0 - off, 1 - ignore rays with 0 throughput, 2 - plus local lights importance sampling
#define USE_BLUE_NOISE                      1

#define TAA_HISTORY_SHARPNESS               0.5 // [0; 1], 0.5 matches Catmull-Rom
#define TAA_MAX_HISTORY_WEIGHT              0.95
#define TAA_MIN_HISTORY_WEIGHT              0.1
#define TAA_MOTION_MAX_REUSE                0.1
#define MAX_MIP_LEVEL                       11.0
#define EMISSION_TEXTURE_MIP_BIAS           5.0
#define ZERO_TROUGHPUT_SAMPLE_NUM           16
#define IMPORTANCE_SAMPLE_NUM               16
#define GLASS_TINT                          float3( 0.9, 0.9, 1.0 )

//=============================================================================================
// MISC
//=============================================================================================

float4 PackNormalAndRoughness( float3 N, float linearRoughness )
{
    float4 p;

    #if( USE_SQRT_ROUGHNESS == 1 )
        linearRoughness = STL::Math::Sqrt01( linearRoughness );
    #endif

    #if( USE_OCT_PACKED_NORMALS == 1 )
        p.xy = STL::Packing::EncodeUnitVector( N, false );
        p.z = linearRoughness;
        p.w = 0;
    #else
        p.xyz = N;
        #if( USE_BEST_FIT_NORMALS == 1 )
            float m = max( abs( N.x ), max( abs( N.y ), abs( N.z ) ) );
            p.xyz *= STL::Math::PositiveRcp( m );
        #endif

        p.xyz = p.xyz * 0.5 + 0.5;
        p.w = linearRoughness;
    #endif

    return p;
}

float4 UnpackNormalAndRoughness( float4 p )
{
    float4 r;
    #if( USE_OCT_PACKED_NORMALS == 1 )
        p.xy = p.xy * 2.0 - 1.0;
        r.xyz = STL::Packing::DecodeUnitVector( p.xy, true, false );
        r.w = p.z;
    #else
        p.xyz = p.xyz * 2.0 - 1.0;
        r.xyz = p.xyz;
        r.w = p.w;
    #endif

    r.xyz = normalize( r.xyz );

    #if( USE_SQRT_ROUGHNESS == 1 )
        r.w *= r.w;
    #endif

    return r;
}

float3 ApplyPostLightingComposition( uint2 pixelPos, float3 Lsum, Texture2D<float4> gIn_TransparentLighting )
{
    // Transparent layer
    float4 transparentLayer = gIn_TransparentLighting[ pixelPos ] * gTransparent;
    Lsum = Lsum * ( 1.0 - transparentLayer.w ) * ( transparentLayer.w != 0.0 ? GLASS_TINT : 1.0 ) + transparentLayer.xyz;

    // Tonemap
    if( gOnScreen == SHOW_FINAL )
        Lsum = STL::Color::HdrToLinear_Uncharted( Lsum );

    // Conversion
    if( gOnScreen == SHOW_FINAL || gOnScreen == SHOW_BASE_COLOR )
        Lsum = STL::Color::LinearToSrgb( Lsum );

    return Lsum;
}

float4 BicubicFilterNoCorners( Texture2D<float4> tex, SamplerState samp, float2 samplePos, float2 invTextureSize, compiletime const float sharpness )
{
    float2 centerPos = floor( samplePos - 0.5 ) + 0.5;
    float2 f = samplePos - centerPos;
    float2 f2 = f * f;
    float2 f3 = f * f2;
    float2 w0 = -sharpness * f3 + 2.0 * sharpness * f2 - sharpness * f;
    float2 w1 = ( 2.0 - sharpness ) * f3 - ( 3.0 - sharpness ) * f2 + 1.0;
    float2 w2 = -( 2.0 - sharpness ) * f3 + ( 3.0 - 2.0 * sharpness ) * f2 + sharpness * f;
    float2 w3 = sharpness * f3 - sharpness * f2;
    float2 wl2 = w1 + w2;
    float2 tc2 = invTextureSize * ( centerPos + w2 * STL::Math::PositiveRcp( wl2 ) );
    float2 tc0 = invTextureSize * ( centerPos - 1.0 );
    float2 tc3 = invTextureSize * ( centerPos + 2.0 );

    float w = wl2.x * w0.y;
    float4 color = tex.SampleLevel( samp, float2( tc2.x, tc0.y ), 0 ) * w;
    float sum = w;

    w = w0.x  * wl2.y;
    color += tex.SampleLevel( samp, float2( tc0.x, tc2.y ), 0 ) * w;
    sum += w;

    w = wl2.x * wl2.y;
    color += tex.SampleLevel( samp, float2( tc2.x, tc2.y ), 0 ) * w;
    sum += w;

    w = w3.x  * wl2.y;
    color += tex.SampleLevel( samp, float2( tc3.x, tc2.y ), 0 ) * w;
    sum += w;

    w = wl2.x * w3.y;
    color += tex.SampleLevel( samp, float2( tc2.x, tc3.y ), 0 ) * w;
    sum += w;

    color *= STL::Math::PositiveRcp( sum );

    return color;
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
