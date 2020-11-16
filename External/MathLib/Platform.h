#pragma once

// NOTE: platform

#include <cstdint>
#include <assert.h>
#include <stdlib.h>
#include <memory.h>

#define PLATFORM_WINDOWS                                0
#define PLATFORM_LINUX                                  1

#define PLATFORM_CPU_MEMORY_ALIGNMENT                   256 // best for AVX

#if( defined _WIN32 || defined _WIN64 )

    #define PLATFORM_PLATFORM                           PLATFORM_WINDOWS

    // NOTE: backward compatibility
    #ifndef WIN32
        #define WIN32
    #endif

    #define PLATFORM_INLINE                             __forceinline
    #define PLATFORM_ALIGN(alignment, x)                __declspec(align(alignment)) x

#else

    #define PLATFORM_PLATFORM                           PLATFORM_LINUX

    #include <unistd.h>

    #define PLATFORM_INLINE                             __attribute__((always_inline))
    #define PLATFORM_ALIGN(alignment, x)                x __attribute__((aligned(alignment)))

#endif

#ifdef __INTEL_COMPILER
    #define PLATFORM_INTEL_COMPILER
#endif

#if defined(PLATFORM_INTEL_COMPILER) || (_MSC_VER>1920)
    #define PLATFORM_HAS_SVML_INTRISICS
#endif

// NOTE: limits

#define PLATFORM_MAX_CHAR                               int8_t(~(1 << (sizeof(int8_t) * 8 - 1)))
#define PLATFORM_MAX_SHORT                              int16_t(~(1 << (sizeof(int16_t) * 8 - 1)))
#define PLATFORM_MAX_INT                                int32_t(~(1 << (sizeof(int32_t) * 8 - 1)))
#define PLATFORM_MAX_INT64                              int64_t(~(1LL << (sizeof(int64_t) * 8 - 1)))

#define PLATFORM_MAX_UCHAR                              uint8_t(~0)
#define PLATFORM_MAX_USHORT                             uint16_t(~0)
#define PLATFORM_MAX_UINT                               uint32_t(~0)
#define PLATFORM_MAX_UINT64                             uint64_t(~0)

#define PLATFORM_INF                                    1e20f

// NOTE: misc

#define PLATFORM_NULL                                   nullptr
#define PLATFORM_UNUSED(x)                              (void)x
#define UNKNOWN_BYTE                                    PLATFORM_MAX_UCHAR
#define UNKNOWN_SHORT                                   PLATFORM_MAX_USHORT
#define LOSHORT(x)                                      (x & 0xFFFF)
#define HISHORT(x)                                      ((x >> 16) & 0xFFFF)
#define BYTE_OFFSET(x)                                  ((uint8_t*)PLATFORM_NULL + (x))

// NOTE: asm level

#define PLATFORM_INTRINSIC_SSE3                         0       // NOTE: +SSSE3
#define PLATFORM_INTRINSIC_SSE4                         1
#define PLATFORM_INTRINSIC_AVX1                         2       // NOTE: +FP16C
#define PLATFORM_INTRINSIC_AVX2                         3       // NOTE: +FMA3

#define PLATFORM_INTRINSIC                              PLATFORM_INTRINSIC_SSE4

#if( _MSC_VER < 1800 )
    #define round(x)        floor(x + T(0.5))
#endif

// NOTE: x32 / x64

#define PLATFORM_x32                                    0
#define PLATFORM_x64                                    1

#if( defined _WIN64 || defined _LINUX64 )

    #define PLATFORM_ADDRESS                            PLATFORM_x64
    #define PLATFORM_XEXT                               "_x64"
    #define PLATFORM_MAX_ALLOC                          2147483648  // 2 Gb

#else

    #define PLATFORM_ADDRESS                            PLATFORM_x32
    #define PLATFORM_XEXT                               "_x32"
    #define PLATFORM_MAX_ALLOC                          536870912   // 512 Mb

#endif

// NOTE: debugging

#ifdef _DEBUG
    #define PLATFORM_DEBUG
#endif

#if( PLATFORM_PLATFORM == PLATFORM_WINDOWS )

    #define DEBUG_StaticAssertMsg(x, msg)               static_assert(x, msg)
    #define DEBUG_StaticAssert(x)                       static_assert(x, #x)

#else

    #define DEBUG_StaticAssertMsg(x, msg)               ((void)0)
    #define DEBUG_StaticAssert(x)                       ((void)0)

#endif

#define TEMP_STR2(x)                                    #x
#define TEMP_STR1(x)                                    TEMP_STR2(x)
#define TEMP_LOCATION                                   __FILE__ "(" TEMP_STR1(__LINE__) "): "

#ifdef PLATFORM_DEBUG

    #define DEBUG_Assert(x)                             assert( x )
    #define DEBUG_AssertMsg(x, msg)                     (void)( (!!(x)) || (_wassert(L"\"" ## TEMP_STR1(x) ## "\" - " ## msg, _CRT_WIDE(__FILE__), __LINE__), 0) )

#else

    #define DEBUG_AssertMsg(x, msg)                     ((void)0)
    #define DEBUG_Assert(x)                             ((void)0)

#endif

#ifdef PLATFORM_INTEL_COMPILER
    #define TEMP_FUNC                                   "unknown"
#else
    #define TEMP_FUNC                                   __FUNCTION__
#endif

#define PLATFORM_MESSAGE(msg)                           __pragma(message(TEMP_LOCATION "message: PLATFORM MESSAGE(" TEMP_FUNC "): " msg))
#define PLATFORM_WARNING(msg)                           __pragma(message(TEMP_LOCATION "warning: PLATFORM WARNING(" TEMP_FUNC "): " msg))
#define PLATFORM_ERROR(msg)                             __pragma(message(TEMP_LOCATION "error: PLATFORM ERROR(" TEMP_FUNC "): " msg))

#define PLATFORM_FIXME(msg)                             __pragma(message(TEMP_LOCATION "warning: PLATFORM FIXME(" TEMP_FUNC "): " msg))
#define PLATFORM_TODO(msg)                              __pragma(message(TEMP_LOCATION "message: PLATFORM TODO(" TEMP_FUNC "): " msg))

#define PLATFORM_DEBUG_CODE                             PLATFORM_FIXME("DEBUG CODE!")
#define PLATFORM_BREAK                                  int32_t _engine_break_ = 0

// NOTE: linking

#define PLATFORM_LIBEXT                                 ".lib"

#define _PLATFORM_LINK(msg)                             __pragma(message(TEMP_LOCATION "message: PLATFORM LINK: " msg))

#define PLATFORM_LINK(x)                                __pragma(comment(lib, x PLATFORM_LIBEXT)) \
                                                        _PLATFORM_LINK(x PLATFORM_LIBEXT)

#define PLATFORM_LINK_XEXT(x)                           __pragma(comment(lib, x PLATFORM_XEXT PLATFORM_LIBEXT)) \
                                                        _PLATFORM_LINK(x PLATFORM_XEXT PLATFORM_LIBEXT)
