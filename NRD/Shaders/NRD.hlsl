/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// NRD v1.16.1

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

float3 direction:
    - direction radiance is coming from

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
    - by definition, normalized hit distance is AO ( ambient occlusion ) for diffuse. SO ( specular occlusion ) for specular
    - AO can be used to emulate 2nd+ diffuse bounces
    - SO can be used to adjust IBL lighting
    - ".w" channel of diffuse / specular output is AO / SO
    - if you don't know which normalization function to choose for diffuse, try "saturate( hitT / C )", where C = 1-3 m
    - if you don't know which normalization function to choose for specular, try "saturate( hitT / C )", where C = 10-40 m
*/

//=================================================================================================================================
// PRIVATE ( DO NOT MODIFY WITHOUT FULL RECOMPILATION OF NRD LIBRARY! )
//=================================================================================================================================

#define NRD_SHOW_MIPS                           1
#define NRD_SHOW_ACCUM_SPEED                    2
#define NRD_SHOW_ANTILAG                        3
#define NRD_SHOW_VIRTUAL_HISTORY_AMOUNT         4
#define NRD_SHOW_VIRTUAL_HISTORY_CONFIDENCE     5
#define NRD_DEBUG                               0 // 0-5

#define NRD_EPS                                 0.01
#define NRD_FP16_VIEWZ_SCALE                    0.0125
#define NRD_FP16_MAX                            65504.0
#define NRD_USE_SQRT_LINEAR_ROUGHNESS           0
#define NRD_USE_OCT_PACKED_NORMALS              0

float3 _NRD_LinearToYCoCg( float3 color )
{
    float Co = color.x - color.z;
    float t = color.z + Co * 0.5;
    float Cg = color.y - t;
    float Y = t + Cg * 0.5;

    return float3( Y, Co, Cg );
}

float3 _NRD_YCoCgToLinear( float3 color )
{
    float t = color.x - color.z * 0.5;
    float g = color.z + t;
    float b = t - color.y * 0.5;
    float r = b + color.y;
    float3 res = float3( r, g, b );

    // Ignore negative values ( minor deviations are possible )
    res = max( res, 0 );

    return res;
}

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

float _NRD_GetColorCompressionExposure( float linearRoughness )
{
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIwLjUvKDErNTAqeCkiLCJjb2xvciI6IiNGNzBBMEEifSx7InR5cGUiOjAsImVxIjoiMC41KigxLXgpLygxKzYwKngpIiwiY29sb3IiOiIjMkJGRjAwIn0seyJ0eXBlIjowLCJlcSI6IjAuNSooMS14KS8oMSsxMDAwKngqeCkrKDEteF4wLjUpKjAuMDMiLCJjb2xvciI6IiMwMDU1RkYifSx7InR5cGUiOjAsImVxIjoiMC42KigxLXgqeCkvKDErNDAwKngqeCkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIxIl0sInNpemUiOlsyOTUwLDk1MF19XQ--

    // No compression
    //return 0;

    // Moderate compression
    //return 0.5 / ( 1.0 + 50.0 * linearRoughness );

    // Less compression for mid-high roughness
    //return 0.5 * ( 1.0 - linearRoughness ) / ( 1.0 + 60.0 * linearRoughness );

    // Close to the previous one, but offers more compression for low roughness
    return 0.5 * ( 1.0 - linearRoughness ) / ( 1.0 + 1000.0 * linearRoughness * linearRoughness ) + ( 1.0 - sqrt( saturate( linearRoughness ) ) ) * 0.03;

    // A modification of the preious one ( simpler )
    //return 0.6 * ( 1.0 - linearRoughness * linearRoughness ) / ( 1.0 + 400.0 * linearRoughness * linearRoughness );
}

float _REBLUR_GetHitDistanceNormalization( float viewZ, float4 hitDistParams, float linearRoughness = 1.0 )
{
    return ( hitDistParams.x + abs( viewZ ) * hitDistParams.y ) * lerp( 1.0, hitDistParams.z, saturate( exp2( hitDistParams.w * linearRoughness * linearRoughness ) ) );
}

float _REBLUR_DecompressNormHitDistance( float compressedNormHitDist, float viewZ, float4 hitDistParams, float linearRoughness = 1.0 )
{
    float f = _REBLUR_GetHitDistanceNormalization( viewZ, hitDistParams, linearRoughness );
    float exposure = _NRD_GetColorCompressionExposure( linearRoughness );
    float decompressedNormHitDist = compressedNormHitDist / max( 1.0 - compressedNormHitDist * exposure, NRD_EPS );
    float normHitDist = saturate( decompressedNormHitDist / f );

    return normHitDist;
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
    float trimmingFactor = trimmingParams.x * STL::Math::SmoothStep( trimmingParams.y, trimmingParams.z, roughness );

    return trimmingFactor;
}

//=================================================================================================================================
// FRONT-END PACKING
//=================================================================================================================================

// REBLUR

// Recommended to be used to "clear" INF pixels
#define REBLUR_INF_DIFF     0
#define REBLUR_INF_SPEC     0

// This function returns AO / SO which REBLUR can decode back to "hit distance" internally
float REBLUR_FrontEnd_GetNormHitDist( float hitDist, float viewZ, float4 hitDistParams, float linearRoughness = 1.0 )
{
    float f = _REBLUR_GetHitDistanceNormalization( viewZ, hitDistParams, linearRoughness );

    return saturate( hitDist / f );
}

float4 REBLUR_FrontEnd_PackRadiance( float3 radiance, float normHitDist, float viewZ, float4 hitDistParams, float linearRoughness = 1.0 )
{
    float exposure = _NRD_GetColorCompressionExposure( linearRoughness );

    radiance = _NRD_LinearToYCoCg( radiance );
    float3 compressedRadiance = radiance / ( 1.0 + radiance.x * exposure );

    float f = _REBLUR_GetHitDistanceNormalization( viewZ, hitDistParams, linearRoughness );
    float d = normHitDist * f;
    float compressedHitDist = d / ( 1.0 + d * exposure );

    return float4( compressedRadiance, compressedHitDist );
}

// SIGMA

// Must be used to "clear" INF pixels
#define SIGMA_INF_SHADOW     float2( NRD_FP16_MAX, NRD_FP16_MAX )

float2 SIGMA_FrontEnd_PackShadow( float viewZ, float distanceToOccluder )
{
    float2 r;
    r.x = 0.0;
    r.y = clamp( viewZ * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );

    [flatten]
    if( distanceToOccluder == NRD_FP16_MAX )
        r.x = NRD_FP16_MAX;
    else if( distanceToOccluder != 0.0 )
        r.x = clamp( distanceToOccluder * NRD_FP16_VIEWZ_SCALE, 0.0001, 32768.0 );

    return r;
}

// RELAX / SVGF

float4 RELAX_FrontEnd_PackRadiance( float3 radiance, float hitDist, float linearRoughness = 1.0 )
{
    float exposure = _NRD_GetColorCompressionExposure( linearRoughness );

    float lum = _NRD_Luminance( radiance );
    float3 compressedRadiance = radiance / ( 1.0 + lum * exposure );

    return float4( compressedRadiance, hitDist ); // TODO: RELAX - .w channel in the diffuse input will be ignored
}

//=================================================================================================================================
// BACK-END UNPACKING
//=================================================================================================================================

// REBLUR

float4 REBLUR_BackEnd_UnpackRadiance( float4 compressedRadianceAndNormHitDist, float viewZ, float4 hitDistParams, float linearRoughness = 1.0 )
{
    float exposure = _NRD_GetColorCompressionExposure( linearRoughness );

    float3 radiance = compressedRadianceAndNormHitDist.xyz / max( 1.0 - compressedRadianceAndNormHitDist.x * exposure, NRD_EPS );
    radiance = _NRD_YCoCgToLinear( radiance );

    float normHitDist = compressedRadianceAndNormHitDist.w;
    #if( NRD_DEBUG == 0 )
        normHitDist = _REBLUR_DecompressNormHitDistance( compressedRadianceAndNormHitDist.w, viewZ, hitDistParams, linearRoughness );
    #endif

    return float4( radiance, normHitDist );
}

// SIGMA

#define SIGMA_BackEnd_UnpackShadow( color )  ( color * color )

// RELAX / SVGF

float4 RELAX_BackEnd_UnpackRadiance( float4 compressedRadiance, float linearRoughness = 1.0 )
{
    float exposure = _NRD_GetColorCompressionExposure( linearRoughness );

    float lum = _NRD_Luminance( compressedRadiance.xyz );
    float3 radiance = compressedRadiance.xyz / max( 1.0 - lum * exposure, NRD_EPS );

    return float4( radiance, compressedRadiance.w ); // TODO: RELAX - output doesn't have .w channel
}
