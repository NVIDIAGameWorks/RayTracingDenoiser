/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "09_Resources.hlsl"

NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 0, 1 );
NRI_RESOURCE( Texture2D<float3>, gIn_DirectLighting, t, 1, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 2, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_BaseColor_Metalness, t, 3, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_Shadow, t, 4, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_DiffHit, t, 5, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_SpecHit, t, 6, 1 );
NRI_RESOURCE( Texture2D<float2>, gIn_IntegratedBRDF, t, 7, 1 );

NRI_RESOURCE( RWTexture2D<float4>, gOut_ComposedImage, u, 8, 1 );

[numthreads( 16, 16, 1)]
void main( uint2 pixelPos : SV_DISPATCHTHREADID)
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;
    float2 sampleUv = pixelUv + gJitter;

    // Normal
    float4 normalAndRoughness = gIn_Normal_Roughness[ pixelPos ];
    float isGround = float( dot( normalAndRoughness.xyz, normalAndRoughness.xyz ) != SKY_MARK );
    normalAndRoughness = UnpackNormalAndRoughness( normalAndRoughness );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Material
    float4 baseColorMetalness = gIn_BaseColor_Metalness[ pixelPos ];

    float3 albedo, Rf0;
    STL::BRDF::ConvertDiffuseMetalnessToAlbedoRf0( baseColorMetalness.xyz, baseColorMetalness.w, albedo, Rf0 );

    // To be used in indirect (!) lighting math
    albedo *= STL::ImportanceSampling::Cosine::GetInversePDF( ) / STL::Math::Pi( 1.0 );

    // Denoised data
    float4 shadowData = NRD_BackEnd_UnpackShadow( gIn_Shadow[ pixelPos ] );
    float3 shadow = lerp( shadowData.yzw, 1.0, shadowData.x );

    float4 indirectSpec = gIn_SpecHit[ pixelPos ];
    if( !gSvgf )
        indirectSpec = NRD_BackEnd_UnpackSpecular( indirectSpec, roughness );
    indirectSpec.xyz *= gIndirectSpecular;

    float4 indirectDiff = gIn_DiffHit[ pixelPos ];
    indirectDiff = NRD_BackEnd_UnpackDiffuse( indirectDiff );
    indirectDiff.xyz *= gIndirectDiffuse;

    // Good denoisers do nothing with sky...
    shadow = lerp( 1.0, shadow, isGround );
    indirectDiff *= isGround;
    indirectSpec *= isGround;

    // Direct lighting and emission
    float3 directLighting = gIn_DirectLighting[ pixelPos ];
    float3 Lsum = directLighting * shadow;

    // Environment (pre-integrated) specular terms
    float viewZ = gIn_ViewZ[ pixelPos ];
    float3 Vv = STL::Geometry::ReconstructViewPosition( pixelUv, gCameraFrustum, viewZ, gIsOrtho );
    float3 V = -STL::Geometry::RotateVector( gViewToWorld, normalize( Vv ) );
    float NoV = abs( dot( N, V ) );
    float3 F = STL::BRDF::EnvironmentTerm_Ross( Rf0, NoV, roughness );

    // We loose G-term if trimming is high, return it back in pre-integrated form
    float2 GG = gIn_IntegratedBRDF.SampleLevel( gLinearSampler, float2( NoV, roughness ), 0.0 );
    float trimmingFactor = GetTrimmingFactor( roughness );
    F *= lerp( GG.x, 1.0, trimmingFactor );

    // Add ambient
    float m = roughness * roughness;
    indirectDiff.w *= GG.y;
    indirectDiff.xyz += gAmbient * indirectDiff.w;
    indirectSpec.xyz += gAmbient * indirectSpec.w * m;

    // Add indirect lighting
    Lsum += indirectDiff.xyz * albedo;
    Lsum += indirectSpec.xyz * F;

    // Debug
    if ( gOnScreen == SHOW_AMBIENT_OCCLUSION )
        Lsum = indirectDiff.w;
    else if ( gOnScreen == SHOW_SPECULAR_OCCLUSION )
        Lsum = indirectSpec.w;
    else if ( gOnScreen == SHOW_SHADOW )
        Lsum = shadow;
    else if ( gOnScreen == SHOW_BASE_COLOR )
        Lsum = baseColorMetalness.xyz;
    else if ( gOnScreen == SHOW_NORMAL )
        Lsum = N * 0.5 + 0.5;
    else if ( gOnScreen == SHOW_ROUGHNESS )
        Lsum = roughness;
    else if ( gOnScreen == SHOW_METALNESS )
        Lsum = baseColorMetalness.w;
    else if ( gOnScreen >= SHOW_WORLD_UNITS )
        Lsum = gOnScreen == SHOW_MIP_SPECULAR ? indirectSpec.xyz : ( directLighting * isGround );

    // Output
    gOut_ComposedImage[ pixelPos ] = float4( Lsum, viewZ * NRD_FP16_VIEWZ_SCALE );
}
