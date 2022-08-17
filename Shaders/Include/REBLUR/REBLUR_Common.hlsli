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

#define REBLUR_MAX_ACCUM_FRAME_NUM                      63.0
#define REBLUR_ACCUMSPEED_BITS                          7 // "( 1 << REBLUR_ACCUMSPEED_BITS ) - 1" must be >= REBLUR_MAX_ACCUM_FRAME_NUM
#define REBLUR_MATERIALID_BITS                          ( 16 - REBLUR_ACCUMSPEED_BITS - REBLUR_ACCUMSPEED_BITS )
#define REBLUR_ROUGHNESS_ULP                            ( 1.0 / 255.0 )
#define REBLUR_NORMAL_ULP                               STL::Math::DegToRad( 0.05 )

// Data packing for the next frame

#define PackViewZ( p )      min( p * NRD_FP16_VIEWZ_SCALE, NRD_FP16_MAX )
#define UnpackViewZ( p )    ( p / NRD_FP16_VIEWZ_SCALE )

float4 PackNormalRoughness( float4 p )
{
    return float4( p.xyz * 0.5 + 0.5, p.w );
}

float4 UnpackNormalAndRoughness( float4 p, bool isNormalized = true )
{
    p.xyz = p.xyz * 2.0 - 1.0;

    if( isNormalized )
        p.xyz = normalize( p.xyz );

    return p;
}

uint PackAccumSpeedsMaterialID( float diffAccumSpeed, float specAccumSpeed, float materialID )
{
    float3 t = float3( diffAccumSpeed, specAccumSpeed, materialID );
    t.xy /= REBLUR_MAX_ACCUM_FRAME_NUM;

    uint p = STL::Packing::RgbaToUint( t.xyzz, REBLUR_ACCUMSPEED_BITS, REBLUR_ACCUMSPEED_BITS, REBLUR_MATERIALID_BITS, 0 );

    return p;
}

float3 UnpackAccumSpeedsMaterialID( uint p )
{
    float3 t = STL::Packing::UintToRgba( p, REBLUR_ACCUMSPEED_BITS, REBLUR_ACCUMSPEED_BITS, REBLUR_MATERIALID_BITS, 0 ).xyz;
    t.xy *= REBLUR_MAX_ACCUM_FRAME_NUM;

    return t;
}

// Internal data

float4 PackInternalData1( float diffAccumSpeed, float diffRadiusScale, float specAccumSpeed, float specRadiusScale )
{
    float4 r;
    r.x = saturate( diffAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.y = diffRadiusScale;
    r.z = saturate( specAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.w = specRadiusScale;

    // Optional
    r.yw = STL::Math::Sqrt01( r.yw );

    // Allow RG8_UNORM for specular only denoiser
    #ifndef REBLUR_DIFFUSE
        r.xy = r.zw;
    #endif

    return r;
}

float4 UnpackInternalData1( float4 p )
{
    // Allow RG8_UNORM for specular only denoiser
    #ifndef REBLUR_DIFFUSE
        p.zw = p.xy;
    #endif

    float4 r;
    r.x = p.x * REBLUR_MAX_ACCUM_FRAME_NUM;
    r.y = p.y;
    r.z = p.z * REBLUR_MAX_ACCUM_FRAME_NUM;
    r.w = p.w;

    // Optional
    r.yw *= r.yw;

    return r;
}

float4 PackInternalData2( float fbits, float curvature, float virtualHistoryAmount, float hitDistScaleForTracking, float viewZ )
{
    // BITS:
    // 0        - free
    // 1        - CatRom flag
    // 2,3,4,5  - occlusion 2x2
    // 6        - free
    // 7        - curvature sign

    float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
    float packedCurvature = curvature * pixelSize; // to fit into 8-bits

    fbits += 128.0 * ( packedCurvature < 0.0 ? 1 : 0 );

    float4 r;
    r.x = saturate( ( fbits + 0.5 ) / 255.0 );
    r.y = abs( packedCurvature );
    r.z = virtualHistoryAmount;
    r.w = hitDistScaleForTracking;

    // Optional
    r.yzw = STL::Math::Sqrt01( r.yzw );

    return r;
}

float3 UnpackInternalData2( float4 p, float viewZ, out uint bits )
{
    // Optional
    p.yzw *= p.yzw;

    bits = uint( p.x * 255.0 + 0.5 );

    float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
    float sgn = ( bits & 128 ) != 0 ? -1.0 : 1.0;
    float curvature = p.y * sgn / pixelSize;

    return float3( p.zw, curvature );
}

// Accumulation speed

float SaturateParallax( float parallax )
{
    // A smooth version of "saturate"
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiJtaW4oeCwxKSIsImNvbG9yIjoiI0YyMDkwOSJ9LHsidHlwZSI6MCwiZXEiOiIxLTJeKC0zLjUqeCp4KSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MTAwMCwid2luZG93IjpbIjAiLCIyIiwiMCIsIjEuMSJdfV0-

    return saturate( 1.0 - exp2( -3.5 * parallax * parallax ) );
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

    return min( accumSpeed, gMaxAccumulatedFrameNum );
}

float GetSpecAccumSpeed( float maxAccumSpeed, float roughness, float NoV, float parallax, float curvature, float viewZ )
{
    // Artificially increase roughness if parallax is low to get a few frames of accumulation // TODO: is there a better way?
    float smbParallaxNorm = SaturateParallax( parallax * REBLUR_PARALLAX_SCALE );
    roughness = roughness + saturate( 0.05 - roughness ) * ( 1.0 - smbParallaxNorm );

    // Recalculate virtual roughness from curvature
    float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
    float curvatureAngleTan = abs( curvature ) * pixelSize * gFramerateScale;

    float percentOfVolume = 0.75;
    float roughnessFromCurvatureAngle = STL::Math::Sqrt01( curvatureAngleTan * ( 1.0 - percentOfVolume ) / percentOfVolume );

    roughness = lerp( roughness, 1.0, roughnessFromCurvatureAngle );

    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIoMS4wNSsoeCp4KV4xLjApLygxLjA1LSh4KngpXjEuMCkiLCJjb2xvciI6IiM1MkExMDgifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC42NikvKDEuMDUtKHgqeCleMC42NikiLCJjb2xvciI6IiNFM0Q4MDkifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC41KS8oMS4wNS0oeCp4KV4wLjUpIiwiY29sb3IiOiIjRjUwQTMxIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiNDIiXSwic2l6ZSI6WzE5MDAsNzAwXX1d
    float acos01sq = saturate( 1.0 - NoV * 0.99999 ); // see AcosApprox()
    float a = STL::Math::Pow01( acos01sq, REBLUR_SPEC_ACCUM_CURVE );
    float b = 1.1 + roughness * roughness;
    float parallaxSensitivity = ( b + a ) / ( b - a );

    float powerScale = 1.0 + parallaxSensitivity * parallax * REBLUR_PARALLAX_SCALE; // TODO: previously was REBLUR_PARALLAX_SCALE => gFramerateScale
    float accumSpeed = GetSpecAccumulatedFrameNum( roughness, powerScale );

    accumSpeed = min( accumSpeed, maxAccumSpeed );

    return accumSpeed * float( gResetHistory == 0 );
}

// Misc

float3 GetViewVector( float3 X, bool isViewSpace = false )
{
    return gOrthoMode == 0.0 ? normalize( -X ) : ( isViewSpace ? float3( 0, 0, -1 ) : gViewVectorWorld.xyz );
}

float3 GetViewVectorPrev( float3 Xprev, float3 cameraDelta )
{
    return gOrthoMode == 0.0 ? normalize( cameraDelta - Xprev ) : gViewVectorWorldPrev.xyz;
}

float GetMinAllowedLimitForHitDistNonLinearAccumSpeed( float roughness )
{
    // TODO: accelerate hit dist accumulation instead of limiting max number of frames?
    /*
    If hit distance weight is non-exponential:
        This function can't return 0, because strict hitDist weight and effects of feedback loop lead to color banding (crunched colors)
    If hit distance weight is exponential:
        Acceleration is still needed to get better disocclusions from "parallax check"
    */

    float acceleration = 0.5 * GetSpecMagicCurve2( roughness );

    return 1.0 / ( 1.0 + acceleration * gMaxAccumulatedFrameNum );
}

float InterpolateAccumSpeeds( float surfaceFrameNum, float virtualFrameNum, float virtualMotionAmount )
{
    // Comparison:
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiI1KigxLXgqeCkrMzEqeCp4IiwiY29sb3IiOiIjMDAwMDAwIn0seyJ0eXBlIjowLCJlcSI6IjEvKCgxLXgpLygxKzUpK3gvKDErMzEpKS0xIiwiY29sb3IiOiIjRjcwQTBBIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiMzEiXX1d

    #if( REBLUR_USE_ACCUM_SPEED_NONLINEAR_INTERPOLATION == 0 )
        return lerp( surfaceFrameNum, virtualFrameNum, virtualMotionAmount * virtualMotionAmount );
    #endif

    float a = 1.0 / ( 1.0 + surfaceFrameNum );
    float b = 1.0 / ( 1.0 + virtualFrameNum );
    float c = lerp( a, b, virtualMotionAmount );

    return 1.0 / c - 1.0;
}

float GetFadeBasedOnAccumulatedFrames( float accumSpeed )
{
    float fade = 1.0; // previously was "saturate( ( 1.0 - nonLinearAccumSpeed ) / 0.95 )"

    // Ignore reconstructed frames
    fade *= STL::Math::LinearStep( REBLUR_FIXED_FRAME_NUM - 1, REBLUR_FIXED_FRAME_NUM + 1, accumSpeed );

    return fade;
}

// Misc ( templates )

#ifdef REBLUR_OCCLUSION

    #define REBLUR_TYPE float

    float MixHistoryAndCurrent( float history, float current, float nonLinearAccumSpeed, float roughness = 1.0 )
    {
        float r = lerp( history, current, max( nonLinearAccumSpeed, GetMinAllowedLimitForHitDistNonLinearAccumSpeed( roughness ) ) );

        return r;
    }

    float ExtractHitDist( float input )
    { return input; }

    // float2
    float GetLuma( float2 input )
    { return input.x; }

    float2 ChangeLuma( float2 input, float newLuma )
    { return float2( newLuma, input.y ); }

    float2 ClampNegativeToZero( float2 input )
    { return float2( max( input.x, 0.0 ), input.y ); }

    // float
    float GetLuma( float input )
    { return input; }

    float ChangeLuma( float input, float newLuma )
    { return newLuma; }

    float ClampNegativeToZero( float input )
    { return max( input.x, 0.0 ); }

#else

    #define REBLUR_TYPE float4

    float4 MixHistoryAndCurrent( float4 history, float4 current, float nonLinearAccumSpeed, float roughness = 1.0 )
    {
        float4 r;
        r.xyz = lerp( history.xyz, current.xyz, nonLinearAccumSpeed );
        r.w = lerp( history.w, current.w, max( nonLinearAccumSpeed, GetMinAllowedLimitForHitDistNonLinearAccumSpeed( roughness ) ) );

        return r;
    }

    float ExtractHitDist( float4 input )
    { return input.w; }

    float GetLuma( float4 input )
    {
        #if( REBLUR_USE_YCOCG == 1 )
            return input.x;
        #else
            return _NRD_Luminance( input.xyz );
        #endif
    }

    float4 ChangeLuma( float4 input, float newLuma )
    {
        float currLuma = GetLuma( input );
        input.xyz *= newLuma / ( currLuma + 1e-6 );

        return input;
    }

    float4 ClampNegativeToZero( float4 input )
    {
        #if( REBLUR_USE_YCOCG == 1 )
            input.xyz = _NRD_YCoCgToLinear( input.xyz );
            input.xyz = _NRD_LinearToYCoCg( input.xyz );
        #else
            input.xyz = max( input.xyz, 0.0 );
        #endif

        input.w = max( input.w, 0.0 );

        return input;
    }

#endif

float2 GetSensitivityToDarkness( float roughness )
{
    // Artificially increase sensitivity to darkness for low roughness, because specular can be very hot
    float sensitivityToDarknessScale = lerp( 3.0, 1.0, roughness );

    #ifdef REBLUR_SH
        return gSensitivityToDarkness * float2( 0.282095, 1.0 );
    #else
        return gSensitivityToDarkness;
    #endif
}

#ifdef REBLUR_OCCLUSION
    float GetColorErrorForAdaptiveRadiusScale( float curr, float prev, float accumSpeed, float roughness = 1.0 )
#else
    float GetColorErrorForAdaptiveRadiusScale( float4 curr, float4 prev, float accumSpeed, float roughness = 1.0 )
#endif
{
#ifdef REBLUR_OCCLUSION
    float2 p = float2( 0, prev );
    float2 c = float2( 0, curr );
#else
    float2 p = float2( GetLuma( prev ), prev.w );
    float2 c = float2( GetLuma( curr ), curr.w );
#endif

    float2 f = abs( c - p ) / ( max( c, p ) + GetSensitivityToDarkness( roughness ) );

    float smc = GetSpecMagicCurve2( roughness );
    float level = lerp( 1.0, 0.15, smc );

    // TODO: use "hitDistFactor" to avoid over blurring contact shadowing?

    float error = max( f.x, f.y );
    error = STL::Math::SmoothStep( 0.0, level, error );
    error *= GetFadeBasedOnAccumulatedFrames( accumSpeed );
    error *= 1.0 - gReference;

    return error;
}

float ComputeAntilagScale(
    #ifdef REBLUR_OCCLUSION
        float history, float signal, float m1, float sigma,
    #else
        float4 history, float4 signal, float4 m1, float4 sigma,
    #endif
    float4 antilagMinMaxThreshold, float2 antilagSigmaScale, float stabilizationStrength,
    float curvatureMulPixelSize, float2 internalData1, float roughness = 1.0
)
{
    // On-edge strictness
    m1 = lerp( m1, signal, abs( curvatureMulPixelSize ) );

    // Antilag
#ifdef REBLUR_OCCLUSION
    float2 h = float2( 0, history );
    float2 c = float2( 0, m1 ); // using signal leads to bias in test #62
    float2 s = float2( 0, sigma );
#else
    float2 h = float2( GetLuma( history ), history.w );
    float2 c = float2( GetLuma( m1 ), m1.w ); // using signal leads to bias in test #62
    float2 s = float2( GetLuma( sigma ), sigma.w );
#endif

    // TODO: if roughness is close to 0, hitDist can be very divergent, antilag potentially can break accumulation
    // on bumpy surfaces, "sensitivityToDarknessScale" can be increased in this case based on curvature

    float2 delta = abs( h - c ) - s * antilagSigmaScale;
    delta /= max( h, c ) + GetSensitivityToDarkness( roughness );

    delta = STL::Math::SmoothStep( antilagMinMaxThreshold.zw, antilagMinMaxThreshold.xy, delta );

    float antilag = min( delta.x, delta.y );
    antilag = lerp( 1.0, antilag, stabilizationStrength );
    antilag = lerp( 1.0, antilag, GetFadeBasedOnAccumulatedFrames( internalData1.x ) );
    antilag = lerp( antilag, 1.0, saturate( internalData1.y ) );

    #if( REBLUR_DEBUG != 0 )
        antilag = 1.0;
    #endif

    return antilag;
}

// Kernel

float GetResponsiveAccumulationAmount( float roughness )
{
    float amount = 1.0 - ( roughness + 1e-6 ) / ( gResponsiveAccumulationRoughnessThreshold + 1e-6 );

    return STL::Math::SmoothStep01( amount );
}

float GetBlurRadius(
    float radius, float radiusBias, float radiusScale,
    float hitDistFactor, float nonLinearAccumSpeed,
    float roughness = 1.0
)
{
    // Scale down if accumulation goes well, keeping some non-zero scale to avoid under-blurring
    float r = lerp( gMinConvergedStateBaseRadiusScale, 1.0, nonLinearAccumSpeed );

    // Avoid over-blurring on contact ( tests 76, 95, 120 )
    hitDistFactor = lerp( hitDistFactor, 1.0, nonLinearAccumSpeed );
    r *= hitDistFactor; // TODO: reduce influence if reprojection confidence is low?

    // IMPORTANT: do not apply "hitDistFactor" to radiusBias because it is responsible for error compensation

    // Main composition
    r = radius * r + max( radiusBias, 2.0 * roughness ); // TODO: it would be good to get rid of "max"
    r *= radiusScale;

    // Modify by lobe spread
    r *= GetSpecMagicCurve2( roughness ); // TODO: roughness at hit needed

    // Disable spatial filtering if radius is 0
    r *= float( radius != 0 );

    return r; // TODO: if luminance stoppers are used, blur radius should depend less on hitDistFactor
}

float GetBlurRadiusScaleBasingOnTrimming( float roughness, float3 trimmingParams )
{
    float trimmingFactor = NRD_GetTrimmingFactor( roughness, trimmingParams );
    float maxScale = 1.0 + 4.0 * roughness * roughness;
    float scale = lerp( maxScale, 1.0, trimmingFactor );

    // TODO: for roughness ~0.2 and trimming = 0 blur radius will be so large and amount of accumulation will be so small that a strobbing effect can appear under motion
    return scale;
}

// Encoding precision aware weight functions ( for reprojection )

float GetEncodingAwareRoughnessWeights( float roughnessCurr, float roughnessPrev, float fraction )
{
    float a = rcp( lerp( 0.01, 1.0, saturate( roughnessCurr * fraction ) ) );
    float d = abs( roughnessPrev - roughnessCurr );

    return STL::Math::SmoothStep01( 1.0 - ( d - REBLUR_ROUGHNESS_ULP ) * a );
}

float GetEncodingAwareNormalWeight( float3 Ncurr, float3 Nprev, float maxAngle )
{
    float a = 1.0 / maxAngle;
    float cosa = saturate( dot( Ncurr, Nprev ) );
    float d = STL::Math::AcosApprox( cosa );

    float w = STL::Math::SmoothStep01( 1.0 - ( d - REBLUR_NORMAL_ULP ) * a );
    w = saturate( w / 0.95 ); // needed because prev normals are RGBA8 ( test 3, 43 if roughness is low )

    return w;
}

// Weight parameters

float GetNormalWeightParams( float nonLinearAccumSpeed, float fraction, float roughness = 1.0 )
{
    // TODO: "pixelRadiusNorm" can be used to estimate how many samples from a potentially bumpy normal map fit into
    // a pixel, i.e. to increase "fraction" a bit if "pixelRadiusNorm" is close to 1. It was used before. Now it's a
    // backup plan. See "GetBlurRadius".

    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
    angle *= lerp( saturate( fraction ), 1.0, nonLinearAccumSpeed ); // TODO: use as "percentOfVolume" instead?

    return 1.0 / max( angle, NRD_NORMAL_ENCODING_ERROR );
}

float2 GetRoughnessWeightParams( float roughness, float fraction )
{
    float a = rcp( lerp( 0.01, 1.0, saturate( roughness * fraction ) ) );
    float b = roughness * a;

    return float2( a, -b );
}

float2 GetCoarseRoughnessWeightParams( float roughness )
{
    return float2( 1.0, -roughness );
}

float2 GetTemporalAccumulationParams( float isInScreenMulFootprintQuality, float accumSpeed )
{
    // Normalization depends on FPS
    float nonLinearAccumSpeed = accumSpeed / ( 1.0 + accumSpeed );
    float norm1 = gFramerateScale * 30.0 * 0.25 / ( 1.0 + gFramerateScale * 30.0 * 0.25 );
    float normAccumSpeed = saturate( nonLinearAccumSpeed / norm1 );

    // TODO: should weight depend on "sigma / signal" ratio to avoid stabilization where it's not needed?
    float w = normAccumSpeed * normAccumSpeed;
    w *= isInScreenMulFootprintQuality;
    w *= float( gResetHistory == 0 );

    // TODO: disocclusion regions on stable bumpy surfaces with super low roughness look better with s = 0
    float s = normAccumSpeed;
    s *= 1.0 - gReference;

    return float2( w, 1.0 + REBLUR_TS_SIGMA_AMPLITUDE * s );
}

// Weights

float GetCombinedWeight
(
    float2 geometryWeightParams, float3 Nv, float3 Xvs,
    float normalWeightParams, float3 N, float4 Ns,
    float2 roughnessWeightParams = 0
)
{
    float3 a = float3( geometryWeightParams.x, normalWeightParams, roughnessWeightParams.x );
    float3 b = float3( geometryWeightParams.y, 0.0, roughnessWeightParams.y );

    float3 t;
    t.x = dot( Nv, Xvs );
    t.y = STL::Math::AcosApprox( saturate( dot( N, Ns.xyz ) ) );
    t.z = Ns.w;

    float3 w = _ComputeWeight( t, a, b );

    return w.x * w.y * w.z;
}

// float4 variants
void BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
    float2 samplePos, float2 invTextureSize,
    float4 bilinearCustomWeights, bool useBicubic,
    Texture2D<float4> tex0, out float4 c0 ) // main - CatRom
{
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( c0, tex0 );
}

void BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
    float2 samplePos, float2 invTextureSize,
    float4 bilinearCustomWeights, bool useBicubic,
    Texture2D<float4> tex0, out float4 c0, // main - CatRom
    Texture2D<float4> tex1, out float4 c1 ) // SH - bilinear
{
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( c0, tex0 );
    _BilinearFilterWithCustomWeights_Color( c1, tex1 );
}

void BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
    float2 samplePos, float2 invTextureSize,
    float4 bilinearCustomWeights, bool useBicubic,
    Texture2D<float4> tex0, out float4 c0, // main - CatRom
    Texture2D<float> tex1, out float c1 ) // fast - bilinear
{
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( c0, tex0 );
    _BilinearFilterWithCustomWeights_Color( c1, tex1 );
}

void BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
    float2 samplePos, float2 invTextureSize,
    float4 bilinearCustomWeights, bool useBicubic,
    Texture2D<float4> tex0, out float4 c0, // main - CatRom
    Texture2D<float4> tex1, out float4 c1, // SH - bilinear
    Texture2D<float> tex2, out float c2 ) // fast - bilinear
{
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( c0, tex0 );
    _BilinearFilterWithCustomWeights_Color( c1, tex1 );
    _BilinearFilterWithCustomWeights_Color( c2, tex2 );
}

// float variants
void BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
    float2 samplePos, float2 invTextureSize,
    float4 bilinearCustomWeights, bool useBicubic,
    Texture2D<float> tex0, out float c0 ) // main - CatRom
{
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( c0, tex0 );
}

void BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
    float2 samplePos, float2 invTextureSize,
    float4 bilinearCustomWeights, bool useBicubic,
    Texture2D<float> tex0, out float c0, // main - CatRom
    Texture2D<float> tex1, out float c1 ) // fast - bilinear
{
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( c0, tex0 );
    _BilinearFilterWithCustomWeights_Color( c1, tex1 );
}
