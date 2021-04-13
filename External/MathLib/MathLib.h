#pragma once

#define MATHLIB 1
#define MATHLIB_MAJOR 1
#define MATHLIB_MINOR 2
#define MATHLIB_DATE "17 May 2020"

#ifdef NDC_DONT_CARE
    // FIXME: absolutely not important here...
    #define PROPS_D3D11
#endif

// IMPORTANT: disable - nonstandard extension used: nameless struct/union
#pragma warning(disable:4201)

// NOTE: all random floating point functions doesn't return zero (because I hate zeroes)
//       ranges: uf - (0; 1], sf - [-1; 0) (0; 1]

// NOTE: C standard rand() (15 bit, floating point with fixed step 1 / 0x7FFF)
// perf: 1x
// thread: safe
#define PLATFORM_RND_CRT 0

// NOTE: extreme fast PRNG (32 bit)
// perf: 15x
// thread: NOT SAFE (but you can create your own instances of sFastRand in another threads)
#define PLATFORM_RND_FAST 1

// NOTE: slower, hardware implemented CSPRNG (cryptographically secure), "rdrand" instruction support is required
// https://software.intel.com/sites/default/files/managed/4d/91/DRNG_Software_Implementation_Guide_2.0.pdf
// perf: 0.4x
// thread: safe
#define PLATFORM_RND_HW 2

//======================================================================================================================
//                                                  Settings
//======================================================================================================================

// NOTE: more precision (a little bit slower)

#define MATH_NEWTONRAPHSON_APROXIMATION

// NOTE: only for debug (not need if you are accurate in horizontal operations)

#define MATH_CHECK_W_IS_ZERO
#undef MATH_CHECK_W_IS_ZERO

// NOTE: only for debug (generate exeptions in rounding operations, only for SSE4)

#define MATH_EXEPTIONS
#undef MATH_EXEPTIONS

#define MATH_NAMESPACE
#undef MATH_NAMESPACE

// NOTE: see RND modes above

#ifndef PLATFORM_RND_MODE
    #define PLATFORM_RND_MODE PLATFORM_RND_FAST
#endif

//======================================================================================================================
//                                                      START
//======================================================================================================================

#ifdef MATH_NAMESPACE
namespace ml {
#endif

#include <math.h>
#include <float.h>
#if defined(__GNUC__) || defined (__clang__)
    #if defined(__i386__) || defined(__x86_64__)
        #include <x86intrin.h>
    #endif
  
    #if defined(__arm__)
        #include <armintr.h>
    #endif
  
    #if defined(__aarch64__)
        #include <arm64intr.h>
    #endif
#else
    #include <intrin.h>
#endif

#include "Platform.h"
#include "NdcConfig.h"

//======================================================================================================================
//                                                      SIMD ASM
//======================================================================================================================

#ifdef MATH_EXEPTIONS
    #define ROUNDING_EXEPTIONS_MASK     _MM_FROUND_RAISE_EXC
#else
    #define ROUNDING_EXEPTIONS_MASK     _MM_FROUND_NO_EXC
#endif

#include "IntrinEmu.h"

typedef __m128      v4f;
typedef __m256      v8f;

typedef __m64       v2i;
typedef __m128i     v4i;
typedef __m256i     v8i;

typedef __m128d     v2d;
typedef __m256d     v4d;

//======================================================================================================================
//                                                  Enums
//======================================================================================================================

enum eClip : uint8_t
{
    CLIP_OUT,
    CLIP_IN,
    CLIP_PARTIAL,
};

enum eCmp : uint8_t
{
    CmpLess         = _CMP_LT_OQ,
    CmpLequal       = _CMP_LE_OQ,
    CmpGreater      = _CMP_GT_OQ,
    CmpGequal       = _CMP_GE_OQ,
    CmpEqual        = _CMP_EQ_OQ,
    CmpNotequal     = _CMP_NEQ_UQ,
};

enum eCoordinate : uint32_t
{
    COORD_X             = 0,
    COORD_Y,
    COORD_Z,
    COORD_W,

    COORD_S             = 0,
    COORD_T,
    COORD_R,
    COORD_Q,

    COORD_2D            = 2,
    COORD_3D,
    COORD_4D,
};

enum ePlaneType : uint32_t
{
    PLANE_LEFT,
    PLANE_RIGHT,
    PLANE_BOTTOM,
    PLANE_TOP,
    PLANE_NEAR,
    PLANE_FAR,

    PLANES_NUM,
    PLANES_NO_NEAR_FAR  = 4,
    PLANES_NO_FAR       = 5,

    PLANE_MASK_L        = 1 << PLANE_LEFT,
    PLANE_MASK_R        = 1 << PLANE_RIGHT,
    PLANE_MASK_B        = 1 << PLANE_BOTTOM,
    PLANE_MASK_T        = 1 << PLANE_TOP,
    PLANE_MASK_N        = 1 << PLANE_NEAR,
    PLANE_MASK_F        = 1 << PLANE_FAR,

    PLANE_MASK_NONE     = 0,
    PLANE_MASK_LRBT     = PLANE_MASK_L | PLANE_MASK_R | PLANE_MASK_B | PLANE_MASK_T,
    PLANE_MASK_NF       = PLANE_MASK_N | PLANE_MASK_F,
    PLANE_MASK_LRBTNF   = PLANE_MASK_LRBT | PLANE_MASK_NF,
};

enum eProjectionData
{
    PROJ_ZNEAR,
    PROJ_ZFAR,
    PROJ_ASPECT,
    PROJ_FOVX,
    PROJ_FOVY,
    PROJ_MINX,
    PROJ_MAXX,
    PROJ_MINY,
    PROJ_MAXY,
    PROJ_DIRX,
    PROJ_DIRY,
    PROJ_ANGLEMINX,
    PROJ_ANGLEMAXX,
    PROJ_ANGLEMINY,
    PROJ_ANGLEMAXY,

    PROJ_NUM,
};

enum eProjectionFlag
{
    PROJ_ORTHO          = 0x00000001,
    PROJ_REVERSED_Z     = 0x00000002,
    PROJ_LEFT_HANDED    = 0x00000004,
};

//======================================================================================================================
//                                                      Misc
//======================================================================================================================

template<class T> PLATFORM_INLINE T Sign(const T& x);
template<class T> PLATFORM_INLINE T Abs(const T& x);
template<class T> PLATFORM_INLINE T Floor(const T& x);
template<class T> PLATFORM_INLINE T Round(const T& x);
template<class T> PLATFORM_INLINE T Fract(const T& x);
template<class T> PLATFORM_INLINE T Mod(const T& x, const T& y);
template<class T> PLATFORM_INLINE T Snap(const T& x, const T& step);
template<class T> PLATFORM_INLINE T Min(const T& x, const T& y);
template<class T> PLATFORM_INLINE T Max(const T& x, const T& y);
template<class T> PLATFORM_INLINE T Clamp(const T& x, const T& a, const T& b);
template<class T> PLATFORM_INLINE T Saturate(const T& x);
template<class T> PLATFORM_INLINE T Lerp(const T& a, const T& b, const T& x);
template<class T> PLATFORM_INLINE T Smoothstep(const T& a, const T& b, const T& x);
template<class T> PLATFORM_INLINE T Linearstep(const T& a, const T& b, const T& x);
template<class T> PLATFORM_INLINE T Step(const T& edge, const T& x);

template<class T> PLATFORM_INLINE T Sin(const T& x);
template<class T> PLATFORM_INLINE T Cos(const T& x);
template<class T> PLATFORM_INLINE T SinCos(const T& x, T* pCos);
template<class T> PLATFORM_INLINE T Tan(const T& x);
template<class T> PLATFORM_INLINE T Asin(const T& x);
template<class T> PLATFORM_INLINE T Acos(const T& x);
template<class T> PLATFORM_INLINE T Atan(const T& x);
template<class T> PLATFORM_INLINE T Atan(const T& y, const T& x);
template<class T> PLATFORM_INLINE T Sqrt(const T& x);
template<class T> PLATFORM_INLINE T Rsqrt(const T& x);
template<class T> PLATFORM_INLINE T Pow(const T& x, const T& y);
template<class T> PLATFORM_INLINE T Log(const T& x);
template<class T> PLATFORM_INLINE T Exp(const T& x);

template<class T> PLATFORM_INLINE T Pi(const T& mul = T(1));
template<class T> PLATFORM_INLINE T RadToDeg(const T& a);
template<class T> PLATFORM_INLINE T DegToRad(const T& a);

template<class T> PLATFORM_INLINE void Swap(T& x, T& y);

template<class T> PLATFORM_INLINE T Slerp(const T& a, const T& b, float x);

template<class T> PLATFORM_INLINE T CurveSmooth(const T& x);
template<class T> PLATFORM_INLINE T CurveSin(const T& x);
template<class T> PLATFORM_INLINE T WaveTriangle(const T& x);
template<class T> PLATFORM_INLINE T WaveTriangleSmooth(const T& x);

template<eCmp cmp, class T> PLATFORM_INLINE bool All(const T& x, const T& y);
template<eCmp cmp, class T> PLATFORM_INLINE bool Any(const T& x, const T& y);

#include "MathLib_f.h"
#include "MathLib_d.h"

//======================================================================================================================
//                                                      Conversions
//======================================================================================================================

PLATFORM_INLINE float3 ToFloat(const double3& x)
{
    v4f r = ymm_to_xmm(x.ymm);

    return r;
}

PLATFORM_INLINE float4 ToFloat(const double4& x)
{
    v4f r = ymm_to_xmm(x.ymm);

    return r;
}

PLATFORM_INLINE float4x4 ToFloat(const double4x4& m)
{
    float4x4 r;
    r.col0 = ymm_to_xmm(m.col0);
    r.col1 = ymm_to_xmm(m.col1);
    r.col2 = ymm_to_xmm(m.col2);
    r.col3 = ymm_to_xmm(m.col3);

    return r;
}

PLATFORM_INLINE double3 ToDouble(const float3& x)
{
    return xmm_to_ymm(x.xmm);
}

PLATFORM_INLINE double4 ToDouble(const float4& x)
{
    return xmm_to_ymm(x.xmm);
}

PLATFORM_INLINE double4x4 ToDouble(const float4x4& m)
{
    double4x4 r;
    r.col0 = xmm_to_ymm(m.col0);
    r.col1 = xmm_to_ymm(m.col1);
    r.col2 = xmm_to_ymm(m.col2);
    r.col3 = xmm_to_ymm(m.col3);

    return r;
}

//======================================================================================================================
//                                                      Integer
//======================================================================================================================

class int2
{
    public:

        union
        {
            struct
            {
                int32_t pv[COORD_2D];
            };

            struct
            {
                int32_t x, y;
            };
        };

    public:

        PLATFORM_INLINE int2()
        {
        }

        PLATFORM_INLINE int2(int32_t a, int32_t b) : x(a), y(b)
        {
        }
};

class uint2
{
    public:

        union
        {
            struct
            {
                uint32_t pv[COORD_2D];
            };

            struct
            {
                uint32_t x, y;
            };
        };

    public:

        PLATFORM_INLINE uint2()
        {
        }

        PLATFORM_INLINE uint2(uint32_t a, uint32_t b) : x(a), y(b)
        {
        }

        PLATFORM_INLINE uint2(const float2& v) : x( uint32_t(v.x) ), y( uint32_t(v.y) )
        {
        }
};

class int4
{
    public:

        union
        {
            struct
            {
                v4i xmm;
            };

            struct
            {
                int32_t pv[COORD_4D];
            };

            struct
            {
                int32_t x, y, z, w;
            };
        };

    public:

        // NOTE: constructors

        PLATFORM_INLINE int4()
        {
        }

        PLATFORM_INLINE int4(int32_t a) : xmm( _mm_set1_epi32(a) )
        {
        }

        PLATFORM_INLINE int4(int32_t a, int32_t b, int32_t c, int32_t d) : xmm( _mm_setr_epi32(a, b, c, d) )
        {
        }

        PLATFORM_INLINE int4(const int32_t* v4) : xmm( _mm_loadu_si128((v4i*)v4) )
        {
        }

        PLATFORM_INLINE int4(const v4i& vec) : xmm(vec)
        {
        }

        // NOTE: set

        PLATFORM_INLINE void Set0()
        {
            xmm = _mm_setzero_si128();
        }

        PLATFORM_INLINE void operator = (const int4& vec)
        {
            xmm = vec.xmm;
        }

        // NOTE: compare

        PLATFORM_INLINE bool operator == (const int4& v) const
        {
            v4f r = _mm_castsi128_ps( _mm_cmpeq_epi32(xmm, v.xmm) );

            return xmm_test4_all(r);
        }

        PLATFORM_INLINE bool operator != (const int4& v) const
        {
            v4f r = _mm_castsi128_ps( _mm_cmpeq_epi32(xmm, v.xmm) );

            return !xmm_test4_all(r);
        }
};

class uint4
{
    public:

        union
        {
            struct
            {
                v4i xmm;
            };

            struct
            {
                uint32_t pv[COORD_4D];
            };

            struct
            {
                uint32_t x, y, z, w;
            };
        };

    public:

        // NOTE: constructors

        PLATFORM_INLINE uint4()
        {
        }

        PLATFORM_INLINE uint4(int32_t a) : xmm( _mm_set1_epi32(a) )
        {
        }

        PLATFORM_INLINE uint4(int32_t a, int32_t b, int32_t c, int32_t d) : xmm( _mm_setr_epi32(a, b, c, d) )
        {
        }

        PLATFORM_INLINE uint4(const int32_t* v4) : xmm( _mm_loadu_si128((v4i*)v4) )
        {
        }

        PLATFORM_INLINE uint4(const v4i& vec) : xmm(vec)
        {
        }

        // NOTE: set

        PLATFORM_INLINE void Set0()
        {
            xmm = _mm_setzero_si128();
        }

        PLATFORM_INLINE void operator = (const uint4& vec)
        {
            xmm = vec.xmm;
        }

        // NOTE: compare

        PLATFORM_INLINE bool operator == (const uint4& v) const
        {
            v4f r = _mm_castsi128_ps( _mm_cmpeq_epi32(xmm, v.xmm) );

            return xmm_test4_all(r);
        }

        PLATFORM_INLINE bool operator != (const uint4& v) const
        {
            v4f r = _mm_castsi128_ps( _mm_cmpeq_epi32(xmm, v.xmm) );

            return xmm_test4_none(r);
        }
};

//======================================================================================================================
// Floating point tricks
//======================================================================================================================

union uFloat
{
    float f;
    uint32_t i;

    PLATFORM_INLINE uFloat()
    {
    }

    PLATFORM_INLINE uFloat(float x) : f(x)
    {
    }

    PLATFORM_INLINE uFloat(uint32_t x) : i(x)
    {
    }

    PLATFORM_INLINE void Abs()
    {
        i &= ~(1 << 31);
    }

    PLATFORM_INLINE bool IsNegative() const
    {
        return (i >> 31) != 0;
    }

    PLATFORM_INLINE uint32_t Mantissa() const
    {
        return i & ((1 << 23) - 1);
    }

    PLATFORM_INLINE uint32_t Exponent() const
    {
        return (i >> 23) & 255;
    }

    PLATFORM_INLINE bool IsInf() const
    {
        return Exponent() == 255 && Mantissa() == 0;
    }

    PLATFORM_INLINE bool IsNan() const
    {
        return Exponent() == 255 && Mantissa() != 0;
    }

    static PLATFORM_INLINE float PrecisionGreater(float x)
    {
        uFloat y(x);
        y.i++;

        return y.f - x;
    }

    static PLATFORM_INLINE float PrecisionLess(float x)
    {
        uFloat y(x);
        y.i--;

        return y.f - x;
    }
};

union uDouble
{
    double f;
    uint64_t i;

    PLATFORM_INLINE uDouble()
    {
    }

    PLATFORM_INLINE uDouble(double x) : f(x)
    {
    }

    PLATFORM_INLINE uDouble(uint64_t x) : i(x)
    {
    }

    PLATFORM_INLINE bool IsNegative() const
    {
        return (i >> 63) != 0;
    }

    PLATFORM_INLINE void Abs()
    {
        i &= ~(1ULL << 63);
    }

    PLATFORM_INLINE uint64_t Mantissa() const
    {
        return i & ((1ULL << 52) - 1);
    }

    PLATFORM_INLINE uint64_t Exponent() const
    {
        return (i >> 52) & 2047;
    }

    PLATFORM_INLINE bool IsInf() const
    {
        return Exponent() == 2047 && Mantissa() == 0;
    }

    PLATFORM_INLINE bool IsNan() const
    {
        return Exponent() == 2047 && Mantissa() != 0;
    }

    static PLATFORM_INLINE double PrecisionGreater(double x)
    {
        uDouble y(x);
        y.i++;

        return y.f - x;
    }

    static PLATFORM_INLINE double PrecisionLess(double x)
    {
        uDouble y(x);
        y.i--;

        return y.f - x;
    }
};

float DoubleToGequal(double dValue);
float DoubleToLequal(double dValue);

//======================================================================================================================
//                                                      Errors
//======================================================================================================================

class cError
{
    public:

        double dMeanAbsError;       // NOTE: mean absolute error
        double dMaxAbsError;        // NOTE: max absolute error
        double dMeanSqrError;       // NOTE: mean squared error
        double dRootMeanSqrError;   // NOTE: root mean squared error
        double dPsnr;               // NOTE: peak signal to noise ratio in dB

        uint32_t uiSamples;

    public:

        PLATFORM_INLINE cError()
        {
            Zero();
        }

        PLATFORM_INLINE void Zero()
        {
            memset(this, 0, sizeof(*this));
        }

        PLATFORM_INLINE void AddSample(double dSample)
        {
            double v = fabs(dSample);

            dMeanAbsError += v;
            dMaxAbsError = Max(dMaxAbsError, v);
            dMeanSqrError += v * v;

            uiSamples++;
        }

        PLATFORM_INLINE void Done(double dScale)
        {
            if( uiSamples )
            {
                double rs = 1.0 / double(uiSamples);

                dMeanAbsError *= rs;
                dMeanSqrError *= rs;
                dRootMeanSqrError = Sqrt(dMeanSqrError);
                dPsnr = (dRootMeanSqrError < c_fEps) ? 999.0 : 20.0 * log10(dScale / dRootMeanSqrError);
            }
        }
};

class cNormalError
{
    public:

        double dAngDevError;            // NOTE: angular deviation error
        double dMeanSqrError;           // NOTE: mean squared error
        double dRootMeanSqrError;       // NOTE: root mean squared error
        double dPsnr;                   // NOTE: peak signal to noise ratio in dB

        uint32_t uiSamples;

    public:

        PLATFORM_INLINE cNormalError()
        {
            Zero();
        }

        PLATFORM_INLINE void Zero()
        {
            memset(this, 0, sizeof(*this));
        }

        PLATFORM_INLINE void AddSample(const float3& n1, const float3& n2, float scale)
        {
            float dot = Dot33(n1, n2);

            dAngDevError += Acos( Clamp(dot, -1.0f, 1.0f) );

            float3 v = (n1 - n2) * (scale * 0.5f);
            dMeanSqrError += LengthSquared(v);

            uiSamples++;
        }

        PLATFORM_INLINE void Done(double dScale)
        {
            if( uiSamples )
            {
                double rs = 1.0 / double(uiSamples);

                dAngDevError *= rs;
                dMeanSqrError *= rs / 3.0;
                dRootMeanSqrError = Sqrt(dMeanSqrError);
                dPsnr = (dRootMeanSqrError < c_fEps) ? 999.0 : 20.0 * log10(dScale / dRootMeanSqrError);
            }
        }
};

//======================================================================================================================
//                                                      Random
//======================================================================================================================

struct sFastRand
{
    // NOTE: vector part
    // Super-Fast MWC1616 Pseudo-Random Number Generator for Intel/AMD Processors (using SSE or SSE4 instruction set)
    // Copyright (c) 2012, Ivan Dimkovic, http://www.digicortex.net/node/22
    // MWC1616: http://www.helsbreth.org/random/rng_mwc1616.html

    v4i m_a;
    v4i m_b;

    // NOTE: scalar part
    // https://software.intel.com/en-us/articles/fast-random-number-generator-on-the-intel-pentiumr-4-processor

    uint32_t m_uiSeed;

    PLATFORM_INLINE sFastRand()
    {
        Seed( rand() );
    }

    PLATFORM_INLINE void Seed(uint32_t uiSeed)
    {
        m_uiSeed = uiSeed;

        uint32_t a1 = ui1();
        uint32_t a2 = ui1();
        uint32_t a3 = ui1();
        uint32_t a4 = ui1();
        uint32_t b1 = ui1();
        uint32_t b2 = ui1();
        uint32_t b3 = ui1();
        uint32_t b4 = ui1();

        m_a = _mm_set_epi32(a4, a3, a2, a1);
        m_b = _mm_set_epi32(b4, b3, b2, b1);
    }

    PLATFORM_INLINE uint32_t ui1()
    {
        m_uiSeed = 214013 * m_uiSeed + 2531011;

        return m_uiSeed;
    }

    PLATFORM_INLINE v4i ui4()
    {
        const v4i mask = _mm_set1_epi32(0xFFFF);
        const v4i m1 = _mm_set1_epi32(0x4650);
        const v4i m2 = _mm_set1_epi32(0x78B7);

        v4i a = m_a;
        v4i b = m_b;

        #if( PLATFORM_INTRINSIC < PLATFORM_INTRINSIC_SSE4 )

            v4i ashift = _mm_srli_epi32(a, 0x10);
            v4i amask = _mm_and_si128(a, mask);
            v4i amullow = _mm_mullo_epi16(amask, m1);
            v4i amulhigh = _mm_mulhi_epu16(amask, m1);
            v4i amulhigh_shift = _mm_slli_epi32(amulhigh, 0x10);
            v4i amul = _mm_or_si128(amullow, amulhigh_shift);
            v4i anew = _mm_add_epi32(amul, ashift);

            v4i bshift = _mm_srli_epi32(b, 0x10);
            v4i bmask = _mm_and_si128(b, mask);
            v4i bmullow = _mm_mullo_epi16(bmask, m2);
            v4i bmulhigh = _mm_mulhi_epu16(bmask, m2);
            v4i bmulhigh_shift = _mm_slli_epi32(bmulhigh, 0x10);
            v4i bmul = _mm_or_si128(bmullow, bmulhigh_shift);
            v4i bnew = _mm_add_epi32(bmul, bshift);

        #else

            v4i amask = _mm_and_si128(a, mask);
            v4i ashift = _mm_srli_epi32(a, 0x10);
            v4i amul = _mm_mullo_epi32(amask, m1);
            v4i anew = _mm_add_epi32(amul, ashift);

            v4i bmask = _mm_and_si128(b, mask);
            v4i bshift = _mm_srli_epi32(b, 0x10);
            v4i bmul = _mm_mullo_epi32(bmask, m2);
            v4i bnew = _mm_add_epi32(bmul, bshift);

        #endif

        m_a = anew;
        m_b = bnew;

        v4i bmasknew = _mm_and_si128(bnew, mask);
        v4i ashiftnew = _mm_slli_epi32(anew, 0x10);
        v4i res = _mm_add_epi32(ashiftnew, bmasknew);

        return res;
    }
};

extern sFastRand g_frand;

namespace Rand
{
    const float m1 = 1.0f / 32768.0f;
    const float m2 = 1.0f / 16384.0f;
    const float a2 = 0.5f / 16384.0f - 1.0f;

    PLATFORM_INLINE void Seed(uint32_t seed)
    {
        #if( PLATFORM_RND_MODE == PLATFORM_RND_CRT )

            srand(seed);

        #elif( PLATFORM_RND_MODE == PLATFORM_RND_FAST )

            g_frand.Seed(seed);

        #else

            DEBUG_AssertMsg(false, "HW RND cannot be seeded!");
            PLATFORM_UNUSED(seed);

        #endif
    }

    // NOTE: special

    PLATFORM_INLINE float uf1_from_rawbits(uint32_t rawbits)
    {
        uFloat rnd(rawbits);
        rnd.i >>= 9;
        rnd.i |= uFloat(1.0f).i;
        rnd.f = 2.0f - rnd.f;

        return rnd.f;
    }

    PLATFORM_INLINE float sf1_from_rawbits(uint32_t rawbits)
    {
        uFloat rnd(rawbits);
        uint32_t sign = rnd.i & 0x80000000;
        rnd.i >>= 8;
        rnd.i |= uFloat(1.0f).i;
        rnd.f = 2.0f - rnd.f;
        rnd.i |= sign;

        return rnd.f;
    }

    // NOTE: scalar

    PLATFORM_INLINE uint32_t ui1()
    {
        uint32_t rnd;

        #if( PLATFORM_RND_MODE == PLATFORM_RND_CRT )

            rnd = rand();

        #elif( PLATFORM_RND_MODE == PLATFORM_RND_FAST )

            rnd = g_frand.ui1();

        #else

            int32_t res = _rdrand32_step(&rnd);
            DEBUG_Assert( res == 1 );
            PLATFORM_UNUSED(res);

        #endif

        return rnd;
    }

    PLATFORM_INLINE uint16_t us1()
    {
        uint16_t rnd;

        #if( PLATFORM_RND_MODE == PLATFORM_RND_CRT )

            rnd = (uint16_t)rand();

        #elif( PLATFORM_RND_MODE == PLATFORM_RND_FAST )

            uint32_t t = ui1();
            rnd = uint16_t(t >> 16);

        #else

            int32_t res = _rdrand16_step(&rnd);
            DEBUG_Assert( res == 1 );
            PLATFORM_UNUSED(res);

        #endif

        return rnd;
    }

    PLATFORM_INLINE float uf1()
    {
        #if( PLATFORM_RND_MODE == PLATFORM_RND_CRT )

            return float(rand() + 1) * m1;

        #else

            return uf1_from_rawbits( ui1() );

        #endif
    }

    PLATFORM_INLINE float sf1()
    {
        #if( PLATFORM_RND_MODE == PLATFORM_RND_CRT )

            return rand() * m2 + a2;

        #else

            return sf1_from_rawbits( ui1() );

        #endif
    }

    // NOTE: vector4

    PLATFORM_INLINE v4i ui4()
    {
        v4i r;

        #if( PLATFORM_RND_MODE == PLATFORM_RND_CRT || PLATFORM_RND_MODE == PLATFORM_RND_HW )

            int32_t a = ui1();
            int32_t b = ui1();
            int32_t c = ui1();
            int32_t d = ui1();

            r = _mm_set_epi32(a, b, c, d);

        #else

            r = g_frand.ui4();

        #endif

        return r;
    }

    PLATFORM_INLINE float4 uf4()
    {
        v4i rnd = ui4();
        v4f r;

        #if( PLATFORM_RND_MODE == PLATFORM_RND_CRT )

            rnd = _mm_add_epi32(rnd, _mm_set1_epi32(1));
            r = _mm_cvtepi32_ps(rnd);
            r = _mm_mul_ps(r, _mm_broadcast_ss(&m1));

        #else

            r = _mm_castsi128_ps(_mm_srli_epi32(rnd, 9));
            r = _mm_or_ps(r, c_xmm1111);
            r = _mm_sub_ps(_mm_set1_ps(2.0f), r);

        #endif

        return r;
    }

    PLATFORM_INLINE float4 sf4()
    {
        v4i rnd = ui4();
        v4f r;

        #if( PLATFORM_RND_MODE == PLATFORM_RND_CRT )

            r = _mm_cvtepi32_ps(rnd);
            r = xmm_madd(r, _mm_broadcast_ss(&m2), _mm_broadcast_ss(&a2));

        #else

            v4f sign = _mm_and_ps(_mm_castsi128_ps(rnd), c_xmmSign);
            r = _mm_castsi128_ps(_mm_srli_epi32(rnd, 8));
            r = _mm_or_ps(r, c_xmm1111);
            r = _mm_sub_ps(_mm_set1_ps(2.0f), r);
            r = _mm_or_ps(r, sign);

        #endif

        return r;
    }

    PLATFORM_INLINE float3 uf3()
    {
        return uf4().xmm;
    }

    PLATFORM_INLINE float3 sf3()
    {
        return sf4().xmm;
    }
}

//======================================================================================================================
//                                                      Misc
//======================================================================================================================

template<class T> PLATFORM_INLINE T Sign(const T& x)
{
    return x < T(0) ? T(-1) : T(1);
}

template<> PLATFORM_INLINE float3 Sign(const float3& x)
{
    return xmm_sign(x.xmm);
}

template<> PLATFORM_INLINE float4 Sign(const float4& x)
{
    return xmm_sign(x.xmm);
}

template<> PLATFORM_INLINE double3 Sign(const double3& x)
{
    return ymm_sign(x.ymm);
}

template<> PLATFORM_INLINE double4 Sign(const double4& x)
{
    return ymm_sign(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Abs(const T& x)
{
    return (T)abs(x);
}

template<> PLATFORM_INLINE float Abs(const float& x)
{
    uFloat f(x);
    f.Abs();

    return f.f;
}

template<> PLATFORM_INLINE float3 Abs(const float3& x)
{
    return xmm_abs(x.xmm);
}

template<> PLATFORM_INLINE float4 Abs(const float4& x)
{
    return xmm_abs(x.xmm);
}

template<> PLATFORM_INLINE double3 Abs(const double3& x)
{
    return ymm_abs(x.ymm);
}

template<> PLATFORM_INLINE double4 Abs(const double4& x)
{
    return ymm_abs(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Floor(const T& x)
{
    return (T)floor(x);
}

template<> PLATFORM_INLINE float3 Floor(const float3& x)
{
    return xmm_floor(x.xmm);
}

template<> PLATFORM_INLINE float4 Floor(const float4& x)
{
    return xmm_floor(x.xmm);
}

template<> PLATFORM_INLINE double3 Floor(const double3& x)
{
    return ymm_floor(x.ymm);
}

template<> PLATFORM_INLINE double4 Floor(const double4& x)
{
    return ymm_floor(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Round(const T& x)
{
    return (T)::round(x);
}

template<> PLATFORM_INLINE float3 Round(const float3& x)
{
    return xmm_round(x.xmm);
}

template<> PLATFORM_INLINE float4 Round(const float4& x)
{
    return xmm_round(x.xmm);
}

template<> PLATFORM_INLINE double3 Round(const double3& x)
{
    return ymm_round(x.ymm);
}

template<> PLATFORM_INLINE double4 Round(const double4& x)
{
    return ymm_round(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Fract(const T& x)
{
    return x - Floor(x);
}

template<> PLATFORM_INLINE float3 Fract(const float3& x)
{
    return xmm_fract(x.xmm);
}

template<> PLATFORM_INLINE float4 Fract(const float4& x)
{
    return xmm_fract(x.xmm);
}

template<> PLATFORM_INLINE double3 Fract(const double3& x)
{
    return ymm_fract(x.ymm);
}

template<> PLATFORM_INLINE double4 Fract(const double4& x)
{
    return ymm_fract(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Mod(const T& x, const T& y)
{
    DEBUG_Assert( y != T(0) );

    return (T)fmod(x, y);
}

template<> PLATFORM_INLINE uint32_t Mod(const uint32_t& x, const uint32_t& y)
{
    DEBUG_Assert( y != 0 );

    return x % y;
}

template<> PLATFORM_INLINE int32_t Mod(const int32_t& x, const int32_t& y)
{
    DEBUG_Assert( y != 0 );

    return x % y;
}

template<> PLATFORM_INLINE float3 Mod(const float3& x, const float3& y)
{
    return xmm_mod(x.xmm, y.xmm);
}

template<> PLATFORM_INLINE float4 Mod(const float4& x, const float4& y)
{
    return xmm_mod(x.xmm, y.xmm);
}

template<> PLATFORM_INLINE double3 Mod(const double3& x, const double3& y)
{
    return ymm_mod(x.ymm, y.ymm);
}

template<> PLATFORM_INLINE double4 Mod(const double4& x, const double4& y)
{
    return ymm_mod(x.ymm, y.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Snap(const T& x, const T& step)
{
    DEBUG_Assert( step > T(0) );

    return Round(x / step) * step;
}

template<> PLATFORM_INLINE int32_t Snap(const int32_t& x, const int32_t& step)
{
    DEBUG_Assert( step > 0 );

    return step * ((x + step - 1) / step);
}

template<> PLATFORM_INLINE uint32_t Snap(const uint32_t& x, const uint32_t& step)
{
    DEBUG_Assert( step > 0 );

    return step * ((x + step - 1) / step);
}

template<> PLATFORM_INLINE float3 Snap(const float3& x, const float3& step)
{
    return Round(x / step) * step;
}

template<> PLATFORM_INLINE double3 Snap(const double3& x, const double3& step)
{
    return Round(x / step) * step;
}

//======================================================================================================================

template<eCmp cmp, class T> PLATFORM_INLINE bool All(const T&, const T&)
{
    DEBUG_StaticAssertMsg(false, "All::only vector types supported");

    return false;
}

template<eCmp cmp> PLATFORM_INLINE bool All(const float3& x, const float3& y)
{
    v4f t = _mm_cmp_ps(x.xmm, y.xmm, cmp);

    return xmm_test3_all(t);
}

template<eCmp cmp> PLATFORM_INLINE bool All(const float4& x, const float4& y)
{
    v4f t = _mm_cmp_ps(x.xmm, y.xmm, cmp);

    return xmm_test4_all(t);
}

template<eCmp cmp> PLATFORM_INLINE bool All(const double3& x, const double3& y)
{
    v4d t = _mm256_cmp_pd(x.ymm, y.ymm, cmp);

    return ymm_test3_all(t);
}

template<eCmp cmp> PLATFORM_INLINE bool All(const double4& x, const double4& y)
{
    v4d t = _mm256_cmp_pd(x.ymm, y.ymm, cmp);

    return ymm_test4_all(t);
}

//======================================================================================================================

template<eCmp cmp, class T> PLATFORM_INLINE bool Any(const T&, const T&)
{
    DEBUG_StaticAssertMsg(false, "Any::only vector types supported");

    return false;
}

template<eCmp cmp> PLATFORM_INLINE bool Any(const float3& x, const float3& y)
{
    v4f t = _mm_cmp_ps(x.xmm, y.xmm, cmp);

    return xmm_test3_any(t);
}

template<eCmp cmp> PLATFORM_INLINE bool Any(const float4& x, const float4& y)
{
    v4f t = _mm_cmp_ps(x.xmm, y.xmm, cmp);

    return xmm_test4_any(t);
}

template<eCmp cmp> PLATFORM_INLINE bool Any(const double3& x, const double3& y)
{
    v4d t = _mm256_cmp_pd(x.ymm, y.ymm, cmp);

    return ymm_test3_any(t);
}

template<eCmp cmp> PLATFORM_INLINE bool Any(const double4& x, const double4& y)
{
    v4d t = _mm256_cmp_pd(x.ymm, y.ymm, cmp);

    return ymm_test4_any(t);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Min(const T& x, const T& y)
{
    return x < y ? x : y;
}

template<> PLATFORM_INLINE float3 Min(const float3& x, const float3& y)
{
    return _mm_min_ps(x.xmm, y.xmm);
}

template<> PLATFORM_INLINE float4 Min(const float4& x, const float4& y)
{
    return _mm_min_ps(x.xmm, y.xmm);
}

template<> PLATFORM_INLINE double3 Min(const double3& x, const double3& y)
{
    return _mm256_min_pd(x.ymm, y.ymm);
}

template<> PLATFORM_INLINE double4 Min(const double4& x, const double4& y)
{
    return _mm256_min_pd(x.ymm, y.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Max(const T& x, const T& y)
{
    return x > y ? x : y;
}

template<> PLATFORM_INLINE float3 Max(const float3& x, const float3& y)
{
    return _mm_max_ps(x.xmm, y.xmm);
}

template<> PLATFORM_INLINE float4 Max(const float4& x, const float4& y)
{
    return _mm_max_ps(x.xmm, y.xmm);
}

template<> PLATFORM_INLINE double3 Max(const double3& x, const double3& y)
{
    return _mm256_max_pd(x.ymm, y.ymm);
}

template<> PLATFORM_INLINE double4 Max(const double4& x, const double4& y)
{
    return _mm256_max_pd(x.ymm, y.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Clamp(const T& x, const T& a, const T& b)
{
    return x < a ? a : (x > b ? b : x);
}

template<> PLATFORM_INLINE float3 Clamp(const float3& x, const float3& vMin, const float3& vMax)
{
    return xmm_clamp(x.xmm, vMin.xmm, vMax.xmm);
}

template<> PLATFORM_INLINE float4 Clamp(const float4& x, const float4& vMin, const float4& vMax)
{
    return xmm_clamp(x.xmm, vMin.xmm, vMax.xmm);
}

template<> PLATFORM_INLINE double3 Clamp(const double3& x, const double3& vMin, const double3& vMax)
{
    return ymm_clamp(x.ymm, vMin.ymm, vMax.ymm);
}

template<> PLATFORM_INLINE double4 Clamp(const double4& x, const double4& vMin, const double4& vMax)
{
    return ymm_clamp(x.ymm, vMin.ymm, vMax.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Saturate(const T& x)
{
    return Clamp(x, T(0), T(1));
}

template<> PLATFORM_INLINE float3 Saturate(const float3& x)
{
    return xmm_saturate(x.xmm);
}

template<> PLATFORM_INLINE float4 Saturate(const float4& x)
{
    return xmm_saturate(x.xmm);
}

template<> PLATFORM_INLINE double3 Saturate(const double3& x)
{
    return ymm_saturate(x.ymm);
}

template<> PLATFORM_INLINE double4 Saturate(const double4& x)
{
    return ymm_saturate(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Lerp(const T& a, const T& b, const T& x)
{
    return a + (b - a) * x;
}

template<> PLATFORM_INLINE float3 Lerp(const float3& a, const float3& b, const float3& x)
{
    return xmm_mix(a.xmm, b.xmm, x.xmm);
}

template<> PLATFORM_INLINE float4 Lerp(const float4& a, const float4& b, const float4& x)
{
    return xmm_mix(a.xmm, b.xmm, x.xmm);
}

template<> PLATFORM_INLINE double3 Lerp(const double3& a, const double3& b, const double3& x)
{
    return ymm_mix(a.ymm, b.ymm, x.ymm);
}

template<> PLATFORM_INLINE double4 Lerp(const double4& a, const double4& b, const double4& x)
{
    return ymm_mix(a.ymm, b.ymm, x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Smoothstep(const T& a, const T& b, const T& x)
{
    T t = Saturate((x - a) / (b - a));

    return t * t * (T(3) - T(2) * t);
}

template<> PLATFORM_INLINE float3 Smoothstep(const float3& a, const float3& b, const float3& x)
{
    return xmm_smoothstep(a.xmm, b.xmm, x.xmm);
}

template<> PLATFORM_INLINE float4 Smoothstep(const float4& a, const float4& b, const float4& x)
{
    return xmm_smoothstep(a.xmm, b.xmm, x.xmm);
}

template<> PLATFORM_INLINE double3 Smoothstep(const double3& a, const double3& b, const double3& x)
{
    return ymm_smoothstep(a.ymm, b.ymm, x.ymm);
}

template<> PLATFORM_INLINE double4 Smoothstep(const double4& a, const double4& b, const double4& x)
{
    return ymm_smoothstep(a.ymm, b.ymm, x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Linearstep(const T& a, const T& b, const T& x)
{
    return Saturate((x - a) / (b - a));
}

template<> PLATFORM_INLINE float3 Linearstep(const float3& a, const float3& b, const float3& x)
{
    return xmm_linearstep(a.xmm, b.xmm, x.xmm);
}

template<> PLATFORM_INLINE float4 Linearstep(const float4& a, const float4& b, const float4& x)
{
    return xmm_linearstep(a.xmm, b.xmm, x.xmm);
}

template<> PLATFORM_INLINE double3 Linearstep(const double3& a, const double3& b, const double3& x)
{
    return ymm_linearstep(a.ymm, b.ymm, x.ymm);
}

template<> PLATFORM_INLINE double4 Linearstep(const double4& a, const double4& b, const double4& x)
{
    return ymm_linearstep(a.ymm, b.ymm, x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Step(const T& edge, const T& x)
{
    return x < edge ? T(0) : T(1);
}

template<> PLATFORM_INLINE float3 Step(const float3& edge, const float3& x)
{
    return xmm_step(edge.xmm, x.xmm);
}

template<> PLATFORM_INLINE float4 Step(const float4& edge, const float4& x)
{
    return xmm_step(edge.xmm, x.xmm);
}

template<> PLATFORM_INLINE double3 Step(const double3& edge, const double3& x)
{
    return ymm_step(edge.ymm, x.ymm);
}

template<> PLATFORM_INLINE double4 Step(const double4& edge, const double4& x)
{
    return ymm_step(edge.ymm, x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Sin(const T& x)
{
    return (T)sin(x);
}

template<> PLATFORM_INLINE float3 Sin(const float3& x)
{
    return _mm_sin_ps(x.xmm);
}

template<> PLATFORM_INLINE float4 Sin(const float4& x)
{
    return _mm_sin_ps(x.xmm);
}

template<> PLATFORM_INLINE double3 Sin(const double3& x)
{
    return _mm256_sin_pd(x.ymm);
}

template<> PLATFORM_INLINE double4 Sin(const double4& x)
{
    return _mm256_sin_pd(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Cos(const T& x)
{
    return (T)cos(x);
}

template<> PLATFORM_INLINE float3 Cos(const float3& x)
{
    return _mm_cos_ps(x.xmm);
}

template<> PLATFORM_INLINE float4 Cos(const float4& x)
{
    return _mm_cos_ps(x.xmm);
}

template<> PLATFORM_INLINE double3 Cos(const double3& x)
{
    return _mm256_cos_pd(x.ymm);
}

template<> PLATFORM_INLINE double4 Cos(const double4& x)
{
    return _mm256_cos_pd(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T SinCos(const T& x, T* pCos)
{
    *pCos = (T)cos(x);

    return (T)sin(x);
}

template<> PLATFORM_INLINE float3 SinCos(const float3& x, float3* pCos)
{
    return _mm_sincos_ps(&pCos->xmm, x.xmm);
}

template<> PLATFORM_INLINE float4 SinCos(const float4& x, float4* pCos)
{
    return _mm_sincos_ps(&pCos->xmm, x.xmm);
}

template<> PLATFORM_INLINE double3 SinCos(const double3& x, double3* pCos)
{
    return _mm256_sincos_pd(&pCos->ymm, x.ymm);
}

template<> PLATFORM_INLINE double4 SinCos(const double4& x, double4* pCos)
{
    return _mm256_sincos_pd(&pCos->ymm, x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Tan(const T& x)
{
    return (T)tan(x);
}

template<> PLATFORM_INLINE float3 Tan(const float3& x)
{
    return _mm_tan_ps(x.xmm);
}

template<> PLATFORM_INLINE float4 Tan(const float4& x)
{
    return _mm_tan_ps(x.xmm);
}

template<> PLATFORM_INLINE double3 Tan(const double3& x)
{
    return _mm256_tan_pd(x.ymm);
}

template<> PLATFORM_INLINE double4 Tan(const double4& x)
{
    return _mm256_tan_pd(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Asin(const T& x)
{
    DEBUG_Assert( x >= T(-1) && x <= T(1) );

    return (T)asin(x);
}

template<> PLATFORM_INLINE float3 Asin(const float3& x)
{
    DEBUG_Assert( All<CmpGequal>(x, float3(-1.0f)) && All<CmpLequal>(x, float3(1.0f)) );

    return _mm_asin_ps(x.xmm);
}

template<> PLATFORM_INLINE float4 Asin(const float4& x)
{
    DEBUG_Assert( All<CmpGequal>(x, float4(-1.0f)) && All<CmpLequal>(x, float4(1.0f)) );

    return _mm_asin_ps(x.xmm);
}

template<> PLATFORM_INLINE double3 Asin(const double3& x)
{
    DEBUG_Assert( All<CmpGequal>(x, double3(-1.0)) && All<CmpLequal>(x, double3(1.0)) );

    return _mm256_asin_pd(x.ymm);
}

template<> PLATFORM_INLINE double4 Asin(const double4& x)
{
    DEBUG_Assert( All<CmpGequal>(x, double4(-1.0)) && All<CmpLequal>(x, double4(1.0)) );

    return _mm256_asin_pd(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Acos(const T& x)
{
    DEBUG_Assert( x >= T(-1) && x <= T(1) );

    return (T)acos(x);
}

template<> PLATFORM_INLINE float3 Acos(const float3& x)
{
    DEBUG_Assert( All<CmpGequal>(x, float3(-1.0f)) && All<CmpLequal>(x, float3(1.0f)) );

    return _mm_acos_ps(x.xmm);
}

template<> PLATFORM_INLINE float4 Acos(const float4& x)
{
    DEBUG_Assert( All<CmpGequal>(x, float4(-1.0f)) && All<CmpLequal>(x, float4(1.0f)) );

    return _mm_acos_ps(x.xmm);
}

template<> PLATFORM_INLINE double3 Acos(const double3& x)
{
    DEBUG_Assert( All<CmpGequal>(x, double3(-1.0)) && All<CmpLequal>(x, double3(1.0)) );

    return _mm256_acos_pd(x.ymm);
}

template<> PLATFORM_INLINE double4 Acos(const double4& x)
{
    DEBUG_Assert( All<CmpGequal>(x, double4(-1.0)) && All<CmpLequal>(x, double4(1.0)) );

    return _mm256_acos_pd(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Atan(const T& x)
{
    return (T)atan(x);
}

template<> PLATFORM_INLINE float3 Atan(const float3& x)
{
    return _mm_atan_ps(x.xmm);
}

template<> PLATFORM_INLINE float4 Atan(const float4& x)
{
    return _mm_atan_ps(x.xmm);
}

template<> PLATFORM_INLINE double3 Atan(const double3& x)
{
    return _mm256_atan_pd(x.ymm);
}

template<> PLATFORM_INLINE double4 Atan(const double4& x)
{
    return _mm256_atan_pd(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Atan(const T& y, const T& x)
{
    return (T)atan2(y, x);
}

template<> PLATFORM_INLINE float3 Atan(const float3& y, const float3& x)
{
    return _mm_atan2_ps(y.xmm, x.xmm);
}

template<> PLATFORM_INLINE float4 Atan(const float4& y, const float4& x)
{
    return _mm_atan2_ps(y.xmm, x.xmm);
}

template<> PLATFORM_INLINE double3 Atan(const double3& y, const double3& x)
{
    return _mm256_atan2_pd(y.ymm, x.ymm);
}

template<> PLATFORM_INLINE double4 Atan(const double4& y, const double4& x)
{
    return _mm256_atan2_pd(y.ymm, x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Sqrt(const T& x)
{
    DEBUG_Assert( x >= T(0) );

    return (T)sqrt(x);
}

template<> PLATFORM_INLINE float3 Sqrt(const float3& x)
{
    return xmm_sqrt(x.xmm);
}

template<> PLATFORM_INLINE float4 Sqrt(const float4& x)
{
    return xmm_sqrt(x.xmm);
}

template<> PLATFORM_INLINE double3 Sqrt(const double3& x)
{
    return ymm_sqrt(x.ymm);
}

template<> PLATFORM_INLINE double4 Sqrt(const double4& x)
{
    return ymm_sqrt(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Rsqrt(const T& x)
{
    DEBUG_Assert( x >= T(0) );

    return T(1) / T(sqrt(x));
}

template<> PLATFORM_INLINE float3 Rsqrt(const float3& x)
{
    return xmm_rsqrt(x.xmm);
}

template<> PLATFORM_INLINE float4 Rsqrt(const float4& x)
{
    return xmm_rsqrt(x.xmm);
}

template<> PLATFORM_INLINE double3 Rsqrt(const double3& x)
{
    return ymm_rsqrt(x.ymm);
}

template<> PLATFORM_INLINE double4 Rsqrt(const double4& x)
{
    return ymm_rsqrt(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Pow(const T& x, const T& y)
{
    DEBUG_Assert( x >= T(0) );

    return (T)pow(x, y);
}

template<> PLATFORM_INLINE float3 Pow(const float3& x, const float3& y)
{
    return _mm_pow_ps(x.xmm, y.xmm);
}

template<> PLATFORM_INLINE float4 Pow(const float4& x, const float4& y)
{
    return _mm_pow_ps(x.xmm, y.xmm);
}

template<> PLATFORM_INLINE double3 Pow(const double3& x, const double3& y)
{
    return _mm256_pow_pd(x.ymm, y.ymm);
}

template<> PLATFORM_INLINE double4 Pow(const double4& x, const double4& y)
{
    return _mm256_pow_pd(x.ymm, y.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Log(const T& x)
{
    DEBUG_Assert( x >= T(0) );

    return (T)log(x);
}

template<> PLATFORM_INLINE float3 Log(const float3& x)
{
    return _mm_log_ps(x.xmm);
}

template<> PLATFORM_INLINE float4 Log(const float4& x)
{
    return _mm_log_ps(x.xmm);
}

template<> PLATFORM_INLINE double3 Log(const double3& x)
{
    return _mm256_log_pd(x.ymm);
}

template<> PLATFORM_INLINE double4 Log(const double4& x)
{
    return _mm256_log_pd(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Exp(const T& x)
{
    return (T)exp(x);
}

template<> PLATFORM_INLINE float3 Exp(const float3& x)
{
    return _mm_exp_ps(x.xmm);
}

template<> PLATFORM_INLINE float4 Exp(const float4& x)
{
    return _mm_exp_ps(x.xmm);
}

template<> PLATFORM_INLINE double3 Exp(const double3& x)
{
    return _mm256_exp_pd(x.ymm);
}

template<> PLATFORM_INLINE double4 Exp(const double4& x)
{
    return _mm256_exp_pd(x.ymm);
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T Pi(const T& mul)
{
    DEBUG_StaticAssertMsg(false, "Pi::only floating point types are supported!");
    return mul;
}

template<class T> PLATFORM_INLINE T RadToDeg(const T& a)
{
    DEBUG_StaticAssertMsg(false, "RadToDeg::only floating point types are supported!");
    return a;
}

template<class T> PLATFORM_INLINE T DegToRad(const T& a)
{
    DEBUG_StaticAssertMsg(false, "DegToRad::only floating point types are supported!");
    return a;
}

template<> PLATFORM_INLINE float Pi(const float& mul)               { return mul * acosf(-1.0f); }
template<> PLATFORM_INLINE float3 Pi(const float3& mul)             { return mul * acosf(-1.0f); }
template<> PLATFORM_INLINE float4 Pi(const float4& mul)             { return mul * acosf(-1.0f); }
template<> PLATFORM_INLINE double Pi(const double& mul)             { return mul * acos(-1.0); }
template<> PLATFORM_INLINE double3 Pi(const double3& mul)           { return mul * acos(-1.0); }
template<> PLATFORM_INLINE double4 Pi(const double4& mul)           { return mul * acos(-1.0); }

template<> PLATFORM_INLINE float RadToDeg(const float& x)           { return x * (180.0f / Pi(1.0f)); }
template<> PLATFORM_INLINE float3 RadToDeg(const float3& x)         { return x * (180.0f / Pi(1.0f)); }
template<> PLATFORM_INLINE float4 RadToDeg(const float4& x)         { return x * (180.0f / Pi(1.0f)); }
template<> PLATFORM_INLINE double RadToDeg(const double& x)         { return x * (180.0 / Pi(1.0)); }
template<> PLATFORM_INLINE double3 RadToDeg(const double3& x)       { return x * (180.0 / Pi(1.0)); }
template<> PLATFORM_INLINE double4 RadToDeg(const double4& x)       { return x * (180.0 / Pi(1.0)); }

template<> PLATFORM_INLINE float DegToRad(const float& x)           { return x * (Pi(1.0f) / 180.0f); }
template<> PLATFORM_INLINE float3 DegToRad(const float3& x)         { return x * (Pi(1.0f) / 180.0f); }
template<> PLATFORM_INLINE float4 DegToRad(const float4& x)         { return x * (Pi(1.0f) / 180.0f); }
template<> PLATFORM_INLINE double DegToRad(const double& x)         { return x * (Pi(1.0) / 180.0); }
template<> PLATFORM_INLINE double3 DegToRad(const double3& x)       { return x * (Pi(1.0) / 180.0); }
template<> PLATFORM_INLINE double4 DegToRad(const double4& x)       { return x * (Pi(1.0) / 180.0); }

//======================================================================================================================

template<class T> PLATFORM_INLINE void Swap(T& x, T& y)
{
    T t = x;
    x = y;
    y = t;
}

template<> PLATFORM_INLINE void Swap(uint32_t& x, uint32_t& y)
{
    x ^= y;
    y ^= x;
    x ^= y;
}

template<class T> PLATFORM_INLINE void Swap(T* x, T* y)
{
    // NOTE: just swap memory, skip constructor/destructor...

    const uint32_t N = sizeof(T);
    uint8_t temp[N];

    memcpy(temp, x, N);
    memcpy(x, y, N);
    memcpy(y, temp, N);
}

//======================================================================================================================

// TODO: add "class Quaternion : public float4", add "double" version...
template<> PLATFORM_INLINE float4 Slerp(const float4& a, const float4& b, float x)
{
    DEBUG_Assert( x >= 0.0f && x <= 1.0f );
    DEBUG_Assert( Abs( Dot44(a, a) - 1.0f ) < 1e-6f );
    DEBUG_Assert( Abs( Dot44(b, b) - 1.0f ) < 1e-6f );

    float4 r;

    float theta = Dot44(a, b);
    if( theta > 0.9995f )
        r = Lerp(a, b, float4(x));
    else
    {
        theta = Acos(theta);

        float k = 1.0f - x;
        float3 s = Sin( float3(theta, k * theta, x * theta) );
        float sn = 1.0f / s.x;
        float wa = s.y * sn;
        float wb = s.z * sn;
        r = a * wa + b * wb;
    }

    r *= Rsqrt( Dot44(r, r) );

    return r;
}

//======================================================================================================================

template<class T> PLATFORM_INLINE T CurveSmooth(const T& x)
{
    return x * x * (3.0 - 2.0 * x);
}

template<class T> PLATFORM_INLINE T CurveSin(const T& x)
{
    return x * (1.0 - x * x / 3.0);
}

template<class T> PLATFORM_INLINE T WaveTriangle(const T& x)
{
    return Abs( Fract( x + T(0.5) ) * T(2.0) - T(1.0) );
}

template<class T> PLATFORM_INLINE T WaveTriangleSmooth(const T& x)
{
    return CurveSmooth( WaveTriangle(x) );
}

//======================================================================================================================

PLATFORM_INLINE float4 AsFloat(const uint4& x)
{
    return _mm_castsi128_ps(x.xmm);
}

PLATFORM_INLINE float AsFloat(uint32_t x)
{
    return *(float*)&x;
}

PLATFORM_INLINE uint4 AsUint(const float4& x)
{
    return _mm_castps_si128(x.xmm);
}

PLATFORM_INLINE uint32_t AsUint(float x)
{
    return *(uint32_t*)&x;
}

//======================================================================================================================
//                                                      ctRect
//======================================================================================================================

template<class T> class ctRect
{
    public:

        union
        {
            struct
            {
                T vMin[COORD_2D];
            };

            struct
            {
                T minx;
                T miny;
            };
        };

        union
        {
            struct
            {
                T vMax[COORD_2D];
            };

            struct
            {
                T maxx;
                T maxy;
            };
        };

    public:

        PLATFORM_INLINE ctRect()
        {
            Clear();
        }

        PLATFORM_INLINE void Clear()
        {
            minx = miny = T(1 << 30);
            maxx = maxy = T(-(1 << 30));
        }

        PLATFORM_INLINE bool IsValid() const
        {
            return maxx > minx && maxy > miny;
        }

        PLATFORM_INLINE void Add(T px, T py)
        {
            minx = Min(minx, px);
            maxx = Max(maxx, px);
            miny = Min(miny, py);
            maxy = Max(maxy, py);
        }

        PLATFORM_INLINE void Add(const T* pPoint2)
        {
            Add(pPoint2[0], pPoint2[1]);
        }

        PLATFORM_INLINE bool IsIntersectWith(const T* pMin, const T* pMax) const
        {
            DEBUG_Assert( IsValid() );

            if( maxx < pMin[0] || maxy < pMin[1] || minx > pMax[0] || miny > pMax[1] )
                return false;

            return true;
        }

        PLATFORM_INLINE bool IsIntersectWith(const ctRect<T>& rRect) const
        {
            return IsIntersectWith(rRect.vMin, rRect.vMax);
        }

        PLATFORM_INLINE eClip GetIntersectionStateWith(const T* pMin, const T* pMax) const
        {
            DEBUG_Assert( IsValid() );

            if( !IsIntersectWith(pMin, pMax) )
                return CLIP_OUT;

            if( minx < pMin[0] && maxx > pMax[0] && miny < pMin[1] && maxy > pMax[1] )
                return CLIP_IN;

            return CLIP_PARTIAL;
        }

        PLATFORM_INLINE eClip GetIntersectionStateWith(const ctRect<T>& rRect) const
        {
            return GetIntersectionStateWith(rRect.vMin, rRect.vMax);
        }
};

//======================================================================================================================
//                                                      Frustum
//======================================================================================================================

class cFrustum
{
    private:

        float4 m_vPlane[PLANES_NUM];
        float4x4 m_mPlanesT;
        v4f m_vMask[PLANES_NUM];

    public:

        PLATFORM_INLINE cFrustum()
        {
        }

        void Setup(uint8_t ucNdcDepthRange, const float4x4& mMvp);
        void Translate(const float3& vPos);

        bool CheckSphere(const float3& center, float fRadius, uint32_t planes = PLANES_NUM) const;
        bool CheckAabb(const float3& minv, const float3& maxv, uint32_t planes = PLANES_NUM) const;
        bool CheckCapsule(const float3& capsule_start, const float3& capsule_axis, float capsule_radius, uint32_t planes = PLANES_NUM) const;

        bool CheckSphere_mask(const float3& center, float fRadius, uint32_t mask, uint32_t planes = PLANES_NUM) const;
        bool CheckAabb_mask(const float3& minv, const float3& maxv, uint32_t mask, uint32_t planes = PLANES_NUM) const;

        eClip CheckSphere_state(const float3& center, float fRadius, uint32_t planes = PLANES_NUM) const;
        eClip CheckAabb_state(const float3& minv, const float3& maxv, uint32_t planes = PLANES_NUM) const;
        eClip CheckCapsule_state(const float3& capsule_start, const float3& capsule_axis, float capsule_radius, uint32_t planes = PLANES_NUM) const;

        eClip CheckSphere_mask_state(const float3& center, float fRadius, uint32_t& mask, uint32_t planes = PLANES_NUM) const;
        eClip CheckAabb_mask_state(const float3& minv, const float3& maxv, uint32_t& mask, uint32_t planes = PLANES_NUM) const;

        PLATFORM_INLINE void SetNearFar(float zNearNeg, float zFarNeg)
        {
            m_vPlane[PLANE_NEAR].w = zNearNeg;
            m_vPlane[PLANE_FAR].w = -zFarNeg;
        }

        PLATFORM_INLINE void SetFar(float zFarNeg)
        {
            m_vPlane[PLANE_FAR].w = -zFarNeg;
        }

        PLATFORM_INLINE const float4& GetPlane(uint32_t plane)
        {
            DEBUG_Assert( plane < PLANES_NUM );

            return m_vPlane[plane];
        }
};

//======================================================================================================================
//                                                      NUMERICAL
//======================================================================================================================

PLATFORM_INLINE float SplitZ_Logarithmic(uint32_t i, uint32_t splits, float fZnear, float fZfar)
{
    float ratio = fZfar / fZnear;
    float k = float(i) / float(splits);
    float z = fZnear * Pow(ratio, k);

    return z;
}

PLATFORM_INLINE float SplitZ_Uniform(uint32_t i, uint32_t splits, float fZnear, float fZfar)
{
    float delta = fZfar - fZnear;
    float k = float(i) / float(splits);
    float z = fZnear + delta * k;

    return z;
}

PLATFORM_INLINE float SplitZ_Mixed(uint32_t i, uint32_t splits, float fZnear, float fZfar, float lambda)
{
    float z_log = SplitZ_Logarithmic(i, splits, fZnear, fZfar);
    float z_uni = SplitZ_Uniform(i, splits, fZnear, fZfar);
    float z = Lerp(z_log, z_uni, lambda);

    return z;
}

uint32_t greatest_common_divisor(uint32_t a, uint32_t b);

PLATFORM_INLINE uint32_t least_common_multiple(uint32_t a, uint32_t b)
{
    return (a * b) / greatest_common_divisor(a, b);
}

// Bit operations
PLATFORM_INLINE uint32_t ReverseBits4(uint32_t x)
{
    x = (( x & 0x5 ) << 1) | (( x & 0xA ) >> 1);
    x = (( x & 0x3 ) << 2) | (( x & 0xC ) >> 2);

    return x;
}

PLATFORM_INLINE uint32_t ReverseBits8(uint32_t x)
{
    x = (( x & 0x55 ) << 1) | (( x & 0xAA ) >> 1);
    x = (( x & 0x33 ) << 2) | (( x & 0xCC ) >> 2);
    x = (( x & 0x0F ) << 4) | (( x & 0xF0 ) >> 4);

    return x;
}

PLATFORM_INLINE uint32_t ReverseBits16(uint32_t x)
{
    x = (( x & 0x5555 ) << 1) | (( x & 0xAAAA ) >> 1);
    x = (( x & 0x3333 ) << 2) | (( x & 0xCCCC ) >> 2);
    x = (( x & 0x0F0F ) << 4) | (( x & 0xF0F0 ) >> 4);
    x = (( x & 0x00FF ) << 8) | (( x & 0xFF00 ) >> 8);

    return x;
}

PLATFORM_INLINE uint32_t ReverseBits32(uint32_t x)
{
    x = (x << 16) | (x >> 16);
    x = (( x & 0x55555555 ) << 1) | (( x & 0xAAAAAAAA ) >> 1);
    x = (( x & 0x33333333 ) << 2) | (( x & 0xCCCCCCCC ) >> 2);
    x = (( x & 0x0F0F0F0F ) << 4) | (( x & 0xF0F0F0F0 ) >> 4);
    x = (( x & 0x00FF00FF ) << 8) | (( x & 0xFF00FF00 ) >> 8);

    return x;
}

PLATFORM_INLINE uint32_t CompactBits(uint32_t x)
{
    x &= 0x55555555;
    x = (x ^ ( x >> 1 )) & 0x33333333;
    x = (x ^ ( x >> 2 )) & 0x0F0F0F0F;
    x = (x ^ ( x >> 4 )) & 0x00FF00FF;
    x = (x ^ ( x >> 8 )) & 0x0000FFFF;

    return x;
}

PLATFORM_INLINE uint32_t Bayer4x4(uint2 samplePos, uint32_t tickIndex)
{
    uint32_t x = samplePos.x & 3;
    uint32_t y = samplePos.y & 3;

    uint32_t a = 2068378560 * (1 - (x >> 1)) + 1500172770 * (x >> 1);
    uint32_t b = (y + ( (x & 1) << 2) ) << 2;

    uint32_t sampleOffset = ReverseBits4(tickIndex);

    return ( (a >> b) + sampleOffset ) & 0xF;
}

PLATFORM_INLINE float RadicalInverse( uint32_t idx, uint32_t base )
{
    float val = 0.0f;
    float rcpBase = 1.0f / ( float ) base;
    float rcpBi = rcpBase;
    while ( idx > 0 ) {
        uint32_t d_i = idx % base;
        val += float( d_i  ) * rcpBi;
        idx = uint32_t( idx * rcpBase );
        rcpBi *= rcpBase;
    }
    return val;
}

PLATFORM_INLINE float2 Halton2D( uint32_t idx )
{
    return float2( RadicalInverse( idx + 1, 3 ), ReverseBits32( idx + 1 ) * 2.3283064365386963e-10f );
}

//======================================================================================================================
//                                                      Misc
//======================================================================================================================

void DecomposeProjection(uint8_t ucNdcOrigin, uint8_t ucNdcDepth, const float4x4& proj, uint32_t* puiFlags, float* pfSettings15, float* pfUnproject2, float* pfFrustum4, float* pfProject3, float* pfSafeNearZ);

// NOTE: 1D cubic interpolator, assumes x > 0.0, constraint parameter = -1
float CubicFilter(float x, float i1, float i2, float i3, float v4i);

void Hammersley(float* pXyz, uint32_t n);

PLATFORM_INLINE float2 Hammersley(uint32_t i, uint32_t uiSamples, uint32_t rnd0 = 0, uint32_t rnd1 = 0)
{
    float E1 = Fract( float(i) / float(uiSamples) + float(rnd0 & 0xffff) / float(1 << 16) );
    float E2 = float( ReverseBits32(i) ^ rnd1 ) * 2.3283064365386963e-10f;

    return float2(E1, E2);
}

// NOTE: overlapping axis-aligned boundary box and triangle (center - aabb center, extents - half size)
bool IsOverlapBoxTriangle(const float3& center, const float3& extents, const float3& tri0, const float3& tri1, const float3& tri2);

// NOTE: barycentric ray-triangle test by Tomas Akenine-Moller
bool IsIntersectRayTriangle(const float3& origin, const float3& dir, const float3& v1, const float3& v2, const float3& v3, float3& out_tuv);
bool IsIntersectRayTriangle(const float3& from, const float3& to, const float3& v1, const float3& v2, const float3& v3, float3& out_intersection, float3& out_normal);

#include "Packed.h"

#pragma warning(default:4201)

#ifdef MATH_NAMESPACE
}
#endif

//======================================================================================================================
//                                                      Sorting
//======================================================================================================================

// NOTE: returns p1 < p2 ? -1 : (p1 > p2 ? 1 : 0)

typedef int32_t (*pfn_cmp_qsort)(const void* p1, const void* p2);

// NOTE: true - swap, false - keep; a - left, b - right

template<class T> inline bool Sort_default_less(const T& a, const T& b)
{
    return a < b;
}

template<class T> inline bool Sort_default_greater(const T& a, const T& b)
{
    return a > b;
}

/*
bool Sort_multikey(const T& a, const T& b)
{
    if( a.property1 > b.property1 )
        return true;

    if( a.property1 == b.property1 )
    {
        if( a.property2 > b.property2 )
            return true;

        if( a.property2 == b.property2 )
            return a.property3 > b.property3;
    }

    return false;
}
*/

// NOTE: heap sort
// memory:      O(1)
// random:      +40% vs qsort
// sorted:      -30% vs qsort
// reversed:    -30% vs qsort

template<class T, bool (*cmp)(const T& a, const T& b)> void Sort_heap(T* a, uint32_t n)
{
    if( n < 2 )
        return;

    uint32_t i = n >> 1;

    for(;;)
    {
        T t;

        if( i > 0 )
            t = a[--i];
        else
        {
            if( --n == 0 )
                return;

            t = a[n];
            a[n] = a[0];
        }

        uint32_t parent = i;
        uint32_t child = (i << 1) + 1;

        while( child < n )
        {
            if( child + 1 < n && cmp(a[child], a[child + 1]) )
                child++;

            if( cmp(t, a[child]) )
            {
                a[parent] = a[child];

                parent = child;
                child = (parent << 1) + 1;
            }
            else
                break;
        }

        a[parent] = t;
    }
}

// NOTE: merge sort
// memory:      O(n), t - temp array, return pointer to sorted array (can be a or t)
// random:      +130% vs qsort
// sorted:      +35% vs qsort
// reversed:    +40% vs qsort

template<class T, bool (*cmp)(const T& a, const T& b)> T* Sort_merge(T* t, T* a, uint32_t n)
{
    if( n < 2 )
        return a;

    uint32_t n2 = n << 1;

    for( uint32_t size = 2; size < n2; size <<= 1 )
    {
        T* tmp = t;

        for( uint32_t i = 0; i < n; i += size )
        {
            uint32_t j = i;
            uint32_t nj = i + (size >> 1);

            if( nj > n )
                nj = n;

            uint32_t k = nj;
            uint32_t nk = i + size;

            if( nk > n )
                nk = n;

            while( j < nj && k < nk )
                *tmp++ = cmp(a[j], a[k]) ? a[j++] : a[k++];

            nj -= j;
            nk -= k;

            if( nj )
            {
                memcpy(tmp, a + j, nj * sizeof(T));
                tmp += nj;
            }

            if( nk )
            {
                memcpy(tmp, a + k, nk * sizeof(T));
                tmp += nk;
            }
        }

        tmp = a;
        a = t;
        t = tmp;
    }

    return a;
}
