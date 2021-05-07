/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// Debug

#define REBLUR_SHOW_ACCUM_SPEED                         1
#define REBLUR_SHOW_VIRTUAL_HISTORY_AMOUNT              2
#define REBLUR_SHOW_VIRTUAL_HISTORY_CONFIDENCE          3
#define REBLUR_SHOW_PARALLAX                            4
#define REBLUR_SHOW_EDGE                                5
#define REBLUR_DEBUG                                    0 // 1-5

// Spatial filtering

#define REBLUR_PRE_BLUR                                 0
#define REBLUR_BLUR                                     1
#define REBLUR_POST_BLUR                                2

// Storage

#define REBLUR_VIEWZ_ACCUMSPEED_BITS                    26, 6, 0, 0
#define REBLUR_NORMAL_ROUGHNESS_ACCUMSPEED_BITS         9, 9, 8, 6
#define REBLUR_MAX_ACCUM_FRAME_NUM                      63 // 6 bits

// Shared data

groupshared float4 s_Normal_Roughness[ BUFFER_Y ][ BUFFER_X ];
groupshared float s_ViewZ[ BUFFER_Y ][ BUFFER_X ];

// Radiance and hit distance compression

float4 CompressRadianceAndNormHitDist( float3 radiance, float normHitDist, float viewZ, float4 hitDistParams, float roughness )
{
    float exposure = GetColorCompressionExposure( roughness );
    float3 compressedRadiance = STL::Color::Compress( radiance, exposure );

    return float4( compressedRadiance, normHitDist );
}

float DecompressNormHitDistance( float compressedHitDist, float viewZ, float4 hitDistParams, float roughness )
{
    return compressedHitDist;
}

float4 DecompressRadianceAndNormHitDist( float3 compressedRadiance, float compressedHitDist, float viewZ, float4 hitDistParams, float roughness )
{
    float exposure = GetColorCompressionExposure( roughness );
    float3 radiance = STL::Color::Decompress( compressedRadiance, exposure );

    float normHitDist = DecompressNormHitDistance( compressedHitDist, viewZ, hitDistParams, roughness );

    return float4( radiance, normHitDist );
}

float GetHitDist( float compressedHitDist, float viewZ, float4 hitDistParams, float roughness, bool isDecompressionNeeded = false )
{
    float normHitDist = isDecompressionNeeded ? DecompressNormHitDistance( compressedHitDist, viewZ, hitDistParams, roughness ) : compressedHitDist;
    float f = _REBLUR_GetHitDistanceNormalization( viewZ, hitDistParams, roughness );

    return normHitDist * f;
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

    float p = STL::Math::PositiveRcp( dot( X, Xt ) );
    float t = STL::Math::LengthSquared( X ) * p;
    t *= STL::Math::LengthSquared( Xt ) * p;
    float parallax = STL::Math::Sqrt01( t - 1.0 );

    parallax *= STL::Math::LinearStep( 0.0006, 0.0012, parallax ); // TODO: fix numerical instabilities
    parallax *= REBLUR_PARALLAX_NORMALIZATION;

    return parallax;
}

float GetParallaxInPixels( float parallax )
{
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

float GetColorErrorForAdaptiveRadiusScale( float4 curr, float4 prev, float nonLinearAccumSpeed, float roughness )
{
    float2 p = float2( _NRD_Luminance( prev.xyz ), prev.w );
    float2 c = float2( _NRD_Luminance( curr.xyz ), curr.w );
    float2 f = abs( c - p ) / ( max( c, p ) + 0.001 );

    float error = max( f.x, f.y );
    error = STL::Math::SmoothStep( 0.0, REBLUR_MAX_ERROR_AMPLITUDE, error );
    error *= STL::Math::LinearStep( 0.04, 0.1, roughness );
    error *= 1.0 - nonLinearAccumSpeed;
    error *= 1.0 - gReference;

    return error;
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
        float minAccumSpeed = min( accumSpeed, ( REBLUR_FRAME_NUM_WITH_HISTORY_FIX - 1 ) * STL::Math::Sqrt01( roughness ) );
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

    float2 r;
    r.x = 1.0 / ( 1.0 + accumSpeed );
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

float GetBlurRadius( float radius, float roughness, float hitDist, float viewZ, float nonLinearAccumSpeed, float accumSpeed, float boost, float error )
{
    // Base radius
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIxLjAtMl4oLTE1LjAqeCkiLCJjb2xvciI6IiMwMDAwMDAifSx7InR5cGUiOjAsImVxIjoiKDEtMl4oLTIwMCp4KngpKSooeF4wLjI1KSIsImNvbG9yIjoiIzIyRUQxNyJ9LHsidHlwZSI6MTAwMCwid2luZG93IjpbIjAiLCIxIiwiMCIsIjEuMSJdLCJzaXplIjpbMTAwMCw1MDBdfV0-
    float t = STL::Math::Pow01( roughness, 0.25 );
    float s = ( 1.0 - exp2( -200.0 * roughness * roughness ) ) * t; // very similar to the function used in GetSpecAccumulatedFrameNum()

    float minAccumSpeed = ( REBLUR_FRAME_NUM_WITH_HISTORY_FIX - 1 ) * STL::Math::Sqrt01( roughness ) + 0.01;
    float f = saturate( accumSpeed / minAccumSpeed );

    // Modify by hit distance
    float hitDistFactor = hitDist / ( hitDist + viewZ );
    hitDistFactor = lerp( 1.0, hitDistFactor, f );
    s *= hitDistFactor;

    // Scale down if accumulation goes well
    s *= nonLinearAccumSpeed;

    // A non zero addition is needed to:
    // - does not introduce bias, but needed to avoid it!
    // - avoid "fluffiness"
    // - keep adaptive scale in post blur in a working state
    float addon = 3.0 * ( 1.0 + 2.0 * boost );
    addon = min( addon, radius * 0.333 );
    addon *= roughness;
    addon *= hitDistFactor;
    addon *= error;

    return s * radius + addon;
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

float GetNormalWeightParams( float nonLinearAccumSpeed, float edge, float error, float roughness = 1.0 )
{
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );

    float s = lerp( 0.05, 0.15, error );
    s = lerp( s, 1.0, edge );
    s = lerp( s, 1.0, nonLinearAccumSpeed );
    angle *= s;

    angle += REBLUR_NORMAL_BANDING_FIX;

    return rcp( angle );
}

float GetNormalWeightParams2( float nonLinearAccumSpeed, float edge, float error, float3 Xv, float3 Nv, float strictness, float roughness = 1.0 )
{
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );

    float s = lerp( 0.05, 0.15, error ); // TODO: 0.15 can be different for BLUR / POST_BLUR passes

    #if 1
        // TODO: without this addition it's GetNormalWeightParams
        // - needed to avoid underblurring on curved surfaces (if the signal is bad)
        // - ideally shouldn't be here
        // - retune?

        s *= strictness;

        // Use bigger angle if normalized angular size is small
        float r = PixelRadiusToWorld( gUnproject, gIsOrtho, gScreenSize.y, Xv.z );
        float a = 1.0 / ( r + 1.0 );
        s *= lerp( lerp( 1.0, 3.0, roughness ), 1.0, a );

        // Use bigger angle on high slopes
        float cosa = saturate( dot( Nv, -normalize( Xv ) ) );
        s = lerp( s, 0.5, STL::BRDF::Pow5( cosa ) * roughness );
    #endif

    s = lerp( s, 1.0, edge );
    s = lerp( s, 1.0, nonLinearAccumSpeed );
    angle *= s;

    angle += REBLUR_NORMAL_BANDING_FIX;

    return rcp( angle );
}

float2 GetRoughnessWeightParams( float roughness0 )
{
    float a = rcp( roughness0 * 0.2 * 0.99 + 0.01 );
    float b = roughness0 * a;

    return float2( a, -b );
}

float2 GetHitDistanceWeightParams( float normHitDist, float nonLinearAccumSpeed, float roughness = 1.0 )
{
    float threshold = exp2( -17.0 * roughness * roughness );
    float scale = lerp( threshold, 1.0, nonLinearAccumSpeed );

    float a = rcp( normHitDist * scale * 0.99 + 0.01 );
    float b = normHitDist * a;

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

float GetGaussianWeight( float r )
{
    // radius is normalized to 1
    return exp( -0.66 * r * r );
}

#define GetRoughnessWeight _ComputeWeight
#define GetHitDistanceWeight _ComputeWeight

// Upsampling
// TODO: history reconstruction can be merged into a single loop for diffuse and specular. Previously it worked slower. Try again?

float4 ReconstructHistoryDiff( float2 diffinternalData, float2 pixelPos, float zScaled, float3 N, float4 rotator, float4x4 mWorldToView, Texture2D<float> texScaledViewZ, Texture2D<float4> texDiff )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    float4 diff = texDiff[ pixelPos ];
    float frameNum = REBLUR_FRAME_NUM_WITH_HISTORY_FIX;

    [branch]
    if( diffinternalData.y < frameNum && REBLUR_USE_HISTORY_FIX == 1 )
    {
        float3 Nv = STL::Geometry::RotateVector( mWorldToView, N );

        float sum = 1.0;
        float2 scale = REBLUR_HISTORY_FIX_MAX_RADIUS * diffinternalData.x * abs( Nv.z ) * gInvScreenSize; // TODO: should max radius depend on parallax?

        [unroll]
        for( int i = 0; i < REBLUR_HISTORY_FIX_SAMPLE_NUM; i++ )
        {
            float2 offset = STL::Geometry::RotateVector( rotator, REBLUR_HISTORY_FIX_SAMPLES[ i ].xy );
            float2 uv = pixelUv + offset * scale;
            float2 uvScaled = uv * gResolutionScale;

            float zsScaled = texScaledViewZ.SampleLevel( gNearestClamp, uvScaled, 0 );

            float w = IsInScreen( uv );
            w *= GetBilateralWeight( zsScaled, zScaled );

            float4 d = texDiff.SampleLevel( gNearestClamp, uvScaled, 0 );
            diff += d * w;
            sum += w;
        }

        diff /= sum;
    }

    return diff;
}

float4 ReconstructHistorySpec( float2 specInternalData, uint2 pixelPos, float zScaled, float3 N, float4 rotator, float4x4 mWorldToView, Texture2D<float> texScaledViewZ, Texture2D<float4> texSpec, float roughness, Texture2D<float4> texNormal )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    float4 spec = texSpec[ pixelPos ];
    float frameNum = REBLUR_FRAME_NUM_WITH_HISTORY_FIX * STL::Math::Sqrt01( roughness );

    [branch]
    if( specInternalData.y < frameNum && REBLUR_USE_HISTORY_FIX == 1 )
    {
        float specNormalParams = GetNormalWeightParamsRoughEstimate( roughness );
        float2 roughnessWeightParams = GetRoughnessWeightParams( roughness );

        float dominantFactor = STL::ImportanceSampling::GetSpecularDominantFactor( 0, roughness, STL_SPECULAR_DOMINANT_DIRECTION_APPROX );
        float3 Nv = STL::Geometry::RotateVector( mWorldToView, N );
        float3 Vv = -normalize( STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, 1.0, gIsOrtho ) );
        float3 Dv = STL::ImportanceSampling::GetSpecularDominantDirectionWithFactor( Nv, Vv, dominantFactor );

        float sum = 1.0;
        float2 scale = REBLUR_HISTORY_FIX_MAX_RADIUS * specInternalData.x * abs( Nv.z ) * STL::Math::Sqrt01( roughness ) * gInvScreenSize; // TODO: should max radius depend on parallax?

        [unroll]
        for( int i = 0; i < REBLUR_HISTORY_FIX_SAMPLE_NUM; i++ )
        {
            float2 offset = STL::Geometry::RotateVector( rotator, REBLUR_HISTORY_FIX_SAMPLES[ i ].xy );
            float2 uv = pixelUv + offset * scale;
            float2 uvScaled = uv * gResolutionScale;

            float zsScaled = texScaledViewZ.SampleLevel( gNearestClamp, uvScaled, 0 );
            float4 Ns = texNormal.SampleLevel( gNearestMirror, uvScaled + gRectOffset, 0 );
            Ns = _NRD_FrontEnd_UnpackNormalAndRoughness( Ns );

            // Weight
            float w = IsInScreen( uv );
            w *= GetBilateralWeight( zsScaled, zScaled );
            w *= GetRoughnessWeight( roughnessWeightParams, Ns.w );

            #if( REBLUR_USE_DOMINANT_DIRECTION_IN_WEIGHT == 1 )
                float3 Nvs = STL::Geometry::RotateVector( mWorldToView, Ns.xyz );
                float3 Vvs = -normalize( STL::Geometry::ReconstructViewPosition( uv, gFrustum, 1.0, gIsOrtho ) );
                float3 Dvs = STL::ImportanceSampling::GetSpecularDominantDirectionWithFactor( Nvs, Vvs, dominantFactor );
                w *= GetNormalWeight( specNormalParams, Dv, Dvs );
            #else
                w *= GetNormalWeight( specNormalParams, N, Ns.xyz );
            #endif

            float4 s = texSpec.SampleLevel( gNearestClamp, uvScaled, 0 );
            spec += s * w;
            sum += w;
        }

        spec /= sum;
    }

    return spec;
}
