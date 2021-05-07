/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// NRD v2.1.1

//=================================================================================================================================
// INPUT PARAMETERS
//=================================================================================================================================
/*
float3 radiance:
    - radiance != irradiance, it's pure energy coming from a particular direction
    - radiance should not include any BRDFs ( material information )
    - radiance can be premultiplied with "exposure"
    - radiance should not include PI for diffuse ( it will be canceled out later when the denoised output will be multiplied with albedo / PI )
    - for diffuse rays
        - use COS-distribution ( or custom importance sampling )
        - if radiance is the result of path tracing pass normalized hit distance as the sum of 1-all hits (always ignore primary hit!)
    - for specular
        - use VNDF sampling ( or custom importance sampling )
        - if radiance is the result of path tracing pass normalized hit distance as the sum of first 1-3 hits (always ignore primary hit!)

float linearRoughness:
    - linearRoughness = sqrt( roughness ), where "roughness" = "m" = "alpha" - specular or real roughness

float normal:
    - world space normal

float viewZ:
    - linear view space Z for primary rays ( linearized camera depth )

float distanceToOccluder:
    - distance to occluder, rules:
        - NoL <= 0         - 0 ( it's very important )
        - NoL > 0 ( hit )  - hit distance
        - NoL > 0 ( miss ) - NRD_FP16_MAX

float normHitDist:
    - normalized hit distance
    - REBLUR must be aware of the normalization function via "nrd::HitDistanceParameters"
    - by definition, normalized hit distance is AO ( ambient occlusion ) for diffuse and SO ( specular occlusion ) for specular
    - AO can be used to emulate 2nd+ diffuse bounces
    - SO can be used to adjust IBL lighting
    - ".w" channel of diffuse / specular output is AO / SO
    - if you don't know which normalization function to choose use default values of "nrd::HitDistanceParameters"
*/

//=================================================================================================================================
// SETTINGS ( DO NOT MODIFY WITHOUT FULL RECOMPILATION OF NRD LIBRARY! )
//=================================================================================================================================

#ifndef NRD_USE_SQRT_LINEAR_ROUGHNESS
    #define NRD_USE_SQRT_LINEAR_ROUGHNESS                       0
#endif

#ifndef NRD_USE_OCT_PACKED_NORMALS
    #define NRD_USE_OCT_PACKED_NORMALS                          0
#endif

//=================================================================================================================================
// PRIVATE
//=================================================================================================================================

#define NRD_EPS                                                 0.01
#define NRD_FP16_VIEWZ_SCALE                                    0.0125
#define NRD_FP16_MAX                                            65504.0

float3 _NRD_DecodeUnitVector( float2 p, const bool bSigned = false, const bool bNormalize = true )
{
    p = bSigned ? p : ( p * 2.0 - 1.0 );

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3( p.xy, 1.0 - abs( p.x ) - abs( p.y ) );
    float t = saturate( -n.z );
    n.xy += n.xy >= 0.0 ? -t : t;

    return bNormalize ? normalize( n ) : n;
}

float _NRD_Luminance( float3 linearColor )
{
    return dot( linearColor, float3( 0.2990, 0.5870, 0.1140 ) );
}

float4 _NRD_FrontEnd_UnpackNormalAndRoughness( float4 p )
{
    float4 r;
    #if( NRD_USE_OCT_PACKED_NORMALS == 1 )
        const bool bSigned = true;
        const bool bNormalize = false;
        p.xy = p.xy * 2.0 - 1.0;
        r.xyz = _NRD_DecodeUnitVector( p.xy, bSigned, bNormalize );
        r.w = p.z;
    #else
        r.xyz = p.xyz * 2.0 - 1.0;
        r.w = p.w;
    #endif

    // Normalization is very important due to potential octahedron encoding and potential "best fit" usage for simple "N * 0.5 + 0.5" method
    r.xyz = normalize( r.xyz );

    #if( NRD_USE_SQRT_LINEAR_ROUGHNESS == 1 )
        r.w *= r.w;
    #endif

    return r;
}

float _REBLUR_GetHitDistanceNormalization( float viewZ, float4 hitDistParams, float linearRoughness = 1.0 )
{
    return ( hitDistParams.x + abs( viewZ ) * hitDistParams.y ) * lerp( 1.0, hitDistParams.z, saturate( exp2( hitDistParams.w * linearRoughness * linearRoughness ) ) );
}

//=================================================================================================================================
// MISC
//=================================================================================================================================

// We loose G-term if trimming is high, return it back in pre-integrated form
// A typical use case is:
/*
    float g = gIn_IntegratedBRDF.SampleLevel( gLinearSampler, float2( NoV, roughness ), 0.0 ).x; // pre-integrated G-term

    float trimmingFactor = NRD_GetTrimmingFactor( roughness, trimmingParams );
    F *= lerp( g, 1.0, trimmingFactor );
    Lsum += spec * F;
*/
float NRD_GetTrimmingFactor( float roughness, float3 trimmingParams )
{
    float trimmingFactor = trimmingParams.x * smoothstep( trimmingParams.y, trimmingParams.z, roughness );

    return trimmingFactor;
}

//=================================================================================================================================
// FRONT-END PACKING
//=================================================================================================================================

//========
// REBLUR
//========

// This function returns AO / SO which REBLUR can decode back to "hit distance" internally
float REBLUR_FrontEnd_GetNormHitDist( float hitDist, float viewZ, float4 hitDistParams, float linearRoughness = 1.0 )
{
    float f = _REBLUR_GetHitDistanceNormalization( viewZ, hitDistParams, linearRoughness );

    return saturate( hitDist / f );
}

float4 REBLUR_FrontEnd_PackRadiance( float3 radiance, float normHitDist, bool sanitize = true )
{
    if( sanitize )
    {
        radiance = ( any( isnan( radiance ) ) || any( isinf( radiance ) ) ) ? 0 : radiance;
        normHitDist = ( isnan( normHitDist ) || isinf( normHitDist ) ) ? 0 : normHitDist;
    }

    return float4( radiance, normHitDist );
}

//========
// RELAX
//========

float4 RELAX_FrontEnd_PackRadiance( float3 radiance, float hitDist, bool sanitize = true )
{
    if( sanitize )
    {
        radiance = ( any( isnan( radiance ) ) || any( isinf( radiance ) ) ) ? 0 : radiance;
        hitDist = ( isnan( hitDist ) || isinf( hitDist ) ) ? 0 : hitDist;
    }

    return float4( radiance, hitDist );
}

//========
// SIGMA
//========

// Must be used to "clear" INF pixels
#define SIGMA_INF_SHADOW        float2( NRD_FP16_MAX, NRD_FP16_MAX )
#define SIGMA_MIN_DISTANCE      0.0001 // not 0, because it means "NoL < 0, stop processing"

// SIGMA ( single light )

float2 SIGMA_FrontEnd_PackShadow( float viewZ, float distanceToOccluder, float tanOfLightAngularRadius, out float shadow )
{
    float2 r;
    r.x = 0.0;
    r.y = clamp( viewZ * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );

    [flatten]
    if( distanceToOccluder == NRD_FP16_MAX )
        r.x = NRD_FP16_MAX;
    else if( distanceToOccluder != 0.0 )
    {
        // Premultiply with angular size here, it's needed to unlock multi-light shadow denoising, when angular size can vary per pixel, depending on which light was sampled
        float projDistanceToOccluder = distanceToOccluder * tanOfLightAngularRadius;

        r.x = clamp( projDistanceToOccluder, SIGMA_MIN_DISTANCE, 32768.0 );
    }

    shadow = float( distanceToOccluder == NRD_FP16_MAX );

    return r;
}

// SIGMA ( multi light )

#define SIGMA_MULTILIGHT_DATATYPE float2x3

SIGMA_MULTILIGHT_DATATYPE SIGMA_FrontEnd_MultiLightStart()
{
    return ( SIGMA_MULTILIGHT_DATATYPE )0;
}

void SIGMA_FrontEnd_MultiLightUpdate( float3 L, float distanceToOccluder, float tanOfLightAngularRadius, float weight, inout SIGMA_MULTILIGHT_DATATYPE multiLightShadowData )
{
    float shadow;
    float distanceToOccluderProj = SIGMA_FrontEnd_PackShadow( 0, distanceToOccluder, tanOfLightAngularRadius, shadow ).x;

    // Weighted sum for "pseudo" translucency
    multiLightShadowData[ 0 ] += L * shadow;

    // Weighted sum for distance to occluder (denoising will be driven by most important light)
    weight *= _NRD_Luminance( L );

    multiLightShadowData[ 1 ] += float3( distanceToOccluderProj * weight, weight, 0 );
}

float2 SIGMA_FrontEnd_MultiLightEnd( float viewZ, SIGMA_MULTILIGHT_DATATYPE multiLightShadowData, float3 Lsum, out float4 shadowTranslucency )
{
    shadowTranslucency.yzw = multiLightShadowData[ 0 ] / max( Lsum, 1e-6 );
    shadowTranslucency.x = _NRD_Luminance( shadowTranslucency.yzw );

    float2 r;
    r.x = multiLightShadowData[ 1 ].x / max( multiLightShadowData[ 1 ].y, 1e-6 );
    r.y = clamp( viewZ * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );

    return r;
}

//=================================================================================================================================
// BACK-END UNPACKING
//=================================================================================================================================

//========
// REBLUR
//========

// unpacks internally

//========
// RELAX
//========

// unpacks internally

//========
// SIGMA
//========

#define SIGMA_BackEnd_UnpackShadow( color )  ( color * color )
