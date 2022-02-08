/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "Poisson.hlsli"

#define NRD_COMMON_SAMPLERS \
    NRD_SAMPLER( SamplerState, gNearestClamp, s, 0 ) \
    NRD_SAMPLER( SamplerState, gNearestMirror, s, 1 ) \
    NRD_SAMPLER( SamplerState, gLinearClamp, s, 2 ) \
    NRD_SAMPLER( SamplerState, gLinearMirror, s, 3 )

// Constants

#define NRD_FRAME                                               0
#define NRD_PIXEL                                               1
#define NRD_RANDOM                                              2 // for experiments only

#define NRD_INF                                                 1e6

// FP16

#ifdef COMPILER_DXC
    #define half_float float16_t
    #define half_float2 float16_t2
    #define half_float3 float16_t3
    #define half_float4 float16_t4
#else
    #define half_float float
    #define half_float2 float2
    #define half_float3 float3
    #define half_float4 float4
#endif

//==================================================================================================================
// DEFAULT SETTINGS (can be modified)
//==================================================================================================================

#ifndef NRD_USE_SANITIZATION
    #define NRD_USE_SANITIZATION                                0 // bool
#endif

#ifndef NRD_USE_QUADRATIC_DISTRIBUTION
    #define NRD_USE_QUADRATIC_DISTRIBUTION                      0 // bool
#endif

#ifndef NRD_BILATERAL_WEIGHT_VIEWZ_SENSITIVITY
    #define NRD_BILATERAL_WEIGHT_VIEWZ_SENSITIVITY              100.0 // w = 1 / (1 + this * z)
#endif

#ifndef NRD_BILATERAL_WEIGHT_CUTOFF
    #define NRD_BILATERAL_WEIGHT_CUTOFF                         0.03 // normalized % // TODO: 1-2%?
#endif

#ifndef NRD_CATROM_SHARPNESS
    #define NRD_CATROM_SHARPNESS                                0.5 // [0; 1], 0.5 matches Catmull-Rom
#endif

#if( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_UNORM8 )
    #define NRD_ENCODING_ERRORS                                 float2( STL::Math::DegToRad( 1.2 ), 0.0025 )
#elif( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_OCT10 )
    #define NRD_ENCODING_ERRORS                                 float2( STL::Math::DegToRad( 0.4 ), 0.001 )
#else
    #define NRD_ENCODING_ERRORS                                 float2( STL::Math::DegToRad( 0 ), 0.0 )
#endif

//==================================================================================================================
// CTA & preloading
//==================================================================================================================

#ifdef NRD_CTA_8X8
    #define GROUP_X                                             8
    #define GROUP_Y                                             8
#else
    #define GROUP_X                                             16
    #define GROUP_Y                                             16
#endif

#ifdef NRD_USE_BORDER_2
    #define BORDER                                              2
#else
    #define BORDER                                              1
#endif

#define BUFFER_X                                                ( GROUP_X + BORDER * 2 )
#define BUFFER_Y                                                ( GROUP_Y + BORDER * 2 )

#define PRELOAD_INTO_SMEM \
    int2 groupBase = pixelPos - threadPos - BORDER; \
    uint stageNum = ( BUFFER_X * BUFFER_Y + GROUP_X * GROUP_Y - 1 ) / ( GROUP_X * GROUP_Y ); \
    [unroll] \
    for( uint stage = 0; stage < stageNum; stage++ ) \
    { \
        uint virtualIndex = threadIndex + stage * GROUP_X * GROUP_Y; \
        uint2 newId = uint2( virtualIndex % BUFFER_X, virtualIndex / BUFFER_Y ); \
        if( stage == 0 || virtualIndex < BUFFER_X * BUFFER_Y ) \
            Preload( newId, groupBase + newId ); \
    } \
    GroupMemoryBarrierWithGroupSync( )

//==================================================================================================================
// SHARED FUNCTIONS
//==================================================================================================================

// Misc

#if( NRD_USE_SANITIZATION == 1 )
    float4 Sanitize( float4 x, float4 replacer )
    {
        uint4 u = asuint( x );
        u &= 255 << 23;
        u = u == ( 255 << 23 );

        u.xy |= u.zw;
        u.x |= u.y;

        return u.x ? replacer : x;
    }

    float Sanitize( float x, float replacer )
    {
        uint u = asuint( x );
        u &= 255 << 23;
        u = u == ( 255 << 23 );

        return u ? replacer : x;
    }
#else
    #define Sanitize( x, replacer ) ( x )
#endif

// sigma = standard deviation, variance = sigma ^ 2
#define GetStdDev( m1, m2 ) sqrt( abs( m2 - m1 * m1 ) ) // sqrt( max( m2 - m1 * m1, 0.0 ) )

float PixelRadiusToWorld( float unproject, float isOrtho, float pixelRadius, float viewZ )
{
     return pixelRadius * unproject * lerp( viewZ, 1.0, abs( isOrtho ) );
}

float4 GetBlurKernelRotation( compiletime const uint mode, uint2 pixelPos, float4 baseRotator, uint frameIndex )
{
    float4 rotator = float4( 1, 0, 0, 1 );

    if( mode == NRD_PIXEL )
    {
        float angle = STL::Sequence::Bayer4x4( pixelPos, frameIndex );
        rotator = STL::Geometry::GetRotator( angle * STL::Math::Pi( 2.0 ) );
    }
    else if( mode == NRD_RANDOM )
    {
        STL::Rng::Initialize( pixelPos, frameIndex );
        float4 rnd = STL::Rng::GetFloat4( );
        rotator = STL::Geometry::GetRotator( rnd.z * STL::Math::Pi( 2.0 ) );
        rotator *= 1.0 + ( rnd.w * 2.0 - 1.0 ) * 0.5;
    }

    rotator = STL::Geometry::CombineRotators( baseRotator, rotator );

    return rotator;
}

float IsInScreen( float2 uv )
{
    return float( all( saturate( uv ) == uv ) );
}

float IsInScreen2x2( float2 uv, float2 screenSize )
{
    float2 offset = 0.5 / screenSize;

    return all( uv > offset ) && all( uv < 1.0 - offset );
}

float2 ApplyCheckerboard( inout float2 uv, uint mode, uint counter, float2 screenSize, float2 invScreenSize, uint frameIndex )
{
    int2 uvi = int2( uv * screenSize );
    bool hasData = STL::Sequence::CheckerBoard( uvi, frameIndex ) == mode;
    if( !hasData )
        uvi.y += ( ( counter & 0x1 ) == 0 ) ? -1 : 1;
    uv = ( float2( uvi ) + 0.5 ) * invScreenSize;

    return float2( uv.x * 0.5, uv.y );
}

bool CompareMaterials( uint m0, uint m, uint mask )
{
    return ( m0 & mask ) == ( m & mask );
}

// Kernel

float2 GetKernelSampleCoordinates( float4x4 mViewToClip, float3 offset, float3 Xv, float3 Tv, float3 Bv, float4 rotator = float4( 1, 0, 0, 1 ) )
{
    #if( NRD_USE_QUADRATIC_DISTRIBUTION == 1 )
        offset.xy *= offset.z;
    #endif

    // We can't rotate T and B instead, because T is skewed
    offset.xy = STL::Geometry::RotateVector( rotator, offset.xy );

    float3 p = Xv + Tv * offset.x + Bv * offset.y;
    float3 clip = STL::Geometry::ProjectiveTransform( mViewToClip, p ).xyw;
    clip.xy /= clip.z; // TODO: clip.z can't be 0, but what if a point is behind the near plane?
    clip.y = -clip.y;
    float2 uv = clip.xy * 0.5 + 0.5;

    return uv;
}

// Weight parameters

float2 GetGeometryWeightParams( float planeDistSensitivity, float frustumHeight, float3 Xv, float3 Nv, float scale = 1.0 )
{
    float a = scale / ( planeDistSensitivity * frustumHeight + 1e-6 );
    float b = -dot( Nv, Xv ) * a;

    return float2( a, b );
}

// Weights

#define _ComputeWeight( p, value ) STL::Math::SmoothStep01( 1.0 - abs( value * p.x + p.y ) )

float GetGeometryWeight( float2 params0, float3 n0, float3 p )
{
    float d = dot( n0, p );

    return _ComputeWeight( params0, d );
}

#define _GetBilateralWeight( z, zc ) \
    z = abs( z - zc ) * rcp( min( abs( z ), abs( zc ) ) + 0.001 ); \
    z = rcp( 1.0 + NRD_BILATERAL_WEIGHT_VIEWZ_SENSITIVITY * z ) * step( z, NRD_BILATERAL_WEIGHT_CUTOFF );

float GetBilateralWeight( float z, float zc )
{ _GetBilateralWeight( z, zc ); return z; }

float2 GetBilateralWeight( float2 z, float zc )
{ _GetBilateralWeight( z, zc ); return z; }

float4 GetBilateralWeight( float4 z, float zc )
{ _GetBilateralWeight( z, zc ); return z; }

// Upsampling

// TODO: uv4 can be used to get vanilla bilinear filter to be used for hit distance only sampling
#define _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init \
    /* Catmul-Rom with 12 taps ( excluding corners ) */ \
    float2 centerPos = floor( samplePos - 0.5 ) + 0.5; \
    float2 f = saturate( samplePos - centerPos ); \
    float2 f2 = f * f; \
    float2 f3 = f * f2; \
    float2 w0 = -NRD_CATROM_SHARPNESS * f3 + 2.0 * NRD_CATROM_SHARPNESS * f2 - NRD_CATROM_SHARPNESS * f; \
    float2 w1 = ( 2.0 - NRD_CATROM_SHARPNESS ) * f3 - ( 3.0 - NRD_CATROM_SHARPNESS ) * f2 + 1.0; \
    float2 w2 = -( 2.0 - NRD_CATROM_SHARPNESS ) * f3 + ( 3.0 - 2.0 * NRD_CATROM_SHARPNESS ) * f2 + NRD_CATROM_SHARPNESS * f; \
    float2 w3 = NRD_CATROM_SHARPNESS * f3 - NRD_CATROM_SHARPNESS * f2; \
    float2 w12 = w1 + w2; \
    float2 tc0 = -1.0; \
    float2 tc2 = w2 / w12; \
    float2 tc3 = 2.0; \
    float4 w; \
    w.x = w12.x * w0.y; \
    w.y = w0.x * w12.y; \
    w.z = w12.x * w12.y; \
    w.w = w3.x * w12.y; \
    float w4 = w12.x * w3.y; \
    /* Fallback to custom bilinear */ \
    w = useBicubic ? w : bilinearCustomWeights; \
    w4 = useBicubic ? w4 : 0.0001; \
    float invSum = 1.0 / ( dot( w, 1.0 ) + w4 ); \
    /* Texture coordinates */ \
    float2 uv0 = centerPos + ( useBicubic ? float2( tc2.x, tc0.y ) : float2( 0, 0 ) ); \
    float2 uv1 = centerPos + ( useBicubic ? float2( tc0.x, tc2.y ) : float2( 1, 0 ) ); \
    float2 uv2 = centerPos + ( useBicubic ? float2( tc2.x, tc2.y ) : float2( 0, 1 ) ); \
    float2 uv3 = centerPos + ( useBicubic ? float2( tc3.x, tc2.y ) : float2( 1, 1 ) ); \
    float2 uv4 = centerPos + ( useBicubic ? float2( tc2.x, tc3.y ) : float2( 0, 0 ) );

#define _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( color, tex ) \
    /* Sampling */ \
    color = tex.SampleLevel( linearSampler, uv0 * invTextureSize, 0 ) * w.x; \
    color += tex.SampleLevel( linearSampler, uv1 * invTextureSize, 0 ) * w.y; \
    color += tex.SampleLevel( linearSampler, uv2 * invTextureSize, 0 ) * w.z; \
    color += tex.SampleLevel( linearSampler, uv3 * invTextureSize, 0 ) * w.w; \
    color += tex.SampleLevel( linearSampler, uv4 * invTextureSize, 0 ) * w4; \
    color *= invSum; \
    /* Avoid negative values from CatRom, but doesn't suit for YCoCg or negative input */ \
    color = max( color, 0 )

float4 BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights( Texture2D<float4> tex0, SamplerState linearSampler, float2 samplePos, float2 invTextureSize, float4 bilinearCustomWeights, bool useBicubic )
{
    float4 color0;

    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( color0, tex0 );

    return color0;
}

float4 BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights( Texture2D<float4> tex0, Texture2D<float4> tex1, SamplerState linearSampler, float2 samplePos, float2 invTextureSize, float4 bilinearCustomWeights, bool useBicubic, out float4 color1 )
{
    float4 color0;

    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( color0, tex0 );
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( color1, tex1 );

    return color0;
}

float BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights( Texture2D<float> tex0, SamplerState linearSampler, float2 samplePos, float2 invTextureSize, float4 bilinearCustomWeights, bool useBicubic )
{
    float color0;

    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( color0, tex0 );

    return color0;
}

float BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights( Texture2D<float> tex0, Texture2D<float> tex1, SamplerState linearSampler, float2 samplePos, float2 invTextureSize, float4 bilinearCustomWeights, bool useBicubic, out float color1 )
{
    float color0;

    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( color0, tex0 );
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( color1, tex1 );

    return color0;
}

float4 BicubicFilterNoCorners( Texture2D<float4> tex, SamplerState linearSampler, float2 samplePos, float2 invTextureSize, compiletime const bool useBicubic = true )
{
    if( !useBicubic )
        return tex.SampleLevel( linearSampler, samplePos * invTextureSize, 0 );

    float4 color;
    float4 bilinearCustomWeights = 0;

    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( color, tex );

    return color;
}

float BicubicFilterNoCorners( Texture2D<float> tex, SamplerState linearSampler, float2 samplePos, float2 invTextureSize, compiletime const float useBicubic = true )
{
    if( !useBicubic )
        return tex.SampleLevel( linearSampler, samplePos * invTextureSize, 0 );

    float color;
    float4 bilinearCustomWeights = 0;

    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( color, tex );

    return color;
}
