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

#define NRD_NONE                                                0
#define NRD_FRAME                                               1
#define NRD_PIXEL                                               2
#define NRD_RANDOM                                              3 // for experiments only

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
// DEFAULT SETTINGS ( can be modified )
//==================================================================================================================

#define NRD_USE_QUADRATIC_DISTRIBUTION                          0 // bool
#define NRD_USE_EXPONENTIAL_WEIGHTS                             0 // bool
#define NRD_BILATERAL_WEIGHT_CUTOFF                             0.03
#define NRD_CATROM_SHARPNESS                                    0.5 // [ 0; 1 ], 0.5 matches Catmull-Rom
#define NRD_RADIANCE_COMPRESSION_MODE                           3 // 0-4, specular color compression for spatial passes
#define NRD_EXP_WEIGHT_DEFAULT_SCALE                            3.0

#if( NRD_NORMAL_ENCODING < NRD_NORMAL_ENCODING_R10G10B10A2_UNORM )
    #define NRD_NORMAL_ENCODING_ERROR                           ( 1.0 / 255.0 )
#elif( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_R10G10B10A2_UNORM )
    #define NRD_NORMAL_ENCODING_ERROR                           ( 1.0 / 1023.0 )
#else
    #define NRD_NORMAL_ENCODING_ERROR                           ( 1.0 / 65535.0 )
#endif

//==================================================================================================================
// CTA & preloading
//==================================================================================================================

#ifndef GROUP_X
    #define GROUP_X                                             16
#endif

#ifndef GROUP_Y
    #define GROUP_Y                                             16
#endif

#ifdef NRD_USE_BORDER_2
    #define BORDER                                              2
#else
    #define BORDER                                              1
#endif

#define BUFFER_X                                                ( GROUP_X + BORDER * 2 )
#define BUFFER_Y                                                ( GROUP_Y + BORDER * 2 )

#define PRELOAD_INTO_SMEM_WITH_TILE_CHECK \
    if( isSky == 0.0 ) \
    { \
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
    } \
    GroupMemoryBarrierWithGroupSync( ); \
    /* Not an elegant way to solve loop variables declaration duplication problem */ \
    int i, j

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
    GroupMemoryBarrierWithGroupSync( ); \
    /* Not an elegant way to solve loop variables declaration duplication problem */ \
    int i, j

//==================================================================================================================
// SHARED FUNCTIONS
//==================================================================================================================

// Misc

// sigma = standard deviation, variance = sigma ^ 2
#define GetStdDev( m1, m2 )                     sqrt( abs( ( m2 ) - ( m1 ) * ( m1 ) ) ) // sqrt( max( m2 - m1 * m1, 0.0 ) )

#if( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_R10G10B10A2_UNORM )
    #define CompareMaterials( m0, m, mask )     ( ( mask ) == 0 ? 1.0 : ( ( m0 ) == ( m ) ) )
#else
    #define CompareMaterials( m0, m, mask )     1.0
#endif

float PixelRadiusToWorld( float unproject, float orthoMode, float pixelRadius, float viewZ )
{
     return pixelRadius * unproject * lerp( viewZ, 1.0, abs( orthoMode ) );
}

float GetFrustumSize( float minRectDimMulUnproject, float orthoMode, float viewZ )
{
    // TODO: let's assume that all NRD instances are independent, i.e. there is no a need to reach
    // "frustumSize" parity between several instances. For example: 3-monitor setup, side monitors
    // are rotated, i.e. width < height
    return minRectDimMulUnproject * lerp( viewZ, 1.0, abs( orthoMode ) );
}

float GetHitDistFactor( float hitDist, float frustumSize )
{
    return saturate( hitDist / frustumSize );
}

float4 GetBlurKernelRotation( compiletime const uint mode, uint2 pixelPos, float4 baseRotator, uint frameIndex )
{
    if( mode == NRD_NONE )
        return STL::Geometry::GetRotator( 0.0 );
    else if( mode == NRD_PIXEL )
    {
        float angle = STL::Sequence::Bayer4x4( pixelPos, frameIndex );
        float4 rotator = STL::Geometry::GetRotator( angle * STL::Math::Pi( 2.0 ) );

        baseRotator = STL::Geometry::CombineRotators( baseRotator, rotator );
    }
    else if( mode == NRD_RANDOM )
    {
        STL::Rng::Hash::Initialize( pixelPos, frameIndex );

        float2 rnd = STL::Rng::Hash::GetFloat2( );
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

float2 ApplyCheckerboardShift( float2 uv, uint mode, uint counter, float2 screenSize, float2 invScreenSize, uint frameIndex )
{
    int2 uvi = int2( uv * screenSize );
    bool hasData = STL::Sequence::CheckerBoard( uvi, frameIndex ) == mode;
    if( !hasData )
        uvi.x += ( ( counter & 0x1 ) == 0 ) ? -1 : 1;

    return ( float2( uvi ) + 0.5 ) * invScreenSize;
}

// Comparison of two methods:
// https://www.desmos.com/calculator/xwq1nrawho
float GetSpecMagicCurve( float roughness, float power = 0.25 )
{
    float f = 1.0 - exp2( -200.0 * roughness * roughness );
    f *= STL::Math::Pow01( roughness, power );

    return f;
}

float GetSpecMagicCurve2( float roughness, float percentOfVolume = 0.987 )
{
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness, percentOfVolume );
    float almostHalfPi = STL::ImportanceSampling::GetSpecularLobeHalfAngle( 1.0, percentOfVolume );

    return saturate( angle / almostHalfPi );
}

/*
Produce same results:
    ComputeParallaxInPixels( Xprev - gCameraDelta, gOrthoMode == 0.0 ? pixelUv : smbPixelUv, gWorldToClip, gRectSize );
    ComputeParallaxInPixels( Xprev + gCameraDelta, gOrthoMode == 0.0 ? smbPixelUv : pixelUv, gWorldToClipPrev, gRectSize );
*/
float ComputeParallaxInPixels( float3 X, float2 uvForZeroParallax, float4x4 mWorldToClip, float2 rectSize )
{
    float2 uv = STL::Geometry::GetScreenUv( mWorldToClip, X );
    float2 parallaxInUv = uv - uvForZeroParallax;
    float parallaxInPixels = length( parallaxInUv * rectSize );

    return parallaxInPixels;
}

float GetColorCompressionExposureForSpatialPasses( float roughness )
{
    // Prerequsites:
    // - to minimize biasing the results compression for high roughness should be avoided (diffuse signal compression can lead to darker image)
    // - the compression function must be monotonic for full roughness range
    // - returned exposure must be used with colors in the HDR range used in tonemapping, i.e. "color * exposure"

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

float EstimateCurvature( float3 Nplane, float3 n, float3 v, float3 N, float3 X )
{
    // https://computergraphics.stackexchange.com/questions/1718/what-is-the-simplest-way-to-compute-principal-curvature-for-a-mesh-triangle

    float NoV = dot( Nplane, v );
    float3 x = 0 + v * dot( X - 0, Nplane ) / NoV;
    float3 edge = x - X;
    float edgeLenSq = STL::Math::LengthSquared( edge );
    float curvature = dot( n - N, edge ) * STL::Math::PositiveRcp( edgeLenSq );

    return curvature;
}

float ApplyThinLensEquation( float NoV, float hitDist, float curvature )
{
    /*
    https://www.geeksforgeeks.org/sign-convention-for-spherical-mirrors/

    Thin lens equation:
        1 / F = 1 / O + 1 / I
        F = R / 2 - focal distance
        C = 1 / R - curvature

    Sign convention:
        convex  : O(-), I(+), C(+)
        concave : O(-), I(+ or -), C(-)

    Why NoV?
        hitDist is not O, we need to find projection to the axis:
            O = hitDist * NoV
        hitDistFocused is not I, we need to reproject it back to the view direction:
            hitDistFocused = I / NoV

    Combine:
        2C = 1 / O + 1 / I
        1 / I = 2C - 1 / O
        1 / I = ( 2CO - 1 ) / O
        I = O / ( 2CO - 1 )

        I = [ ( O * NoV ) / ( 2CO * NoV - 1 ) ] / NoV
        I = O / ( 2CO * NoV - 1 )

    O is always negative, while hit distance is always positive:
        I = -O / ( -2CO * NoV - 1 )
        I = O / ( 2CO * NoV + 1 )

    Interactive graph:
        https://www.desmos.com/calculator/dn9spdgwiz
    */

    // TODO: dropping NoV improves behavior on curved surfaces in general ( see i76, i148, b7, b22, b26 ), but test i133
    // ( low curvature surface observed at grazing angle ) looks significantly worse, especially if motion is accelerated
    float hitDistFocused = hitDist / ( 2.0 * curvature * hitDist * NoV + 1.0 );

    return hitDistFocused;
}

float3 GetXvirtual( float NoV, float hitDist, float curvature, float3 X, float3 Xprev, float3 V, float dominantFactor )
{
    float hitDistFocused = ApplyThinLensEquation( NoV, hitDist, curvature );

    // Only hit distance is provided, not real motion in the virtual world. If the virtual position is close to the
    // surface due to focusing, better to replace current position with previous position because surface motion is known.
    float closenessToSurface = saturate( abs( hitDistFocused ) / ( hitDist + NRD_EPS ) );

    return lerp( Xprev, X, closenessToSurface * dominantFactor ) - V * hitDistFocused * dominantFactor;
}

// Kernel

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

float2 GetGeometryWeightParams( float planeDistSensitivity, float frustumSize, float3 Xv, float3 Nv, float nonLinearAccumSpeed )
{
    float relaxation = lerp( 1.0, 0.25, nonLinearAccumSpeed );
    float a = relaxation / ( planeDistSensitivity * frustumSize );
    float b = -dot( Nv, Xv ) * a;

    return float2( a, b );
}

float2 GetHitDistanceWeightParams( float hitDist, float nonLinearAccumSpeed, float roughness = 1.0 )
{
    // IMPORTANT: since this weight is exponential, 3% can lead to leaks from bright objects in reflections.
    // Even 1% is not enough in some cases, but using a lower value makes things even more fragile
    float smc = GetSpecMagicCurve2( roughness );
    float norm = lerp( NRD_EPS, 1.0, min( nonLinearAccumSpeed, smc ) );
    float a = 1.0 / norm;
    float b = hitDist * a;

    return float2( a, -b );
}

// Weights params

float2 GetRoughnessWeightParams( float roughness, float fraction )
{
    float a = rcp( lerp( 0.01, 1.0, saturate( roughness * fraction ) ) );
    float b = roughness * a;

    return float2( a, -b );
}

float2 GetRoughnessWeightParamsSq( float roughness, float fraction )
{
    return GetRoughnessWeightParams( roughness * roughness, fraction );
}

float2 GetCoarseRoughnessWeightParams( float roughness )
{
    return float2( 1.0, -roughness );
}

// Weights

// IMPORTANT:
// - works for "negative x" only
// - huge error for x < -2, but still applicable for "weight" calculations
// https://www.desmos.com/calculator/cd3mvg1gfo
#define ExpApprox( x ) \
    rcp( ( x ) * ( x ) - ( x ) + 1.0 )

// Must be used for noisy data
// https://www.desmos.com/calculator/9yoyc3is2g
// scale = 3-5 is needed to match energy in "_ComputeNonExponentialWeight" ( especially when used in a recurrent loop )
#define _ComputeExponentialWeight( x, px, py ) \
    ExpApprox( -NRD_EXP_WEIGHT_DEFAULT_SCALE * abs( ( x ) * ( px ) + ( py ) ) )

// A good choice for non noisy data
// IMPORTANT: cutoffs are needed to minimize floating point precision drifting
#define _ComputeNonExponentialWeight( x, px, py ) \
    STL::Math::SmoothStep( 0.999, 0.001, abs( ( x ) * ( px ) + ( py ) ) )

#if( NRD_USE_EXPONENTIAL_WEIGHTS == 1 )
    #define _ComputeWeight( x, px, py ) \
        _ComputeExponentialWeight( x, px, py )
#else
    #define _ComputeWeight( x, px, py ) \
        _ComputeNonExponentialWeight( x, px, py )
#endif

float GetRoughnessWeight( float2 params, float roughness )
{
    return _ComputeWeight( roughness, params.x, params.y );
}

float GetRoughnessWeightSq( float2 params, float roughness )
{
    return GetRoughnessWeight( params, roughness * roughness );
}

float GetHitDistanceWeight( float2 params, float hitDist )
{
    return _ComputeExponentialWeight( hitDist, params.x, params.y );
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
    STL::Math::LinearStep( NRD_BILATERAL_WEIGHT_CUTOFF, 0.0, abs( z - zc ) * rcp( max( abs( z ), abs( zc ) ) ) )

// Upsampling

// TODO: used only for history resampling. This is why "gRectSizePrev" is hardcoded
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
    float2 uv0 = min( centerPos + ( useBicubic ? float2( tc.x, -1.0 ) : float2( 0, 0 ) ), gRectSizePrev - 1.0 ); \
    float2 uv1 = min( centerPos + ( useBicubic ? float2( -1.0, tc.y ) : float2( 1, 0 ) ), gRectSizePrev - 1.0 ); \
    float2 uv2 = min( centerPos + ( useBicubic ? float2( tc.x, tc.y ) : float2( 0, 1 ) ), gRectSizePrev - 1.0 ); \
    float2 uv3 = min( centerPos + ( useBicubic ? float2( 2.0, tc.y ) : float2( 1, 1 ) ), gRectSizePrev - 1.0 ); \
    float2 uv4 = min( centerPos + ( useBicubic ? float2( tc.x, 2.0 ) : f ), gRectSizePrev - 1.0 ); \
    float4 bilinearTaps

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
    bilinearTaps = centerPos.xyxy + float4( 0.0, 0.0, 1.0, 1.0 ); \
    bilinearTaps.zw = min( bilinearTaps.zw, gRectSizePrev - 1.0 ); \
    color = tex.SampleLevel( gNearestClamp, bilinearTaps.xy * invTextureSize, 0 ) * bilinearCustomWeights.x; \
    color += tex.SampleLevel( gNearestClamp, bilinearTaps.zy * invTextureSize, 0 ) * bilinearCustomWeights.y; \
    color += tex.SampleLevel( gNearestClamp, bilinearTaps.xw * invTextureSize, 0 ) * bilinearCustomWeights.z; \
    color += tex.SampleLevel( gNearestClamp, bilinearTaps.zw * invTextureSize, 0 ) * bilinearCustomWeights.w; \
    /* Normalize similarly to "STL::Filtering::ApplyBilinearCustomWeights()" */ \
    sum = dot( bilinearCustomWeights, 1.0 ); \
    color = sum < 0.0001 ? 0 : color * rcp( sum );
