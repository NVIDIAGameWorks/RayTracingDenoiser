/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

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

    return float4( p ? N : 0, t.z );
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

// Internal data

/*
Based on:
https://computergraphics.stackexchange.com/questions/1718/what-is-the-simplest-way-to-compute-principal-curvature-for-a-mesh-triangle

curvature = 1 / R = localCurvature / edgeLength
localCurvature ( unsigned ) = length( N[i] - N )
localCurvature ( signed ) = dot( N[i] - N, X[i] - X ) / length( X[i] - X )
edgeLength = length( X[i] - X )
To fit into 8-bits only local curvature is encoded
*/

float EstimateCurvature( float3 Ni, float3 Vi, float3 N, float3 X )
{
    float3 Xi = 0 + Vi * dot( X - 0, N ) / dot( Vi, N );
    float3 edge = Xi - X;
    float curvature = dot( Ni - N, edge ) * rsqrt( STL::Math::LengthSquared( edge ) );

    // TODO: potentially imprecision mitigation is needed here...

    return curvature;
}

float4 PackDiffSpecInternalData( float diffAccumSpeed, float specAccumSpeed, float curvature, float fbits )
{
    // BITS:
    // 0-2  - specular mip level
    // 3    - surface occlusion
    // 4    - virtual occlusion
    // 5    - curvatrue sign
    // 6-7  - free

    fbits += 32.0 * ( curvature < 0.0 ? 1 : 0 );

    float4 r;
    r.x = saturate( diffAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.y = STL::Math::Sqrt01( abs( curvature ) );
    r.z = saturate( specAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.w = saturate( ( fbits + 0.5 ) / 255.0 );

    return r;
}

float4 UnpackDiffSpecInternalData( float4 p, out float curvature, out uint bits )
{
    float2 accumSpeed = abs( p.xz ) * REBLUR_MAX_ACCUM_FRAME_NUM;

    float4 r;
    r.xz = 1.0 / ( 1.0 + accumSpeed );
    r.yw = accumSpeed;

    bits = uint( p.w * 255.0 + 0.5 );
    curvature = p.y * p.y * ( ( bits & 32 ) != 0 ? -1.0 : 1.0 );

    return r;
}

float4 UnpackDiffSpecInternalData( float4 p, out float curvature )
{
    uint unused;

    return UnpackDiffSpecInternalData( p, curvature, unused );
}

float4 UnpackDiffSpecInternalData( float4 p )
{
    float unused1;
    uint unused2;

    return UnpackDiffSpecInternalData( p, unused1, unused2 );
}

// Accumulation speed

float GetSpecMagicCurve( float roughness, float power = 0.25 ) // TODO: REBLUR_SPEC_ACCUM_BASE_POWER?
{
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIxLjAtMl4oLTE1LjAqeCkiLCJjb2xvciI6IiNGMjE4MTgifSx7InR5cGUiOjAsImVxIjoiKDEtMl4oLTIwMCp4KngpKSooeF4wLjI1KSIsImNvbG9yIjoiIzIyRUQxNyJ9LHsidHlwZSI6MCwiZXEiOiIoMS0yXigtMjAwKngqeCkpKih4XjAuNSkiLCJjb2xvciI6IiMxNzE2MTYifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIxLjEiXSwic2l6ZSI6WzEwMDAsNTAwXX1d

    float f = 1.0 - exp2( -200.0 * roughness * roughness );
    f *= STL::Math::Pow01( roughness, power );

    return f;
}

float GetSpecAccumulatedFrameNum( float roughness, float powerScale )
{
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiI2MyooeF4wLjUpIiwiY29sb3IiOiIjMDAwMDAwIn0seyJ0eXBlIjowLCJlcSI6IjYzKigxLTJeKC0yMDAqeCp4KSkqKHheMC41KSIsImNvbG9yIjoiIzIyRkYwMCJ9LHsidHlwZSI6MCwiZXEiOiI2MyooMS0yXigtMjAwKngqeCkpKih4XjAuNjYpIiwiY29sb3IiOiIjRkMwMzAzIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiNjMiXX1d

    return REBLUR_MAX_ACCUM_FRAME_NUM * GetSpecMagicCurve( roughness, REBLUR_SPEC_ACCUM_BASE_POWER * powerScale );
}

float AdvanceAccumSpeed( float4 prevAccumSpeed, float4 weights )
{
    float4 accumSpeeds = prevAccumSpeed + 1.0;
    float accumSpeed = STL::Filtering::ApplyBilinearCustomWeights( accumSpeeds.x, accumSpeeds.y, accumSpeeds.z, accumSpeeds.w, weights );

    return min( accumSpeed, REBLUR_MAX_ACCUM_FRAME_NUM );
}

float GetSpecAccumSpeed( float maxAccumSpeed, float roughness, float NoV, float parallax )
{
    float acos01sq = saturate( 1.0 - NoV ); // see AcosApprox()

    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIoMS4wNSsoeCp4KV4xLjApLygxLjA1LSh4KngpXjEuMCkiLCJjb2xvciI6IiM1MkExMDgifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC42NikvKDEuMDUtKHgqeCleMC42NikiLCJjb2xvciI6IiNFM0Q4MDkifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC41KS8oMS4wNS0oeCp4KV4wLjUpIiwiY29sb3IiOiIjRjUwQTMxIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiNDIiXSwic2l6ZSI6WzE5MDAsNzAwXX1d
    float a = STL::Math::Pow01( acos01sq, REBLUR_SPEC_ACCUM_CURVE );
    float b = 1.1 + roughness * roughness;
    float parallaxSensitivity = ( b + a ) / ( b - a );

    float powerScale = 1.0 + gFramerateScale * parallax * parallaxSensitivity;
    float accumSpeed = GetSpecAccumulatedFrameNum( roughness, powerScale );

    // TODO: high parallax => accumSpeed = 0 => history fix. Should be the same trick as for confidence applied here?
    accumSpeed = min( accumSpeed, maxAccumSpeed );

    return accumSpeed * float( gResetHistory == 0 );
}

// Misc

// A smooth version of "saturate"
// http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiJtaW4oeCwxKSIsImNvbG9yIjoiI0YyMDkwOSJ9LHsidHlwZSI6MCwiZXEiOiIxLTJeKC0zLjUqeCp4KSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MTAwMCwid2luZG93IjpbIjAiLCIyIiwiMCIsIjEuMSJdfV0-
#define SaturateParallax( parallax )    saturate( 1.0 - exp2( -3.5 * ( parallax ) * ( parallax ) ) )

float GetDisocclusionThreshold( float disocclusionThreshold, float jitterDeltaInPixels, float viewZ, float3 Nflat, float3 V )
{
    float jitterDelta = PixelRadiusToWorld( gUnproject, gIsOrtho, jitterDeltaInPixels, viewZ );
    float NoV = abs( dot( Nflat, V ) );
    disocclusionThreshold += jitterDelta / ( max( NoV, 0.05 ) * viewZ );

    return disocclusionThreshold;
}

float3 GetViewVector( float3 X, bool isViewSpace = false )
{
    return gIsOrtho == 0.0 ? normalize( -X ) : ( isViewSpace ? float3( 0, 0, -1 ) : gViewVectorWorld.xyz );
}

float ComputeParallax( float3 X, float3 Xprev, float4 cameraDelta )
{
    float3 Xt = Xprev - cameraDelta.xyz;
    float p = dot( X, Xt );
    float parallax = sqrt( max( dot( X, X ) * dot( Xt, Xt ) - p * p, 0.0 ) ) / p;

    // Special case for ortho projection, where translation doesn't introduce parallax
    parallax = gIsOrtho != 0 ? cameraDelta.w : parallax; // TODO: do it better!

    return parallax * REBLUR_PARALLAX_NORMALIZATION;
}

float4 GetXvirtual( float3 X, float3 V, float NoV, float roughness, float hitDist, float viewZ, float c )
{
    /*
    The lens equation:
        - c - local curvature
        - C - curvature = c / edgeLength
        - O - object height
        - I - image height
        - F - focal distance
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

    Real curvature from local curvature:
        edgeLength = pixelSize / NoV
        C = c * NoV / pixelSize
    */

    // TODO: better use "edge" from EstimateCurvature?
    float pixelSize = PixelRadiusToWorld( gUnproject, gIsOrtho, 1.0, viewZ );
    c *= NoV / pixelSize;

    float denom = 0.5 + c * hitDist;
    denom = abs( denom ) < 1e-6 ? 0.5 : denom; // fixing imprecision problems
    float hitDistFocused = 0.5 * hitDist / denom;

    // "saturate" is needed to clamp values > 1 if curvature is negative
    float compressionRatio = saturate( ( abs( hitDistFocused ) + 1e-6 ) / ( hitDist + 1e-6 ) );

    // TODO: more complicated method is needed, because if elongation is very small X should become Xprev (signal starts to follow with surface motion)
    float f = STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughness, REBLUR_SPEC_DOMINANT_DIRECTION );
    float3 Xvirtual = X - V * hitDistFocused * f;

    return float4( Xvirtual, compressionRatio );
}

float GetMax( float3 c )
{
    return max( c.x, max( c.y, c.z ) );
}

float GetColorErrorForAdaptiveRadiusScale( float4 curr, float4 prev, float nonLinearAccumSpeed, float roughness, float scale )
{
    float2 p = float2( GetMax( prev.xyz ), prev.w );
    float2 c = float2( GetMax( curr.xyz ), curr.w );
    float2 f = abs( c - p ) / ( max( c, p ) + 0.001 );
    float s = 3.0 - scale;

    float error = max( f.x, f.y ); // TODO: store this value and normalize before use? can be helpful for normal weight
    error = STL::Math::SmoothStep( 0.0, gResidualNoiseLevel * s, error );
    error *= STL::Math::Pow01( roughness, 0.33333 );
    error *= float( nonLinearAccumSpeed != 1.0 );
    error *= 1.0 - gReference;

    return error;
}

float4 MixSurfaceAndVirtualMotion( float4 s, float4 v, float virtualHistoryAmount, float hitDistToSurfaceRatio )
{
    float2 f;
    f.x = virtualHistoryAmount;
    f.y = virtualHistoryAmount * hitDistToSurfaceRatio; // TODO: needed?

    return lerp( s, v, f.xxxy );
}

float4 MixHistoryAndCurrent( float4 history, float4 current, float nonLinearAccumSpeed, float roughness = 1.0 )
{
    float4 r;
    r.xyz = lerp( history.xyz, current.xyz, nonLinearAccumSpeed );
    r.w = lerp( history.w, current.w, max( nonLinearAccumSpeed, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughness ) ) );

    return r;
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

float ComputeAntilagScale
(
    inout float accumSpeed,
    float4 history, float4 signal, float4 m1, float4 sigma,
    float4 antilag1, // .xy - sigma scale, .zw - sensitivity to darkness
    float4 antilag2, // .xy - min threshold, .zw - max threshold
    float curvature, float roughness = 1.0
)
{
    // TODO: if roughness is close to 0, hit dists can be very divergent, antilag potentially can break accumulation
    // on bumpy surfaces, "sensitivityToDarknessScale" can be increased in this case based on curvature

    // Artificially increase sensitivity to darkness for low roughness, because specular can be very hot
    float2 sensitivityToDarknessScale = lerp( 3.0, 1.0, roughness );

    // On-edge strictness
    m1 = lerp( m1, signal, abs( curvature ) );

    // Antilag
    float2 h = float2( GetMax( history.xyz ), history.w );
    float2 m = float2( GetMax( m1.xyz ), m1.w );

    float2 ma = float2( GetMax( m1.xyz - sigma.xyz ), m1.w - sigma.w );
    float2 mb = float2( GetMax( m1.xyz + sigma.xyz ), m1.w + sigma.w );
    float2 s = 0.5 * ( mb - ma );

    float2 delta = abs( h - m ) - s * antilag1.xy;
    delta /= max( m, h ) + antilag1.zw * sensitivityToDarknessScale + 1e-6;
    delta = STL::Math::SmoothStep( antilag2.zw, antilag2.xy, delta );

    float antilag = min( delta.x, delta.y );
    antilag = lerp( antilag, 1.0, 1.0 / ( 1.0 + accumSpeed ) ); // TODO: needed to avoid artefacts due to bad interaction of history, fast history and antilag

    return antilag;
}

// Kernel

float GetBlurRadius( float radius, float hitDist, float viewZ, float nonLinearAccumSpeed, float radiusBias, float radiusScale, float roughness = 1.0 )
{
    // Scale down if accumulation goes well, keeping some non-zero scale to avoid under-blurring
    float r = lerp( REBLUR_MIN_RADIUS_SCALE_AT_CONVERGED_STATE, 1.0, nonLinearAccumSpeed );

    // Modify by hit distance
    float frustumHeight = PixelRadiusToWorld( gUnproject, gIsOrtho, gRectSize.y, viewZ );
    float hitDistFactor = hitDist / ( hitDist + frustumHeight );
    r *= hitDistFactor;

    // Avoid over-blurring on contact
    radiusBias *= lerp( roughness * roughness, 1.0, hitDistFactor );

    // Final
    float pixelRadiusNorm = frustumHeight / ( 1.0 + frustumHeight ); // TODO: replace voodoo magic with physically based math
    pixelRadiusNorm = lerp( 1.0, pixelRadiusNorm, roughness * roughness );

    r = radius * r * ( radiusScale + radiusBias * pixelRadiusNorm ) + max( radiusBias, 2.0 * roughness ) * radiusScale;

    // Post final scales
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

float2x3 GetKernelBasis( float3 V, float3 N, float worldRadius, float roughness = 1.0, float anisoFade = 1.0 )
{
    float3x3 basis = STL::Geometry::GetBasis( N );
    float3 T = basis[ 0 ];
    float3 B = basis[ 1 ];

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

float GetNormalWeightParams( float nonLinearAccumSpeed, float viewZ, float roughness = 1.0, float strictness = 1.0 )
{
    // Estimate how many samples from a potentially bumpy normal map fit in the pixel
    float frustumHeight = PixelRadiusToWorld( gUnproject, gIsOrtho, gRectSize.y, viewZ );
    float pixelRadiusNorm = frustumHeight / ( 1.0 + frustumHeight ); // TODO: replace voodoo magic with physically based math

    float s = lerp( 0.03, 0.16, pixelRadiusNorm );
    s = lerp( s, 1.0, nonLinearAccumSpeed );
    s = saturate( s * strictness );

    float params = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
    params *= s;
    params = 1.0 / max( params, NRD_ENCODING_ERRORS.x );

    return params;
}

float2 GetRoughnessWeightParams( float roughness, float percentOfRoughness = 0.03 )
{
    // IMPORTANT: too small values of "percentOfRoughness" can ruin contact shadowing even if neighboring roughness is absolutely same due to re-packing imprecision problems.
    float a = 1.0 / ( roughness * percentOfRoughness * 0.99 + 0.01 );
    float b = roughness * a;

    return float2( a, -b );
}

float2 GetHitDistanceWeightParams( float normHitDist, float nonLinearAccumSpeed )
{
    float a = 1.0 / nonLinearAccumSpeed; // TODO: previously was "1.0 / ( normHitDist * F( roughness) * 0.99 + 0.01 )"
    float b = normHitDist * a;

    return float2( a, -b );
}

float2 GetTemporalAccumulationParams( float isInScreen, float accumSpeed, float parallax = 0.0, float roughness = 1.0, float virtualHistoryAmount = 0.0 )
{
    // Normalization depends on FPS
    float nonLinearAccumSpeed = accumSpeed / ( 1.0 + accumSpeed );
    float norm1 = gFramerateScale * 30.0 * 0.25 / ( 1.0 + gFramerateScale * 30.0 * 0.25 );
    float normAccumSpeed = saturate( nonLinearAccumSpeed / norm1 );

    // Correct parallax (not needed if virtual motion is confident)
    float correctedParallax = parallax * ( 1.0 - virtualHistoryAmount );

    // Allow bigger sigma scale if motion is close to 0
    float f = SaturateParallax( correctedParallax );

    // TODO: should weight depend on "sigma / signal" ratio to avoid stabilization where it's not needed?
    float w = normAccumSpeed;
    w *= isInScreen;
    w *= lerp( 1.0 - f, 1.0, roughness * roughness );
    w *= float( gResetHistory == 0 );

    // TODO: disocclusion regions on stable bumpy surfaces with super low roughness look better with s = 0
    float s = normAccumSpeed;
    s *= lerp( 1.0, GetSpecMagicCurve( roughness ), f );
    s *= 1.0 - gReference;

    return float2( w, 1.0 + REBLUR_TS_SIGMA_AMPLITUDE * s );
}

// Weights

float GetNormalWeight( float param, float3 N, float3 n )
{
    float cosa = saturate( dot( N, n ) );
    float angle = STL::Math::AcosApprox( cosa );

    return _ComputeWeight( float2( param, -0.001 ), angle );
}

float4 GetNormalWeight4( float param, float3 N, float3 n00, float3 n10, float3 n01, float3 n11 )
{
    float4 cosa;
    cosa.x = dot( N, n00 );
    cosa.y = dot( N, n10 );
    cosa.z = dot( N, n01 );
    cosa.w = dot( N, n11 );

    float4 angle = STL::Math::AcosApprox( saturate( cosa ) );

    return _ComputeWeight( float2( param, -0.001 ), angle );
}

#define GetRoughnessWeight( p, value ) _ComputeWeight( p, value )
#define GetHitDistanceWeight( p, value ) _ComputeWeight( p, value )

float2 GetCombinedWeight
(
    float baseWeight,
    float2 geometryWeightParams, float3 Nv, float3 Xvs,
    float normalWeightParams, float3 N, float4 Ns,
    float2 hitDistanceWeightParams, float normHitDist, float2 minHitDistWeight,
    float2 roughnessWeightParams = 0
)
{
    float4 a = float4( geometryWeightParams.x, normalWeightParams, hitDistanceWeightParams.x, roughnessWeightParams.x );
    float4 b = float4( geometryWeightParams.y, -0.001, hitDistanceWeightParams.y, roughnessWeightParams.y );

    float4 t;
    t.x = dot( Nv, Xvs );
    t.y = STL::Math::AcosApprox( saturate( dot( N, Ns.xyz ) ) );
    t.z = normHitDist;
    t.w = Ns.w;

    t = STL::Math::SmoothStep01( 1.0 - abs( t * a + b ) );
    baseWeight *= t.x * t.y * t.w;

    return baseWeight * lerp( minHitDistWeight, 1.0, t.z );
}

float GetGaussianWeight( float r )
{
    // radius is normalized to 1
    return exp( -0.66 * r * r );
}

// Upsampling

float GetMipLevel( float roughness )
{
    return REBLUR_MIP_NUM * GetSpecMagicCurve( roughness, 0.5 );
}

void ReconstructHistory( float z0, float accumSpeed, float mipLevel, uint2 pixelPos, float2 pixelUv, RWTexture2D<float4> texOut, Texture2D<float4> texIn, Texture2D<float> texScaledViewZ )
{
    mipLevel = max( mipLevel - accumSpeed, 0.0 );
    mipLevel = floor( mipLevel );

    [branch]
    if( mipLevel == 0.0 )
        return;

    float4 blurry = 0.0;
    float sum = 0.0;

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

                float2 uv = saturate( mipUvFootprint00 + offset * invMipSize );
                float2 uvScaled = uv * gResolutionScale;

                float4 z;
                z.x = texScaledViewZ.SampleLevel( gNearestClamp, uvScaled, mipLevel );
                z.y = texScaledViewZ.SampleLevel( gNearestClamp, uvScaled, mipLevel, int2( 1, 0 ) );
                z.z = texScaledViewZ.SampleLevel( gNearestClamp, uvScaled, mipLevel, int2( 0, 1 ) );
                z.w = texScaledViewZ.SampleLevel( gNearestClamp, uvScaled, mipLevel, int2( 1, 1 ) );

                float4 bilateralWeights = GetBilateralWeight( z, z0 );
                float4 w = bilinearWeights * bilateralWeights;

                float4 s00 = texIn.SampleLevel( gNearestClamp, uvScaled, mipLevel - 1 );
                float4 s10 = texIn.SampleLevel( gNearestClamp, uvScaled, mipLevel - 1, int2( 1, 0 ) );
                float4 s01 = texIn.SampleLevel( gNearestClamp, uvScaled, mipLevel - 1, int2( 0, 1 ) );
                float4 s11 = texIn.SampleLevel( gNearestClamp, uvScaled, mipLevel - 1, int2( 1, 1 ) );

                blurry += STL::Filtering::ApplyBilinearCustomWeights( s00, s10, s01, s11, w, false );
                sum += dot( w, 1.0 );
            }
        }

        mipLevel -= 1.0;
    }

    float4 c0 = texOut[ pixelPos ];
    blurry = sum == 0.0 ? c0 : ( blurry / sum );

    float f = 1.0 / ( 1.0 + accumSpeed );
    blurry.xyz = lerp( c0.xyz, blurry.xyz, STL::Math::Sqrt01( f ) );
    blurry.w = lerp( c0.w, blurry.w, f );

    texOut[ pixelPos ] = blurry;
}

void ReconstructHistory( float c0, float z0, float accumSpeed, float mipLevel, uint2 pixelPos, float2 pixelUv, RWTexture2D<float2> texOut, Texture2D<float2> texIn )
{
    mipLevel = max( mipLevel - accumSpeed, 0.0 );
    mipLevel = floor( mipLevel );

    [branch]
    if( mipLevel == 0.0 )
        return;

    float blurry = 0.0;
    float sum = 0.0;

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

                float2 uv = saturate( mipUvFootprint00 + offset * invMipSize );
                float2 uvScaled = uv * gResolutionScale;

                float2 s00 = texIn.SampleLevel( gNearestClamp, uvScaled, mipLevel - 1 );
                float2 s10 = texIn.SampleLevel( gNearestClamp, uvScaled, mipLevel - 1, int2( 1, 0 ) );
                float2 s01 = texIn.SampleLevel( gNearestClamp, uvScaled, mipLevel - 1, int2( 0, 1 ) );
                float2 s11 = texIn.SampleLevel( gNearestClamp, uvScaled, mipLevel - 1, int2( 1, 1 ) );

                float4 z = float4( s00.y, s10.y, s01.y, s11.y );
                float4 bilateralWeights = GetBilateralWeight( z, z0 );
                float4 w = bilinearWeights * bilateralWeights;

                blurry += STL::Filtering::ApplyBilinearCustomWeights( s00.x, s10.x, s01.x, s11.x, w, false );
                sum += dot( w, 1.0 );
            }
        }

        mipLevel -= 1.0;
    }

    blurry = sum == 0.0 ? c0 : ( blurry / sum );

    float f = 1.0 / ( 1.0 + accumSpeed );
    blurry = lerp( c0, blurry, f );

    texOut[ pixelPos ] = float2( blurry, z0 );
}
