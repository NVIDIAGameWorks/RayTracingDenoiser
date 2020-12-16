/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "STL.hlsl"
#include "NRD.hlsl"
#include "_Poisson.hlsl"

NRI_RESOURCE( SamplerState, gNearestClamp, s, 0, 0 );
NRI_RESOURCE( SamplerState, gNearestMirror, s, 1, 0 );
NRI_RESOURCE( SamplerState, gLinearClamp, s, 2, 0 );
NRI_RESOURCE( SamplerState, gLinearMirror, s, 3, 0 );

// Constants

#define NONE                                    0
#define FRAME                                   1
#define PIXEL                                   2
#define RANDOM                                  3

#define MAX_ACCUM_FRAME_NUM                     31 // "2 ^ N - 1"
#define INF                                     1e6

// Debug

#define SHOW_MIPS                               0 // 1 - linear, 2 - linear-bilateral
#define SHOW_ACCUM_SPEED                        0
#define SHOW_ANTILAG                            0

// Booleans

#define USE_QUADRATIC_DISTRIBUTION              0
#define USE_SHADOW_BLUR_RADIUS_FIX              1
#define USE_WEIGHT_CUTOFF_FOR_HISTORY_FIX       1
#define USE_HISTORY_FIX                         1
#define USE_MIX_WITH_ORIGINAL                   1
#define USE_ANTILAG                             1
#define USE_CATROM_RESAMPLING_IN_TA             1 // ~0.13 ms on RTX 2080 @1440p (in each denoiser)
#define USE_CATROM_RESAMPLING_IN_TS             1 // ~0.03 ms on RTX 2080 @1440p
#define USE_ANISOTROPIC_KERNEL                  1 // TODO: add anisotropy support for shadows!

// Settings

#define PRE_BLUR_ROTATOR_MODE                   FRAME
#define PRE_BLUR_RADIUS_SCALE                   1.0
#define PRE_BLUR_NON_LINEAR_ACCUM_SPEED( r )    lerp( 1.0, 0.5, r )
#define PRE_BLUR_NORM_ACCUM_SPEED( r )          lerp( 1.0, 0.5, r )

#define BLUR_ROTATOR_MODE                       PIXEL

#define POST_BLUR_ROTATOR_MODE                  FRAME
#define POST_BLUR_RADIUS_SCALE                  2.0

#define SPEC_ACCUM_BASE_POWER                   0.5 // previously was 0.66 (less agressive accumulation, but virtual reprojection works well on flat surfaces and fixes the issue)
#define SPEC_ACCUM_CURVE                        1.0 // aggressiveness of history rejection depending on viewing angle (1 - low, 0.66 - medium, 0.5 - high)
#define SPEC_NORMAL_VARIANCE_SMOOTHNESS         4.0 // actually, it's power - 1 low (do nothing), 32 high (NoV not sensitive)
#define SPEC_DOMINANT_DIRECTION                 STL_SPECULAR_DOMINANT_DIRECTION_APPROX

#define SHADOW_POISSON_SAMPLE_NUM               8
#define SHADOW_POISSON_SAMPLES                  g_Poisson8
#define SHADOW_PRE_BLUR_ROTATOR_MODE            PIXEL
#define SHADOW_BLUR_ROTATOR_MODE                PIXEL
#define SHADOW_MAX_PIXEL_RADIUS                 32.0
#define SHADOW_PLANE_DISTANCE_SCALE             0.25
#define SHADOW_EDGE_HARDENING_FIX               0.001 // TODO: it works well, but introduces dark halo which is only visible if shadow-only visualization is turned on... do we care?
#define SHADOW_MAX_SIGMA_SCALE                  3.5
#define SHADOW_BLACK_OUT_INF_PIXELS             0 // can be used to avoid killing INF pixels during composition

#define POISSON_SAMPLE_NUM                      8
#define POISSON_SAMPLES                         g_Poisson8
#define NORMAL_BANDING_FIX                      STL::Math::DegToRad( 0.625 ) // mitigate banding introduced by normals stored in RGB8 format // TODO: move to CommonSettings?
#define BLACK_OUT_INF_PIXELS                    0 // can be used to avoid killing INF pixels during composition
#define CHECKERBOARD_SIDE_WEIGHT                0.6
#define LOBE_STRICTNESS_FACTOR                  0.333 // 0.3 - 1.0 (to avoid overblurring)
#define ANTILAG_MIN                             0.02
#define ANTILAG_MAX                             0.1
#define TS_MOTION_MAX_REUSE                     0.11
#define TS_HISTORY_SHARPNESS                    0.5 // [0; 1], 0.5 matches Catmull-Rom
#define TS_SIGMA_AMPLITUDE                      9.0
#define TS_MAX_HISTORY_WEIGHT                   ( ( 64.0 * gFramerateScale - 1.0 ) / ( 64.0 * gFramerateScale ) )
#define MIP_NUM                                 4.999
#define PLANE_DIST_SENSITIVITY                  0.002 // m
#define ALMOST_ZERO_ACCUM_FRAME_NUM             0.01
#define HISTORY_FIX_FRAME_NUM_PERCENTAGE        ( 4.0 / 32.0 ) // 4, 8, 12...
#define MIN_HITDIST_ACCUM_SPEED                 0.1
#define HIT_DIST_INPUT_MIX                      0.25 // preserves responsivness of hit distances (can be 0)
#define BILATERAL_WEIGHT_VIEWZ_SENSITIVITY      500.0
#define BILATERAL_WEIGHT_CUTOFF                 0.05 // normalized %
#define MAX_UNROLLED_SAMPLES                    32
#define NORMAL_ROUGHNESS_ACCUMSPEED_BITS        9, 9, 8, 6

#ifdef TRANSLUCENT_SHADOW
    #define SHADOW_TYPE                         float4
#else
    #define SHADOW_TYPE                         float
#endif

#include "NRD_config.hlsl"

// CTA & preloading

#define BORDER                                  1 // max kernel radius for data from shared memory
#define GROUP_X                                 16
#define GROUP_Y                                 16
#define BUFFER_X                                ( GROUP_X + BORDER * 2 )
#define BUFFER_Y                                ( GROUP_Y + BORDER * 2 )
#define RENAMED_GROUP_Y                         ( ( GROUP_X * GROUP_Y ) / BUFFER_X )

// Rename the 16x16 group into a 18x14 group + some idle threads in the end
// TODO: ignore out-of-screen texels or use "NearestClamp"
#define PRELOAD_INTO_SMEM \
    float linearId = ( threadIndex + 0.5 ) / BUFFER_X; \
    int2 newId = int2( frac( linearId ) * BUFFER_X, linearId ); \
    int2 groupBase = pixelPos - threadId - BORDER; \
    if( newId.y < RENAMED_GROUP_Y ) \
        Preload( newId, groupBase + newId ); \
    newId.y += RENAMED_GROUP_Y; \
    if( newId.y < BUFFER_Y ) \
        Preload( newId, groupBase + newId ); \
    GroupMemoryBarrierWithGroupSync( )

groupshared float4 s_Normal_Roughness[ BUFFER_Y ][ BUFFER_X ];
groupshared float s_ViewZ[ BUFFER_Y ][ BUFFER_X ];

// Misc

#define UnpackViewZ( p ) asfloat( p & ~MAX_ACCUM_FRAME_NUM )
#define UnpackAccumSpeed( p ) ( p & MAX_ACCUM_FRAME_NUM )
#define GetVariance( m1, m2 ) sqrt( abs( m2 - m1 * m1 ) ) // sqrt( max( m2 - m1 * m1, 0.0 )
#define PackShadow( s ) STL::Math::Sqrt01( s )
#define UnpackShadow( s ) ( s * s )

// http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIzMSooeF4wLjY2KSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiIzMSooMS0yXigtMjAwKngqeCkpKih4XjAuNSkiLCJjb2xvciI6IiNGQTBEMEQifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIzMSJdfV0-
#define GetSpecAccumulatedFrameNum( roughness, powerScale ) ( MAX_ACCUM_FRAME_NUM * ( 1.0 - exp2( -200.0 * roughness * roughness ) ) * STL::Math::Pow01( roughness, SPEC_ACCUM_BASE_POWER * powerScale ) )

uint IsWorldSpaceMotion()
{
    return gInf > 0.0 ? 1 : 0;
}

uint2 PackViewZNormalRoughnessAccumSpeeds( float viewZ, float diffAccumSpeed, float3 N, float roughness, float specAccumSpeed )
{
    float4 t;
    t.xy = STL::Packing::EncodeUnitVector( N );
    t.z = roughness;
    t.w = specAccumSpeed / MAX_ACCUM_FRAME_NUM;

    uint2 p;
    p.x = asuint( viewZ );
    p.x &= ~MAX_ACCUM_FRAME_NUM;
    p.x |= uint( diffAccumSpeed + 0.5 );

    p.y = STL::Packing::RgbaToUint( t, NORMAL_ROUGHNESS_ACCUMSPEED_BITS );

    return p;
}

float4 UnpackNormalRoughness( uint p )
{
    float4 t = STL::Packing::UintToRgba( p, NORMAL_ROUGHNESS_ACCUMSPEED_BITS );
    float3 N = STL::Packing::DecodeUnitVector( t.xy );

    return float4( N, t.z );
}

float4 UnpackRoughness( uint4 p )
{
    // see NORMAL_ROUGHNESS_ACCUMSPEED_BITS
    p >>= 9 + 9;
    p &= 0xFF;

    return float4( p ) / 255.0;
}

float4 UnpackNormalRoughnessAccumSpeed( uint p, out float accumSpeed )
{
    float4 t = STL::Packing::UintToRgba( p, NORMAL_ROUGHNESS_ACCUMSPEED_BITS );
    float3 N = STL::Packing::DecodeUnitVector( t.xy );

    accumSpeed = t.w * MAX_ACCUM_FRAME_NUM;

    return float4( N, t.z );
}

float GetHitDistance( float normHitDist, float viewZ, float4 scalingParams, float roughness = 1.0 )
{
    return normHitDist * ( scalingParams.x + scalingParams.y * abs( viewZ ) ) * lerp( scalingParams.z, 1.0, exp2( -roughness * roughness * scalingParams.w ) );
}

float PixelRadiusToWorld( float pixelRadius, float centerZ )
{
     return pixelRadius * gUnproject * lerp( abs( centerZ ), 1.0, abs( gIsOrtho ) );
}

float4 GetBlurKernelRotation( compiletime const uint mode, uint2 pixelPos, float4 baseRotator )
{
    float4 rotator = float4( 1, 0, 0, 1 );

    if( mode == PIXEL )
    {
        float angle = STL::Sequence::Bayer4x4( pixelPos, gFrameIndex );
        rotator = STL::Geometry::GetRotator( angle * STL::Math::Pi( 2.0 ) );
    }
    else if( mode == RANDOM )
    {
        STL::Rng::Initialize( pixelPos, gFrameIndex );
        float4 rnd = STL::Rng::GetFloat4( );
        rotator = STL::Geometry::GetRotator( rnd.z * STL::Math::Pi( 2.0 ) );
        rotator *= 1.0 + ( rnd.w * 2.0 - 1.0 ) * 0.5;
    }

    rotator = STL::Geometry::CombineRotators( baseRotator, rotator );

    return rotator;
}

float2 GetDisocclusionThresholds( float disocclusionThreshold, float jitterDelta, float viewZ, float parallax, float3 Nflat, float3 X, float invDistToPoint )
{
    float angleThreshold = lerp( -0.99, 0.0, saturate( parallax ) );
    float jitterRadius = PixelRadiusToWorld( jitterDelta, viewZ );
    float NoV = abs( dot( Nflat, X ) ) * invDistToPoint;
    disocclusionThreshold += jitterRadius * invDistToPoint / max( NoV, 0.05 );

    return float2( disocclusionThreshold, angleThreshold );
}

float ComputeParallax( float2 pixelUv, float roughnessRatio, float3 X, float3 cameraDelta, float4x4 mWorldToClip )
{
    // X = Prev for surface motion
    // TODO: X = Xvirtual for virtual motion (?)
    float3 Xt = X - cameraDelta;
    float4 clip = STL::Geometry::ProjectiveTransform( mWorldToClip, Xt );
    float2 uv = ( clip.xy / clip.w ) * float2( 0.5, -0.5 ) + 0.5;
    float2 parallaxInUv = uv - pixelUv;
    float parallaxInPixels = length( parallaxInUv * gScreenSize );
    float parallaxInUnits = PixelRadiusToWorld( parallaxInPixels, clip.w );
    float parallax = parallaxInUnits * STL::Math::PositiveRcp( clip.w ); // Tan of the angle between old and new view vectors in the world space
    parallax *= 60.0; // yes, tuned for 60 FPS to get auto-scaled solution if FPS is higher (more accumulation) or lower (less accumulation)
    parallax *= lerp( roughnessRatio, 1.0, 0.25 ); // TODO: to not kill parallax completely (biased modification)

    return parallax;
}

float3 GetXvirtual( float3 X, float3 Xprev, float3 N, float3 V, float roughness, float hitDist )
{
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection( N, V, roughness, SPEC_DOMINANT_DIRECTION );
    float3 Xvirtual = X - V * hitDist * D.w; // TODO: use lerp( Xprev, X, D.w ) instead of X?

    return Xvirtual;
}

float DetectEdge( float3 N, uint2 smemPos )
{
    float3 Navg = N;
    Navg += s_Normal_Roughness[ smemPos.y     ][ smemPos.x + 1 ].xyz;
    Navg += s_Normal_Roughness[ smemPos.y + 1 ][ smemPos.x     ].xyz;
    Navg += s_Normal_Roughness[ smemPos.y + 1 ][ smemPos.x + 1 ].xyz;
    Navg /= 4.0;

    float edge = 1.0 - STL::Math::SmoothStep( 0.94, 0.98, length( Navg ) );

    return edge;
}

float3 ApplyCheckerboard( inout float2 uv, uint mode, uint counter )
{
    int2 uvi = int2( uv * gScreenSize );
    bool hasData = STL::Sequence::CheckerBoard( uvi, gFrameIndex ) == mode;
    if( !hasData )
        uvi.y += ( ( counter & 0x1 ) == 0 ) ? -1 : 1;
    uv = ( float2( uvi ) + 0.5 ) * gInvScreenSize;

    return float3( uv.x * 0.5, uv.y, float( all( saturate( uv ) == uv ) ) );
}

float4 MixLinearAndCatmullRom( float4 linearX, float4 catromX, float4 occlusion0, float4 occlusion1, float4 occlusion2, float4 occlusion3 )
{
    float4 sum = occlusion0;
    sum += occlusion1;
    sum += occlusion2;
    sum += occlusion3;

    float avg = dot( sum, 1.0 / 16.0 );

    return ( avg < 1.0 || USE_CATROM_RESAMPLING_IN_TA == 0 ) ? linearX : catromX;
}

float GetColorErrorForAdaptiveRadiusScale( float luma, float lumaPrev, float nonLinearAccumSpeed, float roughness = 1.0 )
{
    float error = abs( luma - lumaPrev ) * STL::Math::PositiveRcp( max( luma, lumaPrev ) );
    error = STL::Math::SmoothStep( 0.0, 0.1, error );
    error *= STL::Math::LinearStep( 0.04, 0.15, roughness );
    error *= 1.0 - nonLinearAccumSpeed;

    return error;
}

float GetAccumSpeed( float4 prevAccumSpeed, float4 weights, float maxAccumulatedFrameNum, float noisinessBlurrinessBalance, out float accumSpeed )
{
    float4 accumSpeeds = prevAccumSpeed + 1.0;
    float accumSpeedMax = STL::Filtering::ApplyBilinearCustomWeights( accumSpeeds.x, accumSpeeds.y, accumSpeeds.z, accumSpeeds.w, weights );
    accumSpeedMax = min( accumSpeedMax, MAX_ACCUM_FRAME_NUM );

    float accumSpeedClamped = min( accumSpeedMax, maxAccumulatedFrameNum );
    accumSpeed = lerp( accumSpeedMax, accumSpeedClamped, noisinessBlurrinessBalance );

    return lerp( maxAccumulatedFrameNum / MAX_ACCUM_FRAME_NUM, 1.0, noisinessBlurrinessBalance );
}

// Internal data

float2 PackDiffInternalData( float3 accumSpeeds )
{
    return saturate( accumSpeeds.xz / MAX_ACCUM_FRAME_NUM );
}

float3 UnpackDiffInternalData( float2 p )
{
    float3 r;
    r.xz = p * MAX_ACCUM_FRAME_NUM;
    r.y = MAX_ACCUM_FRAME_NUM;

    return r;
}

float3 PackSpecInternalData( float3 accumSpeeds, float virtualHistoryAmount )
{
    float3 r;
    r.xy = saturate( accumSpeeds.xz / MAX_ACCUM_FRAME_NUM );
    r.z = virtualHistoryAmount;

    return r;
}

float3 UnpackSpecInternalData( float3 p, float roughness )
{
    p.xy *= MAX_ACCUM_FRAME_NUM;

    return float3( p.x, GetSpecAccumulatedFrameNum( roughness, 1.0 ), p.y );
}

float3 UnpackSpecInternalData( float3 p, float roughness, out float virtualHistoryAmount )
{
    virtualHistoryAmount = p.z;

    return UnpackSpecInternalData( p, roughness );
}

uint PackDiffSpecInternalData( float3 diffAccumSpeeds, float3 specAccumSpeeds, float virtualHistoryAmount )
{
    float4 t = float4( diffAccumSpeeds.xz, specAccumSpeeds.xz ) / MAX_ACCUM_FRAME_NUM;
    uint r = STL::Packing::RgbaToUint( t, 6, 6, 6, 6 );
    r |= STL::Packing::RgbaToUint( float4( 0, 0, 0, virtualHistoryAmount ), 8, 8, 8, 8 );

    return r;
}

float2x3 UnpackDiffSpecInternalData( uint p, float roughness )
{
    float4 t = STL::Packing::UintToRgba( p, 6, 6, 6, 6 ) * MAX_ACCUM_FRAME_NUM;

    float2x3 r;
    r[ 0 ] = float3( t.x, MAX_ACCUM_FRAME_NUM, t.y );
    r[ 1 ] = float3( t.z, GetSpecAccumulatedFrameNum( roughness, 1.0 ), t.w );

    return r;
}

float2x3 UnpackDiffSpecInternalData( uint p, float roughness, out float virtualHistoryAmount )
{
    float2x3 r = UnpackDiffSpecInternalData( p, roughness );
    virtualHistoryAmount = STL::Packing::UintToRgba( p, 8, 8, 8, 8 ).w;

    return r;
}

// Accumulation speed

float2 GetSpecAccumSpeed( float maxAccumSpeed, float roughness, float NoV, float parallax )
{
    float acos01sq = saturate( 1.0 - NoV ); // see AcosApprox()

    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIoMS4wNSsoeCp4KV4xLjApLygxLjA1LSh4KngpXjEuMCkiLCJjb2xvciI6IiM1MkExMDgifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC42NikvKDEuMDUtKHgqeCleMC42NikiLCJjb2xvciI6IiNFM0Q4MDkifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC41KS8oMS4wNS0oeCp4KV4wLjUpIiwiY29sb3IiOiIjRjUwQTMxIn0seyJ0eXBlIjowLCJlcSI6IigxLjErKHgqeCleMS4wKS8oMS4xLSh4KngpXjEuMCkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCI0MiJdLCJzaXplIjpbMTkwMCw3MDBdfV0-
    float a = STL::Math::Pow01( acos01sq, SPEC_ACCUM_CURVE );
    float b = 1.1 + roughness * roughness; // using virtual motion allows to reduce sensitivity to viewing angle (1.05 => 1.1)
    float parallaxSensitivity = ( b + a ) / ( b - a );
    parallaxSensitivity *= 1.0 - roughness; // TODO: is it a hack?

    float2 powerScale = float2( 1.0 + parallax * parallaxSensitivity, 1.0 );
    float2 accumSpeed = GetSpecAccumulatedFrameNum( roughness, powerScale );

    // TODO: "accumSpeed.x" can be clamped to some portion of "accumSpeed.y", aka accumSpeed.x = clamp( accumSpeed.x, C * accumSpeed.y, maxAccumSpeed )
    accumSpeed.x = min( accumSpeed.x, maxAccumSpeed );

    return accumSpeed * float( gFrameIndex != 0 );
}

// Kernel

float GetBlurRadius( float radius, float roughness, float hitDist, float3 Xv, float nonLinearAccumSpeed )
{
    // Base radius
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIxLjAtMl4oLTE1LjAqeCkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjAsImVxIjoiKDEtMl4oLTIwMCp4KngpKSooeF4wLjI1KSIsImNvbG9yIjoiIzIyRUQxNyJ9LHsidHlwZSI6MTAwMCwid2luZG93IjpbIjAiLCIxIiwiMCIsIjEuMSJdLCJzaXplIjpbMTAwMCw1MDBdfV0-
    float t = STL::Math::Pow01( roughness, 0.25 );
    float s = ( 1.0 - exp2( -200.0 * roughness * roughness ) ) * t; // very similar to the function used in GetSpecAccumulatedFrameNum()

    // Modify by hit distance
    float a = lerp( 0.125 * roughness * roughness, 0.4 * t * t, nonLinearAccumSpeed );
    float hitDistFactor = hitDist / ( hitDist + abs( Xv.z ) );
    s *= lerp( a, 1.0, hitDistFactor );

    // Scale down if accumulation goes well
    s *= nonLinearAccumSpeed;

    // Blur radius must be 1 pixel (maybe a bit lower) at least to keep adaptive scale in post blur in working state (otherwise "darken" effect can appear)
    float addon = lerp( roughness, 1.0, hitDistFactor ) * ( 1.0 - gReference );

    return s * radius + addon;
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

float2x3 GetKernelBasis( float3 Xv, float3 Nv, float worldRadius, float normAccumSpeed = 0.65, float roughness = 1.0 )
{
    // It's needed to skip anisotropy if accumulation doesn't go well
    roughness = lerp( 1.0, roughness, STL::Math::Sqrt01( normAccumSpeed ) );

    float3 Tv, Bv;
    if( roughness < 0.95 )
    {
        float3 Vv = -normalize( Xv );
        float3 Dv = STL::ImportanceSampling::GetSpecularDominantDirection( Nv, Vv, roughness, SPEC_DOMINANT_DIRECTION ).xyz;
        float3 Rv = reflect( -Dv, Nv );

        Tv = normalize( cross( Nv, Rv ) );
        Bv = cross( Rv, Tv );

        #if( USE_ANISOTROPIC_KERNEL == 1 )
            float NoV = abs( dot( Nv, Vv ) );
            float acos01sq = saturate( 1.0 - NoV ); // see AcosApprox()
            float skewFactor = lerp( 1.0, roughness, STL::Math::Sqrt01( acos01sq ) );
            Tv *= lerp( 1.0, skewFactor, normAccumSpeed );
        #endif
    }
    else
    {
        float3x3 basis = STL::Geometry::GetBasis( Nv );
        Tv = basis[ 0 ];
        Bv = basis[ 1 ];
    }

    Tv *= worldRadius;
    Bv *= worldRadius;

    return float2x3( Tv, Bv );
}

float2 GetKernelSampleCoordinates( float3 offset, float3 Xv, float3 Tv, float3 Bv, float4 rotator = float4( 1, 0, 0, 1 ) )
{
    #if( USE_QUADRATIC_DISTRIBUTION == 1 )
        offset.xy *= offset.z;
    #endif

    // We can't rotate T and B instead, because T is skewed
    offset.xy = STL::Geometry::RotateVector( rotator, offset.xy );

    float3 p = Xv + Tv * offset.x + Bv * offset.y;
    float3 clip = mul( gViewToClip, float4( p, 1.0 ) ).xyw;
    clip.xy /= clip.z; // TODO: potentially dangerous!
    clip.y = -clip.y;
    float2 uv = clip.xy * 0.5 + 0.5;

    return uv;
}

// Weight parameters

float GetNormalWeightParamsRoughEstimate( float roughness )
{
    float ang01 = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness ) / STL::Math::DegToRad( 90.0 );
    float angle = STL::Math::DegToRad( lerp( 45.0, 89.0, saturate( ang01 ) ) ); // Yes, very relaxed angles here to not ruin accumulation with enabled jittering. Definitely min angle can't be < 25 deg

    float a = rcp( angle );

    return a;
}

float GetNormalWeightParams( float roughness, float edge, float normAccumSpeed = 1.0 ) // TODO: TA pass uses edge = 0 and normAccumSpeed = 0.7 to get scale = 0.32 (like it was before)
{
    normAccumSpeed *= lerp( 1.0, 0.5, edge );

    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
    angle *= lerp( 1.0, LOBE_STRICTNESS_FACTOR, normAccumSpeed );
    angle *= saturate( 1.22 - 0.88 * normAccumSpeed );
    angle += NORMAL_BANDING_FIX;

    float a = rcp( angle );

    return a;
}

float2 GetRoughnessWeightParams( float roughness0 )
{
    float a = rcp( roughness0 * 0.2 * 0.99 + 0.01 );
    float b = roughness0 * a;

    return float2( -a, b );
}

float2 GetHitDistanceWeightParams( float hitDist0, float normAccumSpeed, float hitDist, float3 Xv )
{
    float hitDistFactor = hitDist / ( hitDist + abs( Xv.z ) );
    float scale = lerp( 1.0, 0.05, STL::Math::Sqrt01( normAccumSpeed ) );
    scale = lerp( scale, 1.0, hitDistFactor * hitDistFactor );

    float a = rcp( hitDist0 * scale * 0.99 + 0.01 );
    float b = hitDist0 * a;

    return float2( -a, b );
}

float2 GetGeometryWeightParams( float3 p0, float3 n0, float centerZ, float scale = 1.0 )
{
    float a = scale / ( PLANE_DIST_SENSITIVITY * ( gMetersToUnits + abs( centerZ ) ) );
    float b = -dot( n0, p0 ) * a;

    return float2( a, b );
}

float2 GetTemporalAccumulationParams( float isInScreen, float accumSpeed, float motionLength, float parallax = 0.0, float roughness = 1.0 )
{
    const float norm = MAX_ACCUM_FRAME_NUM / ( MAX_ACCUM_FRAME_NUM + 1.0 );

    float oneMinusNonLinearAccumSpeed = accumSpeed / ( 1.0 + accumSpeed );
    float roughnessWeight = STL::Math::SmoothStep( 0.0, 0.75, roughness );
    float sigmaAmplitude = oneMinusNonLinearAccumSpeed;
    sigmaAmplitude *= lerp( saturate( 1.0 - parallax ), 1.0, roughnessWeight );
    sigmaAmplitude = TS_SIGMA_AMPLITUDE * sigmaAmplitude + 1.0;

    float historyWeight = isInScreen;
    historyWeight *= 1.0 - STL::Math::SmoothStep( 0.0, TS_MOTION_MAX_REUSE, motionLength );
    historyWeight *= saturate( oneMinusNonLinearAccumSpeed / norm );
    historyWeight *= lerp( saturate( 1.0 - parallax ), 1.0, roughnessWeight );
    historyWeight *= lerp( 1.0, oneMinusNonLinearAccumSpeed * saturate( 1.0 - parallax ), gReference );

    return float2( historyWeight, sigmaAmplitude ) * float( gFrameIndex != 0 );
}

// Weights

#define _ComputeWeight( p, value ) STL::Math::SmoothStep01( 1.0 - abs( value * p.x + p.y ) )

float GetNormalWeight( float params0, float3 n0, float3 n )
{
    float cosa = saturate( dot( n0, n ) );
    float a = STL::Math::AcosApprox( cosa );

    return _ComputeWeight( float2( params0, 0 ), a );
}

float GetGeometryWeight( float2 params0, float3 n0, float3 p )
{
    float d = dot( n0, p );

    return _ComputeWeight( params0, d );
}

#define GetRoughnessWeight _ComputeWeight
#define GetHitDistanceWeight _ComputeWeight

float GetNormalAndRoughnessWeights( float3 N, float normalParams, float2 roughnessParams, uint packedNormalAndRoughness )
{
    float4 normalAndRoughness = UnpackNormalRoughness( packedNormalAndRoughness );
    float normalWeight = GetNormalWeight( normalParams, N, normalAndRoughness.xyz );
    float roughnessWeight = GetRoughnessWeight( roughnessParams, normalAndRoughness.w );
    roughnessWeight = STL::Math::LinearStep( 0.1, 0.9, roughnessWeight );

    return normalWeight * roughnessWeight;
}

// Upsampling

float GetMipLevel( float normAccumSpeed, float accumSpeed, float roughness = 1.0 )
{
    float mip = MIP_NUM * saturate( 1.0 - normAccumSpeed );

    float f = STL::Math::LinearStep( 3.0, 0.0, accumSpeed );
    roughness += 0.25 * f * STL::Math::Pow01( roughness, 0.25 );

    return mip * STL::Math::Sqrt01( roughness );
}

#define _GetBilateralWeight( z, zc, cutoff ) \
    z = abs( z - zc ) * rcp( min( abs( z ), abs( zc ) ) + 0.00001 ); \
    z = rcp( 1.0 + BILATERAL_WEIGHT_VIEWZ_SENSITIVITY * z ) * step( z, cutoff );

float GetBilateralWeight( float z, float zc, float cutoff = BILATERAL_WEIGHT_CUTOFF )
{ _GetBilateralWeight( z, zc, cutoff ); return z; }

float2 GetBilateralWeight( float2 z, float zc, float cutoff = BILATERAL_WEIGHT_CUTOFF )
{ _GetBilateralWeight( z, zc, cutoff ); return z; }

float4 GetBilateralWeight( float4 z, float zc, float cutoff = BILATERAL_WEIGHT_CUTOFF )
{ _GetBilateralWeight( z, zc, cutoff ); return z; }

float4 BicubicFilterNoCorners( Texture2D<float4> tex, SamplerState samp, float2 samplePos, float2 invTextureSize )
{
    #if( USE_CATROM_RESAMPLING_IN_TS == 1 )
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

float4 ReconstructHistory( uint realMipLevel, uint2 screenSizei, float2 pixelUv, float z0, Texture2D<float> texScaledViewZ, Texture2D<float4> texSignal, out float sum )
{
    float4 blurry = 0;
    sum = 0;

#if( USE_WEIGHT_CUTOFF_FOR_HISTORY_FIX == 1 )
    while( sum == 0.0 && realMipLevel != 0 )
    {
#endif
        uint mipLevel = realMipLevel - 1;
        float2 mipSize = float2( screenSizei >> realMipLevel );
        float2 invMipSize = 1.0 / mipSize;
        float2 mipUv = pixelUv * gScreenSize / ( mipSize * float( 1 << realMipLevel ) );

        STL::Filtering::Bilinear filter = STL::Filtering::GetBilinearFilter( mipUv, mipSize );
        float4 bilinearWeights = STL::Filtering::GetBilinearCustomWeights( filter, 1.0 );
        float2 mipUvFootprint00 = saturate( ( filter.origin + 0.5 ) * invMipSize );

        [unroll]
        for( int i = 0; i < 2; i++ )
        {
            [unroll]
            for( int j = 0; j < 2; j++ )
            {
                const float2 offset = float2( i, j ) * 2.0 - 1.0;
                const float2 uv = saturate( mipUvFootprint00 + offset * invMipSize );

                float4 z;
                z.x = texScaledViewZ.SampleLevel( gNearestClamp, uv, realMipLevel );
                z.y = texScaledViewZ.SampleLevel( gNearestClamp, uv, realMipLevel, int2( 1, 0 ) );
                z.z = texScaledViewZ.SampleLevel( gNearestClamp, uv, realMipLevel, int2( 0, 1 ) );
                z.w = texScaledViewZ.SampleLevel( gNearestClamp, uv, realMipLevel, int2( 1, 1 ) );

                #if( USE_WEIGHT_CUTOFF_FOR_HISTORY_FIX == 1 )
                    float cutoff = BILATERAL_WEIGHT_CUTOFF; // TODO: slope scale is needed, but no normals here...
                #else
                    float cutoff = 99999.0;
                #endif

                float4 bilateralWeights = GetBilateralWeight( z, z0, cutoff );
                float4 w = bilinearWeights * bilateralWeights;

                float4 s00 = texSignal.SampleLevel( gNearestClamp, uv, mipLevel );
                float4 s10 = texSignal.SampleLevel( gNearestClamp, uv, mipLevel, int2( 1, 0 ) );
                float4 s01 = texSignal.SampleLevel( gNearestClamp, uv, mipLevel, int2( 0, 1 ) );
                float4 s11 = texSignal.SampleLevel( gNearestClamp, uv, mipLevel, int2( 1, 1 ) );

                blurry += STL::Filtering::ApplyBilinearCustomWeights( s00, s10, s01, s11, w, false );

                sum += dot( w, 1.0 );
            }
        }

#if( USE_WEIGHT_CUTOFF_FOR_HISTORY_FIX == 1 )
        realMipLevel--;
    }

#endif

    blurry *= STL::Math::PositiveRcp( sum );

    return blurry;
}

// Unrolling

#if( POISSON_SAMPLE_NUM <= MAX_UNROLLED_SAMPLES )
    #define UNROLL                         [unroll]
#else
    #define UNROLL
#endif

#if( SHADOW_POISSON_SAMPLE_NUM <= MAX_UNROLLED_SAMPLES )
    #define SHADOW_UNROLL                  [unroll]
#else
    #define SHADOW_UNROLL
#endif
