#pragma once

#define NDC_D3D                         0
#define NDC_OGL                         1

// IMPORTANT: prerequisities (must be defined outside of this file)

#if( defined PROPS_D3D11 || defined PROPS_D3D12 )
    #define NDC_ORIGIN                  NDC_D3D
    #define NDC_DEPTH                   NDC_D3D
#elif( defined PROPS_GL || defined PROPS_VK )
    #define NDC_ORIGIN                  NDC_OGL
    #define NDC_DEPTH                   NDC_OGL
#else
    #error "One of PROPS_D3D11 / PROPS_D3D12 / PROPS_GL / PROPS_VK must be defined!"
#endif

//=========================================================================================
// NOTE: settings
//=========================================================================================

// NOTE: reverse depth
#define DEPTH_REVERSED

// NOTE: depth range
#define DEPTH_RANGE_NEAR                0.0f
#define DEPTH_RANGE_FAR                 1.0f

//=========================================================================================
// NOTE: private
//=========================================================================================

#if( NDC_ORIGIN == NDC_OGL )
    #define NDC_ORIGIN_TOKEN            GL_LOWER_LEFT
#else
    #define NDC_ORIGIN_TOKEN            GL_UPPER_LEFT
#endif

#if( NDC_DEPTH == NDC_OGL )
    #define NDC_DEPTH_TOKEN             GL_NEGATIVE_ONE_TO_ONE
    #define _NDC_NEAR_NO_REVERSE        -1.0f
    #define _DEPTH_C0                   (0.5f * (DEPTH_RANGE_FAR - DEPTH_RANGE_NEAR))
    #define _DEPTH_C1                   (0.5f * (DEPTH_RANGE_FAR + DEPTH_RANGE_NEAR))
    #define _ModifyProjZ(c2, c3)        (c2)
#else
    #define NDC_DEPTH_TOKEN             GL_ZERO_TO_ONE
    #define _NDC_NEAR_NO_REVERSE        0.0f
    #define _DEPTH_C0                   (DEPTH_RANGE_FAR - DEPTH_RANGE_NEAR)
    #define _DEPTH_C1                   DEPTH_RANGE_NEAR
    #define _ModifyProjZ(c2, c3)        (T(0.5) * (c2 + c3))
#endif

#define _DEPTH_FAR_MARKER               99999.0f
#define _DEPTH_NEAR_MARKER              -99999.0f

#define _NDC_FAR_NO_REVERSE             1.0f

#ifdef DEPTH_REVERSED

    #define _NDC_NEAR                   _NDC_FAR_NO_REVERSE
    #define _NDC_FAR                    _NDC_NEAR_NO_REVERSE
    #define _DEPTH_EPS                  -1e-7f

#else

    #define _NDC_NEAR                   _NDC_NEAR_NO_REVERSE
    #define _NDC_FAR                    _NDC_FAR_NO_REVERSE
    #define _DEPTH_EPS                  1e-7f

#endif

//=========================================================================================
// NOTE: public
//=========================================================================================

#ifdef DEPTH_REVERSED

    #define DEPTH_FUNC                  GLID_GREATER
    #define DEPTH_FUNC_EQUAL            GLID_GEQUAL
    #define DEPTH_MaxStr                "in"
    #define DEPTH_MinStr                "ax"
    #define ZbufferMax                  Min
    #define ZbufferMin                  Max

#else

    #define DEPTH_FUNC                  GLID_LESS
    #define DEPTH_FUNC_EQUAL            GLID_LEQUAL
    #define DEPTH_MaxStr                "ax"
    #define DEPTH_MinStr                "in"
    #define ZbufferMax                  Max
    #define ZbufferMin                  Min

#endif

namespace Zbuffer
{
    inline float NdcToDepth(float ndc_z)
    {
        return ndc_z * _DEPTH_C0 + _DEPTH_C1;
    }

    template<class T> inline T ModifyProjZ(bool bReverse, T c2, T c3)
    {
        PLATFORM_UNUSED(bReverse);
        PLATFORM_UNUSED(c3);

        if( bReverse )
            c2 = -c2;

        return _ModifyProjZ(c2, c3);
    }

    inline void UnprojectZ(float* pfUnprojectZ2, float a22, float a23, float a32)
    {
        // z = u0 / (depth + u1)

        pfUnprojectZ2[0] = _DEPTH_C0 * a23 / a32;
        pfUnprojectZ2[1] = -(_DEPTH_C0 * a22 / a32 + _DEPTH_C1);

        // z = 1 / (depth * u0 + u1);

        //pfUnprojectZ2[0] = a32 / (_DEPTH_C0 * a23);
        //pfUnprojectZ2[1] = -(a22 / a23 + _DEPTH_C1 / pfUnprojectZ2[0]);
    }

    const float NdcNear             = _NDC_NEAR;
    const float NdcFar              = _NDC_FAR;
    const float NdcNearNoReverse    = _NDC_NEAR_NO_REVERSE;
    const float NdcFarNoReverse     = _NDC_FAR_NO_REVERSE;
    const float DepthNear           = NdcToDepth(_NDC_NEAR);
    const float DepthFar            = NdcToDepth(_NDC_FAR);
    const float dc0                 = _DEPTH_C0;
    const float dc1                 = _DEPTH_C1;
};
