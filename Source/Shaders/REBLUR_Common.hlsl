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

#define REBLUR_ACCUMSPEED_BITS                          6
#define REBLUR_NORMAL_ROUGHNESS_ACCUMSPEED_BITS         9, 9, 14 - REBLUR_ACCUMSPEED_BITS, REBLUR_ACCUMSPEED_BITS
#define REBLUR_MAX_ACCUM_FRAME_NUM                      ( (1 << REBLUR_ACCUMSPEED_BITS ) - 1 )

// Shared data

groupshared float4 s_Normal_Roughness[ BUFFER_Y ][ BUFFER_X ];
groupshared float s_ViewZ[ BUFFER_Y ][ BUFFER_X ];

float GetHitDist( float normHitDist, float viewZ, float4 hitDistParams, float roughness )
{
    float f = _REBLUR_GetHitDistanceNormalization( viewZ, hitDistParams, roughness );

    return normHitDist * f;
}

// Data packing for the next frame

uint2 PackViewZNormalRoughnessAccumSpeeds( float viewZ, float diffAccumSpeed, float3 N, float roughness, float specAccumSpeed )
{
    float t1 = diffAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM;
    uint crunchedViewZ = asuint( viewZ ) & ~REBLUR_MAX_ACCUM_FRAME_NUM;

    float4 t2;
    t2.xy = STL::Packing::EncodeUnitVector( N );
    t2.z = roughness;
    t2.w = specAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM;

    uint2 p;
    p.x = crunchedViewZ | STL::Packing::RgbaToUint( t1, REBLUR_ACCUMSPEED_BITS, 0, 0, 0 ).x;
    p.y = STL::Packing::RgbaToUint( t2, REBLUR_NORMAL_ROUGHNESS_ACCUMSPEED_BITS );

    return p;
}

float4 UnpackViewZ( uint4 p )
{
    return asfloat( p & ~REBLUR_MAX_ACCUM_FRAME_NUM );
}

float4 UnpackDiffAccumSpeed( uint4 p )
{
    return float4( p & REBLUR_MAX_ACCUM_FRAME_NUM );
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

float GetSpecMagicCurve( float roughness, float power = 0.25 )
{
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIxLjAtMl4oLTE1LjAqeCkiLCJjb2xvciI6IiNGMjE4MTgifSx7InR5cGUiOjAsImVxIjoiKDEtMl4oLTIwMCp4KngpKSooeF4wLjI1KSIsImNvbG9yIjoiIzIyRUQxNyJ9LHsidHlwZSI6MCwiZXEiOiIoMS0yXigtMjAwKngqeCkpKih4XjAuNSkiLCJjb2xvciI6IiMxNzE2MTYifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIxLjEiXSwic2l6ZSI6WzEwMDAsNTAwXX1d

    float f = 1.0 - exp2( -200.0 * roughness * roughness );
    f *= STL::Math::Pow01( roughness, power );

    return f;
}

float GetSpecAccumulatedFrameNum( float roughness, float powerScale )
{
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIzMSooeF4wLjY2KSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiIzMSooMS0yXigtMjAwKngqeCkpKih4XjAuNSkiLCJjb2xvciI6IiNGQTBEMEQifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIzMSJdfV0-

    return REBLUR_MAX_ACCUM_FRAME_NUM * GetSpecMagicCurve( roughness, REBLUR_SPEC_ACCUM_BASE_POWER * powerScale );
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

float4 MixLinearAndCatmullRom( float4 linearX, float4 catromX, float occlusionAvg )
{
    catromX = max( catromX, 0.0 );

    return ( occlusionAvg < 1.0 || REBLUR_USE_CATROM_RESAMPLING_IN_TA == 0 || gReference != 0.0 ) ? linearX : catromX;
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

float4 InterpolateSurfaceAndVirtualMotion( float4 s, float4 v, float virtualHistoryAmount, float hitDistToSurfaceRatio )
{
    float2 f;
    f.x = virtualHistoryAmount;
    f.y = virtualHistoryAmount * hitDistToSurfaceRatio;

    return lerp( s, v, f.xxxy );
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

float GetMipLevel( float roughness, float maxFastAccumulatedFrameNum )
{
    float mip = min( REBLUR_MIP_NUM, maxFastAccumulatedFrameNum );

    return mip * GetSpecMagicCurve( roughness );
}

float ComputeAntilagScale( inout float accumSpeed, float4 history, float4 m1, float4 sigma, float2 temporalAccumulationParams, float maxFastAccumulatedFrameNum, float4 antilag1, float4 antilag2, float roughness = 1.0 )
{
    float2 h = float2( _NRD_Luminance( history.xyz ), history.w );
    float2 m = float2( _NRD_Luminance( m1.xyz ), m1.w );
    float2 s = float2( _NRD_Luminance( sigma.xyz ), sigma.w );

    // Artificially increase sensitivity to darkness for low roughness, because specular can be very hot
    float sensitivityToDarknessScale = lerp( 3.0, 1.0, STL::Math::Sqrt01( roughness ) );

    float2 delta = abs( h - m ) - s * antilag1.xy;
    delta /= max( m, h ) + s * antilag1.xy + antilag1.zw * sensitivityToDarknessScale;
    delta = STL::Math::SmoothStep( antilag2.zw, antilag2.xy, delta );

    float fade = accumSpeed / ( 1.0 + accumSpeed );
    fade *= temporalAccumulationParams.x;

    float antilag = min( delta.x, delta.y );
    antilag = lerp( 1.0, antilag, fade );

    #if( REBLUR_USE_ANTILAG == 0 )
        antilag = 1.0;
    #endif

    #if( REBLUR_USE_LIMITED_ANTILAG == 1 )
        float minAccumSpeed = min( accumSpeed, GetMipLevel( roughness, maxFastAccumulatedFrameNum ) );
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

float2 UnpackSpecInternalData( float3 p )
{
    float accumSpeed = p.x * REBLUR_MAX_ACCUM_FRAME_NUM;

    float2 r;
    r.x = 1.0 / ( 1.0 + accumSpeed );
    r.y = accumSpeed;

    return r;
}

float2 UnpackSpecInternalData( float3 p, out float edge )
{
    edge = p.y;

    return UnpackSpecInternalData( p );
}

float2 UnpackSpecInternalData( float3 p, out float edge, out float virtualHistoryAmount )
{
    edge = p.y;
    virtualHistoryAmount = p.z;

    return UnpackSpecInternalData( p );
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

float4 UnpackDiffSpecInternalData( float4 p )
{
    float4 r;
    r.xy = UnpackDiffInternalData( p.xz );
    r.zw = UnpackSpecInternalData( p.yzw );

    return r;
}

float4 UnpackDiffSpecInternalData( float4 p, out float edge )
{
    edge = p.z;

    return UnpackDiffSpecInternalData( p );
}

float4 UnpackDiffSpecInternalData( float4 p, out float edge, out float virtualHistoryAmount )
{
    edge = p.z;
    virtualHistoryAmount = p.w;

    return UnpackDiffSpecInternalData( p );
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

float GetBlurRadius( float radius, float roughness, float hitDist, float viewZ, float nonLinearAccumSpeed, float boost, float error, float radiusBias, float radiusScale )
{
    // Modify by hit distance
    float hitDistFactor = hitDist / ( hitDist + viewZ );
    float s = hitDistFactor;

    // Scale down if accumulation goes well
    float keepBlurringDistantReflections = saturate( 1.0 - STL::Math::Pow01( roughness, 0.125 ) ) * hitDistFactor;
    s *= lerp( keepBlurringDistantReflections * float( radiusBias != 0.0 ), 1.0, nonLinearAccumSpeed ); // TODO: not apply in BLUR pass too?

    // A non zero addition is needed to avoid under-blurring:
    float addon = 3.0 * ( 1.0 + 2.0 * boost );
    addon = min( addon, radius * 0.333 );
    addon *= roughness;
    addon *= hitDistFactor;
    // addon *= error; // TODO: adds bias

    // Avoid over-blurring on contact
    radiusBias *= lerp( roughness, 1.0, hitDistFactor );

    // Final blur radius
    float r = s * radius + addon;
    r = r * ( radiusScale + radiusBias ) + radiusBias;
    r *= GetSpecMagicCurve( roughness );
    r *= 1.0 - gReference;

    return r;
}

float GetBlurRadiusScaleBasingOnTrimming( float roughness, float3 trimmingParams )
{
    float trimmingFactor = NRD_GetTrimmingFactor( roughness, trimmingParams );
    float maxScale = 1.0 + 4.0 * roughness * roughness;
    float scale = lerp( maxScale, 1.0, trimmingFactor );

    // TODO: for roughness ~0.2 and trimming = 0 blur radius will be so large and amount of accumulation will be so small that a strobbing effect can appear under motion
    return scale;
}

float2x3 GetKernelBasis( float3 X, float3 N, float worldRadius, float roughness = 1.0, float anisoFade = 1.0 )
{
    float3x3 basis = STL::Geometry::GetBasis( N );
    float3 T = basis[ 0 ];
    float3 B = basis[ 1 ];

    float3 V = -normalize( X );
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection( N, V, roughness, REBLUR_SPEC_DOMINANT_DIRECTION );
    float NoD = abs( dot( N, D.xyz ) );

    if( NoD < 0.999 && roughness < REBLUR_SPEC_BASIS_ROUGHNESS_THRESHOLD )
    {
        float3 R = reflect( -D.xyz, N );
        T = normalize( cross( N, R ) );
        B = cross( R, T );

        #if( REBLUR_USE_ANISOTROPIC_KERNEL == 1 )
            float NoV = abs( dot( N, V ) );
            float acos01sq = saturate( 1.0 - NoV ); // see AcosApprox()

            float skewFactor = lerp( 1.0, roughness, D.w );
            skewFactor = lerp( 1.0, skewFactor, STL::Math::Sqrt01( acos01sq ) );

            T *= lerp( skewFactor, 1.0, anisoFade );
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

float GetNormalWeightParams( float nonLinearAccumSpeed, float edge, float error, float roughness = 1.0, float strictness = 1.0 )
{
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );

    // TODO: 0.15 can be different for blur passes
    // TODO: curvature is needed to estimate initial scale
    float s = lerp( 0.04, 0.15, error );
    s *= strictness;

    s = lerp( s, 1.0, edge );
    s = lerp( s, 1.0, nonLinearAccumSpeed );
    angle *= s;

    angle += REBLUR_NORMAL_BANDING_FIX;

    return rcp( angle );
}

float2 GetRoughnessWeightParams( float roughness0, bool strictness = 0.0 )
{
    float a = rcp( roughness0 * 0.2 * 0.99 + 0.01 );
    float b = roughness0 * a;

    return float2( a, -b );
}

float2 GetRoughnessWeightParamForSpatialPasses( float roughness0 )
{
    float a = rcp( roughness0 * 0.05 * 0.99 + 0.01 );
    float b = roughness0 * a;

    return float2( a, -b );
}

float2 GetHitDistanceWeightParams( float normHitDist, float nonLinearAccumSpeed, float roughness = 1.0 )
{
    float threshold = exp2( -17.0 * roughness * roughness ); // TODO: not in line with other weights
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

float4 ReconstructHistory( float accumSpeed, float roughness, uint2 pixelPos, float2 pixelUv, float z0, float maxFastAccumulatedFrameNum, Texture2D<float> texScaledViewZ, Texture2D<float4> texSignal )
{
    float4 center = texSignal[ pixelPos ];
    float4 blurry = 0.0;
    float sum = 0.0;

    float mipLevel = GetMipLevel( roughness, maxFastAccumulatedFrameNum );
    mipLevel = max( mipLevel - accumSpeed, 0.0 );
    mipLevel = floor( mipLevel ) * REBLUR_USE_HISTORY_FIX;

    while( sum == 0.0 && mipLevel > 0.0 )
    {
        float2 mipSize = gScreenSize * exp2( -mipLevel );
        float2 invMipSize = 1.0 / mipSize;

        #if( REBLUR_USE_WIDER_KERNEL_IN_HISTORY_FIX == 1 )
            float2 kernelCenter = pixelUv;
        #else
            float2 kernelCenter = pixelUv - 0.5 * invMipSize;
        #endif

        STL::Filtering::Bilinear filter = STL::Filtering::GetBilinearFilter( kernelCenter, mipSize );

        float4 bilinearWeights = STL::Filtering::GetBilinearCustomWeights( filter, 1.0 );
        float2 mipUvFootprint00 = ( filter.origin + 0.5 ) * invMipSize;

        [unroll]
        for( int i = 0; i <= 1; i++ )
        {
            [unroll]
            for( int j = 0; j <= 1; j++ )
            {
                #if( REBLUR_USE_WIDER_KERNEL_IN_HISTORY_FIX == 1 )
                    float2 offset = float2( i, j ) * 2.0 - 1.0;
                #else
                    float2 offset = float2( i, j );
                #endif

                float2 uv = mipUvFootprint00 + offset * invMipSize;
                float2 uvScaled = uv * gResolutionScale;

                float4 z;
                z.x = texScaledViewZ.SampleLevel( gNearestClamp, uvScaled, mipLevel );
                z.y = texScaledViewZ.SampleLevel( gNearestClamp, uvScaled, mipLevel, int2( 1, 0 ) );
                z.z = texScaledViewZ.SampleLevel( gNearestClamp, uvScaled, mipLevel, int2( 0, 1 ) );
                z.w = texScaledViewZ.SampleLevel( gNearestClamp, uvScaled, mipLevel, int2( 1, 1 ) );

                float4 bilateralWeights = GetBilateralWeight( z, z0 );
                float4 w = bilinearWeights * bilateralWeights;
                w *= IsInScreen( uv );

                float4 s00 = texSignal.SampleLevel( gNearestClamp, uvScaled, mipLevel );
                float4 s10 = texSignal.SampleLevel( gNearestClamp, uvScaled, mipLevel, int2( 1, 0 ) );
                float4 s01 = texSignal.SampleLevel( gNearestClamp, uvScaled, mipLevel, int2( 0, 1 ) );
                float4 s11 = texSignal.SampleLevel( gNearestClamp, uvScaled, mipLevel, int2( 1, 1 ) );

                blurry += STL::Filtering::ApplyBilinearCustomWeights( s00, s10, s01, s11, w, false );
                sum += dot( w, 1.0 );
            }
        }

        mipLevel -= 1.0;
    }

    blurry = sum == 0.0 ? center : ( blurry / sum );

    return blurry;
}
