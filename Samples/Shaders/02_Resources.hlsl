/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "STL.hlsl"

struct Attributes
{
    float4 Position : SV_Position;
    float4 Normal : TEXCOORD0; //.w = TexCoord.x
    float4 View : TEXCOORD1; //.w = TexCoord.y
    float4 Tangent : TEXCOORD2;
};

NRI_RESOURCE( Texture2D, DiffuseMap, t, 0, 1 );
NRI_RESOURCE( Texture2D, SpecularMap, t, 1, 1 );
NRI_RESOURCE( Texture2D, NormalMap, t, 2, 1 );
NRI_RESOURCE( Texture2D, EmissiveMap, t, 3, 1 );
NRI_RESOURCE( SamplerState, AnisotropicSampler, s, 0, 0 );

#define SUN_ANGULAR_SIZE radians( 0.533 )

#define PS_INPUT \
    float2 uv = float2( input.Normal.w, input.View.w ); \
    float3 V = normalize( input.View.xyz ); \
    float3 Nvertex = input.Normal.xyz; \
    Nvertex = normalize( Nvertex ); \
    float4 T = input.Tangent; \
    T.xyz = normalize( T.xyz ); \
    float4 diffuse = DiffuseMap.Sample( AnisotropicSampler, uv ); \
    float3 materialProps = SpecularMap.Sample( AnisotropicSampler, uv ).xyz; \
    float3 emissive = EmissiveMap.Sample( AnisotropicSampler, uv ).xyz; \
    float2 packedNormal = NormalMap.Sample( AnisotropicSampler, uv ).xy; \
    float3 N = STL::Geometry::TransformLocalNormal( packedNormal, T, Nvertex ); \
    float3 albedo, Rf0; \
    STL::BRDF::ConvertDiffuseMetalnessToAlbedoRf0( diffuse.xyz, materialProps.z, albedo, Rf0 ); \
    float roughness = materialProps.y; \
    const float3 sunDirection = normalize( float3( -0.8, -0.8, 1.0 ) ); \
    float3 L = STL::ImportanceSampling::CorrectDirectionToInfiniteSource( N, sunDirection, V, tan( SUN_ANGULAR_SIZE ) ); \
    const float3 Clight = 80000.0; \
    const float exposure = 0.00025

// LIGHTING

#define GLASS_HACK  0x1
#define FAKE_AMBIENT  0x2

float4 Shade( float4 albedo, float3 Rf0, float roughness, float3 emissive, float3 N, float3 L, float3 V, float3 Clight, uint flags )
{
    if ( flags & GLASS_HACK )
    {
        Rf0 = 0.04;
        roughness = 0.1;
    }

    // Direct lighting
    float3 Cdiff, Cspec;
    STL::BRDF::DirectLighting( N, L, V, Rf0, roughness, Cdiff, Cspec );

    if ( flags & FAKE_AMBIENT )
    {
        // Ambient
        const float3 ambient = float3( 1.0, 1.0, 0.8 ) * 0.1;
        Cdiff += ambient;

        // Environment
        const float3 fakeTopColor = float3( 0.8, 0.8, 1.0 );
        const float3 fakeBottomColor = float3( 0.2, 0.15, 0.15 );
        float3 R = reflect( -V, N );
        float3 environment = lerp( fakeBottomColor, fakeTopColor, R.z * 0.5 + 0.5 ) * 0.3;
        float NoV = saturate( dot( N, V ) );
        float3 Kenv = STL::BRDF::EnvironmentTerm( Rf0, NoV, roughness );
        Cspec += Kenv * environment;
    }

    // Output
    float3 Lsum = ( Cdiff * albedo.xyz + Cspec ) * Clight + emissive;

    float4 output;
    output.xyz = Lsum;
    output.w = albedo.w;

    float NoL = saturate( dot( N, L ) );
    float specIntensity = STL::Color::Luminance( Cspec * NoL );
    if ( flags & GLASS_HACK )
        output.w = saturate( 0.5 + specIntensity );

    return output;
}
