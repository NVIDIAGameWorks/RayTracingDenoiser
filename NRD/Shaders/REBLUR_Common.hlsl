/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// Debug

#define REBLUR_SHOW_MIPS                                1
#define REBLUR_SHOW_ACCUM_SPEED                         2
#define REBLUR_SHOW_VIRTUAL_HISTORY_AMOUNT              3
#define REBLUR_SHOW_VIRTUAL_HISTORY_CONFIDENCE          4
#define REBLUR_SHOW_PARALLAX                            5
#define REBLUR_SHOW_EDGE                                6
#define REBLUR_DEBUG                                    0 // 0-6

#define REBLUR_VIEWZ_ACCUMSPEED_BITS                    26, 6, 0, 0
#define REBLUR_NORMAL_ROUGHNESS_ACCUMSPEED_BITS         9, 9, 8, 6
#define REBLUR_MAX_ACCUM_FRAME_NUM                      63 // 6 bits

// Shared data

groupshared float4 s_Normal_Roughness[ BUFFER_Y ][ BUFFER_X ];
groupshared float s_ViewZ[ BUFFER_Y ][ BUFFER_X ];

// Back-end unpacking

float4 REBLUR_BackEnd_UnpackRadiance( float4 compressedRadianceAndNormHitDist, float viewZ, float4 hitDistParams, float linearRoughness = 1.0 )
{
    float exposure = _NRD_GetColorCompressionExposure( linearRoughness );
    float lum = _NRD_Luminance( compressedRadianceAndNormHitDist.xyz );
    float3 radiance = compressedRadianceAndNormHitDist.xyz / max( 1.0 - lum * exposure, NRD_EPS );

    float normHitDist = compressedRadianceAndNormHitDist.w;
    #if( REBLUR_DEBUG == 0 )
        normHitDist = _REBLUR_DecompressNormHitDistance( compressedRadianceAndNormHitDist.w, viewZ, hitDistParams, linearRoughness );
    #endif

    return float4( radiance, normHitDist );
}

// Data packing for the next frame

uint2 PackViewZNormalRoughnessAccumSpeeds( float viewZ, float diffAccumSpeed, float3 N, float roughness, float specAccumSpeed )
{
    float2 t1;
    t1.x = saturate( viewZ / gInf ); // TODO: sqrt?
    t1.y = diffAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM;

    float4 t2;
    t2.xy = STL::Packing::EncodeUnitVector( N );
    t2.z = roughness;
    t2.w = specAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM;

    uint2 p;
    p.x = STL::Packing::RgbaToUint( t1.xyyy, REBLUR_VIEWZ_ACCUMSPEED_BITS );
    p.y = STL::Packing::RgbaToUint( t2, REBLUR_NORMAL_ROUGHNESS_ACCUMSPEED_BITS );

    return p;
}

float4 UnpackViewZ( uint4 p )
{
    float4 norm = float4( p & 0x3FFFFFF ) / float( 0x3FFFFFF );

    return gInf * ( norm < 1.0 ? norm : 999.0 );
}

float4 UnpackDiffAccumSpeed( uint4 p )
{
    return float4( p >> 26 );
}

float4 UnpackNormalRoughness( uint p )
{
    float4 t = STL::Packing::UintToRgba( p, REBLUR_NORMAL_ROUGHNESS_ACCUMSPEED_BITS );
    float3 N = STL::Packing::DecodeUnitVector( t.xy );

    return float4( N, t.z );
}

float4 UnpackRoughness( uint4 p )
{
    p >>= 9 + 9;
    p &= 0xFF;

    return float4( p ) / float( 0xFF );
}

float4 UnpackNormalRoughnessSpecAccumSpeed( uint p, out float accumSpeed )
{
    float4 t = STL::Packing::UintToRgba( p, REBLUR_NORMAL_ROUGHNESS_ACCUMSPEED_BITS );
    float3 N = STL::Packing::DecodeUnitVector( t.xy );

    accumSpeed = t.w * REBLUR_MAX_ACCUM_FRAME_NUM;

    return float4( N, t.z );
}

// Misc

float GetSpecAccumulatedFrameNum( float roughness, float powerScale )
{
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIzMSooeF4wLjY2KSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiIzMSooMS0yXigtMjAwKngqeCkpKih4XjAuNSkiLCJjb2xvciI6IiNGQTBEMEQifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIzMSJdfV0-
    float f = 1.0 - exp2( -200.0 * roughness * roughness );
    f *= STL::Math::Pow01( roughness, REBLUR_SPEC_ACCUM_BASE_POWER * powerScale );

    return REBLUR_MAX_ACCUM_FRAME_NUM * f;
}

float2 GetDisocclusionThresholds( float disocclusionThreshold, float jitterDelta, float viewZ, float parallax, float3 Nflat, float3 X, float invDistToPoint )
{
    float angleThreshold = lerp( -0.99, 0.0, saturate( parallax ) );
    float jitterRadius = PixelRadiusToWorld( gUnproject, gIsOrtho, jitterDelta, viewZ );
    float NoV = abs( dot( Nflat, X ) ) * invDistToPoint;
    disocclusionThreshold += jitterRadius * invDistToPoint / max( NoV, 0.05 );

    return float2( disocclusionThreshold, angleThreshold );
}

float ComputeParallax( float3 X, float3 Xprev, float3 cameraDelta )
{
    float3 Xt = Xprev - cameraDelta;

    float cosa = dot( X, Xt );
    cosa *= STL::Math::Rsqrt( STL::Math::LengthSquared( Xt ) * STL::Math::LengthSquared( X ) );
    cosa = saturate( cosa );
    float parallax = STL::Math::Sqrt01( 1.0 - cosa * cosa ) * STL::Math::PositiveRcp( cosa );

    // OLD "Over 9000" method // TODO: remove
    /*
    float3 clip = STL::Geometry::ProjectiveTransform( mWorldToClip, Xt ).xyw;
    clip.xy /= clip.z;
    clip.y = -clip.y;
    float2 uv = clip.xy * 0.5 + 0.5;

    float2 parallaxInUv = uv - pixelUv;
    float parallaxInPixels = length( parallaxInUv * gScreenSize );
    float parallaxInUnits = PixelRadiusToWorld( gUnproject, gIsOrtho, parallaxInPixels, clip.z );
    float parallax = parallaxInUnits / abs( clip.z );
    */

    parallax *= REBLUR_PARALLAX_NORMALIZATION;
    parallax /= 1.0 + REBLUR_PARALLAX_COMPRESSION_STRENGTH * parallax;

    return parallax;
}

float GetParallaxInPixels( float parallax )
{
    parallax /= 1.0 - REBLUR_PARALLAX_COMPRESSION_STRENGTH * parallax;

    // TODO: add ortho projection support (see ComputeParallax)
    float parallaxInPixels = parallax / ( REBLUR_PARALLAX_NORMALIZATION * gUnproject );

    return parallaxInPixels;
}

float3 GetXvirtual( float3 X, float3 Xprev, float3 V, float NoV, float roughness, float hitDist )
{
    float f = STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughness, REBLUR_SPEC_DOMINANT_DIRECTION );
    float3 Xvirtual = X - V * hitDist * f;

    return Xvirtual; // TODO: more complicated method is needed, because if elongation is very small X should become Xprev (signal starts to follow with surface motion)
}

float DetectEdge( float3 Navg )
{
    return 1.0 - STL::Math::SmoothStep( 0.85, 1.0, length( Navg ) );
}

float DetectEdge( float3 N, uint2 smemPos )
{
    float3 Navg = N;
    Navg += s_Normal_Roughness[ smemPos.y     ][ smemPos.x + 1 ].xyz;
    Navg += s_Normal_Roughness[ smemPos.y + 1 ][ smemPos.x     ].xyz;
    Navg += s_Normal_Roughness[ smemPos.y + 1 ][ smemPos.x + 1 ].xyz;
    Navg *= 0.25;

    float edge = DetectEdge( Navg );

    return edge;
}

float4 MixLinearAndCatmullRom( float4 linearX, float4 catromX, float4 occlusion0, float4 occlusion1, float4 occlusion2, float4 occlusion3 )
{
    float4 sum = occlusion0 + occlusion1 + occlusion2 + occlusion3;
    float avg = dot( sum, 1.0 / 16.0 );

    catromX = max( catromX, 0.0 );

    return ( avg < 1.0 || REBLUR_USE_CATROM_RESAMPLING_IN_TA == 0 || gReference != 0.0 ) ? linearX : catromX;
}

float GetColorErrorForAdaptiveRadiusScale( float4 curr, float4 prev, float nonLinearAccumSpeed, float roughness = 1.0 )
{
    float2 currLuma = float2( _NRD_Luminance( curr.xyz ), curr.w );
    float2 prevLuma = float2( _NRD_Luminance( prev.xyz ), prev.w );
    float2 f = abs( currLuma - prevLuma ) * STL::Math::PositiveRcp( max( currLuma, prevLuma ) );
    float error = max( f.x, f.y );
    error = STL::Math::SmoothStep( 0.0, 0.1, error );
    error *= STL::Math::LinearStep( 0.04, 0.15, roughness );
    error *= 1.0 - nonLinearAccumSpeed;

    return error;
}

float GetHitDist( float compressedNormHitDist, float viewZ, float4 hitDistParams, float linearRoughness = 1.0 )
{
    float normHitDist = _REBLUR_DecompressNormHitDistance( compressedNormHitDist, viewZ, hitDistParams, linearRoughness );
    float f = _REBLUR_GetHitDistanceNormalization( viewZ, hitDistParams, linearRoughness );

    return normHitDist * f;
}

float InterpolateAccumSpeeds( float a, float b, float f )
{
    #if( REBLUR_USE_ACCUM_SPEED_NONLINEAR_INTERPOLATION == 0 )
        return lerp( a, b, f );
    #endif

    a = 1.0 / ( 1.0 + a );
    b = 1.0 / ( 1.0 + b );
    f = lerp( a, b, f );

    return 1.0 / f - 1.0;
}

float ComputeAntilagScale( inout float accumSpeed, float4 history, float4 m1, float4 sigma, float2 temporalAccumulationParams, float4 antilag1, float4 antilag2, float roughness = 1.0 )
{
    float antilag = 1.0;
    float2 h = float2( _NRD_Luminance( history.xyz ), history.w );
    float2 m = float2( _NRD_Luminance( m1.xyz ), m1.w );
    float2 s = float2( _NRD_Luminance( sigma.xyz ), sigma.w );

    #if( REBLUR_USE_ANTILAG == 1 )
        float2 delta = abs( h - m ) - s * antilag1.xy;
        delta /= max( m, h ) + s * antilag1.xy + antilag1.zw;
        delta = STL::Math::SmoothStep( antilag2.zw, antilag2.xy, delta );

        float fade = accumSpeed / ( 1.0 + accumSpeed );
        fade *= temporalAccumulationParams.x;

        antilag = min( delta.x, delta.y );
        antilag = lerp( 1.0, antilag, fade );
    #endif

    #if( REBLUR_USE_LIMITED_ANTILAG == 1 )
        float minAccumSpeed = min( accumSpeed, ( REBLUR_MIP_NUM - 1 ) * STL::Math::Sqrt01( roughness ) );
    #else
        float minAccumSpeed = 0;
    #endif

    accumSpeed = InterpolateAccumSpeeds( minAccumSpeed, accumSpeed, antilag );

    #if( REBLUR_USE_FASTER_BUT_DIRTIER_ANTILAG == 1 )
        antilag = 1.0; // it means that antilag won't affect TS accumulation parameters
    #endif

    return antilag;
}

// Internal data - diffuse

float2 PackDiffInternalData( float accumSpeed, float edge )
{
    float2 r;
    r.x = saturate( accumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.y = edge;

    return r;
}

float2 UnpackDiffInternalData( float2 p )
{
    float accumSpeed = p.x * REBLUR_MAX_ACCUM_FRAME_NUM;

    float2 r;
    r.x = 1.0 / ( 1.0 + accumSpeed );
    r.y = accumSpeed;

    return r;
}

float2 UnpackDiffInternalData( float2 p, out float edge )
{
    edge = p.y;

    return UnpackDiffInternalData( p );
}

// Internal data - specular

float3 PackSpecInternalData( float accumSpeed, float edge, float virtualHistoryAmount )
{
    float3 r;
    r.x = saturate( accumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.y = edge;
    r.z = virtualHistoryAmount;

    return r;
}

float2 UnpackSpecInternalData( float3 p, float roughness )
{
    float accumSpeed = p.x * REBLUR_MAX_ACCUM_FRAME_NUM;

    // TODO: The initial idea was to rescale potentially small "p.x" back to REBLUR_MAX_ACCUM_FRAME_NUM according to maximum allowed accumulated number of frames:
    //   1 / ( 1 + p.x / rescale ) = 1 / ( ( rescale + p.x ) / rescale ) = rescale / ( rescale + p.x )
    // But it looks like rescaling is not needed because:
    // - non linear weights converges quickly
    // - if Nmax is small, blur radius is small too
    // - rescaling could worsen problematic cases for moderate roughness (for example fast strafing standing in front of a metallic surface)
    float rescale = 1.0;
    //rescale = max( GetSpecAccumulatedFrameNum( roughness, 1.0 ), 1.0 ) / REBLUR_MAX_ACCUM_FRAME_NUM;
    //rescale = STL::Math::Pow01( rescale, 0.25 );

    float2 r;
    r.x = rescale / ( rescale + accumSpeed );
    r.y = accumSpeed;

    return r;
}

float2 UnpackSpecInternalData( float3 p, float roughness, out float edge )
{
    edge = p.y;

    return UnpackSpecInternalData( p, roughness );
}

float2 UnpackSpecInternalData( float3 p, float roughness, out float edge, out float virtualHistoryAmount )
{
    edge = p.y;
    virtualHistoryAmount = p.z;

    return UnpackSpecInternalData( p, roughness );
}

// Internal data - diffuse and specular

float4 PackDiffSpecInternalData( float diffAccumSpeed, float specAccumSpeed, float edge, float virtualHistoryAmount )
{
    float4 r;
    r.x = saturate( diffAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.y = saturate( specAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.z = edge;
    r.w = virtualHistoryAmount;

    return r;
}

float4 UnpackDiffSpecInternalData( float4 p, float roughness )
{
    float4 r;
    r.xy = UnpackDiffInternalData( p.xz );
    r.zw = UnpackSpecInternalData( p.yzw, roughness );

    return r;
}

float4 UnpackDiffSpecInternalData( float4 p, float roughness, out float edge )
{
    edge = p.z;

    return UnpackDiffSpecInternalData( p, roughness );
}

float4 UnpackDiffSpecInternalData( float4 p, float roughness, out float edge, out float virtualHistoryAmount )
{
    edge = p.z;
    virtualHistoryAmount = p.w;

    return UnpackDiffSpecInternalData( p, roughness );
}

// Accumulation speed

float GetMipLevel( float accumSpeed, float roughness = 1.0 )
{
    float mip = max( REBLUR_MIP_NUM - accumSpeed, 0.0 );

    float f = STL::Math::LinearStep( 3.0, 0.0, accumSpeed );
    roughness += 0.25 * f * STL::Math::Pow01( roughness, 0.25 );

    return mip * STL::Math::Sqrt01( roughness );
}

float GetAccumSpeed( float4 prevAccumSpeed, float4 weights, float maxAccumulatedFrameNum )
{
    float4 accumSpeeds = prevAccumSpeed + 1.0;
    float accumSpeedUnclamped = STL::Filtering::ApplyBilinearCustomWeights( accumSpeeds.x, accumSpeeds.y, accumSpeeds.z, accumSpeeds.w, weights );
    float accumSpeed = min( accumSpeedUnclamped, maxAccumulatedFrameNum );

    return accumSpeed;
}

float GetSpecAccumSpeed( float maxAccumSpeed, float roughness, float NoV, float parallax )
{
    float acos01sq = saturate( 1.0 - NoV ); // see AcosApprox()

    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIoMS4wNSsoeCp4KV4xLjApLygxLjA1LSh4KngpXjEuMCkiLCJjb2xvciI6IiM1MkExMDgifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC42NikvKDEuMDUtKHgqeCleMC42NikiLCJjb2xvciI6IiNFM0Q4MDkifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC41KS8oMS4wNS0oeCp4KV4wLjUpIiwiY29sb3IiOiIjRjUwQTMxIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiNDIiXSwic2l6ZSI6WzE5MDAsNzAwXX1d
    float a = STL::Math::Pow01( acos01sq, lerp( REBLUR_SPEC_ACCUM_CURVE, 0.5, gReference ) );
    float b = 1.1 + roughness * roughness;
    float parallaxSensitivity = ( b + a ) / ( b - a );

    float powerScale = 1.0 + parallax * parallaxSensitivity;
    float accumSpeed = GetSpecAccumulatedFrameNum( roughness, powerScale );

    accumSpeed = min( accumSpeed, maxAccumSpeed );

    return accumSpeed * float( gFrameIndex != 0 ); // with history reset
}

// Kernel

float GetBlurRadius( float radius, float roughness, float hitDist, float3 Xv, float nonLinearAccumSpeed, float scale = 1.0 )
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

    // A non zero addition is needed to:
    // - avoid getting ~0 blur radius
    // - avoid "fluffiness"
    // - keep adaptive scale in post blur in a working state (TODO: adaptive scale adds own addon)
    float addon = 1.5 * STL::Math::SmoothStep( 0.0, 0.25, roughness );
    addon *= lerp( 0.5, 1.0, hitDistFactor );
    addon *= float( radius != 0.0 );

    return s * radius * scale + addon;
}

float GetBlurRadiusScaleBasingOnTrimming( float roughness, float3 trimmingParams )
{
    float trimmingFactor = NRD_GetTrimmingFactor( roughness, trimmingParams );
    float maxScale = 1.0 + 4.0 * roughness * roughness;
    float scale = lerp( maxScale, 1.0, trimmingFactor );

    // TODO: for roughness ~0.2 and trimming = 0 blur radius will be so large and amount of accumulation will be so small that a strobbing effect can appear under motion
    return scale;
}

float2x3 GetKernelBasis( float3 X, float3 N, float worldRadius, float edge, float roughness = 1.0 )
{
    float3x3 basis = STL::Geometry::GetBasis( N );
    float3 T = basis[ 0 ];
    float3 B = basis[ 1 ];

    float3 V = -normalize( X );
    float3 D = STL::ImportanceSampling::GetSpecularDominantDirection( N, V, roughness, REBLUR_SPEC_DOMINANT_DIRECTION ).xyz;
    float NoD = abs( dot( N, D ) );

    if( NoD < 0.999 && roughness < REBLUR_SPEC_BASIS_ROUGHNESS_THRESHOLD )
    {
        float3 R = reflect( -D, N );
        T = normalize( cross( N, R ) );
        B = cross( R, T );

        #if( REBLUR_USE_ANISOTROPIC_KERNEL == 1 )
            float NoV = abs( dot( N, V ) );
            float acos01sq = saturate( 1.0 - NoV ); // see AcosApprox()
            float skewFactor = lerp( 1.0, roughness, STL::Math::Sqrt01( acos01sq ) );
            T *= lerp( skewFactor, 1.0, edge );
        #endif
    }

    T *= worldRadius;
    B *= worldRadius;

    return float2x3( T, B );
}

// Weight parameters

float GetNormalWeightParamsRoughEstimate( float roughness )
{
    float ang01 = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness ) / STL::Math::DegToRad( 90.0 );
    float angle = STL::Math::DegToRad( lerp( 45.0, 89.0, saturate( ang01 ) ) ); // Yes, very relaxed angles here to not ruin accumulation with enabled jittering. Definitely min angle can't be < 25 deg

    return rcp( angle );
}

float GetNormalWeightParams( float viewZ, float roughness, float edge, float nonLinearAccumSpeed )
{
    float r = PixelRadiusToWorld( gUnproject, gIsOrtho, gScreenSize.y, viewZ );
    float a = 1.0 / ( r + 1.0 ); // estimate normalized angular size
    float b = lerp( 0.15, 0.02, a ); // % of lobe angle
    float f = max( nonLinearAccumSpeed, 0.5 * edge ); // less strict on edges

    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
    angle *= lerp( b, 1.0, f );
    angle += REBLUR_NORMAL_BANDING_FIX;

    return rcp( angle );
}

float2 GetRoughnessWeightParams( float roughness0 )
{
    float a = rcp( roughness0 * 0.2 * 0.99 + 0.01 );
    float b = roughness0 * a;

    return float2( a, -b );
}

float2 GetHitDistanceWeightParams( float hitDist0, float nonLinearAccumSpeed, float hitDist, float3 Xv, float roughness = 1.0 )
{
    // TODO: hit distance weight seems to be very aggressive in some cases
    float accumSpeed = 1.0 / nonLinearAccumSpeed - 1.0;
    float rescale = 1.0 / max( GetSpecAccumulatedFrameNum( roughness, 1.0 ), 1.0 );

    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIxLXNxcnQoeC8zMSkiLCJjb2xvciI6IiNGMDA5MDkifSx7InR5cGUiOjAsImVxIjoiMS8oMSt4KSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiIyXigteCp4KjEvNjQpIiwiY29sb3IiOiIjNjhGMDBFIn0seyJ0eXBlIjowLCJlcSI6IjJeKC14KjE1LzY0KSIsImNvbG9yIjoiIzExNkRGNyJ9LHsidHlwZSI6MTAwMCwid2luZG93IjpbIjAiLCI2NCIsIjAiLCIxIl19XQ--
    float f = exp2( -accumSpeed * rescale * 15.0 );

    float hitDistFactor = hitDist / ( hitDist + abs( Xv.z ) );
    float scale = lerp( 0.1, 1.0, f );
    scale = lerp( scale, 1.0, hitDistFactor * hitDistFactor );

    float a = rcp( hitDist0 * scale * 0.99 + 0.01 );
    float b = hitDist0 * a;

    return float2( a, -b );
}

float2 GetTemporalAccumulationParams( float isInScreen, float accumSpeed, float parallax, float roughness = 1.0, float virtualHistoryAmount = 0.0 )
{
    const float maxStabilizationMinFrameNum = 16.0;
    const float norm = maxStabilizationMinFrameNum / ( 1.0 + maxStabilizationMinFrameNum );

    float motionLength = GetParallaxInPixels( parallax );

    parallax = STL::Math::Pow01( parallax, 0.5 ); // TODO: was 0.25 only if used in TA
    parallax *= 1.0 - virtualHistoryAmount;

    // Rescale back to max range
    accumSpeed *= REBLUR_MAX_ACCUM_FRAME_NUM / max( GetSpecAccumulatedFrameNum( roughness, 1.0 ), 0.25 );

    float oneMinusNonLinearAccumSpeed = accumSpeed / ( 1.0 + accumSpeed );
    float roughnessWeight = STL::Math::SmoothStep( 0.0, 0.75, roughness );
    float sigmaAmplitude = oneMinusNonLinearAccumSpeed;
    sigmaAmplitude *= lerp( saturate( 1.0 - parallax ), 1.0, roughnessWeight );
    sigmaAmplitude *= STL::Math::LinearStep( 0.01, 0.05, roughness );
    sigmaAmplitude = 1.0 + REBLUR_TS_SIGMA_AMPLITUDE * sigmaAmplitude;

    float historyWeight = isInScreen;
    historyWeight *= 1.0 - STL::Math::SmoothStep( 0.0, REBLUR_TS_MOTION_MAX_REUSE * gScreenSize.x, motionLength );
    historyWeight *= saturate( oneMinusNonLinearAccumSpeed / norm );
    historyWeight *= lerp( saturate( 1.0 - parallax ), 1.0, roughnessWeight );
    historyWeight *= lerp( 1.0, oneMinusNonLinearAccumSpeed * saturate( 1.0 - parallax ), gReference );

    return float2( historyWeight, sigmaAmplitude ) * float( gFrameIndex != 0 );
}

// Weights

float GetNormalWeight( float params0, float3 n0, float3 n )
{
    float cosa = saturate( dot( n0, n ) );
    float angle = STL::Math::AcosApprox( cosa );

    return _ComputeWeight( float2( params0, -0.001 ), angle );
}

#define GetRoughnessWeight _ComputeWeight
#define GetHitDistanceWeight _ComputeWeight

// Upsampling

float4 ReconstructHistory( float4 center, uint realMipLevel, uint2 screenSizei, float2 pixelUv, float z0, Texture2D<float> texScaledViewZ, Texture2D<float4> texSignal )
{
    float sum = 0.0;
    float4 blurry = 0.0;

    while( sum == 0.0 && realMipLevel != 0 )
    {
        uint mipLevel = realMipLevel - 1;
        float2 mipSize = float2( screenSizei >> realMipLevel );
        float2 invMipSize = 1.0 / mipSize;
        float2 mipUv = pixelUv * gScreenSize / ( mipSize * float( 1 << realMipLevel ) );

        [unroll]
        for( int i = -1; i <= 1; i++ )
        {
            [unroll]
            for( int j = -1; j <= 1; j++ )
            {
                float2 offset = float2( i, j );
                float2 uv = mipUv + offset * 0.5 * invMipSize;
                float2 uvScaled = uv * gResolutionScale;

                float z = texScaledViewZ.SampleLevel( gLinearClamp, uvScaled, realMipLevel );

                float w = GetBilateralWeight( z, z0 );
                w *= exp2( -dot( offset, offset ) );
                w *= IsInScreen( uv );

                float4 s = texSignal.SampleLevel( gLinearClamp, uvScaled, mipLevel );

                blurry += s * w;
                sum += w;
            }
        }

        realMipLevel--;
    }

    blurry = sum == 0.0 ? center : ( blurry / sum );

    return blurry;
}
