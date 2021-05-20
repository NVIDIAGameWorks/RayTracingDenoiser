#pragma once

// http://www.cs.rit.edu/usr/local/pub/wrc/courses/cg/doc/opengl/redbook/AppJ.pdf
// http://glm.g-truc.net glm/core/type_half.inl

#define F16_M_BITS              10
#define F16_E_BITS              5
#define F16_S_MASK              0x8000

#define UF11_M_BITS             6
#define UF11_E_BITS             5
#define UF11_S_MASK             0x0

#define UF10_M_BITS             5
#define UF10_E_BITS             5
#define UF10_S_MASK             0x0

// f - float
// h - half
// i - int32_t
// s - int16_t
// b - byte
// u - unsigned

namespace Packed
{

// NOTE: pack / unpack float to f16, f11, f10

template<int32_t M_BITS, int32_t E_BITS, int32_t S_MASK> PLATFORM_INLINE uint32_t ToPacked(float val)
{
    const uint32_t E_MASK = (1 << E_BITS) - 1;
    const uint32_t INF = E_MASK << M_BITS;
    const int32_t BIAS = E_MASK >> 1;
    const int32_t ROUND = 1 << (23 - M_BITS - 1);

    // decompose float
    uint32_t f32 = *(uint32_t*)&val;
    uint32_t packed = (f32 >> 16) & S_MASK;
    int32_t e = ((f32 >> 23) & 0xFF) - 127 + BIAS;
    int32_t m = f32 & 0x007FFFFF;

    if( e == 128 + BIAS )
    {
        // Inf
        packed |= INF;

        if( m )
        {
            // NaN
            m >>= 23 - M_BITS;
            packed |= m | (m == 0);
        }
    }
    else if( e > 0 )
    {
        // round to nearest, round "0.5" up
        if( m & ROUND )
        {
            m += ROUND << 1;

            if( m & 0x00800000 )
            {
                // mantissa overflow
                m = 0;
                e++;
            }
        }

        if( e >= E_MASK )
        {
            // exponent overflow - flush to Inf
            packed |= INF;
        }
        else
        {
            // representable value
            m >>= 23 - M_BITS;
            packed |= (e << M_BITS) | m;
        }
    }
    else
    {
        // denormalized or zero
        m = ((m | 0x00800000) >> (1 - e)) + ROUND;
        m >>= 23 - M_BITS;
        packed |= m;
    }

    return packed;
}

template<int32_t M_BITS, int32_t E_BITS, int32_t S_MASK> PLATFORM_INLINE float FromPacked(uint32_t x)
{
    uFloat f;

    const uint32_t E_MASK = (1 << E_BITS) - 1;
    const int32_t BIAS = E_MASK >> 1;
    const float DENORM_SCALE = 1.0f / (1 << (14 + M_BITS));
    const float NORM_SCALE = 1.0f / float(1 << M_BITS);

    int32_t s = (x & S_MASK) << 15;
    int32_t e = (x >> M_BITS) & E_MASK;
    int32_t m = x & ((1 << M_BITS) - 1);

    if( e == 0 )
        f.f = DENORM_SCALE * m;
    else if( e == E_MASK )
        f.i = s | 0x7F800000 | (m << (23 - M_BITS));
    else
    {
        f.f = 1.0f + float(m) * NORM_SCALE;

        if( e < BIAS )
            f.f /= float(1 << (BIAS - e));
        else
            f.f *= float(1 << (e - BIAS));
    }

    if( s )
        f.f = -f.f;

    return f.f;
}

// NOTE: pack / unpack [0; 1] to uint32_t with required bits

template<uint32_t BITS> PLATFORM_INLINE uint32_t ToUint(float x)
{
    DEBUG_Assert( x >= 0.0f && x <= 1.0f );

    const float scale = float((1ull << BITS) - 1ull);

    uint32_t y = (uint32_t)Round(x * scale);

    return y;
}

template<uint32_t BITS> PLATFORM_INLINE float FromUint(uint32_t x)
{
    const float scale = 1.0f / float((1ull << BITS) - 1ull);

    float y = float(x) * scale;

    DEBUG_Assert( y >= 0.0f && y <= 1.0f );

    return y;
}

// NOTE: pack / unpack [-1; 1] to uint32_t with required bits

template<uint32_t BITS> PLATFORM_INLINE uint32_t ToInt(float x)
{
    DEBUG_Assert( x >= -1.0f && x <= 1.0f );

    const float scale = float((1ull << (BITS - 1ull)) - 1ull);
    const uint32_t mask = uint32_t((1ull << BITS) - 1ull);

    int32_t i = (int32_t)Round(x * scale);
    uint32_t y = i & mask;

    return y;
}

template<uint32_t BITS> PLATFORM_INLINE float FromInt(uint32_t x)
{
    const uint32_t sign = uint32_t(1ull << (BITS - 1ull));
    const uint32_t range = sign - 1u;
    const float scale = 1.0f / range;

    if( x & sign )
        x |= ~range;

    int32_t i = x;
    float y = Max(float(i) * scale, -1.0f);

    DEBUG_Assert( y >= -1.0f && y <= 1.0f );

    return y;
}

// NOTE: complex packing (unsigned)

template<uint32_t Rbits, uint32_t Gbits, uint32_t Bbits, uint32_t Abits> PLATFORM_INLINE uint32_t uf4_to_uint(const float4& v)
{
    DEBUG_StaticAssert( Rbits + Gbits + Bbits + Abits <= 32 );

    const uint32_t Rmask = (1 << Rbits) - 1;
    const uint32_t Gmask = (1 << Gbits) - 1;
    const uint32_t Bmask = (1 << Bbits) - 1;
    const uint32_t Amask = (1 << Abits) - 1;

    const uint32_t Gshift = Rbits;
    const uint32_t Bshift = Gshift + Gbits;
    const uint32_t Ashift = Bshift + Bbits;

    const v4f scale = v4f_set(float(Rmask), float(Gmask), float(Bmask), float(Amask));

    v4f t = _mm_mul_ps(v.xmm, scale);
    v4i i = _mm_cvtps_epi32(t);

    uint32_t p = _mm_cvtsi128_si32( i );
    p |= _mm_cvtsi128_si32( _mm_shuffle_epi32(i, _MM_SHUFFLE(1, 1, 1, 1)) ) << Gshift;
    p |= _mm_cvtsi128_si32( _mm_shuffle_epi32(i, _MM_SHUFFLE(2, 2, 2, 2)) ) << Bshift;
    p |= _mm_cvtsi128_si32( _mm_shuffle_epi32(i, _MM_SHUFFLE(3, 3, 3, 3)) ) << Ashift;

    return p;
}

template<> PLATFORM_INLINE uint32_t uf4_to_uint<8, 8, 8, 8>(const float4& v)
{
    v4f t = _mm_mul_ps(v.xmm, _mm_set1_ps(255.0f));
    v4i i = _mm_cvtps_epi32(t);
    i = _mm_shuffle_epi8(i, _mm_set1_epi32(0x0C080400));

    return _mm_cvtsi128_si32(i);
}

PLATFORM_INLINE uint32_t uf2_to_uint1616(float x, float y)
{
    v4f t = v4f_set(x, y, 0.0f, 0.0f);
    t = _mm_mul_ps(t, _mm_set1_ps(65535.0f));
    v4i i = _mm_cvtps_epi32(t);

    uint32_t p = _mm_cvtsi128_si32( i );
    p |= _mm_cvtsi128_si32( _mm_shuffle_epi32(i, _MM_SHUFFLE(1, 1, 1, 1)) ) << 16;

    return p;
}

PLATFORM_INLINE uint32_t uf3_to_packed111110(const float3& v)
{
    DEBUG_Assert( v.x >= 0.0f && v.y >= 0.0f && v.z >= 0.0f );

    uint32_t r = ToPacked<UF11_M_BITS, UF11_E_BITS, UF11_S_MASK>(v.x);
    r |= ToPacked<UF11_M_BITS, UF11_E_BITS, UF11_S_MASK>(v.y) << 11;
    r |= ToPacked<UF10_M_BITS, UF10_E_BITS, UF10_S_MASK>(v.z) << 22;

    return r;
}

// NOTE: complex packing (signed)

template<uint32_t Rbits, uint32_t Gbits, uint32_t Bbits, uint32_t Abits> PLATFORM_INLINE uint32_t sf4_to_int(const float4& v)
{
    DEBUG_StaticAssert( Rbits + Gbits + Bbits + Abits <= 32 );

    const uint32_t Rmask = (1 << Rbits) - 1;
    const uint32_t Gmask = (1 << Gbits) - 1;
    const uint32_t Bmask = (1 << Bbits) - 1;
    const uint32_t Amask = (1 << Abits) - 1;

    const uint32_t Gshift = Rbits;
    const uint32_t Bshift = Gshift + Gbits;
    const uint32_t Ashift = Bshift + Bbits;

    const uint32_t Rrange = (1 << (Rbits - 1)) - 1;
    const uint32_t Grange = (1 << (Gbits - 1)) - 1;
    const uint32_t Brange = (1 << (Bbits - 1)) - 1;
    const uint32_t Arange = (1 << (Abits - 1)) - 1;

    const v4f scale = v4f_set(float(Rrange), float(Grange), float(Brange), float(Arange));
    const v4i mask = _mm_setr_epi32(Rmask, Gmask, Bmask, Amask);

    v4f t = _mm_mul_ps(v.xmm, scale);
    v4i i = _mm_cvtps_epi32(t);
    i = _mm_and_si128(i, mask);

    uint32_t p = _mm_cvtsi128_si32( i );
    p |= _mm_cvtsi128_si32( _mm_shuffle_epi32(i, _MM_SHUFFLE(1, 1, 1, 1)) ) << Gshift;
    p |= _mm_cvtsi128_si32( _mm_shuffle_epi32(i, _MM_SHUFFLE(2, 2, 2, 2)) ) << Bshift;
    p |= _mm_cvtsi128_si32( _mm_shuffle_epi32(i, _MM_SHUFFLE(3, 3, 3, 3)) ) << Ashift;

    return p;
}

template<> PLATFORM_INLINE uint32_t sf4_to_int<8, 8, 8, 8>(const float4& v)
{
    v4f t = _mm_mul_ps(v.xmm, _mm_set1_ps(127.0f));
    v4i i = _mm_cvtps_epi32(t);
    i = _mm_shuffle_epi8(i, _mm_set1_epi32(0x0C080400));

    return _mm_cvtsi128_si32(i);
}

PLATFORM_INLINE uint32_t sf2_to_int1616(float x, float y)
{
    v4f t = v4f_set(x, y, 0.0f, 0.0f);
    t = _mm_mul_ps(t, _mm_set1_ps(32767.0f));
    v4i i = _mm_cvtps_epi32(t);
    i = _mm_and_si128(i, _mm_setr_epi32(65535, 65535, 0, 0));

    uint32_t p = _mm_cvtsi128_si32( i );
    p |= _mm_cvtsi128_si32( _mm_shuffle_epi32(i, _MM_SHUFFLE(1, 1, 1, 1)) ) << 16;

    return p;
}

PLATFORM_INLINE uint16_t sf_to_h(float x)
{
    #if( PLATFORM_INTRINSIC >= PLATFORM_INTRINSIC_AVX1 )

        v4f v = v4f_set(x, 0.0f, 0.0f, 0.0f);
        v4i p = v4f_to_h4(v);

        uint32_t r = _mm_cvtsi128_si32(p);

    #else

        uint32_t r = ToPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(x);

    #endif

    return uint16_t(r);
}

PLATFORM_INLINE uint32_t sf2_to_h2(float x, float y)
{
    #if( PLATFORM_INTRINSIC >= PLATFORM_INTRINSIC_AVX1 )

        v4f v = v4f_set(x, y, 0.0f, 0.0f);
        v4i p = v4f_to_h4(v);

        uint32_t r = _mm_cvtsi128_si32(p);

    #else

        uint32_t r = ToPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(x);
        r |= ToPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(y) << 16;

    #endif

    return r;
}

PLATFORM_INLINE void sf4_to_h4(const float4& v, uint32_t* pu2)
{
    #if( PLATFORM_INTRINSIC >= PLATFORM_INTRINSIC_AVX1 )

        v4i p = v4f_to_h4(v.xmm);

        //*(v2i*)pu2 = _mm_movepi64_pi64(p);
        _mm_storel_epi64((v4i*)pu2, p);

    #else

        pu2[0] = Packed::sf2_to_h2(v.x, v.y);
        pu2[1] = Packed::sf2_to_h2(v.z, v.w);

    #endif
}

// NOTE: complex unpacking (unsigned)

template<uint32_t Rbits, uint32_t Gbits, uint32_t Bbits, uint32_t Abits> PLATFORM_INLINE float4 uint_to_uf4(uint32_t p)
{
    DEBUG_StaticAssert( Rbits + Gbits + Bbits + Abits <= 32 );

    const uint32_t Rmask = (1 << Rbits) - 1;
    const uint32_t Gmask = (1 << Gbits) - 1;
    const uint32_t Bmask = (1 << Bbits) - 1;
    const uint32_t Amask = (1 << Abits) - 1;

    const uint32_t Gshift = Rbits;
    const uint32_t Bshift = Gshift + Gbits;
    const uint32_t Ashift = Bshift + Bbits;

    const v4f scale = v4f_set(1.0f / Rmask, 1.0f / Gmask, 1.0f / Bmask, 1.0f / Amask);

    v4i i = _mm_setr_epi32(p & Rmask, (p >> Gshift) & Gmask, (p >> Bshift) & Bmask, (p >> Ashift) & Amask);
    v4f t = _mm_cvtepi32_ps(i);
    t = _mm_mul_ps(t, scale);

    return t;
}

template<> PLATFORM_INLINE float4 uint_to_uf4<8, 8, 8, 8>(uint32_t p)
{
    #if( PLATFORM_INTRINSIC >= PLATFORM_INTRINSIC_SSE4 )
        v4i i = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(p));
    #else
        v4i i = _mm_set_epi32(p >> 24, (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
    #endif

    v4f t = _mm_cvtepi32_ps(i);
    t = _mm_mul_ps(t, _mm_set1_ps(1.0f / 255.0f));

    return t;
}

PLATFORM_INLINE float3 packed111110_to_uf3(uint32_t p)
{
    float3 v;
    v.x = FromPacked<UF11_M_BITS, UF11_E_BITS, UF11_S_MASK>( p & ((1 << 11) - 1) );
    v.y = FromPacked<UF11_M_BITS, UF11_E_BITS, UF11_S_MASK>( (p >> 11) & ((1 << 11) - 1) );
    v.z = FromPacked<UF10_M_BITS, UF10_E_BITS, UF10_S_MASK>( (p >> 22) & ((1 << 10) - 1) );

    return v;
}

template<uint32_t Rbits, uint32_t Gbits, uint32_t Bbits, uint32_t Abits> PLATFORM_INLINE void uint_to_4ui(uint32_t p, int32_t* v)
{
    DEBUG_StaticAssert( Rbits + Gbits + Bbits + Abits <= 32 );

    const uint32_t Rmask = (1 << Rbits) - 1;
    const uint32_t Gmask = (1 << Gbits) - 1;
    const uint32_t Bmask = (1 << Bbits) - 1;
    const uint32_t Amask = (1 << Abits) - 1;

    const uint32_t Gshift = Rbits;
    const uint32_t Bshift = Gshift + Gbits;
    const uint32_t Ashift = Bshift + Bbits;

    v[0] = p & Rmask;
    v[1] = (p >> Gshift) & Gmask;
    v[2] = (p >> Bshift) & Bmask;
    v[3] = (p >> Ashift) & Amask;
}

// NOTE: complex unpacking (signed)

template<uint32_t Rbits, uint32_t Gbits, uint32_t Bbits, uint32_t Abits> PLATFORM_INLINE float4 int_to_sf4(uint32_t p)
{
    DEBUG_StaticAssert( Rbits + Gbits + Bbits + Abits <= 32 );

    const uint32_t Rmask = (1 << Rbits) - 1;
    const uint32_t Gmask = (1 << Gbits) - 1;
    const uint32_t Bmask = (1 << Bbits) - 1;
    const uint32_t Amask = (1 << Abits) - 1;

    const uint32_t Gshift = Rbits;
    const uint32_t Bshift = Gshift + Gbits;
    const uint32_t Ashift = Bshift + Bbits;

    const uint32_t Rsign = (1 << (Rbits - 1));
    const uint32_t Gsign = (1 << (Gbits - 1));
    const uint32_t Bsign = (1 << (Bbits - 1));
    const uint32_t Asign = (1 << (Abits - 1));

    const v4i vsign = _mm_setr_epi32(Rsign, Gsign, Bsign, Asign);
    const v4i vor = _mm_setr_epi32(~(Rsign - 1), ~(Gsign - 1), ~(Bsign - 1), ~(Asign - 1));
    const v4f vscale = v4f_set(1.0f / (Rsign - 1), 1.0f / (Gsign - 1), 1.0f / (Bsign - 1), 1.0f / (Asign - 1));

    v4i i = _mm_setr_epi32(p & Rmask, (p >> Gshift) & Gmask, (p >> Bshift) & Bmask, (p >> Ashift) & Amask);

    v4i mask = _mm_and_si128(i, vsign);
    v4i ii = _mm_or_si128(i, vor);
    i = xmmi_select(i, ii, _mm_cmpeq_epi32(mask, _mm_setzero_si128()));

    v4f t = _mm_cvtepi32_ps(i);
    t = _mm_mul_ps(t, vscale);
    t = _mm_max_ps(t, _mm_set1_ps(-1.0f));

    return t;
}

template<> PLATFORM_INLINE float4 int_to_sf4<8, 8, 8, 8>(uint32_t p)
{
    #if( PLATFORM_INTRINSIC >= PLATFORM_INTRINSIC_SSE4 )
        v4i i = _mm_cvtepi8_epi32(_mm_cvtsi32_si128(p));
    #else
        v4i i = _mm_set_epi32(int8_t(p >> 24), int8_t((p >> 16) & 0xFF), int8_t((p >> 8) & 0xFF), int8_t(p & 0xFF));
    #endif

    v4f t = _mm_cvtepi32_ps(i);
    t = _mm_mul_ps(t, _mm_set1_ps(1.0f / 127.0f));
    t = _mm_max_ps(t, _mm_set1_ps(-1.0f));

    return t;
}

PLATFORM_INLINE float2 h2_to_sf2(uint32_t ui)
{
    float2 r;

    #if( PLATFORM_INTRINSIC >= PLATFORM_INTRINSIC_AVX1 )

        v4i p = _mm_cvtsi32_si128(ui);
        v4f f = _mm_cvtph_ps(p);

        _mm_storel_pi(&r.mm, f);

    #else

        r.x = FromPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(ui & 0xFFFF);
        r.y = FromPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(ui >> 16);

    #endif

    return r;
}

PLATFORM_INLINE float4 h4_to_sf4(const uint32_t* pu2)
{
    float4 f;

    #if( PLATFORM_INTRINSIC >= PLATFORM_INTRINSIC_AVX1 )

        v4i p = _mm_loadl_epi64((const v4i*)pu2);
        f.xmm = _mm_cvtph_ps(p);

    #else

        f.x = FromPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(pu2[0] & 0xFFFF);
        f.y = FromPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(pu2[0] >> 16);
        f.z = FromPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(pu2[1] & 0xFFFF);
        f.w = FromPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(pu2[1] >> 16);

    #endif

    return f;
}

template<uint32_t Rbits, uint32_t Gbits, uint32_t Bbits, uint32_t Abits> PLATFORM_INLINE void int_to_4i(uint32_t p, int32_t* v)
{
    DEBUG_StaticAssert( Rbits + Gbits + Bbits + Abits <= 32 );

    const uint32_t Rmask = (1 << Rbits) - 1;
    const uint32_t Gmask = (1 << Gbits) - 1;
    const uint32_t Bmask = (1 << Bbits) - 1;
    const uint32_t Amask = (1 << Abits) - 1;

    const uint32_t Gshift = Rbits;
    const uint32_t Bshift = Gshift + Gbits;
    const uint32_t Ashift = Bshift + Bbits;

    const uint32_t Rsign = (1 << (Rbits - 1));
    const uint32_t Gsign = (1 << (Gbits - 1));
    const uint32_t Bsign = (1 << (Bbits - 1));
    const uint32_t Asign = (1 << (Abits - 1));

    const v4i vsign = _mm_setr_epi32(Rsign, Gsign, Bsign, Asign);
    const v4i vor = _mm_setr_epi32(~(Rsign - 1), ~(Gsign - 1), ~(Bsign - 1), ~(Asign - 1));

    v4i i = _mm_setr_epi32(p & Rmask, (p >> Gshift) & Gmask, (p >> Bshift) & Bmask, (p >> Ashift) & Amask);

    v4i mask = _mm_and_si128(i, vsign);
    v4i ii = _mm_or_si128(i, vor);
    i = xmmi_select(i, ii, _mm_cmpeq_epi32(mask, _mm_setzero_si128()));

    _mm_storeu_si128((v4i*)v, i);
}

// Octahedron packing for unit vectors - xonverts a 3D unit vector to a 2D vector with [0; 1] range
// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
// [Cigolle 2014, "A Survey of Efficient Representations for Independent Unit Vectors"]
//                    Mean      Max
// oct     8:8        0.33709   0.94424
// snorm   8:8:8      0.17015   0.38588
// oct     10:10      0.08380   0.23467
// snorm   10:10:10   0.04228   0.09598
// oct     12:12      0.02091   0.05874
PLATFORM_INLINE float2 EncodeUnitVector(const float3& v, bool bSigned = false)
{
    float3 t = v / (Abs(v.x) + Abs(v.y) + Abs(v.z));
    float3 a = t.z >= 0.0f ? t : (float3(1.0f) - Abs(t.yxz())) * Sign(t);

    if( !bSigned )
        a = Saturate(a * 0.5f + 0.5f);

    return float2(a.x, a.y);
}

PLATFORM_INLINE float3 DecodeUnitVector(const float2& p, bool bSigned = false)
{
    float2 t = bSigned ? p : (p * 2.0f - 1.0f);

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3( t.x, t.y, 1.0f - Abs(t.x) - Abs(t.y));
    float a = Saturate(-n.z);
    n.x += n.x >= 0.0f ? -a : a;
    n.y += n.y >= 0.0f ? -a : a;

    return Normalize(n);
}

};

struct half_float
{
    uint16_t us;

    PLATFORM_INLINE half_float()
    {
    }

    PLATFORM_INLINE half_float(float x)
    {
        #if( PLATFORM_INTRINSIC >= PLATFORM_INTRINSIC_AVX1 )

            v4f v = _mm_set_ss(x);
            v4i p = v4f_to_h4(v);

            us = (uint16_t)_mm_cvtsi128_si32(p);

        #else

            us = (uint16_t)Packed::ToPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(x);

        #endif
    }

    PLATFORM_INLINE half_float(uint16_t x) :
        us(x)
    {
    }

    PLATFORM_INLINE operator float() const
    {
        #if( PLATFORM_INTRINSIC >= PLATFORM_INTRINSIC_AVX1 )

            v4i p = _mm_cvtsi32_si128(us);
            v4f f = _mm_cvtph_ps(p);

            return _mm_cvtss_f32(f);

        #else

            return Packed::FromPacked<F16_M_BITS, F16_E_BITS, F16_S_MASK>(us);

        #endif
    }
};
