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
#define REBLUR_NORMAL_BITS                              12
#define REBLUR_NORMAL_ACCUMSPEED_MATERIALID_BITS        REBLUR_NORMAL_BITS, REBLUR_NORMAL_BITS, REBLUR_ACCUMSPEED_BITS, 2 // sum == 32!
#define REBLUR_MAX_ACCUM_FRAME_NUM                      ( ( 1 << REBLUR_ACCUMSPEED_BITS ) - 1 )
#define REBLUR_ROUGHNESS_ULP                            ( 1.0 / 255.0 )
#define REBLUR_NORMAL_ULP                               STL::Math::DegToRad( 0.05 )

// Data packing for the next frame

#define UnpackPrevViewZ( p )    asfloat( p & ~REBLUR_MAX_ACCUM_FRAME_NUM )

uint PackViewZAccumSpeed( float viewZ, float diffAccumSpeed )
{
    uint p = asuint( viewZ ) & ~REBLUR_MAX_ACCUM_FRAME_NUM;
    p |= uint( diffAccumSpeed + 0.5 ); // diffAccumSpeed is in range [0; 63]

    return p;
}

float4 UnpackDiffAccumSpeed( uint4 p )
{
    return float4( p & REBLUR_MAX_ACCUM_FRAME_NUM );
}

uint PackNormalAccumSpeedMaterialID( float3 N, float specAccumSpeed, float materialID )
{
    #if( NRD_USE_OCT_NORMAL_ENCODING == 0 )
        // Avoid quantization issues breaking oct normal encoding due to introduced "wrap from the other side".
        // Needed only if internal normals are less precise as input normals.
        float norm = float( 1 << REBLUR_NORMAL_BITS );
        N = N * 0.5 + 0.5;
        N = floor( N * norm ) / norm;
        N = N * 2.0 - 1.0;
    #endif

    float4 t;
    t.xy = STL::Packing::EncodeUnitVector( N );
    t.z = specAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM;
    t.w = materialID;

    uint p = STL::Packing::RgbaToUint( t, REBLUR_NORMAL_ACCUMSPEED_MATERIALID_BITS );

    return p;
}

float3 UnpackPrevNormalAccumSpeedMaterialID( uint p, out float accumSpeed, out float materialID )
{
    float4 t = STL::Packing::UintToRgba( p, REBLUR_NORMAL_ACCUMSPEED_MATERIALID_BITS );
    float3 N = STL::Packing::DecodeUnitVector( t.xy );

    accumSpeed = t.z * REBLUR_MAX_ACCUM_FRAME_NUM;
    materialID = t.w;

    return p ? N : 0;
}

float3 UnpackPrevNormal( uint p )
{
    float unused1;
    float unused2;

    return UnpackPrevNormalAccumSpeedMaterialID( p, unused1, unused2 );
}

// Internal data

float4 PackDiffSpecInternalData( float diffAccumSpeed, float specAccumSpeed, float curvature, float viewZ, float fbits )
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
    r.x = saturate( diffAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.y = STL::Math::Sqrt01( abs( packedCurvature ) );
    r.z = saturate( specAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.w = saturate( ( fbits + 0.5 ) / 255.0 );

    return r;
}

float4 UnpackDiffSpecInternalData( float4 p, float viewZ, out float curvature, out uint bits )
{
    float2 accumSpeed = abs( p.xz ) * REBLUR_MAX_ACCUM_FRAME_NUM;

    float4 r;
    r.xz = 1.0 / ( 1.0 + accumSpeed );
    r.yw = accumSpeed;

    bits = uint( p.w * 255.0 + 0.5 );

    float packedCurvature = p.y * p.y * ( ( bits & 128 ) != 0 ? -1.0 : 1.0 );
    float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );

    curvature = packedCurvature / pixelSize;

    return r;
}

float4 UnpackDiffSpecInternalData( float4 p )
{
    float unused1;
    uint unused2;

    return UnpackDiffSpecInternalData( p, 1.0, unused1, unused2 );
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

    return min( accumSpeed, REBLUR_USE_HISTORY_FIX_WITHOUT_DISOCCLUSION ? gMaxAccumulatedFrameNum : REBLUR_MAX_ACCUM_FRAME_NUM );
}

float GetSpecAccumSpeed( float maxAccumSpeed, float roughness, float NoV, float parallax )
{
    // TODO: optional scale can be applied to parallax if local curvature is very high, but it can lead to lags

    // Artificially increase roughness if parallax is low to get a few frames of accumulation // TODO: is there a better way?
    roughness = roughness + saturate( 0.05 - roughness ) * ( 1.0 - SaturateParallax( parallax * REBLUR_PARALLAX_SCALE ) );

    float acos01sq = saturate( 1.0 - NoV * 0.99999 ); // see AcosApprox()

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

float3 GetViewVector( float3 X, bool isViewSpace = false )
{
    return gOrthoMode == 0.0 ? normalize( -X ) : ( isViewSpace ? float3( 0, 0, -1 ) : gViewVectorWorld.xyz );
}

float3 GetViewVectorPrev( float3 Xprev, float3 cameraDelta )
{
    return gOrthoMode == 0.0 ? normalize( cameraDelta - Xprev ) : gViewVectorWorldPrev.xyz;
}

float GetMax( float3 c )
{
    return max( c.x, max( c.y, c.z ) );
}

float GetMinAllowedLimitForHitDistNonLinearAccumSpeed( float roughness )
{
    /*
    0 can be used to unblock accumulation of hitDist, but due to strict hitDist weight and effects
    of feedback loop color banding (crunched colors) will appear. It can be solved in two ways:
    - accelerating hitDist accumulation to preserve noise a bit
    - adding some hitDist input to the output in spatial passes

    Previously was:
        nonLinearAccumSpeed = lerp( 0.2, 0.1, STL::Math::Sqrt01( roughness ) );
    */

    #ifdef REBLUR_OCCLUSION
        float k = 0.5;
    #else
        float k = 0.333;
    #endif

    float smc = GetSpecMagicCurve( roughness );
    float s = lerp( 4.0 / ( gMaxAccumulatedFrameNum + 0.001 ), k, smc ); // TODO: can scale for 0 roughness be increased? Should work better if hit distances are clean
    float nonLinearAccumSpeed = 1.0 / ( 1.0 + s * gMaxAccumulatedFrameNum );

    return nonLinearAccumSpeed;
}

float4 MixHistoryAndCurrent( float4 history, float4 current, float nonLinearAccumSpeed, float roughness = 1.0 )
{
    float4 r;
    r.xyz = lerp( history.xyz, current.xyz, nonLinearAccumSpeed );
    r.w = lerp( history.w, current.w, max( nonLinearAccumSpeed, GetMinAllowedLimitForHitDistNonLinearAccumSpeed( roughness ) ) );

    return r;
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

float GetSensitivityToDarkness( float roughness )
{
    // TODO: if roughness is close to 0, hitDist can be very divergent, antilag potentially can break accumulation
    // on bumpy surfaces, "sensitivityToDarknessScale" can be increased in this case based on curvature

    // Artificially increase sensitivity to darkness for low roughness, because specular can be very hot
    float sensitivityToDarknessScale = lerp( 3.0, 1.0, roughness );

    return sensitivityToDarknessScale;
}

float GetColorErrorForAdaptiveRadiusScale( float4 curr, float4 prev, float nonLinearAccumSpeed, float roughness, float scale, bool isOcclusionOnly )
{
    float2 p = float2( isOcclusionOnly ? 0 : GetMax( prev.xyz ), prev.w );
    float2 c = float2( isOcclusionOnly ? 0 : GetMax( curr.xyz ), curr.w );
    float2 f = abs( c - p ) / ( max( c, p ) + gSensitivityToDarkness * GetSensitivityToDarkness( roughness ) + 0.001 );
    float s = 3.0 - scale;

    float error = max( f.x, f.y ); // TODO: store this value and normalize before use? can be helpful for normal weight
    error = STL::Math::SmoothStep( 0.0, saturate( gResidualNoiseLevel * s ), error );
    error *= STL::Math::Pow01( lerp( 0.05, 1.0, roughness ), 0.33333 );
    error *= float( nonLinearAccumSpeed != 1.0 );
    error *= 1.0 - gReference;

    return error;
}

float ComputeAntilagScale(
    float4 history, float4 signal, float4 m1, float4 sigma,
    float4 antilagMinMaxThreshold, float2 antilagSigmaScale, float stabilizationStrength,
    float curvatureMulPixelSize, float nonLinearAccumSpeed, float roughness = 1.0
)
{
    // On-edge strictness
    m1 = lerp( m1, signal, abs( curvatureMulPixelSize ) );

    // Antilag
    float2 h = float2( GetMax( history.xyz ), history.w );
    float2 c = float2( GetMax( m1.xyz ), m1.w ); // TODO: use signal, average antilag before use
    float2 s = float2( GetMax( sigma.xyz ), sigma.w );

    float2 delta = abs( h - c ) - s * antilagSigmaScale;
    delta /= max( h, c ) + gSensitivityToDarkness * GetSensitivityToDarkness( roughness ) + 0.001;
    delta = STL::Math::SmoothStep( antilagMinMaxThreshold.zw, antilagMinMaxThreshold.xy, delta );

    float antilag = min( delta.x, delta.y );
    antilag = lerp( 1.0, antilag, stabilizationStrength );
    antilag = lerp( antilag, 1.0, nonLinearAccumSpeed );

    // IMPORTANT: antilag must be turned off if REBLUR_DEBUG writes into the TS history

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
    float hitDistFactor, float frustumHeight,
    float nonLinearAccumSpeed, float roughness = 1.0
)
{
    // Scale down if accumulation goes well, keeping some non-zero scale to avoid under-blurring
    float r = lerp( gMinConvergedStateBaseRadiusScale, 1.0, nonLinearAccumSpeed );

    // Avoid over-blurring on contact ( if specular reprojection confidence is high )
    hitDistFactor = lerp( hitDistFactor, 1.0, nonLinearAccumSpeed );

    // TODO: test 76 suits well for blur radius tweaking
    r *= hitDistFactor;
    radiusBias *= hitDistFactor;

    // Main composition
    float pixelRadiusNorm = frustumHeight / ( 1.0 + frustumHeight ); // TODO: replace voodoo magic with physically based math
    pixelRadiusNorm = lerp( 1.0, pixelRadiusNorm, roughness * roughness );

    r = radius * r * ( radiusScale + radiusBias * pixelRadiusNorm );
    r += max( radiusBias, 2.0 * roughness ) * radiusScale; // TODO: it would be good to get rid of "max"

    // Modify by roughness, allowing small blur even for 0 roughness if accumulation doesn't go well
    float boost = lerp( 0.07, 0.0, GetResponsiveAccumulationAmount( roughness ) );
    float bumpedRoughness = lerp( roughness, 1.0, boost * nonLinearAccumSpeed );
    r *= GetSpecMagicCurve( bumpedRoughness );

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

float2 GetEncodingAwareRoughnessWeights( float roughnessCurr, float roughnessPrev, float sigma, float fraction )
{
    float2 a = rcp( lerp( 0.01, 1.0, saturate( roughnessCurr * fraction * float2( 2.0, 4.0 ) ) ) );
    float d = abs( roughnessPrev - roughnessCurr );

    return STL::Math::SmoothStep01( 1.0 - ( d - REBLUR_ROUGHNESS_ULP - sigma * float2( 1.0, 0.0 ) ) * a );
}

float GetEncodingAwareNormalWeight( float3 Ncurr, float3 Nprev, float maxAngle )
{
    float a = 1.0 / maxAngle;
    float cosa = saturate( dot( Ncurr, Nprev ) );
    float d = STL::Math::AcosApprox( cosa );

    return STL::Math::SmoothStep01( 1.0 - ( d - REBLUR_NORMAL_ULP ) * a );
}

// Weight parameters

float GetNormalWeightParams( float nonLinearAccumSpeed, float frustumHeight, float fraction, float roughness = 1.0 )
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

float2 GetTemporalAccumulationParams( float isInScreenMulFootprintQuality, float accumSpeed, float parallax = 0.0, float roughness = 1.0, float virtualHistoryAmount = 0.0 )
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
    float w = normAccumSpeed * normAccumSpeed;
    w *= isInScreenMulFootprintQuality;
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

    return _ComputeWeight( float2( param, 0.0 ), angle );
}

float4 GetNormalWeight4( float param, float3 N, float3 n00, float3 n10, float3 n01, float3 n11 )
{
    float4 cosa;
    cosa.x = dot( N, n00 );
    cosa.y = dot( N, n10 );
    cosa.z = dot( N, n01 );
    cosa.w = dot( N, n11 );

    float4 angle = STL::Math::AcosApprox( saturate( cosa ) );

    return _ComputeWeight( float2( param, 0.0 ), angle );
}

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

    t = STL::Math::SmoothStep01( 1.0 - abs( t * a + b ) );

    return t.x * t.y * t.z;
}

// History fix
// Related tests: 20, 23, 24, 27, 28, 54, 59, 65, 66, 76, 81, 98, 112, 117, 124, 126, 128, 134

#ifdef REBLUR_OCCLUSION
    void ReconstructHistory( float z0, float scale, uint2 pixelPos, float2 pixelUv, RWTexture2D<float2> texOut, Texture2D<float2> texIn )
#else
    void ReconstructHistory( float z0, float scale, uint2 pixelPos, float2 pixelUv, RWTexture2D<float4> texOut, Texture2D<float4> texIn, Texture2D<float> texScaledViewZ )
#endif
{
    // Early out
    if( scale < REBLUR_HISTORY_FIX_THRESHOLD_1 )
        return;

    float2 stepSize = scale * gInvScreenSize;
    float sum = 0.0;

    #ifdef REBLUR_OCCLUSION
        float blurry = 0.0;
    #else
        float4 blurry = 0.0;
    #endif

#if( REBLUR_USE_HISTORY_FIX_FAST_PATH == 1 )
    if( scale < REBLUR_HISTORY_FIX_THRESHOLD_2 )
    {
        // Fast path
        [unroll]
        for( int i = -1; i <= 1; i++ )
        {
            [unroll]
            for( int j = -1; j <= 1; j++ )
            {
                float2 tap = float2( i, j );

                float2 uv = pixelUv + tap.xy * stepSize * REBLUR_HISTORY_FIX_STEP;
                float2 uvScaled = uv * gResolutionScale;

                #ifdef REBLUR_OCCLUSION
                    float2 t = texIn.SampleLevel( gLinearClamp, uvScaled, 0 ); // baseMip = 1
                    float s = t.x;
                    float z = t.y;
                #else
                    float4 s = texIn.SampleLevel( gLinearClamp, uvScaled, 0 ); // baseMip = 1
                    float z = texScaledViewZ.SampleLevel( gLinearClamp, uvScaled, 1 ); // baseMip = 0
                #endif

                float w = GetBilateralWeight( z, z0 );
                w *= IsInScreen( uv );
                w *= exp2( -0.25 * STL::Math::LengthSquared( tap ) );

                blurry += s * w;
                sum += w;
            }
        }
    }
    else
#endif
    {
        // Slow path
        [unroll]
        for( int i = -2; i <= 2; i++ )
        {
            [unroll]
            for( int j = -2; j <= 2; j++ )
            {
                float2 tap = float2( i, j );

                float2 uv = pixelUv + tap.xy * stepSize * REBLUR_HISTORY_FIX_STEP;
                float2 uvScaled = uv * gResolutionScale;

                #ifdef REBLUR_OCCLUSION
                    float2 t = texIn.SampleLevel( gLinearClamp, uvScaled, 0 ); // baseMip = 1
                    float s = t.x;
                    float z = t.y;
                #else
                    float4 s = texIn.SampleLevel( gLinearClamp, uvScaled, 0 ); // baseMip = 1
                    float z = texScaledViewZ.SampleLevel( gLinearClamp, uvScaled, 1 ); // baseMip = 0
                #endif

                float w = GetBilateralWeight( z, z0 );
                w *= IsInScreen( uv );
                w *= exp2( -0.25 * STL::Math::LengthSquared( tap ) );

                blurry += s * w;
                sum += w;
            }
        }
    }

    if( sum != 0.0 )
    {
        blurry *= rcp( sum );

        #ifdef REBLUR_OCCLUSION
            texOut[ pixelPos ] = float2( blurry, z0 );
        #else
            texOut[ pixelPos ] = blurry;
        #endif
    }
}
