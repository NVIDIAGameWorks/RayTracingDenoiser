#pragma once

// Provide feedback to: maxim.locktyukhin intel com, phil.j.kerly intel com
// - problem with all immediate values for _mm[256]_cmp_[ps|pd]

typedef __m64 v2i;
typedef __m128d v2d;

typedef __m128 v4f;
typedef __m128i v4i;

//======================================================================================================================
//                                                      SSE4
//======================================================================================================================

#undef _MM_FROUND_TO_NEAREST_INT
#undef _MM_FROUND_TO_NEG_INF
#undef _MM_FROUND_TO_POS_INF
#undef _MM_FROUND_TO_ZERO
#undef _MM_FROUND_CUR_DIRECTION
#undef _MM_FROUND_NO_EXC
#undef _MM_ROUND_NEAREST
#undef _MM_ROUND_DOWN
#undef _MM_ROUND_UP
#undef _MM_ROUND_TOWARD_ZERO
#undef _MM_FROUND_RAISE_EXC
#undef _MM_FROUND_NINT
#undef _MM_FROUND_FLOOR
#undef _MM_FROUND_CEIL
#undef _MM_FROUND_TRUNC
#undef _MM_FROUND_RINT
#undef _MM_FROUND_NEARBYINT
#undef _CMP_EQ_OQ
#undef _CMP_LT_OS
#undef _CMP_LE_OS
#undef _CMP_UNORD_Q
#undef _CMP_NEQ_UQ
#undef _CMP_NLT_US
#undef _CMP_NLE_US
#undef _CMP_ORD_Q
#undef _CMP_EQ_UQ
#undef _CMP_NGE_US
#undef _CMP_NGT_US
#undef _CMP_FALSE_OQ
#undef _CMP_NEQ_OQ
#undef _CMP_GE_OS
#undef _CMP_GT_OS
#undef _CMP_TRUE_UQ
#undef _CMP_EQ_OS
#undef _CMP_LT_OQ
#undef _CMP_LE_OQ
#undef _CMP_UNORD_S
#undef _CMP_NEQ_US
#undef _CMP_NLT_UQ
#undef _CMP_NLE_UQ
#undef _CMP_ORD_S
#undef _CMP_EQ_US
#undef _CMP_NGE_UQ
#undef _CMP_NGT_UQ
#undef _CMP_FALSE_OS
#undef _CMP_NEQ_OS
#undef _CMP_GE_OQ
#undef _CMP_GT_OQ
#undef _CMP_TRUE_US

#define _MM_FROUND_TO_NEAREST_INT 0x00
#define _MM_FROUND_TO_NEG_INF 0x01
#define _MM_FROUND_TO_POS_INF 0x02
#define _MM_FROUND_TO_ZERO 0x03
#define _MM_FROUND_CUR_DIRECTION 0x04
#define _MM_FROUND_NO_EXC 0x08
#define _MM_ROUND_NEAREST 0x0000
#define _MM_ROUND_DOWN 0x2000
#define _MM_ROUND_UP 0x4000
#define _MM_ROUND_TOWARD_ZERO 0x6000

#define _MM_FROUND_RAISE_EXC 0x00
#define _MM_FROUND_NINT      _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_RAISE_EXC
#define _MM_FROUND_FLOOR     _MM_FROUND_TO_NEG_INF     | _MM_FROUND_RAISE_EXC
#define _MM_FROUND_CEIL      _MM_FROUND_TO_POS_INF     | _MM_FROUND_RAISE_EXC
#define _MM_FROUND_TRUNC     _MM_FROUND_TO_ZERO        | _MM_FROUND_RAISE_EXC
#define _MM_FROUND_RINT      _MM_FROUND_CUR_DIRECTION  | _MM_FROUND_RAISE_EXC
#define _MM_FROUND_NEARBYINT _MM_FROUND_CUR_DIRECTION  | _MM_FROUND_NO_EXC

#define _CMP_EQ_OQ    0x00 /* Equal (ordered, non-signaling)  */
#define _CMP_LT_OS    0x01 /* Less-than (ordered, signaling)  */
#define _CMP_LE_OS    0x02 /* Less-than-or-equal (ordered, signaling)  */
#define _CMP_UNORD_Q  0x03 /* Unordered (non-signaling)  */
#define _CMP_NEQ_UQ   0x04 /* Not-equal (unordered, non-signaling)  */
#define _CMP_NLT_US   0x05 /* Not-less-than (unordered, signaling)  */
#define _CMP_NLE_US   0x06 /* Not-less-than-or-equal (unordered, signaling)  */
#define _CMP_ORD_Q    0x07 /* Ordered (non-signaling)   */
#define _CMP_EQ_UQ    0x08 /* Equal (unordered, non-signaling)  */
#define _CMP_NGE_US   0x09 /* Not-greater-than-or-equal (unordered, signaling)  */
#define _CMP_NGT_US   0x0a /* Not-greater-than (unordered, signaling)  */
#define _CMP_FALSE_OQ 0x0b /* False (ordered, non-signaling)  */
#define _CMP_NEQ_OQ   0x0c /* Not-equal (ordered, non-signaling)  */
#define _CMP_GE_OS    0x0d /* Greater-than-or-equal (ordered, signaling)  */
#define _CMP_GT_OS    0x0e /* Greater-than (ordered, signaling)  */
#define _CMP_TRUE_UQ  0x0f /* True (unordered, non-signaling)  */
#define _CMP_EQ_OS    0x10 /* Equal (ordered, signaling)  */
#define _CMP_LT_OQ    0x11 /* Less-than (ordered, non-signaling)  */
#define _CMP_LE_OQ    0x12 /* Less-than-or-equal (ordered, non-signaling)  */
#define _CMP_UNORD_S  0x13 /* Unordered (signaling)  */
#define _CMP_NEQ_US   0x14 /* Not-equal (unordered, signaling)  */
#define _CMP_NLT_UQ   0x15 /* Not-less-than (unordered, non-signaling)  */
#define _CMP_NLE_UQ   0x16 /* Not-less-than-or-equal (unordered, non-signaling)  */
#define _CMP_ORD_S    0x17 /* Ordered (signaling)  */
#define _CMP_EQ_US    0x18 /* Equal (unordered, signaling)  */
#define _CMP_NGE_UQ   0x19 /* Not-greater-than-or-equal (unordered, non-signaling)  */
#define _CMP_NGT_UQ   0x1a /* Not-greater-than (unordered, non-signaling)  */
#define _CMP_FALSE_OS 0x1b /* False (ordered, signaling)  */
#define _CMP_NEQ_OS   0x1c /* Not-equal (ordered, signaling)  */
#define _CMP_GE_OQ    0x1d /* Greater-than-or-equal (ordered, non-signaling)  */
#define _CMP_GT_OQ    0x1e /* Greater-than (ordered, non-signaling)  */
#define _CMP_TRUE_US  0x1f /* True (unordered, signaling)  */

#if( PLATFORM_INTRINSIC < PLATFORM_INTRINSIC_SSE4 )

    // round

    template<int32_t imm> PLATFORM_INLINE __m128 emu_mm_round_ps(const __m128& x)
    {
        DEBUG_StaticAssertMsg(false, "Unsupported rounding mode!");

        return _mm_setzero_ps();
    }

    #undef _mm_round_ps
    #define _mm_round_ps(x, imm) emu_mm_round_ps<imm>(x)

    template<> PLATFORM_INLINE __m128 emu_mm_round_ps<_MM_FROUND_TO_NEAREST_INT | ROUNDING_EXEPTIONS_MASK>(const __m128& x)
    {
        __m128 and0 = _mm_and_ps(_mm_castsi128_ps(_mm_set1_epi32(0x80000000)), x);
        __m128 or0 = _mm_or_ps(and0, _mm_set1_ps(8388608.0f));
        __m128 add0 = _mm_add_ps(x, or0);

        return _mm_sub_ps(add0, or0);
    }

    template<> PLATFORM_INLINE __m128 emu_mm_round_ps<_MM_FROUND_FLOOR | ROUNDING_EXEPTIONS_MASK>(const __m128& x)
    {
        __m128 rnd0 = _mm_round_ps(x, _MM_FROUND_TO_NEAREST_INT | ROUNDING_EXEPTIONS_MASK);
        __m128 cmp0 = _mm_cmplt_ps(x, rnd0);
        __m128 and0 = _mm_and_ps(cmp0, _mm_set1_ps(1.0f));

        return _mm_sub_ps(rnd0, and0);
    }

    template<> PLATFORM_INLINE __m128 emu_mm_round_ps<_MM_FROUND_CEIL | ROUNDING_EXEPTIONS_MASK>(const __m128& x)
    {
        __m128 rnd0 = _mm_round_ps(x, _MM_FROUND_TO_NEAREST_INT | ROUNDING_EXEPTIONS_MASK);
        __m128 cmp0 = _mm_cmpgt_ps(x, rnd0);
        __m128 and0 = _mm_and_ps(cmp0, _mm_set1_ps(1.0f));

        return _mm_add_ps(rnd0, and0);
    }

    template<int32_t imm> PLATFORM_INLINE __m128d emu_mm_round_pd(const __m128d& x)
    {
        DEBUG_StaticAssertMsg(false, "Unsupported rounding mode!");

        return _mm_setzero_pd();
    }

    #undef _mm_round_pd
    #define _mm_round_pd(x, imm) emu_mm_round_pd<imm>(x)

    template<> PLATFORM_INLINE __m128d emu_mm_round_pd<_MM_FROUND_TO_NEAREST_INT | ROUNDING_EXEPTIONS_MASK>(const __m128d& x)
    {
        __m128d and0 = _mm_and_pd(_mm_castsi128_pd(_mm_set_epi32(0x80000000, 0x00000000, 0x80000000, 0x00000000)), x);
        __m128d or0 = _mm_or_pd(and0, _mm_set1_pd(4503599627370496.0));
        __m128d add0 = _mm_add_pd(x, or0);

        return _mm_sub_pd(add0, or0);
    }

    template<> PLATFORM_INLINE __m128d emu_mm_round_pd<_MM_FROUND_FLOOR | ROUNDING_EXEPTIONS_MASK>(const __m128d& x)
    {
        __m128d rnd0 = _mm_round_pd(x, _MM_FROUND_TO_NEAREST_INT | ROUNDING_EXEPTIONS_MASK);
        __m128d cmp0 = _mm_cmplt_pd(x, rnd0);
        __m128d and0 = _mm_and_pd(cmp0, _mm_set1_pd(1.0));

        return _mm_sub_pd(rnd0, and0);
    }

    template<> PLATFORM_INLINE __m128d emu_mm_round_pd<_MM_FROUND_CEIL | ROUNDING_EXEPTIONS_MASK>(const __m128d& x)
    {
        __m128d rnd0 = _mm_round_pd(x, _MM_FROUND_TO_NEAREST_INT | ROUNDING_EXEPTIONS_MASK);
        __m128d cmp0 = _mm_cmpgt_pd(x, rnd0);
        __m128d and0 = _mm_and_pd(cmp0, _mm_set1_pd(1.0));

        return _mm_add_pd(rnd0, and0);
    }

    // dp

    template<int32_t imm> PLATFORM_INLINE __m128 emu_mm_dp_ps(const __m128& x, const __m128& y)
    {
        DEBUG_StaticAssertMsg(false, "Unsupported dp mode!");

        return _mm_setzero_ps();
    }

    #undef _mm_dp_ps
    #define _mm_dp_ps(x, y, imm) emu_mm_dp_ps<imm>(x, y)

    template<> PLATFORM_INLINE __m128 emu_mm_dp_ps<127>(const __m128& x, const __m128& y)
    {
        __m128 r = _mm_mul_ps(x, y);
        r = _mm_and_ps(r, _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000)));
        r = _mm_hadd_ps(r, r);
        r = _mm_hadd_ps(r, r);

        return r;
    }

    template<> PLATFORM_INLINE __m128 emu_mm_dp_ps<255>(const __m128& x, const __m128& y)
    {
        __m128 r = _mm_mul_ps(x, y);
        r = _mm_hadd_ps(r, r);
        r = _mm_hadd_ps(r, r);

        return r;
    }

    // other

    #undef _mm_blendv_ps
    #undef _mm_blendv_pd
    #define _mm_blendv_ps(a, b, mask) _mm_xor_ps(a, _mm_and_ps(mask, _mm_xor_ps(b, a)))
    #define _mm_blendv_pd(a, b, mask) _mm_xor_pd(a, _mm_and_pd(mask, _mm_xor_pd(b, a)))

#endif

//======================================================================================================================
// AVX
//======================================================================================================================

#if( PLATFORM_INTRINSIC < PLATFORM_INTRINSIC_AVX1 )

    #define M256_ALIGN( a ) alignas(a)

    union M256_ALIGN(32) emu__m256
    {
        __m128 emu_m128[2];
        float emu_arr[8];
    };

    union M256_ALIGN(32) emu__m256d
    {
        __m128d emu_m128[2];
        double emu_arr[4];
    };

    union M256_ALIGN(32) emu__m256i
    {
        __m128i emu_m128[2];
        int32_t emu_arr[8];
    };

    #define __EMU_M256_IMPL_M1( type, func ) \
    PLATFORM_INLINE emu##type emu_mm256_##func( const emu##type& m256_param1 ) \
    {   emu##type res; \
        res.emu_m128[0] = _mm_##func( m256_param1.emu_m128[0] ); \
        res.emu_m128[1] = _mm_##func( m256_param1.emu_m128[1] ); \
        return ( res ); \
    }

    #define __EMU_M256_IMPL_M1_RET( ret_type, type, func ) \
    PLATFORM_INLINE emu##ret_type emu_mm256_##func( const emu##type& m256_param1 ) \
    {   emu##ret_type res; \
        res.emu_m128[0] = _mm_##func( m256_param1.emu_m128[0] ); \
        res.emu_m128[1] = _mm_##func( m256_param1.emu_m128[1] ); \
        return ( res ); \
    }

    #define __EMU_M256_IMPL_M1_RET_NAME( ret_type, type, func, name ) \
        PLATFORM_INLINE emu##ret_type emu_mm256_##name( const emu##type& m256_param1 ) \
    {   emu##ret_type res; \
        res.emu_m128[0] = _mm_##func( m256_param1.emu_m128[0] ); \
        res.emu_m128[1] = _mm_##func( m256_param1.emu_m128[1] ); \
        return ( res ); \
    }

    #define __EMU_M256_IMPL_M1_LH( type, type_128, func ) \
    PLATFORM_INLINE emu##type emu_mm256_##func( const type_128& m128_param ) \
    {   emu##type res; \
        res.emu_m128[0] = _mm_##func( m128_param ); \
        __m128 m128_param_high = _mm_movehl_ps( *(__m128*)&m128_param, *(__m128*)&m128_param ); \
        res.emu_m128[1] = _mm_##func( *(type_128*)&m128_param_high ); \
        return ( res ); \
    }

    #define __EMU_M256_IMPL_M1_HL( type_128, type, func ) \
    PLATFORM_INLINE type_128 emu_mm256_##func( const emu##type& m256_param1 ) \
    {   type_128 res, tmp; \
        res = _mm_##func( m256_param1.emu_m128[0] ); \
        tmp = _mm_##func( m256_param1.emu_m128[1] ); \
        *(((int64_t*)&res)+1) = *(int64_t*)&tmp; \
        return ( res ); \
    }

    #define __EMU_M256_IMPL_M1P_DUP( type, type_param, func ) \
    PLATFORM_INLINE emu##type emu_mm256_##func( const type_param& param ) \
    {   emu##type res; \
        res.emu_m128[0] = _mm_##func( param ); \
        res.emu_m128[1] = _mm_##func( param ); \
        return ( res ); \
    }

    #define __EMU_M256_IMPL2_M1I_SHIFT( type, func, shift_for_hi ) \
    PLATFORM_INLINE emu##type emu_mm256_##func( const emu##type& m256_param1, const int32_t param2 ) \
    {   emu##type res; \
        res.emu_m128[0] = emu_mm_##func( m256_param1.emu_m128[0], param2 & ((1<<shift_for_hi)-1) ); \
        res.emu_m128[1] = emu_mm_##func( m256_param1.emu_m128[1], param2 >> shift_for_hi); \
        return ( res ); \
    }

    #define __EMU_M256_IMPL_M2( type, func ) \
    PLATFORM_INLINE emu##type emu_mm256_##func( const emu##type& m256_param1, const emu##type& m256_param2 ) \
    {   emu##type res; \
        res.emu_m128[0] = _mm_##func( m256_param1.emu_m128[0], m256_param2.emu_m128[0] ); \
        res.emu_m128[1] = _mm_##func( m256_param1.emu_m128[1], m256_param2.emu_m128[1] ); \
        return ( res ); \
    }

    #define __EMU_M256_IMPL2_M2T( type, type_2, func ) \
    PLATFORM_INLINE emu##type emu_mm256_##func( const emu##type& m256_param1, const emu##type_2& m256_param2 ) \
    {   emu##type res; \
        res.emu_m128[0] = emu_mm_##func( m256_param1.emu_m128[0], m256_param2.emu_m128[0] ); \
        res.emu_m128[1] = emu_mm_##func( m256_param1.emu_m128[1], m256_param2.emu_m128[1] ); \
        return ( res ); \
    }

    #define __EMU_M256_IMPL_M3( type, func ) \
    PLATFORM_INLINE emu##type emu_mm256_##func( const emu##type& m256_param1, const emu##type& m256_param2, const emu##type& m256_param3 ) \
    {   emu##type res; \
        res.emu_m128[0] = _mm_##func( m256_param1.emu_m128[0], m256_param2.emu_m128[0], m256_param3.emu_m128[0] ); \
        res.emu_m128[1] = _mm_##func( m256_param1.emu_m128[1], m256_param2.emu_m128[1], m256_param3.emu_m128[1] ); \
        return ( res ); \
    }

    __EMU_M256_IMPL_M2( __m256d, add_pd );
    __EMU_M256_IMPL_M2( __m256, add_ps );

    __EMU_M256_IMPL_M2( __m256d, addsub_pd );
    __EMU_M256_IMPL_M2( __m256, addsub_ps );

    __EMU_M256_IMPL_M2( __m256d, and_pd );
    __EMU_M256_IMPL_M2( __m256, and_ps );

    __EMU_M256_IMPL_M2( __m256d, andnot_pd );
    __EMU_M256_IMPL_M2( __m256, andnot_ps );

    __EMU_M256_IMPL_M2( __m256d, div_pd );
    __EMU_M256_IMPL_M2( __m256, div_ps );

    __EMU_M256_IMPL_M2( __m256d, hadd_pd );
    __EMU_M256_IMPL_M2( __m256, hadd_ps );

    __EMU_M256_IMPL_M2( __m256d, hsub_pd );
    __EMU_M256_IMPL_M2( __m256, hsub_ps );

    __EMU_M256_IMPL_M2( __m256d, max_pd );
    __EMU_M256_IMPL_M2( __m256, max_ps );

    __EMU_M256_IMPL_M2( __m256d, min_pd );
    __EMU_M256_IMPL_M2( __m256, min_ps );

    __EMU_M256_IMPL_M2( __m256d, mul_pd );
    __EMU_M256_IMPL_M2( __m256, mul_ps );

    __EMU_M256_IMPL_M2( __m256d, or_pd );
    __EMU_M256_IMPL_M2( __m256, or_ps );

    __EMU_M256_IMPL_M2( __m256d, sub_pd );
    __EMU_M256_IMPL_M2( __m256, sub_ps );

    __EMU_M256_IMPL_M2( __m256d, xor_pd );
    __EMU_M256_IMPL_M2( __m256, xor_ps );

    #define emu_mm_permute_ps(a, control) _mm_castsi128_ps( _mm_shuffle_epi32(_mm_castps_si128(a), control ) )

    #define emu_mm_shuffle_ps(x, y, imm) \
        emu__m256{_mm_shuffle_ps(x.emu_128[0], y.emu_128[0], imm), _mm_shuffle_ps(x.emu_128[1], y.emu_128[1], imm)}

    #define emu_mm256_shuffle_pd(x, y, imm) \
        emu__m256d{_mm_shuffle_pd(x.emu_m128[0], y.emu_m128[0], imm), _mm_shuffle_pd(x.emu_m128[1], y.emu_m128[1], imm)}

    #define emu_mm256_permute_ps(m256_param1, param2) \
        emu__m256d{emu_mm_permute_ps(m256_param1.emu_m128[0],param2), emu_mm_permute_ps(m256_param1.emu_m128[1], param2)}

    // FIXME: in this section SSE4 required, not all SSE4 intrinsic emulated!

    #define emu_mm256_blend_pd(m256_param1, m256_param2, param3) \
        emu__m256d{_mm_blend_pd(m256_param1.emu_m128[0], m256_param2.emu_m128[0], param3 & ((1<<2)-1)), _mm_blend_pd(m256_param1.emu_m128[1], m256_param2.emu_m128[1], param3 >> 2)}

    #define emu_mm256_blend_ps(m256_param1, m256_param2, param3) \
        emu__m256{_mm_blend_ps(m256_param1.emu_m128[0], m256_param2.emu_m128[0], param3 & ((1<<4)-1)), _mm_blend_ps(m256_param1.emu_m128[1], m256_param2.emu_m128[1], param3 >> 4)}

    #define emu_mm256_dp_ps(m256_param1, m256_param2, param3) \
        emu__m256{_mm_dp_ps( m256_param1.emu_m128[0], m256_param2.emu_m128[0], param3 ), _mm_dp_ps( m256_param1.emu_m128[1], m256_param2.emu_m128[1], param3 )}

    #define emu_mm256_round_pd(m256_param1, param2) \
        emu__m256d{_mm_round_pd( m256_param1.emu_m128[0], param2 ), _mm_round_pd( m256_param1.emu_m128[1], param2 )}

    #define emu_mm256_round_ps(m256_param1, param2) \
        emu__m256{_mm_round_ps( m256_param1.emu_m128[0], param2 ), _mm_round_ps( m256_param1.emu_m128[1], param2 )}

    __EMU_M256_IMPL_M3( __m256d, blendv_pd );
    __EMU_M256_IMPL_M3( __m256, blendv_ps );

    const __m128d sign_bits_pd = _mm_castsi128_pd( _mm_set_epi32( 1 << 31, 0, 1 << 31, 0 ) );
    const __m128 sign_bits_ps = _mm_castsi128_ps( _mm_set1_epi32( 1 << 31 ) );

    #define emu_mm_test_impl( op, sfx, vec_type ) \
    PLATFORM_INLINE int32_t emu_mm_test##op##_##sfx(const vec_type& s1, const vec_type& s2) { \
        vec_type t1 = _mm_and_##sfx( s1, sign_bits_##sfx ); \
        vec_type t2 = _mm_and_##sfx( s2, sign_bits_##sfx ); \
        return _mm_test##op##_si128( _mm_cast##sfx##_si128( t1 ), _mm_cast##sfx##_si128( t2 ) ); \
    }

    emu_mm_test_impl( z, pd, __m128d );
    emu_mm_test_impl( c, pd, __m128d );
    emu_mm_test_impl( nzc, pd, __m128d );

    emu_mm_test_impl( z, ps, __m128 );
    emu_mm_test_impl( c, ps, __m128 );
    emu_mm_test_impl( nzc, ps, __m128 );

    #define emu_mm256_test_impl( prfx, op, sfx, sfx_impl, vec_type ) \
    PLATFORM_INLINE int32_t emu_mm256_test##op##_##sfx(const vec_type& s1, const vec_type& s2) { \
        int32_t ret1 = prfx##_test##op##_##sfx_impl( s1.emu_m128[0], s2.emu_m128[0] ); \
        int32_t ret2 = prfx##_test##op##_##sfx_impl( s1.emu_m128[1], s2.emu_m128[1] ); \
        return ( ret1 && ret2 ); \
    };

    emu_mm256_test_impl( _mm, z, si256, si128, emu__m256i );
    emu_mm256_test_impl( _mm, c, si256, si128, emu__m256i );
    emu_mm256_test_impl( _mm, nzc, si256, si128, emu__m256i );

    emu_mm256_test_impl( emu_mm, z, pd, pd, emu__m256d );
    emu_mm256_test_impl( emu_mm, c, pd, pd, emu__m256d );
    emu_mm256_test_impl( emu_mm, nzc, pd, pd, emu__m256d );

    emu_mm256_test_impl( emu_mm, z, ps, ps, emu__m256 );
    emu_mm256_test_impl( emu_mm, c, ps, ps, emu__m256 );
    emu_mm256_test_impl( emu_mm, nzc, ps, ps, emu__m256 );

    // NOTE: end SSE4 section

    template<int32_t imm>   PLATFORM_INLINE __m128 emu_mm_cmp_ps(const __m128&, const __m128&)                      { return _mm_setzero_ps(); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_EQ_OQ>(const __m128& a, const __m128& b)      { return _mm_cmpeq_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_EQ_UQ>(const __m128& a, const __m128& b)      { return _mm_cmpeq_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_EQ_US>(const __m128& a, const __m128& b)      { return _mm_cmpeq_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_EQ_OS>(const __m128& a, const __m128& b)      { return _mm_cmpeq_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_LT_OS>(const __m128& a, const __m128& b)      { return _mm_cmplt_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_LT_OQ>(const __m128& a, const __m128& b)      { return _mm_cmplt_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_LE_OS>(const __m128& a, const __m128& b)      { return _mm_cmple_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_LE_OQ>(const __m128& a, const __m128& b)      { return _mm_cmple_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_GE_OS>(const __m128& a, const __m128& b)      { return _mm_cmpge_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_GE_OQ>(const __m128& a, const __m128& b)      { return _mm_cmpge_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_GT_OS>(const __m128& a, const __m128& b)      { return _mm_cmpgt_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_GT_OQ>(const __m128& a, const __m128& b)      { return _mm_cmpgt_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NEQ_UQ>(const __m128& a, const __m128& b)     { return _mm_cmpneq_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NEQ_OQ>(const __m128& a, const __m128& b)     { return _mm_cmpneq_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NEQ_US>(const __m128& a, const __m128& b)     { return _mm_cmpneq_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NEQ_OS>(const __m128& a, const __m128& b)     { return _mm_cmpneq_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NLT_US>(const __m128& a, const __m128& b)     { return _mm_cmpnlt_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NLT_UQ>(const __m128& a, const __m128& b)     { return _mm_cmpnlt_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NLE_US>(const __m128& a, const __m128& b)     { return _mm_cmpnle_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NLE_UQ>(const __m128& a, const __m128& b)     { return _mm_cmpnle_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NGT_US>(const __m128& a, const __m128& b)     { return _mm_cmpngt_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NGT_UQ>(const __m128& a, const __m128& b)     { return _mm_cmpngt_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NGE_US>(const __m128& a, const __m128& b)     { return _mm_cmpnge_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_NGE_UQ>(const __m128& a, const __m128& b)     { return _mm_cmpnge_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_ORD_Q>(const __m128& a, const __m128& b)      { return _mm_cmpord_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_ORD_S>(const __m128& a, const __m128& b)      { return _mm_cmpord_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_UNORD_Q>(const __m128& a, const __m128& b)    { return _mm_cmpunord_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_UNORD_S>(const __m128& a, const __m128& b)    { return _mm_cmpunord_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_TRUE_UQ>(const __m128& a, const __m128& b)    { return _mm_cmpunord_ps(a, b); }
    template<>              PLATFORM_INLINE __m128 emu_mm_cmp_ps<_CMP_TRUE_US>(const __m128&, const __m128&)        { return _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF)); }

    template<int32_t imm>   PLATFORM_INLINE __m128d emu_mm_cmp_pd(const __m128d&, const __m128d&)                   { return _mm_setzero_pd(); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_EQ_OQ>(const __m128d& a, const __m128d& b)   { return _mm_cmpeq_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_EQ_UQ>(const __m128d& a, const __m128d& b)   { return _mm_cmpeq_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_EQ_US>(const __m128d& a, const __m128d& b)   { return _mm_cmpeq_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_EQ_OS>(const __m128d& a, const __m128d& b)   { return _mm_cmpeq_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_LT_OS>(const __m128d& a, const __m128d& b)   { return _mm_cmplt_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_LT_OQ>(const __m128d& a, const __m128d& b)   { return _mm_cmplt_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_LE_OS>(const __m128d& a, const __m128d& b)   { return _mm_cmple_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_LE_OQ>(const __m128d& a, const __m128d& b)   { return _mm_cmple_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_GE_OS>(const __m128d& a, const __m128d& b)   { return _mm_cmpge_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_GE_OQ>(const __m128d& a, const __m128d& b)   { return _mm_cmpge_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_GT_OS>(const __m128d& a, const __m128d& b)   { return _mm_cmpgt_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_GT_OQ>(const __m128d& a, const __m128d& b)   { return _mm_cmpgt_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NEQ_UQ>(const __m128d& a, const __m128d& b)  { return _mm_cmpneq_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NEQ_OQ>(const __m128d& a, const __m128d& b)  { return _mm_cmpneq_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NEQ_US>(const __m128d& a, const __m128d& b)  { return _mm_cmpneq_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NEQ_OS>(const __m128d& a, const __m128d& b)  { return _mm_cmpneq_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NLT_US>(const __m128d& a, const __m128d& b)  { return _mm_cmpnlt_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NLT_UQ>(const __m128d& a, const __m128d& b)  { return _mm_cmpnlt_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NLE_US>(const __m128d& a, const __m128d& b)  { return _mm_cmpnle_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NLE_UQ>(const __m128d& a, const __m128d& b)  { return _mm_cmpnle_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NGT_US>(const __m128d& a, const __m128d& b)  { return _mm_cmpngt_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NGT_UQ>(const __m128d& a, const __m128d& b)  { return _mm_cmpngt_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NGE_US>(const __m128d& a, const __m128d& b)  { return _mm_cmpnge_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_NGE_UQ>(const __m128d& a, const __m128d& b)  { return _mm_cmpnge_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_ORD_Q>(const __m128d& a, const __m128d& b)   { return _mm_cmpord_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_ORD_S>(const __m128d& a, const __m128d& b)   { return _mm_cmpord_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_UNORD_Q>(const __m128d& a, const __m128d& b) { return _mm_cmpunord_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_UNORD_S>(const __m128d& a, const __m128d& b) { return _mm_cmpunord_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_TRUE_UQ>(const __m128d& a, const __m128d& b) { return _mm_cmpunord_pd(a, b); }
    template<>              PLATFORM_INLINE __m128d emu_mm_cmp_pd<_CMP_TRUE_US>(const __m128d&, const __m128d&)     { return _mm_castsi128_pd(_mm_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF)); }

    template<int imm8>
    PLATFORM_INLINE emu__m256 emu_mm256_cmp_ps(emu__m256 a, emu__m256 b)
    {
        emu__m256 result;
        result.emu_m128[0] = emu_mm_cmp_ps<imm8>( a.emu_m128[0], b.emu_m128[0] );
        result.emu_m128[1] = emu_mm_cmp_ps<imm8>( a.emu_m128[1], b.emu_m128[1] );
        return result;
    }

    template<int imm8>
    PLATFORM_INLINE emu__m256d emu_mm256_cmp_pd(emu__m256d a, emu__m256d b)
    {
        emu__m256d result;
        result.emu_m128[0] = emu_mm_cmp_pd<imm8>( a.emu_m128[0], b.emu_m128[0] );
        result.emu_m128[1] = emu_mm_cmp_pd<imm8>( a.emu_m128[1], b.emu_m128[1] );
        return result;
    }

    #define emu_mm256_cmp_ps(a, b, imm8) emu_mm256_cmp_ps<imm8>(a, b)
    #define emu_mm256_cmp_pd(a, b, imm8) emu_mm256_cmp_pd<imm8>(a, b)

    __EMU_M256_IMPL_M1_LH( __m256d, __m128i, cvtepi32_pd );
    __EMU_M256_IMPL_M1_RET( __m256, __m256i, cvtepi32_ps );
    __EMU_M256_IMPL_M1_HL( __m128, __m256d, cvtpd_ps );
    __EMU_M256_IMPL_M1_RET( __m256i, __m256, cvtps_epi32 );
    __EMU_M256_IMPL_M1_LH( __m256d, __m128, cvtps_pd );
    __EMU_M256_IMPL_M1_HL( __m128i, __m256d, cvttpd_epi32);
    __EMU_M256_IMPL_M1_HL( __m128i, __m256d, cvtpd_epi32);
    __EMU_M256_IMPL_M1_RET( __m256i, __m256, cvttps_epi32 );

    PLATFORM_INLINE __m128  emu_mm256_extractf128_ps(const emu__m256& m1, const int32_t offset) { return m1.emu_m128[ offset ]; }
    PLATFORM_INLINE __m128d emu_mm256_extractf128_pd(const emu__m256d& m1, const int32_t offset) { return m1.emu_m128[ offset ]; }
    PLATFORM_INLINE __m128i emu_mm256_extractf128_si256(const emu__m256i& m1, const int32_t offset) { return m1.emu_m128[ offset ]; }

    PLATFORM_INLINE void emu_mm256_zeroall(void) {}
    PLATFORM_INLINE void emu_mm256_zeroupper(void) {}

    PLATFORM_INLINE __m128 emu_mm_permutevar_ps(const __m128& a, __m128i control)
    {
        int32_t const* sel = (int32_t const*)&control;
        float const* src = (float const*)&a;
        M256_ALIGN(16) float dest[4];
        int32_t i=0;

        for (; i<4; ++i)
            dest[i] = src[ 3 & sel[i] ];

        return ( *(__m128*)dest );
    }
    __EMU_M256_IMPL2_M2T( __m256, __m256i, permutevar_ps );

    PLATFORM_INLINE __m128d emu_mm_permutevar_pd(const __m128d& a, const __m128i& control)
    {
        int64_t const* sel = (int64_t const*)&control;
        double const* src = (double const*)&a;
        M256_ALIGN(16) double dest[2];
        int32_t i=0;

        for (; i<2; ++i)
            dest[i] = src[ (2 & sel[i]) >> 1 ];

        return ( *(__m128d*)dest );
    }
    __EMU_M256_IMPL2_M2T( __m256d, __m256i, permutevar_pd );

    PLATFORM_INLINE __m128d emu_mm_permute_pd(const __m128d& a, int32_t control)
    {
        double const* src = (double const*)&a;
        M256_ALIGN(16) double dest[2];
        int32_t i=0;

        for (; i<2; ++i)
            dest[i] = src[ 1 & (control >> i) ];

        return ( *(__m128d*)dest );
    }
    __EMU_M256_IMPL2_M1I_SHIFT( __m256d, permute_pd, 2 );


    #define emu_mm256_permute2f128_impl( name, m128_type, m256_type ) \
    PLATFORM_INLINE m256_type name( const m256_type& m1, const m256_type& m2, int32_t control) { \
        m256_type res; \
        __m128 zero = _mm_setzero_ps(); \
        const m128_type param[4] = { m1.emu_m128[0], m1.emu_m128[1], m2.emu_m128[0], m2.emu_m128[1] }; \
        res.emu_m128[0] = (control & 8) ? *(m128_type*)&zero : param[ control & 0x3 ]; control >>= 4; \
        res.emu_m128[1] = (control & 8) ? *(m128_type*)&zero : param[ control & 0x3 ]; \
        return ( res ); \
    }

    emu_mm256_permute2f128_impl( emu_mm256_permute2f128_ps, __m128, emu__m256 );
    emu_mm256_permute2f128_impl( emu_mm256_permute2f128_pd, __m128d, emu__m256d );
    emu_mm256_permute2f128_impl( emu_mm256_permute2f128_si256, __m128i, emu__m256i );

    #define emu_mm_broadcast_impl( name, res_type, type ) \
    PLATFORM_INLINE res_type  name(type const *a) { \
        const size_t size = sizeof( res_type ) / sizeof( type ); \
        M256_ALIGN(32) type res[ size ]; \
        size_t i = 0; \
        for ( ; i < size; ++i ) \
            res[ i ] = *a; \
        return (*(res_type*)&res); \
    }

    emu_mm_broadcast_impl( emu_mm256_broadcast_ss, emu__m256, float )

    emu_mm_broadcast_impl( emu_mm_broadcast_sd, __m128, double )
    emu_mm_broadcast_impl( emu_mm256_broadcast_sd, emu__m256d, double )

    emu_mm_broadcast_impl( emu_mm256_broadcast_ps, emu__m256, __m128 )
    emu_mm_broadcast_impl( emu_mm256_broadcast_pd, emu__m256d, __m128d )

    PLATFORM_INLINE emu__m256 emu_mm256_insertf128_ps(const emu__m256& a, const __m128& b, int32_t offset)
    {
        emu__m256 t = a;
        t.emu_m128[ offset ] = b;
        return t;
    }

    PLATFORM_INLINE emu__m256d emu_mm256_insertf128_pd(const emu__m256d& a, const __m128d& b, int32_t offset)
    {
        emu__m256d t = a;
        t.emu_m128[ offset ] = b;
        return t;
    }

    PLATFORM_INLINE emu__m256i emu_mm256_insertf128_si256(const emu__m256i& a,const __m128i& b, int32_t offset)
    {
        emu__m256i t = a;
        t.emu_m128[ offset ] = b;
        return t;
    }

    #define emu_mm_load_impl( name, sfx, m256_sfx, m256_type, type_128, type ) \
    PLATFORM_INLINE emu##m256_type  emu_mm256_##name##_##m256_sfx(const type* a) { \
        emu##m256_type res; \
        res.emu_m128[0] = _mm_##name##_##sfx( (const type_128 *)a ); \
        res.emu_m128[1] = _mm_##name##_##sfx( (const type_128 *)(1+(const __m128 *)a) ); \
        return (res); \
    }

    #define emu_mm_store_impl( name, sfx, m256_sfx, m256_type, type_128, type ) \
    PLATFORM_INLINE void emu_mm256_##name##_##m256_sfx(type *a, const emu##m256_type& b) { \
        _mm_##name##_##sfx( (type_128*)a, b.emu_m128[0] ); \
        _mm_##name##_##sfx( (type_128*)(1+(__m128*)a), b.emu_m128[1] ); \
    }

    emu_mm_load_impl( load, pd, pd, __m256d, double, double );
    emu_mm_store_impl( store, pd, pd, __m256d, double, double );

    emu_mm_load_impl( load, ps, ps, __m256, float, float );
    emu_mm_store_impl( store, ps, ps, __m256, float, float );

    emu_mm_load_impl( loadu, pd, pd, __m256d, double, double );
    emu_mm_store_impl( storeu, pd, pd, __m256d, double, double );

    emu_mm_load_impl( loadu, ps, ps, __m256, float, float );
    emu_mm_store_impl( storeu, ps, ps, __m256, float, float );

    emu_mm_load_impl( load, si128, si256, __m256i, __m128i, emu__m256i );
    emu_mm_store_impl( store, si128, si256, __m256i, __m128i, emu__m256i );

    emu_mm_load_impl( loadu, si128, si256, __m256i, __m128i, emu__m256i );
    emu_mm_store_impl( storeu, si128, si256, __m256i, __m128i, emu__m256i );


    #define emu_maskload_impl( name, vec_type, mask_vec_type, type, mask_type ) \
    PLATFORM_INLINE vec_type  name(type const *a, const mask_vec_type& mask) { \
        const size_t size_type = sizeof( type ); \
        const size_t size = sizeof( vec_type ) / size_type; \
        M256_ALIGN(32) type res[ size ]; \
        const mask_type* p_mask = (const mask_type*)&mask; \
        size_t i = 0; \
        mask_type sign_bit = 1; \
        sign_bit <<= (8*size_type - 1); \
        for ( ; i < size; ++i ) \
            res[ i ] = (sign_bit & *(p_mask + i)) ? *(a+i) : 0; \
        return (*(vec_type*)&res); \
    }

    #define emu_maskstore_impl( name, vec_type, mask_vec_type, type, mask_type ) \
    PLATFORM_INLINE void  name(type *a, const mask_vec_type& mask, const vec_type& data) { \
        const size_t size_type = sizeof( type ); \
        const size_t size = sizeof( vec_type ) / sizeof( type ); \
        type* p_data = (type*)&data; \
        const mask_type* p_mask = (const mask_type*)&mask; \
        size_t i = 0; \
        mask_type sign_bit = 1; \
        sign_bit <<= (8*size_type - 1); \
        for ( ; i < size; ++i ) \
            if ( *(p_mask + i ) & sign_bit) \
                *(a + i) = *(p_data + i); \
    }

    emu_maskload_impl( emu_mm256_maskload_pd, emu__m256d, emu__m256i, double, int64_t );
    emu_maskstore_impl( emu_mm256_maskstore_pd, emu__m256d, emu__m256i, double, int64_t );

    emu_maskload_impl( emu_mm_maskload_pd, __m128d, __m128i, double, int64_t );
    emu_maskstore_impl( emu_mm_maskstore_pd, __m128d, __m128i, double, int64_t );

    emu_maskload_impl( emu_mm256_maskload_ps, emu__m256, emu__m256i, float, int32_t );
    emu_maskstore_impl( emu_mm256_maskstore_ps, emu__m256, emu__m256i, float, int32_t );

    emu_maskload_impl( emu_mm_maskload_ps, __m128, __m128i, float, int32_t );
    emu_maskstore_impl( emu_mm_maskstore_ps, __m128, __m128i, float, int32_t );


    __EMU_M256_IMPL_M1( __m256, movehdup_ps );
    __EMU_M256_IMPL_M1( __m256, moveldup_ps );
    __EMU_M256_IMPL_M1( __m256d, movedup_pd );

    emu_mm_load_impl( lddqu, si128, si256, __m256i, __m128i, emu__m256i );

    emu_mm_store_impl( stream, si128, si256, __m256i, __m128i, emu__m256i );
    emu_mm_store_impl( stream, pd, pd, __m256d, double, double );
    emu_mm_store_impl( stream, ps, ps, __m256, float, float );


    __EMU_M256_IMPL_M1( __m256, rcp_ps );
    __EMU_M256_IMPL_M1( __m256, rsqrt_ps );

    __EMU_M256_IMPL_M1( __m256d, sqrt_pd );
    __EMU_M256_IMPL_M1( __m256, sqrt_ps );

    __EMU_M256_IMPL_M2( __m256d, unpackhi_pd );
    __EMU_M256_IMPL_M2( __m256, unpackhi_ps );
    __EMU_M256_IMPL_M2( __m256d, unpacklo_pd );
    __EMU_M256_IMPL_M2( __m256, unpacklo_ps );


    PLATFORM_INLINE int32_t emu_mm256_movemask_pd(const emu__m256d& a)
    {
        return (_mm_movemask_pd( a.emu_m128[1] ) << 2) | _mm_movemask_pd( a.emu_m128[0] );
    }

    PLATFORM_INLINE int32_t emu_mm256_movemask_ps(const emu__m256& a)
    {
        return (_mm_movemask_ps( a.emu_m128[1] ) << 4) | _mm_movemask_ps( a.emu_m128[0] );
    }

    PLATFORM_INLINE emu__m256d emu_mm256_setzero_pd(void)
    {
        return emu__m256d{ _mm_setzero_pd(), _mm_setzero_pd() };
    }

    PLATFORM_INLINE emu__m256 emu_mm256_setzero_ps(void)
    {
        return emu__m256{ _mm_setzero_ps(), _mm_setzero_ps() };
    }

    PLATFORM_INLINE emu__m256i emu_mm256_setzero_si256(void)
    {
        return emu__m256i{ _mm_setzero_si128(), _mm_setzero_si128() };
    }

    PLATFORM_INLINE emu__m256d emu_mm256_set_pd(double a1, double a2, double a3, double a4)
    {
        return emu__m256d{ _mm_set_pd( a3, a4 ), _mm_set_pd( a1, a2 ) };
    }

    PLATFORM_INLINE emu__m256 emu_mm256_set_ps(float a1, float a2, float a3, float a4, float a5, float a6, float a7, float a8)
    {
        return emu__m256{ _mm_set_ps( a5, a6, a7, a8 ), _mm_set_ps( a1, a2, a3, a4 ) };
    }

    PLATFORM_INLINE emu__m256i emu_mm256_set_epi8(int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7, int8_t a8,
                                           int8_t a9, int8_t a10, int8_t a11, int8_t a12, int8_t a13, int8_t a14, int8_t a15, int8_t a16,
                                           int8_t a17, int8_t a18, int8_t a19, int8_t a20, int8_t a21, int8_t a22, int8_t a23, int8_t a24,
                                           int8_t a25, int8_t a26, int8_t a27, int8_t a28, int8_t a29, int8_t a30, int8_t a31, int8_t a32)
    {
        return emu__m256i{ _mm_set_epi8( a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32 ),
                          _mm_set_epi8( a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16 ) };
    }

    PLATFORM_INLINE emu__m256i emu_mm256_set_epi16(int16_t a1, int16_t a2, int16_t a3, int16_t a4, int16_t a5, int16_t a6, int16_t a7, int16_t a8,
                                                           int16_t a9, int16_t a10, int16_t a11, int16_t a12, int16_t a13, int16_t a14, int16_t a15, int16_t a16)
    {
        return emu__m256i{ _mm_set_epi16( a9, a10, a11, a12, a13, a14, a15, a16 ),
                          _mm_set_epi16( a1, a2, a3, a4, a5, a6, a7, a8 ) };
    }

    PLATFORM_INLINE emu__m256i emu_mm256_set_epi32(int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7, int32_t a8)
    {
        return emu__m256i{ _mm_set_epi32( a5, a6, a7, a8 ), _mm_set_epi32( a1, a2, a3, a4 ) };
    }

    PLATFORM_INLINE __m128i emu_mm_set_epi64x(int64_t a, int64_t b)
    {
        return _mm_set_epi64x(a, b);
    }

    PLATFORM_INLINE emu__m256i emu_mm256_set_epi64x(int64_t a1, int64_t a2, int64_t a3, int64_t a4)
    {
        return emu__m256i{ emu_mm_set_epi64x( a3, a4 ), emu_mm_set_epi64x( a1, a2 ) };
    }

    PLATFORM_INLINE emu__m256d emu_mm256_setr_pd(double a1, double a2, double a3, double a4)
    {
        return emu__m256d{ _mm_setr_pd( a1, a2 ), _mm_setr_pd( a3, a4 ) };
    }

    PLATFORM_INLINE emu__m256 emu_mm256_setr_ps(float a1, float a2, float a3, float a4, float a5, float a6, float a7, float a8)
    {
        return emu__m256{ _mm_setr_ps( a1, a2, a3, a4 ), _mm_setr_ps( a5, a6, a7, a8 ) };
    }

    PLATFORM_INLINE emu__m256i emu_mm256_setr_epi8(int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7, int8_t a8,
                                                          int8_t a9, int8_t a10, int8_t a11, int8_t a12, int8_t a13, int8_t a14, int8_t a15, int8_t a16,
                                                          int8_t a17, int8_t a18, int8_t a19, int8_t a20, int8_t a21, int8_t a22, int8_t a23, int8_t a24,
                                                          int8_t a25, int8_t a26, int8_t a27, int8_t a28, int8_t a29, int8_t a30, int8_t a31, int8_t a32)
    {
        return emu__m256i{ _mm_setr_epi8( a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16 ),
                          _mm_setr_epi8( a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32 )};
    }

    PLATFORM_INLINE emu__m256i emu_mm256_setr_epi16(int16_t a1, int16_t a2, int16_t a3, int16_t a4, int16_t a5, int16_t a6, int16_t a7, int16_t a8,
                                                           int16_t a9, int16_t a10, int16_t a11, int16_t a12, int16_t a13, int16_t a14, int16_t a15, int16_t a16)
    {
        return emu__m256i{ _mm_setr_epi16( a1, a2, a3, a4, a5, a6, a7, a8 ),
                          _mm_setr_epi16( a9, a10, a11, a12, a13, a14, a15, a16 ) };
    }

    PLATFORM_INLINE emu__m256i emu_mm256_setr_epi32(int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7, int32_t a8)
    {
        return emu__m256i{ _mm_setr_epi32( a1, a2, a3, a4 ), _mm_setr_epi32( a5, a6, a7, a8 ) };
    }

    PLATFORM_INLINE emu__m256i emu_mm256_setr_epi64x(int64_t a1, int64_t a2, int64_t a3, int64_t a4)
    {
        return emu__m256i{ emu_mm_set_epi64x( a2, a1 ), emu_mm_set_epi64x( a4, a3 ) };
    }

    __EMU_M256_IMPL_M1P_DUP( __m256d, double, set1_pd );
    __EMU_M256_IMPL_M1P_DUP( __m256, float, set1_ps );
    __EMU_M256_IMPL_M1P_DUP( __m256i, int8_t, set1_epi8 );
    __EMU_M256_IMPL_M1P_DUP( __m256i, int16_t, set1_epi16 );
    __EMU_M256_IMPL_M1P_DUP( __m256i, int32_t, set1_epi32 );

    PLATFORM_INLINE emu__m256i emu_mm256_set1_epi64x(int64_t a)
    {
        int64_t res[4] = { a, a, a, a };
        return *((emu__m256i*)res);
    }

    /*
     * Support intrinsics to do vector type casts. These intrinsics do not introduce
     * extra moves to generated code. When cast is done from a 128 to 256-bit type
     * the low 128 bits of the 256-bit result contain source parameter value; the
     * upper 128 bits of the result are undefined
     */
    __EMU_M256_IMPL_M1_RET( __m256, __m256d, castpd_ps );
    __EMU_M256_IMPL_M1_RET( __m256d, __m256, castps_pd );

    __EMU_M256_IMPL_M1_RET_NAME( __m256i, __m256, castps_si128, castps_si256 );
    __EMU_M256_IMPL_M1_RET_NAME( __m256i, __m256d, castpd_si128, castpd_si256 );

    __EMU_M256_IMPL_M1_RET_NAME( __m256, __m256i, castsi128_ps, castsi256_ps );
    __EMU_M256_IMPL_M1_RET_NAME( __m256d, __m256i, castsi128_pd, castsi256_pd );

    PLATFORM_INLINE __m128 emu_mm256_castps256_ps128(const emu__m256& a)
    {
        return ( a.emu_m128[0] );
    }

    PLATFORM_INLINE __m128d emu_mm256_castpd256_pd128(const emu__m256d& a)
    {
        return ( a.emu_m128[0] );
    }

    PLATFORM_INLINE __m128i emu_mm256_castsi256_si128(const emu__m256i& a)
    {
        return ( a.emu_m128[0] );
    }

    PLATFORM_INLINE emu__m256 emu_mm256_castps128_ps256(const __m128& a)
    {
        return emu__m256{ a, _mm_setzero_ps() };
    }

    PLATFORM_INLINE emu__m256d emu_mm256_castpd128_pd256(const __m128d& a)
    {
        return emu__m256d{ a, _mm_setzero_pd() };
    }

    PLATFORM_INLINE emu__m256i emu_mm256_castsi128_si256(const __m128i& a)
    {
        return emu__m256i{ a, _mm_setzero_si128() };
    }

    #undef _mm256_add_pd
    #undef _mm256_add_ps
    #define _mm256_add_pd emu_mm256_add_pd
    #define _mm256_add_ps emu_mm256_add_ps

    #undef _mm256_addsub_pd
    #undef _mm256_addsub_ps
    #define _mm256_addsub_pd emu_mm256_addsub_pd
    #define _mm256_addsub_ps emu_mm256_addsub_ps

    #undef _mm256_and_pd
    #undef _mm256_and_ps
    #define _mm256_and_pd emu_mm256_and_pd
    #define _mm256_and_ps emu_mm256_and_ps

    #undef _mm256_andnot_pd
    #undef _mm256_andnot_ps
    #define _mm256_andnot_pd emu_mm256_andnot_pd
    #define _mm256_andnot_ps emu_mm256_andnot_ps

    #undef _mm256_blend_pd
    #undef _mm256_blend_ps
    #define _mm256_blend_pd emu_mm256_blend_pd
    #define _mm256_blend_ps emu_mm256_blend_ps

    #undef _mm256_blendv_pd
    #undef _mm256_blendv_ps
    #define _mm256_blendv_pd emu_mm256_blendv_pd
    #define _mm256_blendv_ps emu_mm256_blendv_ps

    #undef _mm256_div_pd
    #undef _mm256_div_ps
    #define _mm256_div_pd emu_mm256_div_pd
    #define _mm256_div_ps emu_mm256_div_ps

    #undef _mm256_dp_ps
    #define _mm256_dp_ps emu_mm256_dp_ps

    #undef _mm256_hadd_pd
    #undef _mm256_hadd_ps
    #define _mm256_hadd_pd emu_mm256_hadd_pd
    #define _mm256_hadd_ps emu_mm256_hadd_ps

    #undef _mm256_hsub_pd
    #undef _mm256_hsub_ps
    #define _mm256_hsub_pd emu_mm256_hsub_pd
    #define _mm256_hsub_ps emu_mm256_hsub_ps

    #undef _mm256_max_pd
    #undef _mm256_max_ps
    #define _mm256_max_pd emu_mm256_max_pd
    #define _mm256_max_ps emu_mm256_max_ps

    #undef _mm256_min_pd
    #undef _mm256_min_ps
    #define _mm256_min_pd emu_mm256_min_pd
    #define _mm256_min_ps emu_mm256_min_ps

    #undef _mm256_mul_pd
    #undef _mm256_mul_ps
    #define _mm256_mul_pd emu_mm256_mul_pd
    #define _mm256_mul_ps emu_mm256_mul_ps

    #undef _mm256_or_pd
    #undef _mm256_or_ps
    #define _mm256_or_pd emu_mm256_or_pd
    #define _mm256_or_ps emu_mm256_or_ps

    #undef _mm256_shuffle_pd
    #undef _mm256_shuffle_ps
    #define _mm256_shuffle_pd emu_mm256_shuffle_pd
    #define _mm256_shuffle_ps emu_mm256_shuffle_ps

    #undef _mm256_sub_pd
    #undef _mm256_sub_ps
    #define _mm256_sub_pd emu_mm256_sub_pd
    #define _mm256_sub_ps emu_mm256_sub_ps

    #undef _mm256_xor_pd
    #undef _mm256_xor_ps
    #define _mm256_xor_pd emu_mm256_xor_pd
    #define _mm256_xor_ps emu_mm256_xor_ps

    #undef _mm_cmp_pd
    #undef _mm256_cmp_pd
    #define _mm_cmp_pd(a, b, imm) emu_mm_cmp_pd<imm>(a, b)
    #define _mm256_cmp_pd emu_mm256_cmp_pd

    #undef _mm_cmp_ps
    #undef _mm256_cmp_ps
    #define _mm_cmp_ps(a, b, imm) emu_mm_cmp_ps<imm>(a, b)
    #define _mm256_cmp_ps emu_mm256_cmp_ps

    #undef _mm256_cvtepi32_pd
    #undef _mm256_cvtepi32_ps
    #define _mm256_cvtepi32_pd emu_mm256_cvtepi32_pd
    #define _mm256_cvtepi32_ps emu_mm256_cvtepi32_ps

    #undef _mm256_cvtpd_ps
    #undef _mm256_cvtps_epi32
    #undef _mm256_cvtps_pd
    #define _mm256_cvtpd_ps emu_mm256_cvtpd_ps
    #define _mm256_cvtps_epi32 emu_mm256_cvtps_epi32
    #define _mm256_cvtps_pd emu_mm256_cvtps_pd

    #undef _mm256_cvttpd_epi32
    #undef _mm256_cvtpd_epi32
    #undef _mm256_cvttps_epi32
    #define _mm256_cvttpd_epi32 emu_mm256_cvttpd_epi32
    #define _mm256_cvtpd_epi32 emu_mm256_cvtpd_epi32
    #define _mm256_cvttps_epi32 emu_mm256_cvttps_epi32

    #undef _mm256_extractf128_ps
    #undef _mm256_extractf128_pd
    #undef _mm256_extractf128_si256
    #define _mm256_extractf128_ps emu_mm256_extractf128_ps
    #define _mm256_extractf128_pd emu_mm256_extractf128_pd
    #define _mm256_extractf128_si256 emu_mm256_extractf128_si256

    #undef _mm256_zeroall
    #undef _mm256_zeroupper
    #define _mm256_zeroall emu_mm256_zeroall
    #define _mm256_zeroupper emu_mm256_zeroupper

    #undef _mm256_permutevar_ps
    #undef _mm_permutevar_ps
    #define _mm256_permutevar_ps emu_mm256_permutevar_ps
    #define _mm_permutevar_ps emu_mm_permutevar_ps

    #undef _mm256_permute_ps
    #undef _mm_permute_ps
    #define _mm256_permute_ps emu_mm256_permute_ps
    #define _mm_permute_ps emu_mm_permute_ps

    #undef _mm256_permutevar_pd
    #undef _mm_permutevar_pd
    #define _mm256_permutevar_pd emu_mm256_permutevar_pd
    #define _mm_permutevar_pd emu_mm_permutevar_pd

    #undef _mm256_permute_pd
    #undef _mm_permute_pd
    #define _mm256_permute_pd emu_mm256_permute_pd
    #define _mm_permute_pd emu_mm_permute_pd

    #undef _mm256_permute2f128_ps
    #undef _mm256_permute2f128_pd
    #undef _mm256_permute2f128_si256
    #define _mm256_permute2f128_ps emu_mm256_permute2f128_ps
    #define _mm256_permute2f128_pd emu_mm256_permute2f128_pd
    #define _mm256_permute2f128_si256 emu_mm256_permute2f128_si256

    #undef _mm256_broadcast_ss
    #undef _mm_broadcast_ss
    #define _mm256_broadcast_ss emu_mm256_broadcast_ss
    #define _mm_broadcast_ss(x) _mm_set1_ps(*(x))

    #undef _mm256_broadcast_sd
    //#define _mm256_broadcast_sd(x) emu_mm256_set1_pd(*(x))
    #define _mm256_broadcast_sd emu_mm256_broadcast_sd

    #undef _mm256_broadcast_ps
    #undef _mm256_broadcast_pd
    #define _mm256_broadcast_ps emu_mm256_broadcast_ps
    #define _mm256_broadcast_pd emu_mm256_broadcast_pd

    #undef _mm256_insertf128_ps
    #undef _mm256_insertf128_pd
    #undef _mm256_insertf128_si256
    #define _mm256_insertf128_ps emu_mm256_insertf128_ps
    #define _mm256_insertf128_pd emu_mm256_insertf128_pd
    #define _mm256_insertf128_si256 emu_mm256_insertf128_si256

    #undef _mm256_load_pd
    #undef _mm256_store_pd
    #undef _mm256_load_ps
    #undef _mm256_store_ps
    #define _mm256_load_pd emu_mm256_load_pd
    #define _mm256_store_pd emu_mm256_store_pd
    #define _mm256_load_ps emu_mm256_load_ps
    #define _mm256_store_ps emu_mm256_store_ps

    #undef _mm256_loadu_pd
    #undef _mm256_storeu_pd
    #undef _mm256_loadu_ps
    #undef _mm256_storeu_ps
    #define _mm256_loadu_pd emu_mm256_loadu_pd
    #define _mm256_storeu_pd emu_mm256_storeu_pd
    #define _mm256_loadu_ps emu_mm256_loadu_ps
    #define _mm256_storeu_ps emu_mm256_storeu_ps

    #undef _mm256_load_si256
    #undef _mm256_store_si256
    #undef _mm256_loadu_si256
    #undef _mm256_storeu_si256
    #define _mm256_load_si256 emu_mm256_load_si256
    #define _mm256_store_si256 emu_mm256_store_si256
    #define _mm256_loadu_si256 emu_mm256_loadu_si256
    #define _mm256_storeu_si256 emu_mm256_storeu_si256

    #undef _mm256_maskload_pd
    #undef _mm256_maskstore_pd
    #undef _mm_maskload_pd
    #undef _mm_maskstore_pd
    #define _mm256_maskload_pd emu_mm256_maskload_pd
    #define _mm256_maskstore_pd emu_mm256_maskstore_pd
    #define _mm_maskload_pd emu_mm_maskload_pd
    #define _mm_maskstore_pd emu_mm_maskstore_pd

    #undef _mm256_maskload_ps
    #undef _mm256_maskstore_ps
    #undef _mm_maskload_ps
    #undef _mm_maskstore_ps
    #define _mm256_maskload_ps emu_mm256_maskload_ps
    #define _mm256_maskstore_ps emu_mm256_maskstore_ps
    #define _mm_maskload_ps emu_mm_maskload_ps
    #define _mm_maskstore_ps emu_mm_maskstore_ps

    #undef _mm256_movehdup_ps
    #undef _mm256_moveldup_ps
    #define _mm256_movehdup_ps emu_mm256_movehdup_ps
    #define _mm256_moveldup_ps emu_mm256_moveldup_ps

    #undef _mm256_movedup_pd
    #undef _mm256_lddqu_si256
    #define _mm256_movedup_pd emu_mm256_movedup_pd
    #define _mm256_lddqu_si256 emu_mm256_lddqu_si256

    #undef _mm256_stream_si256
    #undef _mm256_stream_pd
    #undef _mm256_stream_ps
    #define _mm256_stream_si256 emu_mm256_stream_si256
    #define _mm256_stream_pd emu_mm256_stream_pd
    #define _mm256_stream_ps emu_mm256_stream_ps

    #undef _mm256_rcp_ps
    #undef _mm256_rsqrt_ps
    #define _mm256_rcp_ps emu_mm256_rcp_ps
    #define _mm256_rsqrt_ps emu_mm256_rsqrt_ps

    #undef _mm256_sqrt_pd
    #undef _mm256_sqrt_ps
    #define _mm256_sqrt_pd emu_mm256_sqrt_pd
    #define _mm256_sqrt_ps emu_mm256_sqrt_ps

    #undef _mm256_round_pd
    #define _mm256_round_pd emu_mm256_round_pd

    #undef _mm256_round_ps
    #define _mm256_round_ps emu_mm256_round_ps

    #undef _mm256_unpackhi_pd
    #undef _mm256_unpackhi_ps
    #define _mm256_unpackhi_pd emu_mm256_unpackhi_pd
    #define _mm256_unpackhi_ps emu_mm256_unpackhi_ps

    #undef _mm256_unpacklo_pd
    #undef _mm256_unpacklo_ps
    #define _mm256_unpacklo_pd emu_mm256_unpacklo_pd
    #define _mm256_unpacklo_ps emu_mm256_unpacklo_ps

    #undef _mm256_testz_si256
    #undef _mm256_testc_si256
    #undef _mm256_testnzc_si256
    #define _mm256_testz_si256 emu_mm256_testz_si256
    #define _mm256_testc_si256 emu_mm256_testc_si256
    #define _mm256_testnzc_si256 emu_mm256_testnzc_si256

    #undef _mm256_testz_pd
    #undef _mm256_testc_pd
    #undef _mm256_testnzc_pd
    #undef _mm_testz_pd
    #undef _mm_testc_pd
    #undef _mm_testnzc_pd
    #define _mm256_testz_pd emu_mm256_testz_pd
    #define _mm256_testc_pd emu_mm256_testc_pd
    #define _mm256_testnzc_pd emu_mm256_testnzc_pd
    #define _mm_testz_pd emu_mm_testz_pd
    #define _mm_testc_pd emu_mm_testc_pd
    #define _mm_testnzc_pd emu_mm_testnzc_pd

    #undef _mm256_testz_ps
    #undef _mm256_testc_ps
    #undef _mm256_testnzc_ps
    #undef _mm_testz_ps
    #undef _mm_testc_ps
    #undef _mm_testnzc_ps
    #define _mm256_testz_ps emu_mm256_testz_ps
    #define _mm256_testc_ps emu_mm256_testc_ps
    #define _mm256_testnzc_ps emu_mm256_testnzc_ps
    #define _mm_testz_ps emu_mm_testz_ps
    #define _mm_testc_ps emu_mm_testc_ps
    #define _mm_testnzc_ps emu_mm_testnzc_ps

    #undef _mm256_movemask_pd
    #undef _mm256_movemask_ps
    #define _mm256_movemask_pd emu_mm256_movemask_pd
    #define _mm256_movemask_ps emu_mm256_movemask_ps

    #undef _mm256_setzero_pd
    #undef _mm256_setzero_ps
    #undef _mm256_setzero_si256
    #define _mm256_setzero_pd emu_mm256_setzero_pd
    #define _mm256_setzero_ps emu_mm256_setzero_ps
    #define _mm256_setzero_si256 emu_mm256_setzero_si256

    #undef _mm256_set_pd
    #undef _mm256_set_ps
    #undef _mm256_set_epi8
    #undef _mm256_set_epi16
    #undef _mm256_set_epi32
    #undef _mm256_set_epi64x
    #define _mm256_set_pd emu_mm256_set_pd
    #define _mm256_set_ps emu_mm256_set_ps
    #define _mm256_set_epi8 emu_mm256_set_epi8
    #define _mm256_set_epi16 emu_mm256_set_epi16
    #define _mm256_set_epi32 emu_mm256_set_epi32
    #define _mm256_set_epi64x emu_mm256_set_epi64x

    #undef _mm256_setr_pd
    #undef _mm256_setr_ps
    #undef _mm256_setr_epi8
    #undef _mm256_setr_epi16
    #undef _mm256_setr_epi32
    #undef _mm256_setr_epi64x
    #define _mm256_setr_pd emu_mm256_setr_pd
    #define _mm256_setr_ps emu_mm256_setr_ps
    #define _mm256_setr_epi8 emu_mm256_setr_epi8
    #define _mm256_setr_epi16 emu_mm256_setr_epi16
    #define _mm256_setr_epi32 emu_mm256_setr_epi32
    #define _mm256_setr_epi64x emu_mm256_setr_epi64x

    #undef _mm256_set1_pd
    #undef _mm256_set1_ps
    #undef _mm256_set1_epi8
    #undef _mm256_set1_epi16
    #undef _mm256_set1_epi32
    #undef _mm256_set1_epi64x
    #define _mm256_set1_pd emu_mm256_set1_pd
    #define _mm256_set1_ps emu_mm256_set1_ps
    #define _mm256_set1_epi8 emu_mm256_set1_epi8
    #define _mm256_set1_epi16 emu_mm256_set1_epi16
    #define _mm256_set1_epi32 emu_mm256_set1_epi32
    #define _mm256_set1_epi64x emu_mm256_set1_epi64x

    #undef _mm256_castpd_ps
    #undef _mm256_castps_pd
    #undef _mm256_castps_si256
    #undef _mm256_castpd_si256
    #undef _mm256_castsi256_ps
    #undef _mm256_castsi256_pd
    #undef _mm256_castps256_ps128
    #undef _mm256_castpd256_pd128
    #undef _mm256_castsi256_si128
    #undef _mm256_castps128_ps256
    #undef _mm256_castpd128_pd256
    #undef _mm256_castsi128_si256
    #define _mm256_castpd_ps emu_mm256_castpd_ps
    #define _mm256_castps_pd emu_mm256_castps_pd
    #define _mm256_castps_si256 emu_mm256_castps_si256
    #define _mm256_castpd_si256 emu_mm256_castpd_si256
    #define _mm256_castsi256_ps emu_mm256_castsi256_ps
    #define _mm256_castsi256_pd emu_mm256_castsi256_pd
    #define _mm256_castps256_ps128 emu_mm256_castps256_ps128
    #define _mm256_castpd256_pd128 emu_mm256_castpd256_pd128
    #define _mm256_castsi256_si128 emu_mm256_castsi256_si128
    #define _mm256_castps128_ps256 emu_mm256_castps128_ps256
    #define _mm256_castpd128_pd256 emu_mm256_castpd128_pd256
    #define _mm256_castsi128_si256 emu_mm256_castsi128_si256

    typedef emu__m256 v8f;
    typedef emu__m256i v8i;
    typedef emu__m256d v4d;

#else

    typedef __m256 v8f;
    typedef __m256i v8i;
    typedef __m256d v4d;

#endif

//======================================================================================================================
// AVX2
//======================================================================================================================

#if( PLATFORM_INTRINSIC < PLATFORM_INTRINSIC_AVX2 )

    PLATFORM_INLINE v4d emu_mm256_permute4x64_pd(const v4d& x, int32_t imm)
    {
        DEBUG_Assert( imm >= 0 && imm <= PLATFORM_MAX_UCHAR );

        v4d r;
        const double* src = (double const*)&x;
        double* dst = (double*)&r;

        dst[0] = src[(imm >> 0) & 0x3];
        dst[1] = src[(imm >> 2) & 0x3];
        dst[2] = src[(imm >> 4) & 0x3];
        dst[3] = src[(imm >> 6) & 0x3];

        return r;
    }

    #undef _mm256_cvtepi32_epi64
    #undef _mm256_permute4x64_pd
    #define _mm256_cvtepi32_epi64(a) _mm256_castpd_si256(_mm256_cmp_pd(_mm256_cvtepi32_pd(_mm_and_si128(a, _mm_set1_epi32(1))), _mm256_set1_pd(1.0), _CMP_EQ_OQ))
    #define _mm256_permute4x64_pd emu_mm256_permute4x64_pd

    #undef _mm_fmadd_ps
    #undef _mm_fmsub_ps
    #undef _mm_fnmadd_ps
    #define _mm_fmadd_ps(a, b, c) _mm_add_ps(_mm_mul_ps(a, b), c)
    #define _mm_fmsub_ps(a, b, c) _mm_sub_ps(_mm_mul_ps(a, b), c)
    #define _mm_fnmadd_ps(a, b, c) _mm_sub_ps(c, _mm_mul_ps(a, b))

    #undef _mm256_fmadd_pd
    #undef _mm256_fmsub_pd
    #undef _mm256_fnmadd_pd
    #define _mm256_fmadd_pd(a, b, c) _mm256_add_pd(_mm256_mul_pd(a, b), c)
    #define _mm256_fmsub_pd(a, b, c) _mm256_sub_pd(_mm256_mul_pd(a, b), c)
    #define _mm256_fnmadd_pd(a, b, c) _mm256_sub_pd(c, _mm256_mul_pd(a, b))

#endif

//======================================================================================================================
// V4F
//======================================================================================================================

const float c_fEps                                      = 1.1920928955078125e-7f; // pow(2, -23)
const float c_fInf                                      = -logf(0.0f);

const v4f c_v4f_Inf                                     = _mm_set1_ps(c_fInf);
const v4f c_v4f_InfMinus                                = _mm_set1_ps(-c_fInf);
const v4f c_v4f_0001                                    = _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f);
const v4f c_v4f_1111                                    = _mm_set1_ps(1.0f);
const v4f c_v4f_Sign                                    = _mm_castsi128_ps(_mm_set1_epi32(0x80000000));
const v4f c_v4f_FFF0                                    = _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000));

#define v4f_mask_dp(xi, yi, zi, wi, xo, yo, zo, wo)     (xo | (yo << 1) | (zo << 2) | (wo << 3) | (xi << 4) | (yi << 5) | (zi << 6) | (wi << 7))
#define v4f_mask_dp4                                    v4f_mask_dp(1, 1, 1, 1, 1, 1, 1, 1)
#define v4f_mask_dp3                                    v4f_mask_dp(1, 1, 1, 0, 1, 1, 1, 1)

#define v4f_mask(x, y, z, w)                            (x | (y << 1) | (z << 2) | (w << 3))
#define v4f_mask_x                                      v4f_mask(1, 0, 0, 0)
#define v4f_mask_xy                                     v4f_mask(1, 1, 0, 0)
#define v4f_mask_xyz                                    v4f_mask(1, 1, 1, 0)
#define v4f_mask_xyzw                                   v4f_mask(1, 1, 1, 1)

#define v4f_set(x, y, z, w)                          _mm_setr_ps(x, y, z, w)
#define v4f_zero                                        _mm_setzero_ps()
#define v4f_setw1(x)                                    _mm_or_ps(_mm_and_ps(x, c_v4f_FFF0), c_v4f_0001)
#define v4f_setw0(x)                                    _mm_and_ps(x, c_v4f_FFF0)

#define v4f_test1_none(v)                               ((_mm_movemask_ps(v) & v4f_mask_x) == 0)
#define v4f_test1_all(v)                                ((_mm_movemask_ps(v) & v4f_mask_x) != 0)

#define v4f_bits3(v)                                    (_mm_movemask_ps(v) & v4f_mask_xyz)
#define v4f_test3(v, x, y, z)                           (v4f_bits3(v) == v4f_mask(x, y, z, 0))
#define v4f_test3_all(v)                                (v4f_bits3(v) == v4f_mask_xyz)
#define v4f_test3_none(v)                               (v4f_bits3(v) == 0)
#define v4f_test3_any(v)                                (v4f_bits3(v) != 0)

#define v4f_bits4(v)                                    _mm_movemask_ps(v)
#define v4f_test4(v, x, y, z, w)                        (v4f_bits4(v) == v4f_mask(x, y, z, w))
#define v4f_test4_all(v)                                (v4f_bits4(v) == v4f_mask_xyzw)
#define v4f_test4_none(v)                               (v4f_bits4(v) == 0)
#define v4f_test4_any(v)                                (v4f_bits4(v) != 0)

// NOTE: < 0

#define v4f_isnegative1_all(v)                          v4f_test1_all(v)
#define v4f_isnegative3_all(v)                          v4f_test3_all(v)
#define v4f_isnegative4_all(v)                          v4f_test4_all(v)

// NOTE: >= 0

#define v4f_ispositive1_all(v)                          v4f_test1_none(v)
#define v4f_ispositive3_all(v)                          v4f_test3_none(v)
#define v4f_ispositive4_all(v)                          v4f_test4_none(v)

#define v4f_swizzle(v, x, y, z, w)                      _mm_permute_ps(v, _MM_SHUFFLE(w, z, y, x))

#define v4f_shuffle(v0, v1, i0, j0, i1, j1)             _mm_shuffle_ps(v0, v1, _MM_SHUFFLE(j1, i1, j0, i0))

#define v4f_Azw_Bzw(a, b)                               _mm_movehl_ps(a, b)
#define v4f_Axy_Bxy(a, b)                               _mm_movelh_ps(a, b)
#define v4f_Ax_Byzw(a, b)                               _mm_move_ss(b, a)
#define v4f_Az_Bz_Aw_Bw(a, b)                           _mm_unpackhi_ps(a, b)
#define v4f_Ax_Bx_Ay_By(a, b)                           _mm_unpacklo_ps(a, b)

#define v4f_get_x(x)                                    _mm_cvtss_f32(x)
#define v4f_store_x(ptr, x)                             _mm_store_ss(ptr, x)

#define v4f_negate_comp(v, x, y, z, w)                  _mm_xor_ps(v, _mm_castsi128_ps(_mm_setr_epi32(x ? 0x80000000 : 0, y ? 0x80000000 : 0, z ? 0x80000000 : 0, w ? 0x80000000 : 0)))
#define v4f_negate(v)                                   _mm_xor_ps(v, c_v4f_Sign)
#define v4f_abs(v)                                      _mm_andnot_ps(c_v4f_Sign, v)

#define v4f_greater(a, b)                               _mm_cmpgt_ps(a, b)
#define v4f_less(a, b)                                  _mm_cmplt_ps(a, b)
#define v4f_gequal(a, b)                                _mm_cmpge_ps(a, b)
#define v4f_lequal(a, b)                                _mm_cmple_ps(a, b)
#define v4f_equal(a, b)                                 _mm_cmpeq_ps(a, b)
#define v4f_notequal(a, b)                              _mm_cmpneq_ps(a, b)

#define v4f_greater0_all(a)                             v4f_test4_all( v4f_greater(a, v4f_zero) )
#define v4f_gequal0_all(a)                              v4f_test4_all( v4f_gequal(a, v4f_zero) )
#define v4f_not0_all(a)                                 v4f_test4_all( v4f_notequal(a, v4f_zero) )

#define v4f_rsqrt_(a)                                   _mm_rsqrt_ps(a)
#define v4f_rcp_(a)                                     _mm_rcp_ps(a)

#define v4f_hadd(a, b)                                  _mm_hadd_ps(a, b)
#define v4f_dot33(a, b)                                 _mm_dp_ps(a, b, v4f_mask_dp3)
#define v4f_dot44(a, b)                                 _mm_dp_ps(a, b, v4f_mask_dp4)
#define v4f_dot43(a, b)                                 _mm_dp_ps(a, v4f_setw1(b), v4f_mask_dp4)

#define v4f_round(x)                                    _mm_round_ps(x, _MM_FROUND_TO_NEAREST_INT | ROUNDING_EXEPTIONS_MASK)
#define v4f_floor(x)                                    _mm_round_ps(x, _MM_FROUND_FLOOR | ROUNDING_EXEPTIONS_MASK)
#define v4f_ceil(x)                                     _mm_round_ps(x, _MM_FROUND_CEIL | ROUNDING_EXEPTIONS_MASK)

#define v4f_select(a, b, mask)                          _mm_blendv_ps(a, b, mask)

#define v4f_madd(a, b, c)                               _mm_fmadd_ps(a, b, c)
#define v4f_msub(a, b, c)                               _mm_fmsub_ps(a, b, c)
#define v4f_nmadd(a, b, c)                              _mm_fnmadd_ps(a, b, c)

#define _v4f_vselecti(mask, x, y)                       v4f_select(y, x, _mm_castsi128_ps(mask))
#define _v4f_vselect(mask, x, y)                        v4f_select(y, x, mask)
#define _v4f_iselect(mask, x, y)                        _mm_castps_si128(v4f_select(_mm_castsi128_ps(y), _mm_castsi128_ps(x), mask))
#define _v4f_mulsign(x, y)                              _mm_xor_ps(x, _mm_and_ps(y, c_v4f_Sign))
#define _v4f_negatei(x)                                 _mm_sub_epi32(_mm_setzero_si128(), x)
#define _v4f_is_inf(x)                                  v4f_equal(v4f_abs(x), c_v4f_Inf)
#define _v4f_is_pinf(x)                                 v4f_equal(x, c_v4f_Inf)
#define _v4f_is_ninf(x)                                 v4f_equal(x, c_v4f_InfMinus)
#define _v4f_is_nan(x)                                  v4f_notequal(x, x)
#define _v4f_is_inf2(x, y)                              _mm_and_ps(_v4f_is_inf(x), _mm_or_ps(_mm_and_ps(x, c_v4f_Sign), y))

#ifdef MATH_CHECK_W_IS_ZERO

    PLATFORM_INLINE bool v4f_is_w_zero(const v4f& x)
    {
        v4f t = v4f_equal(x, v4f_zero);

        return (v4f_bits4(t) & v4f_mask(0, 0, 0, 1)) == v4f_mask(0, 0, 0, 1);
    }

#else

    #define v4f_is_w_zero(x)        (true)

#endif

PLATFORM_INLINE v4f v4f_sqrt(const v4f& r)
{
    DEBUG_Assert( v4f_gequal0_all(r) );

    return _mm_sqrt_ps(r);
}

PLATFORM_INLINE v4f v4f_sign(const v4f& x)
{
    // NOTE: 1 for +0, -1 for -0

    v4f v = _mm_and_ps(x, c_v4f_Sign);

    return _mm_or_ps(v, c_v4f_1111);
}

PLATFORM_INLINE v4f v4f_fract(const v4f& x)
{
    v4f flr0 = v4f_floor(x);
    v4f sub0 = _mm_sub_ps(x, flr0);

    return sub0;
}

PLATFORM_INLINE v4f v4f_clamp(const v4f& x, const v4f& vmin, const v4f& vmax)
{
    v4f min0 = _mm_min_ps(x, vmax);

    return _mm_max_ps(min0, vmin);
}

PLATFORM_INLINE v4f v4f_saturate(const v4f& x)
{
    v4f min0 = _mm_min_ps(x, c_v4f_1111);

    return _mm_max_ps(min0, v4f_zero);
}

PLATFORM_INLINE v4f v4f_step(const v4f& edge, const v4f& x)
{
    v4f cmp = v4f_gequal(x, edge);

    return _mm_and_ps(c_v4f_1111, cmp);
}

PLATFORM_INLINE v4f v4f_linearstep(const v4f& edge0, const v4f& edge1, const v4f& x)
{
    v4f sub0 = _mm_sub_ps(x, edge0);
    v4f sub1 = _mm_sub_ps(edge1, edge0);
    v4f div0 = _mm_div_ps(sub0, sub1);

    return v4f_saturate(div0);
}

PLATFORM_INLINE v4f v4f_length(const v4f& x)
{
    v4f r = v4f_dot33(x, x);

    return _mm_sqrt_ps(r);
}

#if( PLATFORM_INTRINSIC >= PLATFORM_INTRINSIC_AVX1 )
PLATFORM_INLINE v4i v4f_to_h4(const v4f& x)
{
    #pragma warning(push)
    #pragma warning(disable : 4556)

    return _mm_cvtps_ph(x, _MM_FROUND_TO_NEAREST_INT | ROUNDING_EXEPTIONS_MASK);

    #pragma warning(pop)
}
#endif

PLATFORM_INLINE v4i xmmi_select(const v4i& x, const v4i& y, const v4i& mask)
{
    return _mm_or_si128(_mm_and_si128(mask, x), _mm_andnot_si128(mask, y));
}

PLATFORM_INLINE v4f v4f_cross(const v4f& x, const v4f& y)
{
    v4f a = v4f_swizzle(x, 1, 2, 0, 3);
    v4f b = v4f_swizzle(y, 2, 0, 1, 3);
    v4f c = v4f_swizzle(x, 2, 0, 1, 3);
    v4f d = v4f_swizzle(y, 1, 2, 0, 3);

    c = _mm_mul_ps(c, d);

    return v4f_msub(a, b, c);
}

PLATFORM_INLINE v4f v4f_rsqrt(const v4f& r)
{
    DEBUG_Assert( v4f_greater0_all(r) );

    #ifdef MATH_NEWTONRAPHSON_APROXIMATION

        v4f c = v4f_rsqrt_(r);
        v4f a = _mm_mul_ps(c, _mm_set1_ps(0.5f));
        v4f t = _mm_mul_ps(r, c);
        v4f b = v4f_nmadd(t, c, _mm_set1_ps(3.0f));

        return _mm_mul_ps(a, b);

    #else

        return v4f_rsqrt_(r);

    #endif
}

PLATFORM_INLINE v4f v4f_rcp(const v4f& r)
{
    DEBUG_Assert( v4f_not0_all(r) );

    #ifdef MATH_NEWTONRAPHSON_APROXIMATION

        v4f c = v4f_rcp_(r);
        v4f a = _mm_mul_ps(c, r);
        v4f b = _mm_add_ps(c, c);

        return v4f_nmadd(a, c, b);

    #else

        return v4f_rcp_(r);

    #endif
}

PLATFORM_INLINE v4f v4f_mod(const v4f& x, const v4f& y)
{
    v4f div = _mm_div_ps(x, y);
    v4f flr = v4f_floor(div);

    return v4f_nmadd(y, flr, x);
}

PLATFORM_INLINE v4f v4f_mix(const v4f& a, const v4f& b, const v4f& x)
{
    v4f sub0 = _mm_sub_ps(b, a);

    return v4f_madd(sub0, x, a);
}

PLATFORM_INLINE v4f v4f_smoothstep(const v4f& edge0, const v4f& edge1, const v4f& x)
{
    v4f b = v4f_linearstep(edge0, edge1, x);
    v4f c = v4f_nmadd(_mm_set1_ps(2.0f), b, _mm_set1_ps(3.0f));
    v4f t = _mm_mul_ps(b, b);

    return _mm_mul_ps(t, c);
}

PLATFORM_INLINE v4f v4f_normalize(const v4f& x)
{
    v4f r = v4f_dot33(x, x);
    r = v4f_rsqrt(r);

    return _mm_mul_ps(x, r);
}

//======================================================================================================================
// V4D
//======================================================================================================================

const double c_dEps                                     = 1.11022302462515654042e-16; // pow(2, -52)
const double c_dInf                                     = -log(0.0);

const v4d c_v4d_Inf                                     = _mm256_set1_pd(c_dInf);
const v4d c_v4d_InfMinus                                = _mm256_set1_pd(-c_dInf);
const v4d c_v4d_0001                                    = _mm256_setr_pd(0.0, 0.0, 0.0, 1.0);
const v4d c_v4d_1111                                    = _mm256_set1_pd(1.0);
const v4d c_v4d_Sign                                    = _mm256_castsi256_pd(_mm256_set_epi32(0x80000000, 0x00000000, 0x80000000, 0x00000000, 0x80000000, 0x00000000, 0x80000000, 0x00000000));
const v4d c_v4d_FFF0                                    = _mm256_castsi256_pd(_mm256_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000));

#define v4d_mask_dp(xi, yi, zi, wi, xo, yo, zo, wo)     (xo | (yo << 1) | (zo << 2) | (wo << 3) | (xi << 4) | (yi << 5) | (zi << 6) | (wi << 7))
#define v4d_mask_dp4                                    v4d_mask_dp(1, 1, 1, 1, 1, 1, 1, 1)
#define v4d_mask_dp3                                    v4d_mask_dp(1, 1, 1, 0, 1, 1, 1, 1)

#define v4d_mask(x, y, z, w)                            (x | (y << 1) | (z << 2) | (w << 3))
#define v4d_mask_x                                      v4d_mask(1, 0, 0, 0)
#define v4d_mask_xy                                     v4d_mask(1, 1, 0, 0)
#define v4d_mask_xyz                                    v4d_mask(1, 1, 1, 0)
#define v4d_mask_xyzw                                   v4d_mask(1, 1, 1, 1)

#define v4d_set(x, y, z, w)                             _mm256_setr_pd(x, y, z, w)
#define v4d_zero                                        _mm256_setzero_pd()
#define v4d_setw1(x)                                    _mm256_or_pd(_mm256_and_pd(x, c_v4d_FFF0), c_v4d_0001)
#define v4d_setw0(x)                                    _mm256_and_pd(x, c_v4d_FFF0)

#define v4d_test1_none(v)                               ((_mm256_movemask_pd(v) & v4d_mask_x) == 0)
#define v4d_test1_all(v)                                ((_mm256_movemask_pd(v) & v4d_mask_x) != 0)

#define v4d_bits3(v)                                    (_mm256_movemask_pd(v) & v4d_mask_xyz)
#define v4d_test3(v, x, y, z)                           (v4d_bits3(v) == v4d_mask(x, y, z, 0))
#define v4d_test3_all(v)                                (v4d_bits3(v) == v4d_mask_xyz)
#define v4d_test3_none(v)                               (v4d_bits3(v) == 0)
#define v4d_test3_any(v)                                (v4d_bits3(v) != 0)

#define v4d_bits4(v)                                    _mm256_movemask_pd(v)
#define v4d_test4(v, x, y, z, w)                        (v4d_bits4(v) == v4d_mask(x, y, z, w))
#define v4d_test4_all(v)                                (v4d_bits4(v) == v4d_mask_xyzw)
#define v4d_test4_none(v)                               (v4d_bits4(v) == 0)
#define v4d_test4_any(v)                                (v4d_bits4(v) != 0)

// NOTE: < 0

#define v4d_isnegative1_all(v)                          v4d_test1_all(v)
#define v4d_isnegative3_all(v)                          v4d_test3_all(v)
#define v4d_isnegative4_all(v)                          v4d_test4_all(v)

// NOTE: >= 0

#define v4d_ispositive1_all(v)                          v4d_test1_none(v)
#define v4d_ispositive3_all(v)                          v4d_test3_none(v)
#define v4d_ispositive4_all(v)                          v4d_test4_none(v)

#define v4d_Azw_Bzw(a, b)                               _mm256_permute2f128_pd(a, b, (1 << 4) | 3)
#define v4d_Axy_Bxy(a, b)                               _mm256_permute2f128_pd(a, b, (2 << 4) | 0)
#define v4d_Ay_By_Aw_Bw(a, b)                           _mm256_unpackhi_pd(a, b)
#define v4d_Ax_Bx_Az_Bz(a, b)                           _mm256_unpacklo_pd(a, b)

#define v4d_get_x(x)                                    _mm_cvtsd_f64( _mm256_extractf128_pd(x, 0) )
#define v4d_store_x(ptr, x)                             _mm_storel_pd(ptr, _mm256_extractf128_pd(x, 0))

#define v4d_negate_comp(v, x, y, z, w)                  _mm256_xor_pd(v, _mm256_castsi256_pd(_mm256_setr_epi32(x ? 0x80000000 : 0, 0, y ? 0x80000000 : 0, 0, z ? 0x80000000 : 0, 0, w ? 0x80000000 : 0, 0)))
#define v4d_negate(v)                                   _mm256_xor_pd(v, c_v4d_Sign)
#define v4d_abs(v)                                      _mm256_andnot_pd(c_v4d_Sign, v)

#define v4d_greater(a, b)                               _mm256_cmp_pd(a, b, _CMP_GT_OQ)
#define v4d_less(a, b)                                  _mm256_cmp_pd(a, b, _CMP_LT_OQ)
#define v4d_gequal(a, b)                                _mm256_cmp_pd(a, b, _CMP_GE_OQ)
#define v4d_lequal(a, b)                                _mm256_cmp_pd(a, b, _CMP_LE_OQ)
#define v4d_equal(a, b)                                 _mm256_cmp_pd(a, b, _CMP_EQ_OQ)
#define v4d_notequal(a, b)                              _mm256_cmp_pd(a, b, _CMP_NEQ_UQ)

#define v4d_greater0_all(a)                             v4d_test4_all( v4d_greater(a, v4d_zero) )
#define v4d_gequal0_all(a)                              v4d_test4_all( v4d_gequal(a, v4d_zero) )
#define v4d_not0_all(a)                                 v4d_test4_all( v4d_notequal(a, v4d_zero) )

#define v4d_rsqrt_(a)                                   _mm256_div_pd(c_v4d_1111, _mm256_sqrt_pd(a))
#define v4d_rcp_(a)                                     _mm256_div_pd(c_v4d_1111, a)

#define v4d_round(x)                                    _mm256_round_pd(x, _MM_FROUND_TO_NEAREST_INT | ROUNDING_EXEPTIONS_MASK)
#define v4d_floor(x)                                    _mm256_round_pd(x, _MM_FROUND_FLOOR | ROUNDING_EXEPTIONS_MASK)
#define v4d_ceil(x)                                     _mm256_round_pd(x, _MM_FROUND_CEIL | ROUNDING_EXEPTIONS_MASK)

#define v4d_select(a, b, mask)                          _mm256_blendv_pd(a, b, mask)

#define v4f_to_v4d(a)                                   _mm256_cvtps_pd(a)
#define v4d_to_v4f(a)                                   _mm256_cvtpd_ps(a);

// IMPORTANT: use it in some strange cases...
#define v4d_zero_upper                                  _mm256_zeroupper
#define v4d_zero_all                                    _mm256_zeroall

#define v4d_swizzle(v, x, y, z, w)                      _mm256_permute4x64_pd(v, _MM_SHUFFLE(w, z, y, x))
#define v4d_shuffle(v0, v1, i0, j0, i1, j1)             _mm256_blend_pd(_mm256_permute4x64_pd(v0, _MM_SHUFFLE(j1, i1, j0, i0)), _mm256_permute4x64_pd(v1, _MM_SHUFFLE(j1, i1, j0, i0)), 0xC)

#define v4d_madd(a, b, c)                               _mm256_fmadd_pd(a, b, c)
#define v4d_msub(a, b, c)                               _mm256_fmsub_pd(a, b, c)
#define v4d_nmadd(a, b, c)                              _mm256_fnmadd_pd(a, b, c)

#define _v4d_mulsign(x, y)                              _mm256_xor_pd(x, _mm256_and_pd(y, c_v4d_Sign))
#define _v4d_is_inf(x)                                  v4d_equal(v4d_abs(x), c_v4d_Inf)
#define _v4d_is_pinf(x)                                 v4d_equal(x, c_v4d_Inf)
#define _v4d_is_ninf(x)                                 v4d_equal(x, c_v4d_InfMinus)
#define _v4d_is_nan(x)                                  v4d_notequal(x, x)
#define _v4d_is_inf2(x, y)                              _mm256_and_pd(_v4d_is_inf(x), _mm256_or_pd(_mm256_and_pd(x, c_v4d_Sign), y))

#ifdef MATH_CHECK_W_IS_ZERO

    PLATFORM_INLINE bool v4d_is_w_zero(const v4d& x)
    {
        v4d t = v4d_equal(x, v4d_zero);

        return (v4d_bits4(t) & v4d_mask(0, 0, 0, 1)) == v4d_mask(0, 0, 0, 1);
    }

#else

    #define v4d_is_w_zero(x)        (true)

#endif

PLATFORM_INLINE v4d v4d_dot33(const v4d& a, const v4d& b)
{
    DEBUG_Assert( v4d_is_w_zero(a) && v4d_is_w_zero(b) );

    v4d r = _mm256_mul_pd(a, b);
    r = v4d_setw0(r);
    r = _mm256_hadd_pd(r, _mm256_permute2f128_pd(r, r, (0 << 4) | 3));
    r = _mm256_hadd_pd(r, r);

    return r;
}

PLATFORM_INLINE v4d v4d_dot44(const v4d& a, const v4d& b)
{
    v4d r = _mm256_mul_pd(a, b);
    r = _mm256_hadd_pd(r, _mm256_permute2f128_pd(r, r, (0 << 4) | 3));
    r = _mm256_hadd_pd(r, r);

    return r;
}

PLATFORM_INLINE v4d v4d_dot43(const v4d& a, const v4d& b)
{
    DEBUG_Assert( v4d_is_w_zero(b) );

    v4d r = v4d_setw1(b);
    r = _mm256_mul_pd(a, r);
    r = _mm256_hadd_pd(r, _mm256_permute2f128_pd(r, r, (0 << 4) | 3));
    r = _mm256_hadd_pd(r, r);

    return r;
}

PLATFORM_INLINE v4d v4d_sqrt(const v4d& r)
{
    DEBUG_Assert( v4d_gequal0_all(r) );

    return _mm256_sqrt_pd(r);
}

PLATFORM_INLINE v4d v4d_sign(const v4d& x)
{
    // NOTE: 1 for +0, -1 for -0

    v4d v = _mm256_and_pd(x, c_v4d_Sign);

    return _mm256_or_pd(v, c_v4d_1111);
}

PLATFORM_INLINE v4d v4d_fract(const v4d& x)
{
    v4d flr0 = v4d_floor(x);
    v4d sub0 = _mm256_sub_pd(x, flr0);

    return sub0;
}

PLATFORM_INLINE v4d v4d_clamp(const v4d& x, const v4d& vmin, const v4d& vmax)
{
    v4d min0 = _mm256_min_pd(x, vmax);

    return _mm256_max_pd(min0, vmin);
}

PLATFORM_INLINE v4d v4d_saturate(const v4d& x)
{
    v4d min0 = _mm256_min_pd(x, c_v4d_1111);

    return _mm256_max_pd(min0, v4d_zero);
}

PLATFORM_INLINE v4d v4d_step(const v4d& edge, const v4d& x)
{
    v4d cmp = v4d_gequal(x, edge);

    return _mm256_and_pd(c_v4d_1111, cmp);
}

PLATFORM_INLINE v4d v4d_linearstep(const v4d& edge0, const v4d& edge1, const v4d& x)
{
    v4d sub0 = _mm256_sub_pd(x, edge0);
    v4d sub1 = _mm256_sub_pd(edge1, edge0);
    v4d div0 = _mm256_div_pd(sub0, sub1);

    return v4d_saturate(div0);
}

PLATFORM_INLINE v4d v4d_length(const v4d& x)
{
    v4d r = v4d_dot33(x, x);

    return _mm256_sqrt_pd(r);
}

PLATFORM_INLINE v4d v4d_cross(const v4d& x, const v4d& y)
{
    v4d a = v4d_swizzle(x, 1, 2, 0, 3);
    v4d b = v4d_swizzle(y, 2, 0, 1, 3);
    v4d c = v4d_swizzle(x, 2, 0, 1, 3);
    v4d d = v4d_swizzle(y, 1, 2, 0, 3);

    c = _mm256_mul_pd(c, d);

    return v4d_msub(a, b, c);
}

PLATFORM_INLINE v4d v4d_rsqrt(const v4d& r)
{
    DEBUG_Assert( v4d_greater0_all(r) );

    #ifdef MATH_NEWTONRAPHSON_APROXIMATION

        v4d c = v4d_rsqrt_(r);
        v4d a = _mm256_mul_pd(c, _mm256_set1_pd(0.5));
        v4d t = _mm256_mul_pd(r, c);
        v4d b = v4d_nmadd(t, c, _mm256_set1_pd(3.0));

        return _mm256_mul_pd(a, b);

    #else

        return v4d_rsqrt_(r);

    #endif
}

PLATFORM_INLINE v4d v4d_rcp(const v4d& r)
{
    DEBUG_Assert( v4d_not0_all(r) );

    #ifdef MATH_NEWTONRAPHSON_APROXIMATION

        v4d c = v4d_rcp_(r);
        v4d a = _mm256_mul_pd(c, r);
        v4d b = _mm256_add_pd(c, c);

        return v4d_nmadd(a, c, b);

    #else

        return v4d_rcp_(r);

    #endif
}

PLATFORM_INLINE v4d v4d_mod(const v4d& x, const v4d& y)
{
    v4d div = _mm256_div_pd(x, y);
    v4d flr = v4d_floor(div);

    return v4d_nmadd(y, flr, x);
}

PLATFORM_INLINE v4d v4d_mix(const v4d& a, const v4d& b, const v4d& x)
{
    v4d sub0 = _mm256_sub_pd(b, a);

    return v4d_madd(sub0, x, a);
}

PLATFORM_INLINE v4d v4d_smoothstep(const v4d& edge0, const v4d& edge1, const v4d& x)
{
    v4d b = v4d_linearstep(edge0, edge1, x);
    v4d c = v4d_nmadd(_mm256_set1_pd(2.0), b, _mm256_set1_pd(3.0));
    v4d t = _mm256_mul_pd(b, b);

    return _mm256_mul_pd(t, c);
}

PLATFORM_INLINE v4d v4d_normalize(const v4d& x)
{
    v4d r = v4d_dot33(x, x);
    r = v4d_rsqrt(r);

    return _mm256_mul_pd(x, r);
}

//======================================================================================================================
// TRANSCENDENTAL INTRINSICS
//======================================================================================================================

// IMPORTANT: based on Sleef 2.80
// https://bitbucket.org/eschnett/vecmathlib/src
// http://shibatch.sourceforge.net/

#if( !PLATFORM_HAS_TRANSCENDENTAL_INTRINSICS )
    constexpr float Cf_PI4_A = 0.78515625f;
    constexpr float Cf_PI4_B = 0.00024187564849853515625f;
    constexpr float Cf_PI4_C = 3.7747668102383613586e-08f;
    constexpr float Cf_PI4_D = 1.2816720341285448015e-12f;
    constexpr float c_f[] =
    {
        0.31830988618379067154f,
        0.00282363896258175373077393f,
        -0.0159569028764963150024414f,
        0.0425049886107444763183594f,
        -0.0748900920152664184570312f,
        0.106347933411598205566406f,
        -0.142027363181114196777344f,
        0.199926957488059997558594f,
        -0.333331018686294555664062f,
        1.57079632679489661923f,
        5.421010862427522E-20f,
        1.8446744073709552E19f,
        -Cf_PI4_A * 4.0f,
        -Cf_PI4_B * 4.0f,
        -Cf_PI4_C * 4.0f,
        -Cf_PI4_D * 4.0f,
        2.6083159809786593541503e-06f,
        -0.0001981069071916863322258f,
        0.00833307858556509017944336f,
        -0.166666597127914428710938f,
        -Cf_PI4_A * 2.0f,
        -Cf_PI4_B * 2.0f,
        -Cf_PI4_C * 2.0f,
        -Cf_PI4_D * 2.0f,
        0.63661977236758134308f,
        -0.000195169282960705459117889f,
        0.00833215750753879547119141f,
        -0.166666537523269653320312f,
        -2.71811842367242206819355e-07f,
        2.47990446951007470488548e-05f,
        -0.00138888787478208541870117f,
        0.0416666641831398010253906f,
        -0.5f,
        1.0f,
        0.00927245803177356719970703f,
        0.00331984995864331722259521f,
        0.0242998078465461730957031f,
        0.0534495301544666290283203f,
        0.133383005857467651367188f,
        0.333331853151321411132812f,
        0.78539816339744830962f,
        -1.0f,
        0.5f,
        3.14159265358979323846f,
        0.7071f,
        0.2371599674224853515625f,
        0.285279005765914916992188f,
        0.400005519390106201171875f,
        0.666666567325592041015625f,
        2.0f,
        0.693147180559945286226764f,
        1.442695040888963407359924681001892137426645954152985934135449406931f,
        -0.693145751953125f,
        -1.428606765330187045e-06f,
        0.00136324646882712841033936f,
        0.00836596917361021041870117f,
        0.0416710823774337768554688f,
        0.166665524244308471679688f,
        0.499999850988388061523438f
    };

    PLATFORM_INLINE v4f _v4f_is_inf_or_zero(const v4f& x)
    {
        v4f t = v4f_abs(x);

        return _mm_or_ps(v4f_equal(t, v4f_zero), v4f_equal(t, c_v4f_Inf));
    }

    PLATFORM_INLINE v4f _v4f_atan2(const v4f& y, const v4f& x)
    {
        v4i q = _v4f_iselect(_mm_cmplt_ps(x, v4f_zero), _mm_set1_epi32(-2), _mm_setzero_si128());
        v4f r = v4f_abs(x);

        v4f mask = _mm_cmplt_ps(r, y);
        q = _v4f_iselect(mask, _mm_add_epi32(q, _mm_set1_epi32(1)), q);
        v4f s = _v4f_vselect(mask, v4f_negate(r), y);
        v4f t = _mm_max_ps(r, y);

        s = _mm_div_ps(s, t);
        t = _mm_mul_ps(s, s);

        v4f u = _mm_broadcast_ss(c_f + 1);
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 2));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 3));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 4));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 5));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 6));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 7));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 8));

        t = v4f_madd(s, _mm_mul_ps(t, u), s);
        t = v4f_madd(_mm_cvtepi32_ps(q), _mm_broadcast_ss(c_f + 9), t);

        return t;
    }

    PLATFORM_INLINE v4i _v4f_logbp1(const v4f& d)
    {
        v4f m = _mm_cmplt_ps(d, _mm_broadcast_ss(c_f + 10));
        v4f r = _v4f_vselect(m, _mm_mul_ps(_mm_broadcast_ss(c_f + 11), d), d);
        v4i q = _mm_and_si128(_mm_srli_epi32(_mm_castps_si128(r), 23), _mm_set1_epi32(0xff));
        q = _mm_sub_epi32(q, _v4f_iselect(m, _mm_set1_epi32(64 + 0x7e), _mm_set1_epi32(0x7e)));

        return q;
    }

    PLATFORM_INLINE v4f _v4f_ldexp(const v4f& x, const v4i& q)
    {
        v4i m = _mm_srai_epi32(q, 31);
        m = _mm_slli_epi32(_mm_sub_epi32(_mm_srai_epi32(_mm_add_epi32(m, q), 6), m), 4);
        v4i t = _mm_sub_epi32(q, _mm_slli_epi32(m, 2));
        m = _mm_add_epi32(m, _mm_set1_epi32(0x7f));
        m = _mm_and_si128(_mm_cmpgt_epi32(m, _mm_setzero_si128()), m);
        v4i n = _mm_cmpgt_epi32(m, _mm_set1_epi32(0xff));
        m = _mm_or_si128(_mm_andnot_si128(n, m), _mm_and_si128(n, _mm_set1_epi32(0xff)));
        v4f u = _mm_castsi128_ps(_mm_slli_epi32(m, 23));
        v4f r = _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(_mm_mul_ps(x, u), u), u), u);
        u = _mm_castsi128_ps(_mm_slli_epi32(_mm_add_epi32(t, _mm_set1_epi32(0x7f)), 23));

        return _mm_mul_ps(r, u);
    }

    PLATFORM_INLINE v4f emu_mm_sin_ps(const v4f& x)
    {
        v4i q = _mm_cvtps_epi32( _mm_mul_ps(x, _mm_broadcast_ss(c_f + 0)) );
        v4f u = _mm_cvtepi32_ps(q);

        v4f r = v4f_madd(u, _mm_broadcast_ss(c_f + 12), x);
        r = v4f_madd(u, _mm_broadcast_ss(c_f + 13), r);
        r = v4f_madd(u, _mm_broadcast_ss(c_f + 14), r);
        r = v4f_madd(u, _mm_broadcast_ss(c_f + 15), r);

        v4f s = _mm_mul_ps(r, r);

        r = _mm_castsi128_ps(_mm_xor_si128(_mm_and_si128(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1)), _mm_castps_si128(c_v4f_Sign)), _mm_castps_si128(r)));

        u = _mm_broadcast_ss(c_f + 16);
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 17));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 18));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 19));
        u = v4f_madd(s, _mm_mul_ps(u, r), r);

        u = _mm_or_ps(_v4f_is_inf(r), u);

        return u;
    }

    PLATFORM_INLINE v4f emu_mm_cos_ps(const v4f& x)
    {
        v4i q = _mm_cvtps_epi32(_mm_sub_ps(_mm_mul_ps(x, _mm_broadcast_ss(c_f + 0)), _mm_broadcast_ss(c_f + 42)));
        q = _mm_add_epi32(_mm_add_epi32(q, q), _mm_set1_epi32(1));

        v4f u = _mm_cvtepi32_ps(q);

        v4f r = v4f_madd(u, _mm_broadcast_ss(c_f + 20), x);
        r = v4f_madd(u, _mm_broadcast_ss(c_f + 21), r);
        r = v4f_madd(u, _mm_broadcast_ss(c_f + 22), r);
        r = v4f_madd(u, _mm_broadcast_ss(c_f + 23), r);

        v4f s = _mm_mul_ps(r, r);

        r = _mm_castsi128_ps(_mm_xor_si128(_mm_and_si128(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_setzero_si128()), _mm_castps_si128(c_v4f_Sign)), _mm_castps_si128(r)));

        u = _mm_broadcast_ss(c_f + 16);
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 17));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 18));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 19));
        u = v4f_madd(s, _mm_mul_ps(u, r), r);

        u = _mm_or_ps(_v4f_is_inf(r), u);

        return u;
    }

    PLATFORM_INLINE v4f emu_mm_sincos_ps(v4f* pCos, const v4f& d)
    {
        v4i q = _mm_cvtps_epi32(_mm_mul_ps(d, _mm_broadcast_ss(c_f + 24)));

        v4f s = d;

        v4f u = _mm_cvtepi32_ps(q);
        s = v4f_madd(u, _mm_broadcast_ss(c_f + 20), s);
        s = v4f_madd(u, _mm_broadcast_ss(c_f + 21), s);
        s = v4f_madd(u, _mm_broadcast_ss(c_f + 22), s);
        s = v4f_madd(u, _mm_broadcast_ss(c_f + 23), s);

        v4f t = s;

        s = _mm_mul_ps(s, s);

        u = _mm_broadcast_ss(c_f + 25);
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 26));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 27));
        u = _mm_mul_ps(_mm_mul_ps(u, s), t);

        v4f rx = _mm_add_ps(t, u);

        u = _mm_broadcast_ss(c_f + 28);
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 29));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 30));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 31));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 32));

        v4f ry = v4f_madd(s, u, _mm_broadcast_ss(c_f + 33));

        v4f m = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(0)));
        v4f rrx = _v4f_vselect(m, rx, ry);
        v4f rry = _v4f_vselect(m, ry, rx);

        m = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_set1_epi32(2)));
        rrx = _mm_xor_ps(_mm_and_ps(m, c_v4f_Sign), rrx);

        m = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(_mm_add_epi32(q, _mm_set1_epi32(1)), _mm_set1_epi32(2)), _mm_set1_epi32(2)));
        rry = _mm_xor_ps(_mm_and_ps(m, c_v4f_Sign), rry);

        m = _v4f_is_inf(d);

        *pCos = _mm_or_ps(m, rry);

        return _mm_or_ps(m, rrx);
    }

    PLATFORM_INLINE v4f emu_mm_tan_ps(const v4f& x)
    {
        v4i q = _mm_cvtps_epi32(_mm_mul_ps(x, _mm_broadcast_ss(c_f + 24)));
        v4f r = x;

        v4f u = _mm_cvtepi32_ps(q);
        r = v4f_madd(u, _mm_broadcast_ss(c_f + 20), r);
        r = v4f_madd(u, _mm_broadcast_ss(c_f + 21), r);
        r = v4f_madd(u, _mm_broadcast_ss(c_f + 22), r);
        r = v4f_madd(u, _mm_broadcast_ss(c_f + 23), r);

        v4f s = _mm_mul_ps(r, r);

        v4i m = _mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1));
        r = _mm_castsi128_ps(_mm_xor_si128(_mm_and_si128(m, _mm_castps_si128(c_v4f_Sign)), _mm_castps_si128(r)));

        u = _mm_broadcast_ss(c_f + 34);
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 35));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 36));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 37));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 38));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 39));

        u = v4f_madd(s, _mm_mul_ps(u, r), r);
        u = _v4f_vselecti(m, v4f_rcp(u), u);

        u = _mm_or_ps(_v4f_is_inf(r), u);

        return u;
    }

    PLATFORM_INLINE v4f emu_mm_atan_ps(const v4f& d)
    {
        v4i q = _v4f_iselect(_mm_cmplt_ps(d, v4f_zero), _mm_set1_epi32(2), _mm_setzero_si128());
        v4f s = v4f_abs(d);

        v4f mask = _mm_cmplt_ps(_mm_broadcast_ss(c_f + 33), s);
        q = _v4f_iselect(mask, _mm_add_epi32(q, _mm_set1_epi32(1)), q);
        s = _v4f_vselect(mask, v4f_rcp(s), s);

        v4f t = _mm_mul_ps(s, s);

        v4f u = _mm_broadcast_ss(c_f + 1);
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 2));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 3));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 4));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 5));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 6));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 7));
        u = v4f_madd(u, t, _mm_broadcast_ss(c_f + 8));

        t = v4f_madd(s, _mm_mul_ps(t, u), s);
        t = _v4f_vselecti(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1)), _mm_sub_ps(_mm_broadcast_ss(c_f + 9), t), t);

        t = _mm_castsi128_ps(_mm_xor_si128(_mm_and_si128(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_set1_epi32(2)), _mm_castps_si128(c_v4f_Sign)), _mm_castps_si128(t)));

        return t;
    }

    PLATFORM_INLINE v4f emu_mm_atan2_ps(const v4f& y, const v4f& x)
    {
        v4f r = _v4f_atan2(v4f_abs(y), x);

        r = _v4f_mulsign(r, x);
        r = _v4f_vselect(_v4f_is_inf_or_zero(x), _mm_sub_ps(_mm_broadcast_ss(c_f + 9), _v4f_is_inf2(x, _v4f_mulsign(_mm_broadcast_ss(c_f + 9), x))), r);
        r = _v4f_vselect(_v4f_is_inf(y), _mm_sub_ps(_mm_broadcast_ss(c_f + 9), _v4f_is_inf2(x, _v4f_mulsign(_mm_broadcast_ss(c_f + 40), x))), r);
        r = _v4f_vselect(_mm_cmpeq_ps(y, v4f_zero), _mm_xor_ps(_mm_cmpeq_ps(v4f_sign(x), _mm_broadcast_ss(c_f + 41)), _mm_broadcast_ss(c_f + 43)), r);

        r = _mm_or_ps(_mm_or_ps(_v4f_is_nan(x), _v4f_is_nan(y)), _v4f_mulsign(r, y));

        return r;
    }

    PLATFORM_INLINE v4f emu_mm_asin_ps(const v4f& d)
    {
        v4f x = _mm_add_ps(_mm_broadcast_ss(c_f + 33), d);
        v4f y = _mm_sub_ps(_mm_broadcast_ss(c_f + 33), d);
        x = _mm_mul_ps(x, y);
        x = v4f_sqrt(x);
        x = _mm_or_ps(_v4f_is_nan(x), _v4f_atan2(v4f_abs(d), x));

        return _v4f_mulsign(x, d);
    }

    PLATFORM_INLINE v4f emu_mm_acos_ps(const v4f& d)
    {
        v4f x = _mm_add_ps(_mm_broadcast_ss(c_f + 33), d);
        v4f y = _mm_sub_ps(_mm_broadcast_ss(c_f + 33), d);
        x = _mm_mul_ps(x, y);
        x = v4f_sqrt(x);
        x = _v4f_mulsign(_v4f_atan2(x, v4f_abs(d)), d);
        y = _mm_and_ps(_mm_cmplt_ps(d, v4f_zero), _mm_broadcast_ss(c_f + 43));
        x = _mm_add_ps(x, y);

        return x;
    }

    PLATFORM_INLINE v4f emu_mm_log_ps(const v4f& d)
    {
        v4f x = _mm_mul_ps(d, _mm_broadcast_ss(c_f + 44));
        v4i e = _v4f_logbp1(x);
        v4f m = _v4f_ldexp(d, _v4f_negatei(e));
        v4f r = x;

        x = _mm_div_ps(_mm_add_ps(_mm_broadcast_ss(c_f + 41), m), _mm_add_ps(_mm_broadcast_ss(c_f + 33), m));
        v4f x2 = _mm_mul_ps(x, x);

        v4f t = _mm_broadcast_ss(c_f + 45);
        t = v4f_madd(t, x2, _mm_broadcast_ss(c_f + 46));
        t = v4f_madd(t, x2, _mm_broadcast_ss(c_f + 47));
        t = v4f_madd(t, x2, _mm_broadcast_ss(c_f + 48));
        t = v4f_madd(t, x2, _mm_broadcast_ss(c_f + 49));

        x = v4f_madd(x, t, _mm_mul_ps(_mm_broadcast_ss(c_f + 50), _mm_cvtepi32_ps(e)));
        x = _v4f_vselect(_v4f_is_pinf(r), c_v4f_Inf, x);

        x = _mm_or_ps(_mm_cmpgt_ps(v4f_zero, r), x);
        x = _v4f_vselect(_mm_cmpeq_ps(r, v4f_zero), c_v4f_InfMinus, x);

        return x;
    }

    PLATFORM_INLINE v4f emu_mm_exp_ps(const v4f& d)
    {
        v4i q = _mm_cvtps_epi32(_mm_mul_ps(d, _mm_broadcast_ss(c_f + 51)));

        v4f s = v4f_madd(_mm_cvtepi32_ps(q), _mm_broadcast_ss(c_f + 52), d);
        s = v4f_madd(_mm_cvtepi32_ps(q), _mm_broadcast_ss(c_f + 53), s);

        v4f u = _mm_broadcast_ss(c_f + 54);
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 55));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 56));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 57));
        u = v4f_madd(u, s, _mm_broadcast_ss(c_f + 58));

        u = _mm_add_ps(_mm_broadcast_ss(c_f + 33), v4f_madd(_mm_mul_ps(s, s), u, s));
        u = _v4f_ldexp(u, q);

        u = _mm_andnot_ps(_v4f_is_ninf(d), u);

        return u;
    }

    #undef _mm_sin_ps
    #undef _mm_cos_ps
    #undef _mm_sincos_ps
    #undef _mm_tan_ps
    #undef _mm_atan_ps
    #undef _mm_atan2_ps
    #undef _mm_asin_ps
    #undef _mm_acos_ps
    #undef _mm_log_ps
    #undef _mm_exp_ps
    #undef _mm_pow_ps
    #define _mm_sin_ps              emu_mm_sin_ps
    #define _mm_cos_ps              emu_mm_cos_ps
    #define _mm_sincos_ps           emu_mm_sincos_ps
    #define _mm_tan_ps              emu_mm_tan_ps
    #define _mm_atan_ps             emu_mm_atan_ps
    #define _mm_atan2_ps            emu_mm_atan2_ps
    #define _mm_asin_ps             emu_mm_asin_ps
    #define _mm_acos_ps             emu_mm_acos_ps
    #define _mm_log_ps              emu_mm_log_ps
    #define _mm_exp_ps              emu_mm_exp_ps
    #define _mm_pow_ps(x, y)        emu_mm_exp_ps( _mm_mul_ps( emu_mm_log_ps(x), y ) )
#endif

#if( PLATFORM_INTRINSIC < PLATFORM_INTRINSIC_AVX1 || !PLATFORM_HAS_TRANSCENDENTAL_INTRINSICS )
    constexpr double Cd_PI4_A = 0.78539816290140151978;
    constexpr double Cd_PI4_B = 4.9604678871439933374e-10;
    constexpr double Cd_PI4_C = 1.1258708853173288931e-18;
    constexpr double Cd_PI4_D = 1.7607799325916000908e-27;
    constexpr double c_d[] =
    {
        1.0,
        -1.88796008463073496563746e-05,
        0.000209850076645816976906797,
        -0.00110611831486672482563471,
        0.00370026744188713119232403,
        -0.00889896195887655491740809,
        0.016599329773529201970117,
        -0.0254517624932312641616861,
        0.0337852580001353069993897,
        -0.0407629191276836500001934,
        0.0466667150077840625632675,
        -0.0523674852303482457616113,
        0.0587666392926673580854313,
        -0.0666573579361080525984562,
        0.0769219538311769618355029,
        -0.090908995008245008229153,
        0.111111105648261418443745,
        -0.14285714266771329383765,
        0.199999999996591265594148,
        -0.333333333333311110369124,
        1.57079632679489661923,
        4.9090934652977266E-91,
        2.037035976334486E90,
        300+0x3fe,
        0x3fe,
        0.31830988618379067154,
        -Cd_PI4_A * 4.0,
        -Cd_PI4_B * 4.0,
        -Cd_PI4_C * 4.0,
        -Cd_PI4_D * 4.0,
        -7.97255955009037868891952e-18,
        2.81009972710863200091251e-15,
        -7.64712219118158833288484e-13,
        1.60590430605664501629054e-10,
        -2.50521083763502045810755e-08,
        2.75573192239198747630416e-06,
        -0.000198412698412696162806809,
        0.00833333333333332974823815,
        -0.166666666666666657414808,
        -0.5,
        -Cd_PI4_A * 2.0,
        -Cd_PI4_B * 2.0,
        -Cd_PI4_C * 2.0,
        -Cd_PI4_D * 2.0,
        0.63661977236758134308,
        1.58938307283228937328511e-10,
        -2.50506943502539773349318e-08,
        2.75573131776846360512547e-06,
        -0.000198412698278911770864914,
        0.0083333333333191845961746,
        -0.166666666666666130709393,
        -1.13615350239097429531523e-11,
        2.08757471207040055479366e-09,
        -2.75573144028847567498567e-07,
        2.48015872890001867311915e-05,
        -0.00138888888888714019282329,
        0.0416666666666665519592062,
        0.78539816339744830962,
        -1.0,
        3.14159265358979323846,
        0.7071,
        0.148197055177935105296783,
        0.153108178020442575739679,
        0.181837339521549679055568,
        0.22222194152736701733275,
        0.285714288030134544449368,
        0.399999999989941956712869,
        0.666666666666685503450651,
        2.0,
        0.693147180559945286226764,
        1.442695040888963407359924681001892137426645954152985934135449406931,
        -0.69314718055966295651160180568695068359375,
        -0.28235290563031577122588448175013436025525412068e-12,
        2.08860621107283687536341e-09,
        2.51112930892876518610661e-08,
        2.75573911234900471893338e-07,
        2.75572362911928827629423e-06,
        2.4801587159235472998791e-05,
        0.000198412698960509205564975,
        0.00138888888889774492207962,
        0.00833333333331652721664984,
        0.0416666666666665047591422,
        0.166666666666666851703837,
        0.5,
        1.01419718511083373224408e-05,
        -2.59519791585924697698614e-05,
        5.23388081915899855325186e-05,
        -3.05033014433946488225616e-05,
        7.14707504084242744267497e-05,
        8.09674518280159187045078e-05,
        0.000244884931879331847054404,
        0.000588505168743587154904506,
        0.00145612788922812427978848,
        0.00359208743836906619142924,
        0.00886323944362401618113356,
        0.0218694882853846389592078,
        0.0539682539781298417636002,
        0.133333333333125941821962,
        0.333333333333334980164153,
    };

    PLATFORM_INLINE v4d _v4d_is_inf_or_zero(const v4d& x)
    {
        v4d t = v4d_abs(x);

        return _mm256_or_pd(v4d_equal(t, v4d_zero), v4d_equal(t, c_v4d_Inf));
    }

    #define _v4d_vselect(mask, x, y)        v4d_select(y, x, mask)

    PLATFORM_INLINE v4i _v4d_selecti(const v4d& d0, const v4d& d1, const v4i& x, const v4i& y)
    {
        __m128i mask = _mm256_cvtpd_epi32(_mm256_and_pd(v4d_less(d0, d1), _mm256_broadcast_sd(c_d + 0)));
        mask = _mm_cmpeq_epi32(mask, _mm_set1_epi32(1));

        return _v4f_iselect(_mm_castsi128_ps(mask), x, y);
    }

    PLATFORM_INLINE v4d _v4d_cmp_4i(const v4i& x, const v4i& y)
    {
        v4i t = _mm_cmpeq_epi32(x, y);

        return _mm256_castsi256_pd( _mm256_cvtepi32_epi64(t) );
    }

    PLATFORM_INLINE v4d _v4d_atan2(const v4d& y, const v4d& x)
    {
        v4i q = _v4d_selecti(x, v4d_zero, _mm_set1_epi32(-2), _mm_setzero_si128());
        v4d r = v4d_abs(x);

        q = _v4d_selecti(r, y, _mm_add_epi32(q, _mm_set1_epi32(1)), q);
        v4d p = v4d_less(r, y);
        v4d s = _v4d_vselect(p, v4d_negate(r), y);
        v4d t = _mm256_max_pd(r, y);

        s = _mm256_div_pd(s, t);
        t = _mm256_mul_pd(s, s);

        v4d u = _mm256_broadcast_sd(c_d + 1);
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 2));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 3));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 4));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 5));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 6));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 7));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 8));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 9));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 10));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 11));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 12));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 13));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 14));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 15));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 16));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 17));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 18));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 19));

        t = v4d_madd(s, _mm256_mul_pd(t, u), s);
        t = v4d_madd(_mm256_cvtepi32_pd(q), _mm256_broadcast_sd(c_d + 20), t);

        return t;
    }

    PLATFORM_INLINE v4i _v4d_logbp1(const v4d& d)
    {
        v4d m = v4d_less(d, _mm256_broadcast_sd(c_d + 21));
        v4d t = _v4d_vselect(m, _mm256_mul_pd(_mm256_broadcast_sd(c_d + 22), d), d);
        v4i c = _mm256_cvtpd_epi32(_v4d_vselect(m, _mm256_broadcast_sd(c_d + 23), _mm256_broadcast_sd(c_d + 24)));
        v4i q = _mm_castpd_si128(_mm256_castpd256_pd128(t));
        q = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(q), v4f_zero, _MM_SHUFFLE(0,0,3,1)));
        v4i r = _mm_castpd_si128(_mm256_extractf128_pd(t, 1));
        r = _mm_castps_si128(_mm_shuffle_ps(v4f_zero, _mm_castsi128_ps(r), _MM_SHUFFLE(3,1,0,0)));
        q = _mm_or_si128(q, r);
        q = _mm_srli_epi32(q, 20);
        q = _mm_sub_epi32(q, c);

        return q;
    }

    PLATFORM_INLINE v4d _v4d_pow2i(const v4i& q)
    {
        v4i t = _mm_add_epi32(_mm_set1_epi32(0x3ff), q);
        t = _mm_slli_epi32(t, 20);
        v4i r = _mm_shuffle_epi32(t, _MM_SHUFFLE(1,0,0,0));
        v4d y = _mm256_castpd128_pd256(_mm_castsi128_pd(r));
        r = _mm_shuffle_epi32(t, _MM_SHUFFLE(3,2,2,2));
        y = _mm256_insertf128_pd(y, _mm_castsi128_pd(r), 1);
        y = _mm256_and_pd(y, _mm256_castsi256_pd(_mm256_set_epi32(0xfff00000, 0, 0xfff00000, 0, 0xfff00000, 0, 0xfff00000, 0)));

        return y;
    }

    PLATFORM_INLINE v4d _v4d_ldexp(const v4d& x, const v4i& q)
    {
        v4i m = _mm_srai_epi32(q, 31);
        m = _mm_slli_epi32(_mm_sub_epi32(_mm_srai_epi32(_mm_add_epi32(m, q), 9), m), 7);
        v4i t = _mm_sub_epi32(q, _mm_slli_epi32(m, 2));
        m = _mm_add_epi32(_mm_set1_epi32(0x3ff), m);
        m = _mm_andnot_si128(_mm_cmplt_epi32(m, _mm_setzero_si128()), m);
        v4i n = _mm_cmpgt_epi32(m, _mm_set1_epi32(0x7ff));
        m = _mm_or_si128(_mm_andnot_si128(n, m), _mm_and_si128(n, _mm_set1_epi32(0x7ff)));
        m = _mm_slli_epi32(m, 20);
        v4i r = _mm_shuffle_epi32(m, _MM_SHUFFLE(1,0,0,0));
        v4d y = _mm256_castpd128_pd256(_mm_castsi128_pd(r));
        r = _mm_shuffle_epi32(m, _MM_SHUFFLE(3,2,2,2));
        y = _mm256_insertf128_pd(y, _mm_castsi128_pd(r), 1);
        y = _mm256_and_pd(y, _mm256_castsi256_pd(_mm256_set_epi32(0xfff00000, 0, 0xfff00000, 0, 0xfff00000, 0, 0xfff00000, 0)));

        return _mm256_mul_pd(_mm256_mul_pd(_mm256_mul_pd(_mm256_mul_pd(_mm256_mul_pd(x, y), y), y), y), _v4d_pow2i(t));
    }

    PLATFORM_INLINE v4d emu_mm256_sin_pd(const v4d& d)
    {
        v4i q = _mm256_cvtpd_epi32(_mm256_mul_pd(d, _mm256_broadcast_sd(c_d + 25)));

        v4d u = _mm256_cvtepi32_pd(q);

        v4d r = v4d_madd(u, _mm256_broadcast_sd(c_d + 26), d);
        r = v4d_madd(u, _mm256_broadcast_sd(c_d + 27), r);
        r = v4d_madd(u, _mm256_broadcast_sd(c_d + 28), r);
        r = v4d_madd(u, _mm256_broadcast_sd(c_d + 29), r);

        v4d s = _mm256_mul_pd(r, r);

        r = _mm256_xor_pd(_mm256_and_pd(_v4d_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1)), c_v4d_Sign), r);

        u = _mm256_broadcast_sd(c_d + 30);
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 31));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 32));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 33));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 34));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 35));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 36));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 37));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 38));

        u = v4d_madd(s, _mm256_mul_pd(u, r), r);

        return u;
    }

    PLATFORM_INLINE v4d emu_mm256_cos_pd(const v4d& d)
    {
        v4i q = _mm256_cvtpd_epi32(v4d_madd(d, _mm256_broadcast_sd(c_d + 25), _mm256_broadcast_sd(c_d + 39)));
        q = _mm_add_epi32(_mm_add_epi32(q, q), _mm_set1_epi32(1));

        v4d u = _mm256_cvtepi32_pd(q);

        v4d r = v4d_madd(u, _mm256_broadcast_sd(c_d + 40), d);
        r = v4d_madd(u, _mm256_broadcast_sd(c_d + 41), r);
        r = v4d_madd(u, _mm256_broadcast_sd(c_d + 42), r);
        r = v4d_madd(u, _mm256_broadcast_sd(c_d + 43), r);

        v4d s = _mm256_mul_pd(r, r);

        r = _mm256_xor_pd(_mm256_and_pd(_v4d_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_setzero_si128()), c_v4d_Sign), r);

        u = _mm256_broadcast_sd(c_d + 30);
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 31));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 32));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 33));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 34));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 35));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 36));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 37));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 38));

        u = v4d_madd(s, _mm256_mul_pd(u, r), r);

        return u;
    }

    PLATFORM_INLINE v4d emu_mm256_sincos_pd(v4d* pCos, const v4d& d)
    {
        v4i q = _mm256_cvtpd_epi32(_mm256_mul_pd(d, _mm256_broadcast_sd(c_d + 44)));
        v4d s = d;

        v4d u = _mm256_cvtepi32_pd(q);
        s = v4d_madd(u, _mm256_broadcast_sd(c_d + 40), s);
        s = v4d_madd(u, _mm256_broadcast_sd(c_d + 41), s);
        s = v4d_madd(u, _mm256_broadcast_sd(c_d + 42), s);
        s = v4d_madd(u, _mm256_broadcast_sd(c_d + 43), s);

        v4d t = s;

        s = _mm256_mul_pd(s, s);

        u = _mm256_broadcast_sd(c_d + 45);
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 46));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 47));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 48));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 49));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 50));
        u = _mm256_mul_pd(_mm256_mul_pd(u, s), t);

        v4d rx = _mm256_add_pd(t, u);

        u = _mm256_broadcast_sd(c_d + 51);
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 52));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 53));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 54));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 55));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 56));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 39));

        v4d ry = v4d_madd(s, u, _mm256_broadcast_sd(c_d + 0));

        v4d m = _v4d_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_setzero_si128());
        v4d rrx = _v4d_vselect(m, rx, ry);
        v4d rry = _v4d_vselect(m, ry, rx);

        m = _v4d_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_set1_epi32(2));
        rrx = _mm256_xor_pd(_mm256_and_pd(m, c_v4d_Sign), rrx);

        m = _v4d_cmp_4i(_mm_and_si128(_mm_add_epi32(q, _mm_set1_epi32(1)), _mm_set1_epi32(2)), _mm_set1_epi32(2));
        rry = _mm256_xor_pd(_mm256_and_pd(m, c_v4d_Sign), rry);

        m = _v4d_is_inf(d);
        *pCos = _mm256_or_pd(m, rry);

        return _mm256_or_pd(m, rrx);
    }

    PLATFORM_INLINE v4d emu_mm256_tan_pd(const v4d& d)
    {
        v4i q = _mm256_cvtpd_epi32(_mm256_mul_pd(d, _mm256_broadcast_sd(c_d + 44)));

        v4d u = _mm256_cvtepi32_pd(q);

        v4d x = v4d_madd(u, _mm256_broadcast_sd(c_d + 40), d);
        x = v4d_madd(u, _mm256_broadcast_sd(c_d + 41), x);
        x = v4d_madd(u, _mm256_broadcast_sd(c_d + 42), x);
        x = v4d_madd(u, _mm256_broadcast_sd(c_d + 43), x);

        v4d s = _mm256_mul_pd(x, x);

        v4d m = _v4d_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1));
        x = _mm256_xor_pd(_mm256_and_pd(m, c_v4d_Sign), x);

        u = _mm256_broadcast_sd(c_d + 84);
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 85));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 86));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 87));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 88));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 89));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 90));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 91));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 92));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 93));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 94));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 95));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 96));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 97));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 98));

        u = v4d_madd(s, _mm256_mul_pd(u, x), x);
        u = _v4d_vselect(m, v4d_rcp(u), u);

        u = _mm256_or_pd(_v4d_is_inf(d), u);

        return u;
    }

    PLATFORM_INLINE v4d emu_mm256_atan_pd(const v4d& s)
    {
        v4i q = _v4d_selecti(s, v4d_zero, _mm_set1_epi32(2), _mm_setzero_si128());
        v4d r = v4d_abs(s);

        q = _v4d_selecti(_mm256_broadcast_sd(c_d + 0), r, _mm_add_epi32(q, _mm_set1_epi32(1)), q);
        r = _v4d_vselect(v4d_less(_mm256_broadcast_sd(c_d + 0), r), v4d_rcp(r), r);

        v4d t = _mm256_mul_pd(r, r);

        v4d u = _mm256_broadcast_sd(c_d + 1);
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 2));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 3));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 4));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 5));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 6));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 7));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 8));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 9));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 10));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 11));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 12));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 13));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 14));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 15));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 16));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 17));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 18));
        u = v4d_madd(u, t, _mm256_broadcast_sd(c_d + 19));

        t = v4d_madd(r, _mm256_mul_pd(t, u), r);

        t = _v4d_vselect(_v4d_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1)), _mm256_sub_pd(_mm256_broadcast_sd(c_d + 20), t), t);
        t = _mm256_xor_pd(_mm256_and_pd(_v4d_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_set1_epi32(2)), c_v4d_Sign), t);

        return t;
    }

    PLATFORM_INLINE v4d emu_mm256_atan2_pd(const v4d& y, const v4d& x)
    {
        v4d r = _v4d_atan2(v4d_abs(y), x);

        r = _v4d_mulsign(r, x);
        r = _v4d_vselect(_mm256_or_pd(_v4d_is_inf(x), v4d_equal(x, v4d_zero)), _mm256_sub_pd(_mm256_broadcast_sd(c_d + 20), _v4d_is_inf2(x, _v4d_mulsign(_mm256_broadcast_sd(c_d + 20), x))), r);
        r = _v4d_vselect(_v4d_is_inf(y), _mm256_sub_pd(_mm256_broadcast_sd(c_d + 20), _v4d_is_inf2(x, _v4d_mulsign(_mm256_broadcast_sd(c_d + 57), x))), r);
        r = _v4d_vselect(v4d_equal(y, v4d_zero), _mm256_and_pd(v4d_equal(v4d_sign(x), _mm256_broadcast_sd(c_d + 58)), _mm256_broadcast_sd(c_d + 59)), r);

        r = _mm256_or_pd(_mm256_or_pd(_v4d_is_nan(x), _v4d_is_nan(y)), _v4d_mulsign(r, y));
        return r;
    }

    PLATFORM_INLINE v4d emu_mm256_asin_pd(const v4d& d)
    {
        v4d x = _mm256_add_pd(_mm256_broadcast_sd(c_d + 0), d);
        v4d y = _mm256_sub_pd(_mm256_broadcast_sd(c_d + 0), d);
        x = _mm256_mul_pd(x, y);
        x = v4d_sqrt(x);
        x = _mm256_or_pd(_v4d_is_nan(x), _v4d_atan2(v4d_abs(d), x));

        return _v4d_mulsign(x, d);
    }

    PLATFORM_INLINE v4d emu_mm256_acos_pd(const v4d& d)
    {
        v4d x = _mm256_add_pd(_mm256_broadcast_sd(c_d + 0), d);
        v4d y = _mm256_sub_pd(_mm256_broadcast_sd(c_d + 0), d);
        x = _mm256_mul_pd(x, y);
        x = v4d_sqrt(x);
        x = _v4d_mulsign(_v4d_atan2(x, v4d_abs(d)), d);
        y = _mm256_and_pd(v4d_less(d, v4d_zero), _mm256_broadcast_sd(c_d + 59));
        x = _mm256_add_pd(x, y);

        return x;
    }

    PLATFORM_INLINE v4d emu_mm256_log_pd(const v4d& d)
    {
        v4i e = _v4d_logbp1(_mm256_mul_pd(d, _mm256_broadcast_sd(c_d + 60)));
        v4d m = _v4d_ldexp(d, _v4f_negatei(e));

        v4d x = _mm256_div_pd(_mm256_add_pd(_mm256_broadcast_sd(c_d + 58), m), _mm256_add_pd(_mm256_broadcast_sd(c_d + 0), m));
        v4d x2 = _mm256_mul_pd(x, x);

        v4d t = _mm256_broadcast_sd(c_d + 61);
        t = v4d_madd(t, x2, _mm256_broadcast_sd(c_d + 62));
        t = v4d_madd(t, x2, _mm256_broadcast_sd(c_d + 63));
        t = v4d_madd(t, x2, _mm256_broadcast_sd(c_d + 64));
        t = v4d_madd(t, x2, _mm256_broadcast_sd(c_d + 65));
        t = v4d_madd(t, x2, _mm256_broadcast_sd(c_d + 66));
        t = v4d_madd(t, x2, _mm256_broadcast_sd(c_d + 67));
        t = v4d_madd(t, x2, _mm256_broadcast_sd(c_d + 68));

        x = v4d_madd(x, t, _mm256_mul_pd(_mm256_broadcast_sd(c_d + 69), _mm256_cvtepi32_pd(e)));

        x = _v4d_vselect(_v4d_is_pinf(d), c_v4d_Inf, x);
        x = _mm256_or_pd(v4d_greater(v4d_zero, d), x);
        x = _v4d_vselect(v4d_equal(d, v4d_zero), c_v4d_InfMinus, x);

        return x;
    }

    PLATFORM_INLINE v4d emu_mm256_exp_pd(const v4d& d)
    {
        v4i q = _mm256_cvtpd_epi32(_mm256_mul_pd(d, _mm256_broadcast_sd(c_d + 70)));

        v4d s = v4d_madd(_mm256_cvtepi32_pd(q), _mm256_broadcast_sd(c_d + 71), d);
        s = v4d_madd(_mm256_cvtepi32_pd(q), _mm256_broadcast_sd(c_d + 72), s);

        v4d u = _mm256_broadcast_sd(c_d + 73);
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 74));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 75));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 76));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 77));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 78));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 79));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 80));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 81));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 82));
        u = v4d_madd(u, s, _mm256_broadcast_sd(c_d + 83));

        u = _mm256_add_pd(_mm256_broadcast_sd(c_d + 0), v4d_madd(_mm256_mul_pd(s, s), u, s));

        u = _v4d_ldexp(u, q);

        u = _mm256_andnot_pd(_v4d_is_ninf(d), u);

        return u;
    }

    #undef _mm256_sin_pd
    #undef _mm256_cos_pd
    #undef _mm256_sincos_pd
    #undef _mm256_tan_pd
    #undef _mm256_atan_pd
    #undef _mm256_atan2_pd
    #undef _mm256_asin_pd
    #undef _mm256_acos_pd
    #undef _mm256_log_pd
    #undef _mm256_exp_pd
    #undef _mm256_pow_pd
    #define _mm256_sin_pd           emu_mm256_sin_pd
    #define _mm256_cos_pd           emu_mm256_cos_pd
    #define _mm256_sincos_pd        emu_mm256_sincos_pd
    #define _mm256_tan_pd           emu_mm256_tan_pd
    #define _mm256_atan_pd          emu_mm256_atan_pd
    #define _mm256_atan2_pd         emu_mm256_atan2_pd
    #define _mm256_asin_pd          emu_mm256_asin_pd
    #define _mm256_acos_pd          emu_mm256_acos_pd
    #define _mm256_log_pd           emu_mm256_log_pd
    #define _mm256_exp_pd           emu_mm256_exp_pd
    #define _mm256_pow_pd(x, y)     emu_mm256_exp_pd( _mm256_mul_pd( emu_mm256_log_pd(x), y ) )
#endif
