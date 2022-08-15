/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "Poisson.hlsli"

// Constants

#define NRD_FRAME                                               0
#define NRD_PIXEL                                               1
#define NRD_RANDOM                                              2 // for experiments only

#define NRD_INF                                                 1e6

// FP16

#ifdef NRD_COMPILER_DXC
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

#define NRD_USE_QUADRATIC_DISTRIBUTION                          0 // bool
#define NRD_USE_EXPONENTIAL_WEIGHTS                             0 // bool
#define NRD_BILATERAL_WEIGHT_CUTOFF                             0.03
#define NRD_CATROM_SHARPNESS                                    0.5 // [ 0; 1 ], 0.5 matches Catmull-Rom
#define NRD_NORMAL_ENCODING_ERROR                               STL::Math::DegToRad( 0.5 )
#define NRD_PARALLAX_NORMALIZATION                              30.0
#define NRD_RADIANCE_COMPRESSION_MODE                           3 // 0-4, specular color compression for spatial passes
#define NRD_EXP_WEIGHT_DEFAULT_SCALE                            3.0

//==================================================================================================================
// CTA & preloading
//==================================================================================================================

#if 0 // CTA override
    #define GROUP_X                                             16
    #define GROUP_Y                                             16
#else
    #ifdef NRD_CTA_8X8
        #define GROUP_X                                         8
        #define GROUP_Y                                         8
    #else
        #define GROUP_X                                         16
        #define GROUP_Y                                         16
    #endif
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
        uint2 newId = uint2( virtualIndex % BUFFER_X, virtualIndex / BUFFER_X ); \
        if( stage == 0 || virtualIndex < BUFFER_X * BUFFER_Y ) \
            Preload( newId, groupBase + newId ); \
    } \
    GroupMemoryBarrierWithGroupSync( )

//==================================================================================================================
// SHARED FUNCTIONS
//==================================================================================================================

// Misc

// sigma = standard deviation, variance = sigma ^ 2
#define GetStdDev( m1, m2 )                     sqrt( abs( ( m2 ) - ( m1 ) * ( m1 ) ) ) // sqrt( max( m2 - m1 * m1, 0.0 ) )

#if( NRD_USE_MATERIAL_ID == 1 )
    #define CompareMaterials( m0, m, mask )     ( ( mask ) == 0 ? 1.0 : ( ( m0 ) == ( m ) ) )
#else
    #define CompareMaterials( m0, m, mask )     1.0
#endif

float PixelRadiusToWorld( float unproject, float orthoMode, float pixelRadius, float viewZ )
{
     return pixelRadius * unproject * lerp( viewZ, 1.0, abs( orthoMode ) );
}

float GetHitDistFactor( float hitDist, float frustumHeight, float scale = 1.0 )
{
    return saturate( hitDist / ( hitDist * scale + frustumHeight ) );
}

float4 GetBlurKernelRotation( compiletime const uint mode, uint2 pixelPos, float4 baseRotator, uint frameIndex )
{
    if( mode == NRD_PIXEL )
    {
        float angle = STL::Sequence::Bayer4x4( pixelPos, frameIndex );
        float4 rotator = STL::Geometry::GetRotator( angle * STL::Math::Pi( 2.0 ) );

        baseRotator = STL::Geometry::CombineRotators( baseRotator, rotator );
    }
    else if( mode == NRD_RANDOM )
    {
        STL::Rng::Initialize( pixelPos, frameIndex );

        float2 rnd = STL::Rng::GetFloat2( );
        float4 rotator = STL::Geometry::GetRotator( rnd.x * STL::Math::Pi( 2.0 ) );
        rotator *= 1.0 + ( rnd.y * 2.0 - 1.0 ) * 0.5;

        baseRotator = STL::Geometry::CombineRotators( baseRotator, rotator );
    }

    return baseRotator;
}

float IsInScreen( float2 uv )
{
    return float( all( saturate( uv ) == uv ) );
}

float2 ApplyCheckerboard( inout float2 uv, uint mode, uint counter, float2 screenSize, float2 invScreenSize, uint frameIndex )
{
    int2 uvi = int2( uv * screenSize );
    bool hasData = STL::Sequence::CheckerBoard( uvi, frameIndex ) == mode;
    if( !hasData )
        uvi.x += ( ( counter & 0x1 ) == 0 ) ? -1 : 1;
    uv = ( float2( uvi ) + 0.5 ) * invScreenSize;

    return float2( uv.x * 0.5, uv.y );
}

float GetSpecMagicCurve( float roughness, float power = 0.25 )
{
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIoMS0yXigtMjAwKngqeCkpKih4XjAuMDEpIiwiY29sb3IiOiIjMDAwMDAwIn0seyJ0eXBlIjowLCJlcSI6IigxLTJeKC0yMDAqeCp4KSkqKHheMC4xKSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiIoMS0yXigtMjAwKngqeCkpKih4XjAuMjUpIiwiY29sb3IiOiIjMDBGRjA5In0seyJ0eXBlIjowLCJlcSI6IigxLTJeKC0yMDAqeCp4KSkqKHheMC4zKSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiIoMS0yXigtMjAwKngqeCkpKih4XjAuNSkiLCJjb2xvciI6IiNENjIwMDAifSx7InR5cGUiOjAsImVxIjoiKDEtMl4oLTIwMCp4KngpKSooeF4wLjcpIiwiY29sb3IiOiIjMDAwMDAwIn0seyJ0eXBlIjowLCJlcSI6IigxLTJeKC0yMDAqeCp4KSkqKHheMC45KSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiIoMS0yXigtMjAwKngqeCkpKih4XjAuOTkpIiwiY29sb3IiOiIjMDAwMDAwIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiMS4xIl0sInNpemUiOlsxMDAwLDUwMF19XQ--

    float f = 1.0 - exp2( -200.0 * roughness * roughness );
    f *= STL::Math::Pow01( roughness, power );

    return f;
}

float GetSpecMagicCurve2( float roughness, float percentOfVolume = 0.987 )
{
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiJhdGFuKDAuOTkqeCp4LygxLTAuOTkpKS8oYXRhbigwLjk5LygxLTAuOTkpKSkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjAsImVxIjoiYXRhbigwLjk4Nyp4KngvKDEtMC45ODcpKS8oYXRhbigwLjk4Ny8oMS0wLjk4NykpKSIsImNvbG9yIjoiIzE5QkEwMCJ9LHsidHlwZSI6MCwiZXEiOiJhdGFuKDAuOTcqeCp4LygxLTAuOTcpKS8oYXRhbigwLjk3LygxLTAuOTcpKSkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjAsImVxIjoiYXRhbigwLjk1KngqeC8oMS0wLjk1KSkvKGF0YW4oMC45NS8oMS0wLjk1KSkpIiwiY29sb3IiOiIjMDAwMDAwIn0seyJ0eXBlIjowLCJlcSI6ImF0YW4oMC45KngqeC8oMS0wLjkpKS8oYXRhbigwLjkvKDEtMC45KSkpIiwiY29sb3IiOiIjMDAwMDAwIn0seyJ0eXBlIjowLCJlcSI6ImF0YW4oMC44NSp4KngvKDEtMC44NSkpLyhhdGFuKDAuODUvKDEtMC44NSkpKSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiJhdGFuKDAuOCp4KngvKDEtMC44KSkvKGF0YW4oMC44LygxLTAuOCkpKSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiJhdGFuKDAuNzUqeCp4LygxLTAuNzUpKS8oYXRhbigwLjc1LygxLTAuNzUpKSkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjAsImVxIjoiYXRhbigwLjcqeCp4LygxLTAuNykpLyhhdGFuKDAuNy8oMS0wLjcpKSkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIxLjEiXSwic2l6ZSI6WzEwMDAsNTAwXX1d
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIoMS0yXigtMjAwKngqeCkpKih4XjAuMjUpIiwiY29sb3IiOiIjMDAwMDAwIn0seyJ0eXBlIjowLCJlcSI6ImF0YW4oMC45ODcqeCp4LygxLTAuOTg3KSkvKGF0YW4oMC45ODcvKDEtMC45ODcpKSkiLCJjb2xvciI6IiMxREQ2MDAifSx7InR5cGUiOjAsImVxIjoiYXRhbigwLjk3KngqeC8oMS0wLjk3KSkvKGF0YW4oMC45Ny8oMS0wLjk3KSkpIiwiY29sb3IiOiIjQ0MzMDAwIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiMS4xIl0sInNpemUiOlsxMDAwLDUwMF19XQ--

    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness, percentOfVolume );
    float almostHalfPi = STL::ImportanceSampling::GetSpecularLobeHalfAngle( 1.0, percentOfVolume );

    return saturate( angle / almostHalfPi );
}

float ComputeParallax( float3 X, float2 uvForZeroParallax, float4x4 mWorldToClip, float2 rectSize, float unproject, float orthoMode )
{
    float3 clip = STL::Geometry::ProjectiveTransform( mWorldToClip, X ).xyw;
    clip.xy /= clip.z;
    clip.y = -clip.y;

    float2 uv = clip.xy * 0.5 + 0.5;
    float invDist = orthoMode == 0.0 ? rsqrt( STL::Math::LengthSquared( X ) ) : rcp( clip.z );

    float2 parallaxInUv = uv - uvForZeroParallax;
    float parallaxInPixels = length( parallaxInUv * rectSize );
    float parallaxInUnits = PixelRadiusToWorld( unproject, orthoMode, parallaxInPixels, clip.z );
    float parallax = parallaxInUnits * invDist;

    return parallax * NRD_PARALLAX_NORMALIZATION;
}

float GetParallaxInPixels( float parallax, float unproject )
{
    float smbParallaxInPixels = parallax / ( NRD_PARALLAX_NORMALIZATION * unproject );

    return smbParallaxInPixels;
}

float GetColorCompressionExposureForSpatialPasses( float roughness )
{
    // Prerequsites:
    // - to minimize biasing the results compression for high roughness should be avoided (diffuse signal compression can lead to darker image)
    // - the compression function must be monotonic for full roughness range
    // - returned exposure must be used with colors in the HDR range used in tonemapping, i.e. "color * exposure"

    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIwLjUvKDErNTAqeCkiLCJjb2xvciI6IiNGNzBBMEEifSx7InR5cGUiOjAsImVxIjoiMC41KigxLXgpLygxKzYwKngpIiwiY29sb3IiOiIjMkJGRjAwIn0seyJ0eXBlIjowLCJlcSI6IjAuNSooMS14KS8oMSsxMDAwKngqeCkrKDEteF4wLjUpKjAuMDMiLCJjb2xvciI6IiMwMDU1RkYifSx7InR5cGUiOjAsImVxIjoiMC42KigxLXgqeCkvKDErNDAwKngqeCkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIxIl0sInNpemUiOlsyOTUwLDk1MF19XQ--

    // Moderate compression
    #if( NRD_RADIANCE_COMPRESSION_MODE == 1 )
        return 0.5 / ( 1.0 + 50.0 * roughness );
    // Less compression for mid-high roughness
    #elif( NRD_RADIANCE_COMPRESSION_MODE == 2 )
        return 0.5 * ( 1.0 - roughness ) / ( 1.0 + 60.0 * roughness );
    // Close to the previous one, but offers more compression for low roughness
    #elif( NRD_RADIANCE_COMPRESSION_MODE == 3 )
        return 0.5 * ( 1.0 - roughness ) / ( 1.0 + 1000.0 * roughness * roughness ) + ( 1.0 - sqrt( saturate( roughness ) ) ) * 0.03;
    // A modification of the preious one ( simpler )
    #elif( NRD_RADIANCE_COMPRESSION_MODE == 4 )
        return 0.6 * ( 1.0 - roughness * roughness ) / ( 1.0 + 400.0 * roughness * roughness );
    // No compression
    #else
        return 0;
    #endif
}

// Thin lens

float EstimateCurvature( float3 Ni, float3 Vi, float3 N, float3 X )
{
    // https://computergraphics.stackexchange.com/questions/1718/what-is-the-simplest-way-to-compute-principal-curvature-for-a-mesh-triangle

    float NoV = dot( Vi, N );
    float3 Xi = 0 + Vi * dot( X - 0, N ) / NoV;
    float3 edge = Xi - X;
    float edgeLenSq = STL::Math::LengthSquared( edge );
    float curvature = dot( Ni - N, edge ) / edgeLenSq;

    return curvature;
}

float ApplyThinLensEquation( float NoV, float hitDist, float curvature )
{
    /*
    Why NoV?

    hitDist is not O, we need to find projection to the axis:
        O = hitDist * NoV

    hitDistFocused is not I, we need to reproject it back to the view direction:
        hitDistFocused = I / NoV

    Combine:
        I = 0.5 * O / ( 0.5 + C * O )
        hitDistFocused = [ 0.5 * NoV * hitDist / ( 0.5 + C * NoV * hitDist ) ] / NoV
    */

    float hitDistFocused = 0.5 * hitDist / ( 0.5 + curvature * NoV * hitDist );

    return hitDistFocused;
}

float3 GetXvirtual(
    float NoV, float hitDist, float curvature,
    float3 X, float3 Xprev, float3 V,
    float dominantFactor )
{
    /*
    C - curvature
    O - object distance
    I - image distance
    F - focal distance

    The lens equation:
        [Eq 1] 1 / O + 1 / I = 1 / F
        [Eq 2] For a spherical mirror F = -0.5 * R
        [Eq 3] R = 1 / C

    Find I from [Eq 1]:
        1 / I = 1 / F - 1 / O
        1 / I = ( O - F ) / ( F * O )
        I = F * O / ( O - F )

    Apply [Eq 2]:
        I = -0.5 * R * O / ( O + 0.5 * R )

    Apply [Eq 3]:
        I = ( -0.5 * O / C ) / ( O + 0.5 / C )
        I = ( -0.5 * O / C ) / ( ( O * C + 0.5 ) / C )
        I = ( -0.5 * O / C ) * ( C / ( O * C + 0.5 ) )
        I = -0.5 * O / ( 0.5 + C * O )

    Reverse sign because I is negative:
        I = 0.5 * O / ( 0.5 + C * O )
    */

    float hitDistFocused = ApplyThinLensEquation( NoV, hitDist, curvature );

    // "saturate" is needed to clamp values > 1 if curvature is negative
    float compressionRatio = saturate( ( abs( hitDistFocused ) + 1e-6 ) / ( hitDist + 1e-6 ) );

    float3 Xvirtual = lerp( Xprev, X, compressionRatio * dominantFactor ) - V * hitDistFocused * dominantFactor;

    return Xvirtual;
}

// Kernel

float2x3 GetKernelBasis( float3 D, float3 N, float NoD, float worldRadius, float roughness = 1.0, float anisoFade = 1.0 )
{
    float3x3 basis = STL::Geometry::GetBasis( N );

    float3 T = basis[ 0 ];
    float3 B = basis[ 1 ];

    if( roughness < 0.95 && NoD < 0.999 )
    {
        float3 R = reflect( -D, N );
        T = normalize( cross( N, R ) );
        B = cross( R, T );

        float skewFactor = lerp( roughness, 1.0, NoD );
        T *= lerp( skewFactor, 1.0, anisoFade );
    }

    T *= worldRadius;
    B *= worldRadius;

    return float2x3( T, B );
}

float2 GetKernelSampleCoordinates( float4x4 mToClip, float3 offset, float3 X, float3 T, float3 B, float4 rotator = float4( 1, 0, 0, 1 ) )
{
    #if( NRD_USE_QUADRATIC_DISTRIBUTION == 1 )
        offset.xy *= offset.z;
    #endif

    // We can't rotate T and B instead, because T is skewed
    offset.xy = STL::Geometry::RotateVector( rotator, offset.xy );

    float3 p = X + T * offset.x + B * offset.y;
    float3 clip = STL::Geometry::ProjectiveTransform( mToClip, p ).xyw;
    clip.xy /= clip.z;
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

float2 GetHitDistanceWeightParams( float hitDist, float nonLinearAccumSpeed, float roughness = 1.0 )
{
    float smc = GetSpecMagicCurve2( roughness );
    float norm = min( nonLinearAccumSpeed, smc * 0.97 + 0.03 );
    float a = 1.0 / norm;
    float b = hitDist * a;

    return float2( a, -b );
}

// Weights

// IMPORTANT:
// - works for "negative x" only
// - huge error for x < -2, but still applicable for "weight" calculations
// http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiJleHAoeCkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjAsImVxIjoiMS8oeCp4LXgrMSkiLCJjb2xvciI6IiMwRkIwMDAifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyItMTAiLCIwIiwiMCIsIjEiXX1d
// TODO: use for all weights? definitely a must for "noisy" data comparison when confidence is unclear
#define ExpApprox( x ) \
    rcp( ( x ) * ( x ) - ( x ) + 1.0 )

// Must be used for noisy data
// http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIxLWFicygoeC0wLjUpLzAuMikiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjAsImVxIjoiZXhwKC0yKmFicygoeC0wLjUpLzAuMikpIiwiY29sb3IiOiIjRkYwMDE1In0seyJ0eXBlIjowLCJlcSI6IjEvKCgtMyphYnMoKHgtMC41KS8wLjIpKV4yLSgtMyphYnMoKHgtMC41KS8wLjIpKSsxKSIsImNvbG9yIjoiIzAwQTgyNyJ9LHsidHlwZSI6MTAwMCwid2luZG93IjpbIjAiLCIxIiwiMCIsIjEiXX1d
// scale = 3-5 is needed to match energy in "_ComputeNonExponentialWeight" ( especially when used in a recurrent loop )
#define _ComputeExponentialWeight( x, px, py, scale ) \
    ExpApprox( -scale * abs( ( x ) * ( px ) + ( py ) ) )

// A good choice for non noisy data
#define _ComputeNonExponentialWeight( x, px, py ) \
    STL::Math::SmoothStep01( 1.0 - abs( ( x ) * ( px ) + ( py ) ) )

#if( NRD_USE_EXPONENTIAL_WEIGHTS == 1 )
    #define _ComputeWeight( x, px, py ) \
        _ComputeExponentialWeight( x, px, py, NRD_EXP_WEIGHT_DEFAULT_SCALE )
#else
    #define _ComputeWeight( x, px, py ) \
        _ComputeNonExponentialWeight( x, px, py )
#endif

float GetRoughnessWeight( float2 params, float roughness )
{
    return _ComputeWeight( roughness, params.x, params.y );
}

float GetHitDistanceWeight( float2 params, float hitDist )
{
    return _ComputeExponentialWeight( hitDist, params.x, params.y, NRD_EXP_WEIGHT_DEFAULT_SCALE );
}

float GetGeometryWeight( float2 params, float3 n0, float3 p )
{
    float d = dot( n0, p );

    return _ComputeWeight( d, params.x, params.y );
}

float GetNormalWeight( float param, float3 N, float3 n )
{
    float cosa = saturate( dot( N, n ) );
    float angle = STL::Math::AcosApprox( cosa );

    return _ComputeWeight( angle, param, 0.0 );
}

float GetGaussianWeight( float r )
{
    return exp( -0.66 * r * r ); // assuming r is normalized to 1
}

// Only for checkerboard resolve and some "lazy" comparisons

#define GetBilateralWeight( z, zc ) \
    STL::Math::LinearStep( NRD_BILATERAL_WEIGHT_CUTOFF, 0.0, abs( z - zc ) * rcp( max( abs( z ), abs( zc ) ) + 1e-6 ) )

// Upsampling

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
    float2 tc = w2 / w12; \
    float4 w; \
    w.x = w12.x * w0.y; \
    w.y = w0.x * w12.y; \
    w.z = w12.x * w12.y; \
    w.w = w3.x * w12.y; \
    float w4 = w12.x * w3.y; \
    /* Fallback to custom bilinear */ \
    w = useBicubic ? w : bilinearCustomWeights; \
    w4 = useBicubic ? w4 : 0.0; \
    float sum = dot( w, 1.0 ) + w4; \
    /* Texture coordinates */ \
    float2 uv0 = centerPos + ( useBicubic ? float2( tc.x, -1.0 ) : float2( 0, 0 ) ); \
    float2 uv1 = centerPos + ( useBicubic ? float2( -1.0, tc.y ) : float2( 1, 0 ) ); \
    float2 uv2 = centerPos + ( useBicubic ? float2( tc.x, tc.y ) : float2( 0, 1 ) ); \
    float2 uv3 = centerPos + ( useBicubic ? float2( 2.0, tc.y ) : float2( 1, 1 ) ); \
    float2 uv4 = centerPos + ( useBicubic ? float2( tc.x, 2.0 ) : f ); // can be used to get a free bilinear sample after some massaging

/*
IMPORTANT:
- 0 can be returned if only a single tap is valid from the 2x2 footprint and pure bilinear weights
  are close to 0 near this tap. The caller must handle this case manually. "Footprint quality"
  can be used to accelerate accumulation and avoid the problem.
- can return negative values
*/
#define _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( color, tex ) \
    /* Sampling */ \
    color = tex.SampleLevel( gLinearClamp, uv0 * invTextureSize, 0 ) * w.x; \
    color += tex.SampleLevel( gLinearClamp, uv1 * invTextureSize, 0 ) * w.y; \
    color += tex.SampleLevel( gLinearClamp, uv2 * invTextureSize, 0 ) * w.z; \
    color += tex.SampleLevel( gLinearClamp, uv3 * invTextureSize, 0 ) * w.w; \
    color += tex.SampleLevel( gLinearClamp, uv4 * invTextureSize, 0 ) * w4; \
    /* Normalize similarly to "STL::Filtering::ApplyBilinearCustomWeights()" */ \
    color = sum < 0.0001 ? 0 : color * rcp( sum );

#define _BilinearFilterWithCustomWeights_Color( color, tex ) \
    /* Sampling */ \
    color = tex.SampleLevel( gNearestClamp, centerPos * invTextureSize, 0 ) * bilinearCustomWeights.x; \
    color += tex.SampleLevel( gNearestClamp, centerPos * invTextureSize, 0, int2( 1, 0 ) ) * bilinearCustomWeights.y; \
    color += tex.SampleLevel( gNearestClamp, centerPos * invTextureSize, 0, int2( 0, 1 ) ) * bilinearCustomWeights.z; \
    color += tex.SampleLevel( gNearestClamp, centerPos * invTextureSize, 0, int2( 1, 1 ) ) * bilinearCustomWeights.w; \
    /* Normalize similarly to "STL::Filtering::ApplyBilinearCustomWeights()" */ \
    sum = dot( bilinearCustomWeights, 1.0 ); \
    color = sum < 0.0001 ? 0 : color * rcp( sum );
