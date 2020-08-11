/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsl"
#include "STL.hlsl"
#include "Poisson.hlsl"

NRI_RESOURCE( SamplerState, gNearestClamp, s, 0, 0 );
NRI_RESOURCE( SamplerState, gNearestMirror, s, 1, 0 );
NRI_RESOURCE( SamplerState, gLinearClamp, s, 2, 0 );
NRI_RESOURCE( SamplerState, gLinearMirror, s, 3, 0 );

// Constants

#define NONE                                    0
#define FRAME                                   1
#define PIXEL                                   2
#define MAX_ACCUM_FRAME_NUM                     31 // "2 ^ N - 1"
#define CHECKERBOARD_OFF                        2

// Debug

#define SHOW_MIPS                               0 // 1 - linear, 2 - linear-bilateral
#define SHOW_ACCUM_SPEED                        0
#define SHOW_ANTILAG                            0

// Booleans

#define CHECKERBOARD_SUPPORT                    1 // +0.06 ms on RTX 2080 @ 1440p - permutations are not needed
#define BLACK_OUT_INF_PIXELS                    0 // can be used to avoid killing INF pixels during composition
#define USE_QUADRATIC_DISTRIBUTION              0
#define USE_PSEUDO_FLAT_NORMALS                 1
#define USE_SHADOW_BLUR_RADIUS_FIX              1
#define USE_BILATERAL_WEIGHT_CUTOFF_FOR_DIFF    0
#define USE_BILATERAL_WEIGHT_CUTOFF_FOR_SPEC    1
#define USE_HISTORY_FIX                         1
#define USE_MIX_WITH_ORIGINAL                   1
#define USE_NORMAL_WEIGHT_IN_DIFF_PRE_BLUR      0
#define USE_ANTILAG                             1
#define USE_CATMULLROM_RESAMPLING_IN_TA         1 // TODO: adds 0.3 ms on RTX 2080 @ 1440p
#define USE_CATMULLROM_RESAMPLING_IN_TS         1 // adds 0.1 ms on RTX 2080 @ 1440p
#define USE_SQRT_ROUGHNESS                      1

// Settings

#define DIFF_POISSON_SAMPLE_NUM                 8
#define DIFF_POISSON_SAMPLES                    g_Poisson8
#define DIFF_PRE_BLUR_RADIUS_SCALE              0.5 // previously was 0.25
#define DIFF_PRE_BLUR_ROTATOR_MODE              FRAME
#define DIFF_BLUR_ROTATOR_MODE                  PIXEL
#define DIFF_POST_BLUR_ROTATOR_MODE             NONE

#define SPEC_POISSON_SAMPLE_NUM                 8
#define SPEC_POISSON_SAMPLES                    g_Poisson8
#define SPEC_PRE_BLUR_RADIUS_SCALE              1.0 // previously was 0.5
#define SPEC_PRE_BLUR_ROTATOR_MODE              FRAME
#define SPEC_BLUR_ROTATOR_MODE                  PIXEL
#define SPEC_POST_BLUR_ROTATOR_MODE             NONE
#define SPEC_ACCUM_BASE_POWER                   0.5 // previously was 0.66 (less agressive accumulation, but virtual reprojection works well on flat surfaces and fixes the issue)
#define SPEC_ACCUM_CURVE                        1.0 // aggressiveness of history rejection depending on viewing angle (1 - low, 0.66 - medium, 0.5 - high)

#define SHADOW_POISSON_SAMPLE_NUM               8
#define SHADOW_POISSON_SAMPLES                  g_Poisson8
#define SHADOW_MAX_PIXEL_RADIUS                 32.0
#define SHADOW_PLANE_DISTANCE_SCALE             0.25

#define ANTILAG_MIN                             0.02
#define ANTILAG_MAX                             0.1
#define TS_MOTION_MAX_REUSE                     0.11
#define TS_HISTORY_SHARPNESS                    0.5 // [0; 1], 0.5 matches Catmull-Rom
#define TS_SIGMA_AMPLITUDE                      9.0 // previously was 3.0
#define TS_MAX_HISTORY_WEIGHT                   ( 63.0 / 64.0 ) // previously was 0.9
#define DITHERING_AMPLITUDE                     0.005
#define MIP_NUM                                 4.999
#define PLANE_DIST_SENSITIVITY                  0.002 // m
#define ALMOST_ZERO_ACCUM_FRAME_NUM             0.01
#define HISTORY_FIX_FRAME_NUM_PERCENTAGE        ( 4.0 / 32.0 ) // 4, 8, 12...
#define MIN_HITDIST_ACCUM_SPEED                 0.1
#define HIT_DIST_INPUT_MIX                      0.0
#define BILATERAL_WEIGHT_VIEWZ_SENSITIVITY      500.0
#define BILATERAL_WEIGHT_CUTOFF                 0.05 // normalized %
#define MAX_UNROLLED_SAMPLES                    32

// CTA size

#define BORDER                                  1 // max radius of blur used for shared memory
#define GROUP_X                                 16
#define GROUP_Y                                 16
#define BUFFER_X                                ( GROUP_X + BORDER * 2 )
#define BUFFER_Y                                ( GROUP_Y + BORDER * 2 )
#define RENAMED_GROUP_Y                         ( ( GROUP_X * GROUP_Y ) / BUFFER_X )

// Misc

float4 UnpackNormalAndRoughness( float4 p, bool bNormalize = true )
{
    p.xyz = p.xyz * 2.0 - 1.0;

    if( bNormalize )
        p.xyz *= STL::Math::Rsqrt( STL::Math::LengthSquared( p.xyz ) );

    #if( USE_SQRT_ROUGHNESS == 1 )
        p.w *= p.w;
    #endif

    return p;
}

#define UnpackViewZ( p ) asfloat( p & ~MAX_ACCUM_FRAME_NUM )
#define UnpackAccumSpeed( p ) ( p & MAX_ACCUM_FRAME_NUM )

uint PackViewZAndAccumSpeed( float z, float accumSpeed )
{
    uint p = asuint( z );
    p &= ~MAX_ACCUM_FRAME_NUM;
    p |= uint( accumSpeed + 0.5 );

    return p;
}

float GetHitDistance( float normHitDist, float viewZ, float4 scalingParams, float roughness = 1.0 )
{
    return normHitDist * ( scalingParams.x + scalingParams.y * abs( viewZ ) ) * lerp( scalingParams.z, 1.0, exp2( -roughness * roughness * scalingParams.w ) );
}

float PixelRadiusToWorld( float pixelRadius, float centerZ, float unproject, float isOrtho )
{
     return pixelRadius * unproject * lerp( abs( centerZ ), 1.0, abs( isOrtho ) );
}

// Accumulation speed (accumSpeeds.y is not packed since it's a known constant)

uint PackDiffInternalData( float3 accumSpeeds, float reprojectedHistoryLuma )
{
    uint p = STL::Packing::RgbaToUint( saturate( accumSpeeds.xzzz / MAX_ACCUM_FRAME_NUM ), 8, 8 );
    p |= f32tof16( reprojectedHistoryLuma ) << 16;

    return p;
}

float4 UnpackDiffInternalData( uint p )
{
    float4 r;
    r.xz = STL::Packing::UintToRgba( p & 0xFFFF, 8, 8 ).xy * MAX_ACCUM_FRAME_NUM;
    r.y = MAX_ACCUM_FRAME_NUM;
    r.w = f16tof32( p >> 16 );

    return r;
}

// http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIzMSooeF4wLjY2KSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiIzMSooMS0yXigtMjAwKngqeCkpKih4XjAuNSkiLCJjb2xvciI6IiNGQTBEMEQifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIzMSJdfV0-
#define GetSpecAccumulatedFrameNum( roughness, powerScale ) ( MAX_ACCUM_FRAME_NUM * ( 1.0 - exp2( -200.0 * roughness * roughness ) ) * STL::Math::Pow01( roughness, SPEC_ACCUM_BASE_POWER * powerScale ) )

float4 PackSpecInternalData( float3 accumSpeeds, float virtualHistoryAmount, float parallax )
{
    float4 r;
    r.xy = saturate( accumSpeeds.xz / MAX_ACCUM_FRAME_NUM );
    r.z = virtualHistoryAmount;
    r.w = parallax;

    return r;
}

float3 UnpackSpecInternalData( float4 p, float roughness )
{
    float3 r;
    r.xz = MAX_ACCUM_FRAME_NUM * p.xy;
    r.y = GetSpecAccumulatedFrameNum( roughness, 1.0 );

    return r;
}

float2 GetSpecAccumSpeed( float maxAccumSpeed, float roughness, float NoV, float parallax )
{
    float acos01sq = saturate( 1.0 - NoV ); // see AcosApprox()

    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIoMS4wNSsoeCp4KV4xLjApLygxLjA1LSh4KngpXjEuMCkiLCJjb2xvciI6IiM1MkExMDgifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC42NikvKDEuMDUtKHgqeCleMC42NikiLCJjb2xvciI6IiNFM0Q4MDkifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC41KS8oMS4wNS0oeCp4KV4wLjUpIiwiY29sb3IiOiIjRjUwQTMxIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiNDIiXSwic2l6ZSI6WzE5MDAsNzAwXX1d
    float a = STL::Math::Pow01( acos01sq, SPEC_ACCUM_CURVE );
    float b = 1.05 + roughness * roughness;
    float parallaxSensitivity = ( b + a ) / ( b - a );
    parallaxSensitivity *= 1.0 - roughness; // TODO: is it a hack?

    float2 powerScale = float2( 1.0 + parallax * parallaxSensitivity, 1.0 );
    float2 accumSpeed = GetSpecAccumulatedFrameNum( roughness, powerScale );

    accumSpeed.x = min( accumSpeed.x, maxAccumSpeed );

    return accumSpeed;
}

// Kernel

float GetBlurRadius( float radius, float roughness, float hitDist, float3 Xv, float nonLinearAccumSpeed )
{
    // Base radius
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIxLjAtMl4oLTE1LjAqeCkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjAsImVxIjoiKDEtMl4oLTIwMCp4KngpKSooeF4wLjI1KSIsImNvbG9yIjoiIzIyRUQxNyJ9LHsidHlwZSI6MTAwMCwid2luZG93IjpbIjAiLCIxIiwiMCIsIjEuMSJdLCJzaXplIjpbMTAwMCw1MDBdfV0-
    float t = STL::Math::Pow01( roughness, 0.25 );
    float s = ( 1.0 - exp2( -200.0 * roughness * roughness ) ) * t; // very similar to the function used in GetSpecAccumulatedFrameNum()

    // Modify by hit distance
    float a = lerp( 0.25 * roughness * roughness, 0.5 * t * t, nonLinearAccumSpeed );
    float hitDistFactor = hitDist / ( hitDist + abs( Xv.z ) );
    s *= lerp( a, 1.0, hitDistFactor );

    // Scale down if accumulation goes well
    s *= lerp( 0.1, 1.0, nonLinearAccumSpeed );

    return s * radius;
}

float GetTrimmingFactor( float roughness, float3 trimmingParams )
{
    return STL::Math::SmoothStep( trimmingParams.y, trimmingParams.z, roughness );
}

float GetBlurRadiusScaleBasingOnTrimming( float roughness, float3 trimmingParams )
{
    float trimmingFactor = trimmingParams.x * GetTrimmingFactor( roughness, trimmingParams );
    float maxScale = 1.0 + 4.0 * roughness * roughness;
    float scale = lerp( maxScale, 1.0, trimmingFactor );

    // TODO: for roughness ~0.2 and trimming = 0 blur radius will be so large and amount of accumulation will be so small that a strobbing effect can appear under motion
    return scale;
}

void GetKernelBasis( float3 Xv, float3 Nv, float roughness, float worldRadius, float normAccumSpeed, uint anisotropicFiltering, out float3 Tv, out float3 Bv )
{
    roughness = lerp( 1.0, roughness, STL::Math::Sqrt01( normAccumSpeed ) ); // TODO: it was a useful experiment - keep it? It helps "a bit" for glancing angles

    if( roughness < 0.95 )
    {
        float3 Vv = -normalize( Xv );
        float3 Dv = STL::ImportanceSampling::GetSpecularDominantDirection( Nv, Vv, roughness );
        float3 Rv = reflect( -Dv, Nv );

        Tv = normalize( cross( Nv, Rv ) );
        Bv = cross( Rv, Tv );
    }
    else
    {
        float3x3 basis = STL::Geometry::GetBasis( Nv );
        Tv = basis[ 0 ];
        Bv = basis[ 1 ];
    }

    Tv *= worldRadius;
    Bv *= worldRadius;

    if( anisotropicFiltering )
    {
        float acos01sq = saturate( 1.0 - abs( Nv.z ) ); // see AcosApprox()
        float skewFactor = lerp( 1.0, roughness, STL::Math::Sqrt01( acos01sq ) );
        Tv *= lerp( 1.0, skewFactor, normAccumSpeed );
    }
}

float2 GetKernelSampleCoordinates( float4x4 mViewToClip, float2 jitter, float3 offset, float3 Xv, float3 Tv, float3 Bv, float4 rotator = float4( 1, 0, 0, 1 ) )
{
    #if( USE_QUADRATIC_DISTRIBUTION == 1 )
        offset.xy *= offset.z;
    #endif

    // We can't rotate T and B instead, because T is skewed
    offset.xy = STL::Geometry::RotateVector( rotator, offset.xy );

    float3 p = Xv + Tv * offset.x + Bv * offset.y;
    float3 clip = mul( mViewToClip, float4( p, 1.0 ) ).xyw;
    clip.xy /= clip.z;
    clip.y = -clip.y;
    float2 uv = clip.xy * 0.5 + 0.5 - jitter;

    return uv;
}

// Weight parameters

float2 GetNormalWeightParams( float roughness, float accumSpeed = MAX_ACCUM_FRAME_NUM, float normAccumSpeed = 1.0 )
{
    // Fade out normal weights if less than 25% of allowed frames are accumulated
    float k = STL::Math::LinearStep( 0.0, 0.25, normAccumSpeed );

    // Remap to avoid killing normal weights completely
    float remap = 0.1 * k + 0.9;

    // Ignore normal weights if ~0 frames are accumulated (not "allowed" frames, it's needed for better tracking of discarded regions)
    float f = lerp( k, remap, saturate( accumSpeed / ALMOST_ZERO_ACCUM_FRAME_NUM ) );

    // This is the main parameter - cone angle
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
    angle *= 0.5; // to avoid overblurring // TODO: 0.333? Search for all 0.333 and create a macro?
    angle *= 1.0 - 0.66 * STL::Math::LinearStep( 0.25, 1.0, normAccumSpeed );

    // Mitigate banding introduced by normals stored in RGB8 format
    angle += STL::Math::DegToRad( 2.5 );

    return float2( angle, f );
}

float2 GetRoughnessWeightParams( float roughness0 )
{
    float a = rcp( roughness0 * 0.333 * 0.99 + 0.01 );
    float b = roughness0 * a;

    return float2( a, b );
}

float2 GetHitDistanceWeightParams( float roughness0, float hitDist0 )
{
    float a = rcp( hitDist0 * 0.9999 + 0.0001 );
    float b = hitDist0 * a;
    float fade = 1.0 - STL::Math::SmoothStep( 0.5, 1.0, roughness0 );

    return float2( a, b ) * fade;
}

float GetGeometryWeightParams( float metersToUnits, float centerZ )
{
    // Returns - 1 / "max possible distance between two parallel planes where samples can be shared"

    // TODO: can depend on:
    // - normAccumSpeed (0 can't be used!)
    // - can be different for post- and pre- blurs
    // ...but it looks like "no magic" works well and improves the quality of upsampling
    float scale = 1.0;

    return scale / ( PLANE_DIST_SENSITIVITY * ( metersToUnits + abs( centerZ ) ) );
}

float2 GetTemporalAccumulationParams( float isInScreen, float accumSpeed, float motionLength, float parallax = 0.0, float roughness = 1.0 )
{
    float nonLinearAccumSpeed = 1.0 / ( 1.0 + accumSpeed );
    float roughnessWeight = STL::Math::SmoothStep( 0.0, 0.75, roughness );

    float sigmaAmplitude = 1.0 + TS_SIGMA_AMPLITUDE * roughnessWeight * ( 1.0 - nonLinearAccumSpeed );

    float historyWeight = isInScreen;
    historyWeight *= 1.0 - STL::Math::SmoothStep( 0.0, TS_MOTION_MAX_REUSE, motionLength );
    historyWeight *= 1.0 - nonLinearAccumSpeed;
    historyWeight *= lerp( saturate( 1.0 - parallax ), 1.0, roughnessWeight );

    return float2( historyWeight, sigmaAmplitude );
}

// Weights

float GetNormalWeight( float2 params0, float3 n0, float3 n )
{
    // Assuming that "n0" is normalized and "n" is not!
    float cosa = saturate( dot( n0, n ) * STL::Math::Rsqrt( STL::Math::LengthSquared( n ) ) );
    float a = STL::Math::AcosApprox( cosa );
    a = 1.0 - STL::Math::SmoothStep( 0.0, params0.x, a );

    return saturate( a * params0.y + 1.0 - params0.y );
}

float GetGeometryWeight( float3 p0, float3 n0, float3 p, float geometryWeightParams )
{
    // We want to "blur" in tangent plane ( more or less )
    float3 ray = p - p0;
    float distToPlane = dot( n0, ray );
    float w = saturate( 1.0 - abs( distToPlane ) * geometryWeightParams );

    return w;
}

float GetRoughnessWeight( float2 params0, float roughness )
{
    return saturate( 1.0 - abs( params0.y - roughness * params0.x ) );
}

float GetHitDistanceWeight( float2 params0, float hitDist )
{
    return saturate( 1.0 - abs( params0.y - hitDist * params0.x ) );
}

// Upsampling

float GetMipLevel( float normAccumSpeed, float accumSpeed, float roughness = 1.0 )
{
    float mip = MIP_NUM * saturate( 1.0 - normAccumSpeed );

    float f = STL::Math::LinearStep( 3.0, 0.0, accumSpeed );
    roughness += 0.25 * f * STL::Math::Pow01( roughness, 0.25 );

    return mip * STL::Math::Sqrt01( roughness );
}

/*
// TODO: fix minor light leaking in history fix passes!
The following code helps to avoid light leaking during history fix pass, but can lead to ignoring all sample candidates...
I'm not using it right now (i.e. "w" can't be 0) because it really works worse, especially in complicated cases where there is
no a suitable sample. What to do if "sumWeight == 0"? I tried hierachical approach (switching to previous mip level) but it
looked worse that what I have now.
*/
#define _GetBilateralWeight( z, zc, cutoff ) \
    z = abs( z - zc ) * rcp( min( abs( z ), abs( zc ) ) + 0.00001 ); \
    z = rcp( 1.0 + BILATERAL_WEIGHT_VIEWZ_SENSITIVITY * z ) * step( z, cutoff );

float GetBilateralWeight( float z, float zc, float cutoff = 99999.0 )
{ _GetBilateralWeight( z, zc, cutoff ); return z; }

float2 GetBilateralWeight( float2 z, float zc, float cutoff = 99999.0 )
{ _GetBilateralWeight( z, zc, cutoff ); return z; }

float4 GetBilateralWeight( float4 z, float zc, float cutoff = 99999.0 )
{ _GetBilateralWeight( z, zc, cutoff ); return z; }

float4 BicubicFilterNoCorners( Texture2D<float4> tex, SamplerState samp, float2 samplePos, float2 invTextureSize )
{
    #if( USE_CATMULLROM_RESAMPLING_IN_TS == 1 )
        const float sharpness = TS_HISTORY_SHARPNESS;

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

        return color; // Can return negative values due to negative lobes! Additionally. YCoCg can have negative chroma!
    #else
        return tex.SampleLevel( samp, samplePos * invTextureSize, 0 );
    #endif
}

// Unrolling

#if( DIFF_POISSON_SAMPLE_NUM <= MAX_UNROLLED_SAMPLES )
    #define DIFF_UNROLL                         [unroll]
#else
    #define DIFF_UNROLL
#endif

#if( SPEC_POISSON_SAMPLE_NUM <= MAX_UNROLLED_SAMPLES )
    #define SPEC_UNROLL                         [unroll]
#else
    #define SPEC_UNROLL
#endif

#if( SHADOW_POISSON_SAMPLE_NUM <= MAX_UNROLLED_SAMPLES )
    #define SHADOW_UNROLL                       [unroll]
#else
    #define SHADOW_UNROLL
#endif
