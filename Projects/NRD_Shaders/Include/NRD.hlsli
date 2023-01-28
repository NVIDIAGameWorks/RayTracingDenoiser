/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// NRD v4.0

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
        - if radiance is the result of path tracing, pass normalized hit distance as the sum of 1-all hits (always ignore primary hit!)
    - for specular
        - use VNDF sampling ( or custom importance sampling )
        - if radiance is the result of path tracing, pass normalized hit distance as the sum of first 1-3 hits (always ignore primary hit!)

float roughness:
    - "linear roughness" = sqrt( "m" ), where "m" = "alpha" - GGX roughness

float normal:
    - world-space normal

float viewZ:
    - linear view space Z for primary rays ( linearized camera depth )

float distanceToOccluder:
    - distance to occluder, must follow the rules:
        - NoL <= 0         - 0 ( it's very important )
        - NoL > 0 ( hit )  - hit distance
        - NoL > 0 ( miss ) - NRD_FP16_MAX

float tanOfLightAngularRadius:
    - tan( lightAngularSize * 0.5 )
    - angular size is computed from the shadow receiving point
    - in other words, tanOfLightAngularRadius = distanceToLight / lightRadius

float normHitDist:
    - normalized hit distance, gotten by using "REBLUR_FrontEnd_GetNormHitDist"
    - REBLUR must be aware of the normalization function via "nrd::HitDistanceParameters"
    - by definition, normalized hit distance is AO ( ambient occlusion ) for diffuse and SO ( specular occlusion ) for specular
    - AO can be used to emulate 2nd+ diffuse bounces
    - SO can be used to adjust IBL lighting
    - ".w" channel of diffuse / specular output is AO / SO
    - if you don't know which normalization function to choose use default values of "nrd::HitDistanceParameters"

NOTE: if "roughness" is needed as an input parameter use is as "isDiffuse ? 1 : roughness"
*/

 // IMPORTANT: DO NOT MODIFY THIS FILE WITHOUT FULL RECOMPILATION OF NRD LIBRARY!

 #ifndef NRD_INCLUDED
 #define NRD_INCLUDED
#include "NRDEncoding.hlsli"

#if( !defined( NRD_NORMAL_ENCODING ) || !defined( NRD_ROUGHNESS_ENCODING ) )
    #ifdef NRD_HEADER_ONLY
        #error "Include 'NRDEncoding.hlsli' file beforehand to get a match with the settings NRD has been compiled with. Or define encoding variants using Cmake parameters."
    #else
        #error "For NRD project compilation, encoding variants must be set using Cmake parameters."
    #endif
#endif

//=================================================================================================================================
// BINDINGS
//=================================================================================================================================

#define NRD_CONSTANT_BUFFER_SPACE_INDEX                                                 0
#define NRD_SAMPLERS_SPACE_INDEX                                                        0
#define NRD_RESOURCES_SPACE_INDEX                                                       0

#define NRD_MERGE_TOKENS_( _0, _1 )                                                     _0 ## _1
#define NRD_MERGE_TOKENS( _0, _1 )                                                      NRD_MERGE_TOKENS_( _0, _1 )

#if( defined NRD_COMPILER_UNREAL_ENGINE )

    #ifndef NRD_CS_MAIN
        #define NRD_CS_MAIN                                                             main
    #endif

    #define NRD_EXPORT

    #define NRD_CONSTANTS_START
    #define NRD_CONSTANT( constantType, constantName )                                  constantType constantName;
    #define NRD_CONSTANTS_END

    #define NRD_INPUT_TEXTURE_START
    #define NRD_INPUT_TEXTURE( resourceType, resourceName, regName, bindingIndex )      resourceType resourceName;
    #define NRD_INPUT_TEXTURE_END

    #define NRD_OUTPUT_TEXTURE_START
    #define NRD_OUTPUT_TEXTURE( resourceType, resourceName, regName, bindingIndex )     resourceType resourceName;
    #define NRD_OUTPUT_TEXTURE_END

    #define NRD_SAMPLER_START
    #define NRD_SAMPLER( resourceType, resourceName, regName, bindingIndex )            resourceType resourceName;
    #define NRD_SAMPLER_END

#elif( defined NRD_COMPILER_FXC )

    #ifndef NRD_CS_MAIN
        #define NRD_CS_MAIN                                                             main
    #endif

    #define NRD_EXPORT

    #define NRD_CONSTANTS_START                                                         cbuffer globalConstants : register( b0 ) {
    #define NRD_CONSTANT( constantType, constantName )                                  constantType constantName;
    #define NRD_CONSTANTS_END                                                           };

    #define NRD_INPUT_TEXTURE_START
    #define NRD_INPUT_TEXTURE( resourceType, resourceName, regName, bindingIndex )      resourceType resourceName : register( regName ## bindingIndex );
    #define NRD_INPUT_TEXTURE_END

    #define NRD_OUTPUT_TEXTURE_START
    #define NRD_OUTPUT_TEXTURE( resourceType, resourceName, regName, bindingIndex )     resourceType resourceName : register( regName ## bindingIndex );
    #define NRD_OUTPUT_TEXTURE_END

    #define NRD_SAMPLER_START
    #define NRD_SAMPLER( resourceType, resourceName, regName, bindingIndex )            resourceType resourceName : register( regName ## bindingIndex );
    #define NRD_SAMPLER_END

#elif( defined NRD_COMPILER_DXC )

    #ifndef NRD_CS_MAIN
        #define NRD_CS_MAIN                                                             main
    #endif

    #define NRD_EXPORT

    #define NRD_CONSTANTS_START                                                         cbuffer globalConstants : register( b0, NRD_MERGE_TOKENS( space, NRD_CONSTANT_BUFFER_SPACE_INDEX ) ) {
    #define NRD_CONSTANT( constantType, constantName )                                  constantType constantName;
    #define NRD_CONSTANTS_END                                                           };

    #define NRD_INPUT_TEXTURE_START
    #define NRD_INPUT_TEXTURE( resourceType, resourceName, regName, bindingIndex )      resourceType resourceName : register( regName ## bindingIndex, NRD_MERGE_TOKENS( space, NRD_RESOURCES_SPACE_INDEX ) );
    #define NRD_INPUT_TEXTURE_END

    #define NRD_OUTPUT_TEXTURE_START
    #define NRD_OUTPUT_TEXTURE( resourceType, resourceName, regName, bindingIndex )     resourceType resourceName : register( regName ## bindingIndex, NRD_MERGE_TOKENS( space, NRD_RESOURCES_SPACE_INDEX ) );
    #define NRD_OUTPUT_TEXTURE_END

    #define NRD_SAMPLER_START
    #define NRD_SAMPLER( resourceType, resourceName, regName, bindingIndex )            resourceType resourceName : register( regName ## bindingIndex, NRD_MERGE_TOKENS( space, NRD_SAMPLERS_SPACE_INDEX ) );
    #define NRD_SAMPLER_END

#elif( defined NRD_COMPILER_PSSLC )

    // Helpers
    #define EXPAND( x )                                                                 x
    #define GET_NTH_MACRO_4_arg( a, b, c, d, NAME, ... )                                NAME
    #define GET_NTH_MACRO_3_arg( a, b, c, NAME, ... )                                   NAME
    #define SampleLevel3( a, b, c )                                                     SampleLOD( a, b, ( float )( c ) )
    #define SampleLevel4( a, b, c, d )                                                  SampleLOD( a, b, ( float )( c ), d )
    #define GatherRed3( a, b, c )                                                       GatherRed( ( a ), ( b ), int2( c ) )
    #define GatherRed2( a, b )                                                          GatherRed( ( a ), ( b ) )
    #define GatherGreen3( a, b, c )                                                     GatherGreen( ( a ), ( b ), int2( c ) )
    #define GatherGreen2( a, b )                                                        GatherGreen( ( a ), ( b ) )

    #ifndef NRD_CS_MAIN
        #define NRD_CS_MAIN                                                             main
    #endif

    #ifdef EXPORT_NAME
        #define NRD_EXPORT                                                              [ CxxSymbol( EXPORT_NAME ) ]
    #else
        #define NRD_EXPORT
    #endif

    #define NRD_CONSTANTS_START                                                         ConstantBuffer globalConstants : register( b0 ) {
    #define NRD_CONSTANT( constantType, constantName )                                  constantType constantName;
    #define NRD_CONSTANTS_END                                                           };

    #define NRD_INPUT_TEXTURE_START
    #define NRD_INPUT_TEXTURE( resourceType, resourceName, regName, bindingIndex )      resourceType resourceName : register( regName ## bindingIndex );
    #define NRD_INPUT_TEXTURE_END

    #define NRD_OUTPUT_TEXTURE_START
    #define NRD_OUTPUT_TEXTURE( resourceType, resourceName, regName, bindingIndex )     resourceType resourceName : register( regName ## bindingIndex );
    #define NRD_OUTPUT_TEXTURE_END

    #define NRD_SAMPLER_START
    #define NRD_SAMPLER( resourceType, resourceName, regName, bindingIndex )            resourceType resourceName : register( regName ## bindingIndex );
    #define NRD_SAMPLER_END

    #define numthreads                                                                  NUM_THREADS
    #define groupshared                                                                 thread_group_memory
    #define SV_GroupId                                                                  S_GROUP_ID
    #define SV_GroupIndex                                                               S_GROUP_INDEX
    #define SV_GroupThreadId                                                            S_GROUP_THREAD_ID
    #define SV_DispatchThreadId                                                         S_DISPATCH_THREAD_ID
    #define GroupMemoryBarrierWithGroupSync                                             ThreadGroupMemoryBarrierSync
    #define GroupMemoryBarrier                                                          ThreadGroupMemoryBarrier
    #define RWTexture2D                                                                 RW_Texture2D
    #define cbuffer                                                                     ConstantBuffer
    #define SampleLevel( ... )                                                          EXPAND( GET_NTH_MACRO_4_arg( __VA_ARGS__, SampleLevel4, SampleLevel3 )( __VA_ARGS__ ) )
    #define GatherRed( ... )                                                            EXPAND( GET_NTH_MACRO_3_arg( __VA_ARGS__, GatherRed3, GatherRed2 )( __VA_ARGS__ ) )
    #define GatherGreen( ... )                                                          EXPAND( GET_NTH_MACRO_3_arg( __VA_ARGS__, GatherGreen3, GatherGreen2 )( __VA_ARGS__ ) )
    #define reversebits                                                                 ReverseBits
    #define InterlockedAdd( ... )                                                       AtomicAdd( __VA_ARGS__ )
    #define InterlockedMax( ... )                                                       AtomicMax( __VA_ARGS__ )
    #define unorm

#elif( defined( NRD_INPUT_TEXTURE ) && defined( NRD_OUTPUT_TEXTURE ) && defined( NRD_CONSTANTS_START ) && defined( NRD_CONSTANT ) && defined( NRD_CONSTANTS_END ) )

    #ifndef NRD_CS_MAIN
        #define NRD_CS_MAIN                                                             main
    #endif

    #define NRD_EXPORT

    // Custom engine that has already defined all the macros

#elif( !defined( NRD_HEADER_ONLY ) )
    #error "Define one of NRD_COMPILER_[FXC/DXC/PSSLC/UNREAL_ENGINE] or add custom bindings (use already defined platforms as a reference). Or define NRD_HEADER_ONLY to use this file as a header file only."
#endif

//=================================================================================================================================
// PRIVATE
//=================================================================================================================================

#if !defined(__cplusplus)

// Normal encoding variants ( match NormalEncoding )
#define NRD_NORMAL_ENCODING_RGBA8_UNORM                                                 0
#define NRD_NORMAL_ENCODING_RGBA8_SNORM                                                 1
#define NRD_NORMAL_ENCODING_R10G10B10A2_UNORM                                           2 // supports material ID bits
#define NRD_NORMAL_ENCODING_RGBA16_UNORM                                                3
#define NRD_NORMAL_ENCODING_RGBA16_SNORM                                                4 // also can be used with FP formats

// Roughness encoding variants ( match RoughnessEncoding )
#define NRD_ROUGHNESS_ENCODING_SQ_LINEAR                                                0 // linearRoughness * linearRoughness
#define NRD_ROUGHNESS_ENCODING_LINEAR                                                   1 // linearRoughness
#define NRD_ROUGHNESS_ENCODING_SQRT_LINEAR                                              2 // sqrt( linearRoughness )

#define NRD_FP16_MIN                                                                    1e-7 // min allowed hitDist (0 = no data)
#define NRD_FP16_MAX                                                                    65504.0
#define NRD_FP16_VIEWZ_SCALE                                                            0.125 // TODO: tuned for meters, needs to be scaled down for cm and mm
#define NRD_PI                                                                          3.14159265358979323846
#define NRD_EPS                                                                         1e-6

// ViewZ packing into FP16
float _NRD_PackViewZ( float z )
{
    return clamp( z * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );
}

// Oct packing
float2 _NRD_EncodeUnitVector( float3 v, const bool bSigned = false )
{
    v /= dot( abs( v ), 1.0 );

    float2 octWrap = ( 1.0 - abs( v.yx ) ) * ( step( 0.0, v.xy ) * 2.0 - 1.0 );
    v.xy = v.z >= 0.0 ? v.xy : octWrap;

    return bSigned ? v.xy : v.xy * 0.5 + 0.5;
}

float3 _NRD_DecodeUnitVector( float2 p, const bool bSigned = false, const bool bNormalize = true )
{
    p = bSigned ? p : ( p * 2.0 - 1.0 );

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3( p.xy, 1.0 - abs( p.x ) - abs( p.y ) );
    float t = saturate( -n.z );
    n.xy -= t * ( step( 0.0, n.xy ) * 2.0 - 1.0 );

    return bNormalize ? normalize( n ) : n;
}

// Color space
float _NRD_Luminance( float3 linearColor )
{
    return dot( linearColor, float3( 0.2990, 0.5870, 0.1140 ) );
}

float3 _NRD_LinearToYCoCg( float3 color )
{
    float Y = dot( color, float3( 0.25, 0.5, 0.25 ) );
    float Co = dot( color, float3( 0.5, 0.0, -0.5 ) );
    float Cg = dot( color, float3( -0.25, 0.5, -0.25 ) );

    return float3( Y, Co, Cg );
}

float3 _NRD_YCoCgToLinear( float3 color )
{
    float t = color.x - color.z;

    float3 r;
    r.y = color.x + color.z;
    r.x = t + color.y;
    r.z = t - color.y;

    return max( r, 0.0 );
}

// Hit distance normalization
float _REBLUR_GetHitDistanceNormalization( float viewZ, float4 hitDistParams, float roughness = 1.0 )
{
    return ( hitDistParams.x + abs( viewZ ) * hitDistParams.y ) * lerp( 1.0, hitDistParams.z, saturate( exp2( hitDistParams.w * roughness * roughness ) ) );
}

//=================================================================================================================================
// SPHERICAL HARMONICS
//=================================================================================================================================

// Spherical harmonics
// https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/gdc2018-precomputedgiobalilluminationinfrostbite.pdf

struct NRD_SH
{
    float3 c0_chroma;
    float3 c1;
    float normHitDist;
};

NRD_SH NRD_SH_Create( float3 color, float3 direction, float normHitDist = 1.0 )
{
    float3 YCoCg = _NRD_LinearToYCoCg( color );

    NRD_SH sh;
    sh.c0_chroma = YCoCg;
    sh.c1 = direction * YCoCg.x;
    sh.normHitDist = normHitDist;

    return sh;
}

float3 NRD_SH_ExtractColor( NRD_SH sh )
{
    return _NRD_YCoCgToLinear( sh.c0_chroma );
}

float3 NRD_SH_ExtractDirection( NRD_SH sh )
{
    return sh.c1 / max( length( sh.c1 ), NRD_EPS );
}

void NRD_SH_Add( inout NRD_SH result, NRD_SH x )
{
    result.c0_chroma += x.c0_chroma;
    result.c1 += x.c1;
}

void NRD_SH_Mul( inout NRD_SH result, float x )
{
    result.c0_chroma *= x;
    result.c1 *= x;
}

float3 NRD_SH_ResolveColor( NRD_SH sh, float3 N )
{
    float Y = 0.5 * dot( N, sh.c1 ) + 0.25 * sh.c0_chroma.x;

    // 2 - hemisphere, 4 - sphere
    Y *= 2.0;

    // Corrected color reproduction
    Y = max( Y, 0.0 );

    float modifier = ( Y + NRD_EPS ) / ( sh.c0_chroma.x + NRD_EPS );
    float2 CoCg = sh.c0_chroma.yz * modifier;

    return _NRD_YCoCgToLinear( float3( Y, CoCg ) );
}

//=================================================================================================================================
// FRONT-END
//=================================================================================================================================

//========
// NRD
//========

// This function is used in all denoisers to decode normal, roughness and optional materialID
// IN_NORMAL_ROUGHNESS => X
float4 NRD_FrontEnd_UnpackNormalAndRoughness( float4 p, out float materialID )
{
    float4 r;
    #if( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_R10G10B10A2_UNORM )
        r.xyz = _NRD_DecodeUnitVector( p.xy, false, false );
        r.w = p.z;

        materialID = p.w;
    #else
        #if( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_RGBA8_UNORM || NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_RGBA16_UNORM )
            p.xyz = p.xyz * 2.0 - 1.0;
        #endif

        r.xyz = p.xyz;
        r.w = p.w;

        materialID = 0;
    #endif

    r.xyz = normalize( r.xyz );

    #if( NRD_ROUGHNESS_ENCODING == NRD_ROUGHNESS_ENCODING_SQRT_LINEAR )
        r.w *= r.w;
    #elif( NRD_ROUGHNESS_ENCODING == NRD_ROUGHNESS_ENCODING_SQ_LINEAR )
        r.w = sqrt( r.w );
    #endif

    return r;
}

// IN_NORMAL_ROUGHNESS => X
float4 NRD_FrontEnd_UnpackNormalAndRoughness( float4 p )
{
    float unused;

    return NRD_FrontEnd_UnpackNormalAndRoughness( p, unused );
}

// Not used in NRD
// X => IN_NORMAL_ROUGHNESS
float4 NRD_FrontEnd_PackNormalAndRoughness( float3 N, float roughness, uint materialID = 0 )
{
    float4 p;

    #if( NRD_ROUGHNESS_ENCODING == NRD_ROUGHNESS_ENCODING_SQRT_LINEAR )
        roughness = sqrt( saturate( roughness ) );
    #elif( NRD_ROUGHNESS_ENCODING == NRD_ROUGHNESS_ENCODING_SQ_LINEAR )
        roughness *= roughness;
    #endif

    #if( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_R10G10B10A2_UNORM )
        p.xy = _NRD_EncodeUnitVector( N, false );
        p.z = roughness;
        p.w = saturate( materialID / 3.0 );
    #else
        // Best fit ( optional )
        N /= max( abs( N.x ), max( abs( N.y ), abs( N.z ) ) );

        #if( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_RGBA8_UNORM || NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_RGBA16_UNORM )
            N = N * 0.5 + 0.5;
        #endif

        p.xyz = N;
        p.w = roughness;
    #endif

    return p;
}

//========
// REBLUR
//========

// This function returns AO / SO which REBLUR can decode back to "hit distance" internally
float REBLUR_FrontEnd_GetNormHitDist( float hitDist, float viewZ, float4 hitDistParams, float roughness = 1.0 )
{
    float f = _REBLUR_GetHitDistanceNormalization( viewZ, hitDistParams, roughness );

    return saturate( hitDist / f );
}

// X => IN_DIFF_RADIANCE_HITDIST
// X => IN_SPEC_RADIANCE_HITDIST
// normHitDist must be packed by "REBLUR_FrontEnd_GetNormHitDist"
float4 REBLUR_FrontEnd_PackRadianceAndNormHitDist( float3 radiance, float normHitDist, bool sanitize = true )
{
    if( sanitize )
    {
        radiance = any( isnan( radiance ) | isinf( radiance ) ) ? 0 : clamp( radiance, 0, NRD_FP16_MAX );
        normHitDist = ( isnan( normHitDist ) | isinf( normHitDist ) ) ? 0 : saturate( normHitDist );
    }

    // "0" is reserved to mark "no data" samples, skipped due to probabilistic sampling
    if( normHitDist != 0 )
        normHitDist = max( normHitDist, NRD_FP16_MIN );

    radiance = _NRD_LinearToYCoCg( radiance );

    return float4( radiance, normHitDist );
}

// X => IN_DIFF_SH0 and IN_DIFF_SH1
// X => IN_SPEC_SH0 and IN_SPEC_SH1
// normHitDist must be packed by "REBLUR_FrontEnd_GetNormHitDist"
float4 REBLUR_FrontEnd_PackSh( float3 radiance, float normHitDist, float3 direction, out float4 out1, bool sanitize = true )
{
    if( sanitize )
    {
        radiance = any( isnan( radiance ) | isinf( radiance ) ) ? 0 : clamp( radiance, 0, NRD_FP16_MAX );
        normHitDist = ( isnan( normHitDist ) | isinf( normHitDist ) ) ? 0 : saturate( normHitDist );
    }

    // "0" is reserved to mark "no data" samples, skipped due to probabilistic sampling
    if( normHitDist != 0 )
        normHitDist = max( normHitDist, NRD_FP16_MIN );

    NRD_SH sh = NRD_SH_Create( radiance, direction, normHitDist );

    // IN_DIFF_SH0 / IN_SPEC_SH0
    float4 out0 = float4( sh.c0_chroma, sh.normHitDist );

    // IN_DIFF_SH1 / IN_SPEC_SH1
    out1 = float4( sh.c1, 0.0 );

    return out0;
}

// X => IN_DIFF_DIRECTION_HITDIST
// normHitDist must be packed by "REBLUR_FrontEnd_GetNormHitDist"
float4 REBLUR_FrontEnd_PackDirectionalOcclusion( float3 direction, float normHitDist, bool sanitize = true )
{
    if( sanitize )
    {
        direction = any( isnan( direction ) | isinf( direction ) ) ? 0 : direction;
        normHitDist = ( isnan( normHitDist ) | isinf( normHitDist ) ) ? 0 : saturate( normHitDist );
    }

    // "0" is reserved to mark "no data" samples, skipped due to probabilistic sampling
    if( normHitDist != 0 )
        normHitDist = max( normHitDist, NRD_FP16_MIN );

    NRD_SH sh = NRD_SH_Create( normHitDist, direction );

    return float4( sh.c1, sh.c0_chroma.x );
}

//========
// RELAX
//========

// X => IN_DIFF_RADIANCE_HITDIST
// X => IN_SPEC_RADIANCE_HITDIST
float4 RELAX_FrontEnd_PackRadianceAndHitDist( float3 radiance, float hitDist, bool sanitize = true )
{
    if( sanitize )
    {
        radiance = any( isnan( radiance ) | isinf( radiance ) ) ? 0 : clamp( radiance, 0, NRD_FP16_MAX );
        hitDist = ( isnan( hitDist ) | isinf( hitDist ) ) ? 0 : clamp( hitDist, 0, NRD_FP16_MAX );
    }

    // "0" is reserved to mark "no data" samples, skipped due to probabilistic sampling
    if( hitDist != 0 )
        hitDist = max( hitDist, NRD_FP16_MIN );

    return float4( radiance, hitDist );
}

//========
// SIGMA
//========

#define SIGMA_MIN_DISTANCE      0.0001 // not 0, because it means "NoL < 0, stop processing"

// SIGMA ( single light )

// X => IN_SHADOWDATA
float2 SIGMA_FrontEnd_PackShadow( float viewZ, float distanceToOccluder, float tanOfLightAngularRadius )
{
    float2 r;
    r.x = 0.0;
    r.y = _NRD_PackViewZ( viewZ );

    [flatten]
    if( distanceToOccluder == NRD_FP16_MAX )
        r.x = NRD_FP16_MAX;
    else if( distanceToOccluder != 0.0 )
    {
        float distanceToOccluderProj = distanceToOccluder * tanOfLightAngularRadius;
        r.x = clamp( distanceToOccluderProj, SIGMA_MIN_DISTANCE, 32768.0 );
    }

    return r;
}

// X => IN_SHADOWDATA and IN_SHADOW_TRANSLUCENCY
float2 SIGMA_FrontEnd_PackShadow( float viewZ, float distanceToOccluder, float tanOfLightAngularRadius, float3 translucency, out float4 out2 )
{
    // IN_SHADOW_TRANSLUCENCY
    out2.x = float( distanceToOccluder == NRD_FP16_MAX );
    out2.yzw = saturate( translucency );

    // IN_SHADOWDATA
    float2 out1 = SIGMA_FrontEnd_PackShadow( viewZ, distanceToOccluder, tanOfLightAngularRadius );

    return out1;
}

// SIGMA multi-light ( experimental )

#define SIGMA_MULTILIGHT_DATATYPE float2x3

SIGMA_MULTILIGHT_DATATYPE SIGMA_FrontEnd_MultiLightStart()
{
    return ( SIGMA_MULTILIGHT_DATATYPE )0;
}

void SIGMA_FrontEnd_MultiLightUpdate( float3 L, float distanceToOccluder, float tanOfLightAngularRadius, float weight, inout SIGMA_MULTILIGHT_DATATYPE multiLightShadowData )
{
    float shadow = float( distanceToOccluder == NRD_FP16_MAX );
    float distanceToOccluderProj = SIGMA_FrontEnd_PackShadow( 0, distanceToOccluder, tanOfLightAngularRadius ).x;

    // Weighted sum for "pseudo" translucency
    multiLightShadowData[ 0 ] += L * shadow;

    // Weighted sum for distance to occluder (denoising will be driven by most important light)
    weight *= _NRD_Luminance( L );

    multiLightShadowData[ 1 ] += float3( distanceToOccluderProj * weight, weight, 0 );
}

// X => IN_SHADOWDATA and IN_SHADOW_TRANSLUCENCY
float2 SIGMA_FrontEnd_MultiLightEnd( float viewZ, SIGMA_MULTILIGHT_DATATYPE multiLightShadowData, float3 Lsum, out float4 out2 )
{
    // IN_SHADOW_TRANSLUCENCY
    out2.yzw = multiLightShadowData[ 0 ] / max( Lsum, NRD_EPS );
    out2.x = _NRD_Luminance( out2.yzw );

    // IN_SHADOWDATA
    float2 out1;
    out1.x = multiLightShadowData[ 1 ].x / max( multiLightShadowData[ 1 ].y, NRD_EPS );
    out1.y = _NRD_PackViewZ( viewZ );

    return out1;
}

//=================================================================================================================================
// BACK-END
//=================================================================================================================================

//========
// REBLUR
//========

// OUT_DIFF_RADIANCE_HITDIST => X
// OUT_SPEC_RADIANCE_HITDIST => X
float4 REBLUR_BackEnd_UnpackRadianceAndNormHitDist( float4 data )
{
    data.xyz = _NRD_YCoCgToLinear( data.xyz );

    return data;
}

// OUT_DIFF_SH0 and OUT_DIFF_SH1 => X
// OUT_SPEC_SH0 and OUT_SPEC_SH1 => X
NRD_SH REBLUR_BackEnd_UnpackSh( float4 sh0, float4 sh1 )
{
    NRD_SH sh;
    sh.c0_chroma = sh0.xyz;
    sh.c1 = sh1.xyz;
    sh.normHitDist = sh0.w;

    return sh;
}

// OUT_DIFF_DIRECTION_HITDIST => X
NRD_SH REBLUR_BackEnd_UnpackDirectionalOcclusion( float4 data )
{
    NRD_SH sh;
    sh.c0_chroma = float3( data.w, 0.0, 0.0 );
    sh.c1 = data.xyz;
    sh.normHitDist = NRD_SH_ExtractColor( sh ).x;

    return sh;
}

//========
// RELAX
//========

// OUT_DIFF_RADIANCE_HITDIST => X
// OUT_SPEC_RADIANCE_HITDIST => X
float4 RELAX_BackEnd_UnpackRadiance( float4 color )
{
    return color;
}

//========
// SIGMA
//========

// OUT_SHADOW_TRANSLUCENCY => X
//   SIGMA_SHADOW:
//      float shadowData = SIGMA_BackEnd_UnpackShadow( shadowData );
//      shadow = shadowData;
//   SIGMA_SHADOW_TRANSLUCENCY:
//      float4 shadowData = SIGMA_BackEnd_UnpackShadow( shadowData );
//      float3 finalShadowCommon = lerp( shadowData.yzw, 1.0, shadowData.x ); // or
//      float3 finalShadowExotic = shadowData.yzw * shadowData.x; // or
//      float3 finalShadowMoreExotic = shadowData.yzw;
#define SIGMA_BackEnd_UnpackShadow( color )  ( color * color )

//=================================================================================================================================
// MISC
//=================================================================================================================================

// Needs to be used to avoid summing up NAN/INF values in many rays per pixel scenarios
bool NRD_IsValidRadiance( float3 radiance )
{
    return any( isnan( radiance ) | isinf( radiance ) ) ? false : true;
}

// Scales normalized hit distance back to real length
float REBLUR_GetHitDist( float normHitDist, float viewZ, float4 hitDistParams, float roughness )
{
    float scale = _REBLUR_GetHitDistanceNormalization( viewZ, hitDistParams, roughness );

    return normHitDist * scale;
}

#endif

#endif
