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
#define REBLUR_NORMAL_ULP                               atan( 1.0 / 255.0 )
#define REBLUR_CURVATURE_PACKING_SCALE                  0.1

// Internal data ( from the previous frame )

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

uint PackInternalData( float diffAccumSpeed, float specAccumSpeed, float materialID )
{
    float3 t = float3( diffAccumSpeed, specAccumSpeed, materialID );
    t.xy = min( t.xy, gMaxAccumulatedFrameNum ) / REBLUR_MAX_ACCUM_FRAME_NUM;

    uint p = STL::Packing::RgbaToUint( t.xyzz, REBLUR_ACCUMSPEED_BITS, REBLUR_ACCUMSPEED_BITS, REBLUR_MATERIALID_BITS, 0 );

    return p;
}

float3 UnpackInternalData( uint p )
{
    float3 t = STL::Packing::UintToRgba( p, REBLUR_ACCUMSPEED_BITS, REBLUR_ACCUMSPEED_BITS, REBLUR_MATERIALID_BITS, 0 ).xyz;
    t.xy *= REBLUR_MAX_ACCUM_FRAME_NUM;

    return t;
}

// Intermediate data ( in the current frame )

float4 PackData1( float diffAccumSpeed, float diffError, float specAccumSpeed, float specError )
{
    float4 r;
    r.x = saturate( diffAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.y = diffError;
    r.z = saturate( specAccumSpeed / REBLUR_MAX_ACCUM_FRAME_NUM );
    r.w = specError;

    // Allow RG8_UNORM for specular only denoiser
    #ifndef REBLUR_DIFFUSE
        r.xy = r.zw;
    #endif

    return r;
}

float4 UnpackData1( float4 p )
{
    // Allow R8_UNORM for specular only denoiser
    #ifndef REBLUR_DIFFUSE
        p.zw = p.xy;
    #endif

    p.xz *= REBLUR_MAX_ACCUM_FRAME_NUM;

    return p;
}

float4 PackData2( float fbits, float curvature, float virtualHistoryAmount, float viewZ )
{
    // BITS:
    // 0        - free // TODO: can be used to store "skip HistoryFix" bit
    // 1        - CatRom flag
    // 2,3,4,5  - occlusion 2x2
    // 6        - free // TODO: free!
    // 7        - curvature sign

    float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
    float packedCurvature = curvature * pixelSize * REBLUR_CURVATURE_PACKING_SCALE; // to fit into 8-bits

    fbits += 128.0 * ( packedCurvature < 0.0 ? 1 : 0 );

    float4 r;
    r.x = saturate( ( fbits + 0.5 ) / 255.0 );
    r.y = abs( packedCurvature );
    r.z = virtualHistoryAmount;
    r.w = 0.0; // TODO: unused

    // Optional
    r.yzw = STL::Math::Sqrt01( r.yzw );

    return r;
}

float3 UnpackData2( float4 p, float viewZ, out uint bits )
{
    // Optional
    p.yzw *= p.yzw;

    bits = uint( p.x * 255.0 + 0.5 );

    float pixelSize = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
    float sgn = ( bits & 128 ) != 0 ? -1.0 : 1.0;
    float curvature = p.y * sgn / ( pixelSize * REBLUR_CURVATURE_PACKING_SCALE );

    return float3( p.zw, curvature );
}

// Accumulation speed

float GetFPS( float period = 1.0 )
{
    return gFramerateScale * 30.0 * period;
}

float GetSmbAccumSpeed( float smbSpecAccumSpeedFactor, float vmbPixelsTraveled, float smbParallax, float specAccumSpeed, float roughness )
{
    float smbSpecAccumSpeed = GetFPS( ) / ( 1.0 + smbSpecAccumSpeedFactor * vmbPixelsTraveled );

    // ( Optional ) Accelerate if parallax is high // TODO: migrate to "smbParallaxInPixels"?
    float f = 1.0 - roughness * roughness;
    f *= smbSpecAccumSpeed / ( 1.0 + smbSpecAccumSpeed );
    smbSpecAccumSpeed /= 1.0 + smbParallax * f;

    return min( smbSpecAccumSpeed, specAccumSpeed );
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

    float frameNum = 0.5 * GetSpecMagicCurve2( roughness ) * gMaxAccumulatedFrameNum;

    return 1.0 / ( 1.0 + frameNum );
}

float GetFadeBasedOnAccumulatedFrames( float accumSpeed )
{
    float fade = STL::Math::LinearStep( gHistoryFixFrameNum, gHistoryFixFrameNum * 2.0, accumSpeed );

    return fade;
}

// Misc ( templates )

// Hit distance is normalized
float ClampNegativeHitDistToZero( float hitDist )
{ return saturate( hitDist ); }

float GetLumaScale( float currLuma, float newLuma )
{
    // IMPORTANT" "saturate" of below must be used if "vmbAllowCatRom = vmbAllowCatRom && specAllowCatRom"
    // is not used. But we can't use "saturate" because otherwise fast history clamping won't be able to
    // increase energy ( i.e. clamp a lower value to a bigger one from fast history ).

    return ( newLuma + NRD_EPS ) / ( currLuma + NRD_EPS );
}

#ifdef REBLUR_OCCLUSION

    #define REBLUR_TYPE float

    float MixHistoryAndCurrent( float history, float current, float f, float roughness = 1.0 )
    {
        float r = lerp( history, current, max( f, GetMinAllowedLimitForHitDistNonLinearAccumSpeed( roughness ) ) );

        return r;
    }

    float ExtractHitDist( float input )
    { return input; }

    float GetLuma( float input )
    { return input; }

    float ChangeLuma( float input, float newLuma )
    { return input * GetLumaScale( input, newLuma ); }

    float ClampNegativeToZero( float input )
    { return ClampNegativeHitDistToZero( input ); }

    float Xxxy( float2 w )
    { return w.y; }

#elif( defined REBLUR_DIRECTIONAL_OCCLUSION )

    #define REBLUR_TYPE float4

    float4 MixHistoryAndCurrent( float4 history, float4 current, float f, float roughness = 1.0 )
    {
        float4 r;
        r.xyz = lerp( history.xyz, current.xyz, f );
        r.w = lerp( history.w, current.w, max( f, GetMinAllowedLimitForHitDistNonLinearAccumSpeed( roughness ) ) );

        return r;
    }

    float MixFastHistoryAndCurrent( float history, float current, float f )
    {
        return lerp( history, current, f );
    }

    float ExtractHitDist( float4 input )
    { return input.w; }

    float GetLuma( float4 input )
    { return input.w; }

    float4 ChangeLuma( float4 input, float newLuma )
    {
        return input * GetLumaScale( GetLuma( input ), newLuma );
    }

    float4 ClampNegativeToZero( float4 input )
    { return ChangeLuma( input, ClampNegativeHitDistToZero( input.w ) ); }

    float4 Xxxy( float2 w )
    { return w.xxxy; }

#else

    #define REBLUR_TYPE float4

    float4 MixHistoryAndCurrent( float4 history, float4 current, float f, float roughness = 1.0 )
    {
        float4 r;
        r.xyz = lerp( history.xyz, current.xyz, f );
        r.w = lerp( history.w, current.w, max( f, GetMinAllowedLimitForHitDistNonLinearAccumSpeed( roughness ) ) );

        return r;
    }

    float MixFastHistoryAndCurrent( float history, float current, float f )
    {
        return lerp( history, current, f );
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
        input.xyz *= GetLumaScale( GetLuma( input ), newLuma );

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

        input.w = ClampNegativeHitDistToZero( input.w );

        return input;
    }

    float4 Xxxy( float2 w )
    { return w.xxxy; }

#endif

float2 GetSensitivityToDarkness( float roughness )
{
    // Artificially increase sensitivity to darkness for low roughness, because specular can be very hot
    // TODO: should depend on curvature for low roughness?
    float sensitivityToDarknessScale = lerp( 3.0, 1.0, roughness );

    return gSensitivityToDarkness * sensitivityToDarknessScale;
}

float GetColorErrorForAdaptiveRadiusScale( REBLUR_TYPE curr, REBLUR_TYPE prev, float accumSpeed, float roughness = 1.0 )
{
    // TODO: track temporal variance instead
    float2 p = float2( GetLuma( prev ), ExtractHitDist( prev ) );
    float2 c = float2( GetLuma( curr ), ExtractHitDist( curr ) );
    float2 f = abs( c - p ) / ( max( c, p ) + GetSensitivityToDarkness( roughness ) );

    float error = max( f.x, f.y );
    error = STL::Math::SmoothStep( 0.0, 0.15, error );
    error *= GetFadeBasedOnAccumulatedFrames( accumSpeed );
    error *= 1.0 - gReference;

    return error;
}

float ComputeAntilagScale(
    REBLUR_TYPE history, REBLUR_TYPE signal, REBLUR_TYPE m1, REBLUR_TYPE sigma,
    float4 antilagMinMaxThreshold, float2 antilagSigmaScale, float stabilizationStrength,
    float curvatureMulPixelSize, float accumSpeed, float roughness = 1.0
)
{
    // On-edge strictness
    m1 = lerp( m1, signal, abs( curvatureMulPixelSize ) );

    // Antilag
    float2 h = float2( GetLuma( history ), ExtractHitDist( history ) );
    float2 c = float2( GetLuma( m1 ), ExtractHitDist( m1 ) ); // using signal leads to bias in test #62
    float2 s = float2( GetLuma( sigma ), ExtractHitDist( sigma ) );

    float2 delta = abs( h - c ) - s * antilagSigmaScale;
    delta /= max( h, c ) + GetSensitivityToDarkness( roughness );

    delta = STL::Math::SmoothStep( antilagMinMaxThreshold.zw, antilagMinMaxThreshold.xy, delta );

    float antilag = min( delta.x, delta.y );
    antilag = lerp( 1.0, antilag, stabilizationStrength );
    antilag = lerp( 1.0, antilag, GetFadeBasedOnAccumulatedFrames( accumSpeed ) );

    #if( REBLUR_DEBUG != 0 )
        antilag = 1.0;
    #endif

    return antilag;
}

// Kernel

float GetResponsiveAccumulationAmount( float roughness )
{
    float amount = 1.0 - ( roughness + NRD_EPS ) / ( gResponsiveAccumulationRoughnessThreshold + NRD_EPS );

    return STL::Math::SmoothStep01( amount );
}

float2x3 GetKernelBasis( float3 D, float3 N, float NoD, float roughness = 1.0, float anisoFade = 1.0 )
{
    float3x3 basis = STL::Geometry::GetBasis( N );

    float3 T = basis[ 0 ];
    float3 B = basis[ 1 ];

    if( NoD < 0.999 )
    {
        float3 R = reflect( -D, N );
        T = normalize( cross( N, R ) );
        B = cross( R, T );

        float skewFactor = lerp( 0.5 + 0.5 * roughness, 1.0, NoD );
        T *= lerp( skewFactor, 1.0, anisoFade );
    }

    return float2x3( T, B );
}

// Encoding precision aware weight functions ( for reprojection )

float GetEncodingAwareRoughnessWeights( float roughnessCurr, float roughnessPrev, float fraction )
{
    float a = rcp( lerp( 0.01, 1.0, saturate( roughnessCurr * fraction ) ) );
    float d = abs( roughnessPrev - roughnessCurr );

    return STL::Math::SmoothStep01( 1.0 - ( d - REBLUR_ROUGHNESS_ULP ) * a );
}

float GetEncodingAwareNormalWeight( float3 Ncurr, float3 Nprev, float maxAngle, float angleThreshold = 0.0 )
{
    float a = 1.0 / maxAngle;
    float cosa = saturate( dot( Ncurr, Nprev ) );
    float d = STL::Math::AcosApprox( cosa );

    angleThreshold += REBLUR_NORMAL_ULP;

    // Anything below "angleThreshold" is ignored
    float w = STL::Math::SmoothStep01( 1.0 - ( d - angleThreshold ) * a );

    // Needed to mitigate imprecision issues because prev normals are RGBA8 ( test 3, 43 if roughness is low )
    w = saturate( w / 0.95 );

    return w;
}

// Weight parameters

float GetNormalWeightParams( float nonLinearAccumSpeed, float fraction, float roughness = 1.0 )
{
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
    angle *= lerp( saturate( fraction ), 1.0, nonLinearAccumSpeed ); // TODO: use as "percentOfVolume" instead?

    return 1.0 / max( angle, REBLUR_NORMAL_ULP );
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
    // TODO: make it "gMaxAccumulatedFrameNum" dependent?
    float frameNum = GetFPS( 0.5 );
    float nonLinearAccumSpeed = accumSpeed / ( 1.0 + accumSpeed );
    float norm1 = frameNum * 0.5 / ( 1.0 + frameNum * 0.5 );
    float normAccumSpeed = saturate( nonLinearAccumSpeed / norm1 );

    float w = frameNum / ( 1.0 + frameNum );
    w *= normAccumSpeed * normAccumSpeed;
    w *= isInScreenMulFootprintQuality;

    float s = normAccumSpeed;
    s *= 1.0 - gReference;

    return float2( w, 1.0 + 3.0 * gFramerateScale * s );
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
