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

// IMPORTANT: if REBLUR_NORMAL_ULP == 1, then "GetEncodingAwareNormalWeight" for 0-roughness
// can return values < 1 even for same normals due to data re-packing
#define REBLUR_ROUGHNESS_ULP                            ( 1.5 / 255.0 )
#define REBLUR_NORMAL_ULP                               ( 2.0 / 255.0 )

#define REBLUR_MAX_ACCUM_FRAME_NUM                      63.0
#define REBLUR_ACCUMSPEED_BITS                          7 // "( 1 << REBLUR_ACCUMSPEED_BITS ) - 1" must be >= REBLUR_MAX_ACCUM_FRAME_NUM
#define REBLUR_MATERIALID_BITS                          ( 16 - REBLUR_ACCUMSPEED_BITS - REBLUR_ACCUMSPEED_BITS )

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
    t.xy /= REBLUR_MAX_ACCUM_FRAME_NUM;

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

uint PackData2( float fbits, float curvature, float virtualHistoryAmount )
{
    // BITS:
    // 0     - CatRom flag
    // 1-4   - occlusion 2x2
    // other - free // TODO: use if needed

    uint p = uint( fbits + 0.5 );

    p |= uint( saturate( virtualHistoryAmount ) * 255.0 + 0.5 ) << 8;
    p |= f32tof16( curvature ) << 16;

    return p;
}

float2 UnpackData2( uint p, float viewZ, out uint bits )
{
    bits = p & 0xFF;

    float virtualHistoryAmount = float( ( p >> 8 ) & 0xFF ) / 255.0;
    float curvature = f16tof32( p >> 16 );

    return float2( virtualHistoryAmount, curvature );
}

// Accumulation speed

float GetFPS( float period = 1.0 ) // TODO: can be handy, but not used
{
    return gFramerateScale * 30.0 * period;
}

float GetSmbAccumSpeed( float smbSpecAccumSpeedFactor, float diffParallaxInPixels, float viewZ, float specAccumSpeed, float maxAngle )
{
    // "diffParallaxInPixels" is the difference between true specular motion (vmb) and surface based motion (smb),
    // but "vmb" parallax can only be used if virtual motion confidence is high
    float smbSpecAccumSpeed = gMaxAccumulatedFrameNum / ( 1.0 + smbSpecAccumSpeedFactor * diffParallaxInPixels );

    // Tests 142, 148 and 155 ( or anything with very low roughness and curved surfaces )
    float ta = PixelRadiusToWorld( gUnproject, gOrthoMode, diffParallaxInPixels, viewZ ) / viewZ;
    float ca = STL::Math::Rsqrt( 1.0 + ta * ta );
    float angle = STL::Math::AcosApprox( ca );
    smbSpecAccumSpeed *= STL::Math::SmoothStep( maxAngle, 0.0, angle );

    // Do not trust too small values
    smbSpecAccumSpeed *= STL::Math::LinearStep( 0.5, 1.5, smbSpecAccumSpeed );

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
    float a = gHistoryFixFrameNum * 2.0 / 3.0 + 1e-6;
    float b = gHistoryFixFrameNum * 4.0 / 3.0 + 2e-6;

    return STL::Math::LinearStep( a, b, accumSpeed );
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

float MixFastHistoryAndCurrent( float history, float current, float f )
{
    return lerp( history, current, f );
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
    error *= gNonReferenceAccumulation;

    return error;
}

float ComputeAntilagScale(
    REBLUR_TYPE history, REBLUR_TYPE signal, REBLUR_TYPE m1, REBLUR_TYPE sigma,
    float4 antilagMinMaxThreshold, float2 antilagSigmaScale, float stabilizationStrength,
    float curvatureMulPixelSize, float accumSpeed, float roughness = 1.0
)
{
    // On-edge strictness
    m1 = lerp( m1, signal, saturate( abs( curvatureMulPixelSize ) ) );

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

float GetEncodingAwareNormalWeight( float3 Ncurr, float3 Nprev, float maxAngle, float angleThreshold = 0.0 )
{
    // Anything below "angleThreshold" is ignored
    angleThreshold += REBLUR_NORMAL_ULP;

    float cosa = saturate( dot( Ncurr, Nprev ) );

    float a = 1.0 / maxAngle;
    float d = STL::Math::AcosApprox( cosa );

    float w = STL::Math::SmoothStep01( 1.0 - ( d - angleThreshold ) * a );

    // Needed to mitigate imprecision issues because prev normals are RGBA8 ( test 3, 43 if roughness is low )
    w = STL::Math::SmoothStep( 0.05, 0.95, w );

    return w;
}

// Weight parameters

float GetNormalWeightParams( float nonLinearAccumSpeed, float fraction, float roughness = 1.0 )
{
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
    angle *= lerp( saturate( fraction ), 1.0, nonLinearAccumSpeed ); // TODO: use as "percentOfVolume" instead?

    return 1.0 / max( angle, REBLUR_NORMAL_ULP );
}

float2 GetTemporalAccumulationParams( float isInScreenMulFootprintQuality, float accumSpeed )
{
    float w = isInScreenMulFootprintQuality;
    w *= accumSpeed / ( 1.0 + accumSpeed );

    float s = w;
    s *= gNonReferenceAccumulation;

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

void BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
    float2 samplePos, float2 invTextureSize,
    float4 bilinearCustomWeights, bool useBicubic,
    Texture2D<REBLUR_TYPE> tex0, out REBLUR_TYPE c0 ) // main - CatRom
{
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( c0, tex0 );
}

void BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
    float2 samplePos, float2 invTextureSize,
    float4 bilinearCustomWeights, bool useBicubic,
    Texture2D<REBLUR_TYPE> tex0, out REBLUR_TYPE c0, // main - CatRom
    Texture2D<REBLUR_FAST_TYPE> tex1, out REBLUR_FAST_TYPE c1 ) // fast - bilinear
{
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( c0, tex0 );
    _BilinearFilterWithCustomWeights_Color( c1, tex1 );
}

void BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
    float2 samplePos, float2 invTextureSize,
    float4 bilinearCustomWeights, bool useBicubic,
    Texture2D<REBLUR_TYPE> tex0, out REBLUR_TYPE c0, // main - CatRom
    Texture2D<REBLUR_SH_TYPE> tex1, out REBLUR_SH_TYPE c1 ) // SH - bilinear
{
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( c0, tex0 );
    _BilinearFilterWithCustomWeights_Color( c1, tex1 );
}

void BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
    float2 samplePos, float2 invTextureSize,
    float4 bilinearCustomWeights, bool useBicubic,
    Texture2D<REBLUR_TYPE> tex0, out REBLUR_TYPE c0, // main - CatRom
    Texture2D<REBLUR_FAST_TYPE> tex1, out REBLUR_FAST_TYPE c1, // fast - bilinear
    Texture2D<REBLUR_SH_TYPE> tex2, out REBLUR_SH_TYPE c2 ) // SH - bilinear
{
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Init;
    _BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights_Color( c0, tex0 );
    _BilinearFilterWithCustomWeights_Color( c1, tex1 );
    _BilinearFilterWithCustomWeights_Color( c2, tex2 );
}
