/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

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
    - NRD must be aware of the normalization function via "nrd::HitDistanceParameters"
    - by definition, normalized hit distance is AO ( ambient occlusion ) for diffuse. SO ( specular occlusion ) for specular
    - AO can be used to emulate 2nd+ diffuse bounces
    - SO can be used to adjust IBL lighting
    - ".w" channel of diffuse / specular output is AO / SO
    - if you don't know which normalization function to choose for diffuse, try "saturate( hitT / C )", where C = 1-3 m
    - if you don't know which normalization function to choose for specular, try "saturate( hitT / C )", where C = 10-40 m
*/

//=================================================================================================================================
// PUBLIC
//=================================================================================================================================

// This function can be tuned at any time ( NRD doesn't use NRD_FrontEnd_PackSpecular and NRD_BackEnd_UnpackSpecular internally )
float NRD_GetColorCompressionExposure( float linearRoughness )
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

//=================================================================================================================================
// PRIVATE ( DO NOT MODIFY WITHOUT FULL RECOMPILATION OF NRD LIBRARY! )
//=================================================================================================================================

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

//=================================================================================================================================
// FRONT-END PACKING
//=================================================================================================================================

// NRD

// Recommended to be used to "clear" INF pixels
#define NRD_INF_DIFF                    0
#define NRD_INF_SPEC                    0

// Must be used to "clear" INF pixels
#define NRD_INF_SHADOW                  float2( NRD_FP16_MAX, NRD_FP16_MAX )

float4 NRD_FrontEnd_PackRadiance( float3 radiance, float normHitDist, float linearRoughness = 1.0 )
{
    radiance = _NRD_LinearToYCoCg( radiance );

    float exposure = NRD_GetColorCompressionExposure( linearRoughness );
    float lum = radiance.x;
    float k = lum * exposure;
    float3 compressedRadiance = radiance / ( 1.0 + k );

    return float4( compressedRadiance, normHitDist );
}

float2 NRD_FrontEnd_PackShadow( float viewZ, float distanceToOccluder )
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
    float exposure = NRD_GetColorCompressionExposure( linearRoughness );
    float lum = _NRD_Luminance( radiance );
    float k = lum * exposure;
    float3 compressedRadiance = radiance / ( 1.0 + k );

    return float4( compressedRadiance, hitDist ); // TODO: RELAX - .w channel in the diffuse input will be ignored
}

//=================================================================================================================================
// BACK-END UNPACKING
//=================================================================================================================================

// NRD

float4 NRD_BackEnd_UnpackRadiance( float4 compressedRadiance_normHitDist, float linearRoughness = 1.0 )
{
    float exposure = NRD_GetColorCompressionExposure( linearRoughness );
    float lum = compressedRadiance_normHitDist.x;
    float k = lum * exposure;
    float3 radiance = compressedRadiance_normHitDist.xyz / max( 1.0 - k, NRD_EPS );

    radiance = _NRD_YCoCgToLinear( radiance );

    return float4( radiance, compressedRadiance_normHitDist.w );
}

#define NRD_BackEnd_UnpackShadow( color )  ( color * color )

// RELAX / SVGF

float4 RELAX_BackEnd_UnpackRadiance( float4 compressedRadiance, float linearRoughness = 1.0 )
{
    float exposure = NRD_GetColorCompressionExposure( linearRoughness );
    float lum = _NRD_Luminance( compressedRadiance.xyz );
    float k = lum * exposure;
    float3 radiance = compressedRadiance.xyz / max( 1.0 - k, NRD_EPS );

    return float4( radiance, compressedRadiance.w ); // TODO: RELAX - output doesn't have .w channel
}
