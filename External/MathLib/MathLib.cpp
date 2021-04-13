#define NDC_DONT_CARE
#include "MathLib.h"

#ifdef MATH_NAMESPACE
namespace ml {
#endif

/*============================================================================================================
                                            Consts
============================================================================================================*/

const float4x4 float4x4::identity
(
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
);

const double4x4 double4x4::identity
(
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
);

const float3 float3::ortx(1.0f, 0.0f, 0.0f);
const float3 float3::orty(0.0f, 1.0f, 0.0f);
const float3 float3::ortz(0.0f, 0.0f, 1.0f);
const float3 float3::one(1.0f);

sFastRand g_frand;

/*============================================================================================================
                                            Frustum
============================================================================================================*/

PLATFORM_INLINE bool MvpToPlanes(uint8_t ucNdcDepth, const float4x4& m, float4* pvPlane6)
{
    float4x4 mt;
    m.TransposeTo(mt);

    float4 l = mt.GetCol3() + mt.GetCol0();
    float4 r = mt.GetCol3() - mt.GetCol0();
    float4 b = mt.GetCol3() + mt.GetCol1();
    float4 t = mt.GetCol3() - mt.GetCol1();
    float4 f = mt.GetCol3() - mt.GetCol2();
    float4 n = mt.GetCol2();

    if( ucNdcDepth == NDC_OGL )
        n += mt.GetCol3();

    // NOTE: side planes

    l *= Rsqrt( Dot33(l.xmm, l.xmm) );
    r *= Rsqrt( Dot33(r.xmm, r.xmm) );
    b *= Rsqrt( Dot33(b.xmm, b.xmm) );
    t *= Rsqrt( Dot33(t.xmm, t.xmm) );

    // NOTE: near & far planes

    n /= Max( Sqrt( Dot33(n.xmm, n.xmm) ), FLT_MIN );
    f /= Max( Sqrt( Dot33(f.xmm, f.xmm) ), FLT_MIN );

    // NOTE: handle reversed projection

    bool bReversed = Abs(n.w) > Abs(f.w);

    if( bReversed )
        Swap(n, f);

    // NOTE: handle infinite projection

    if( Dot33(f.xmm, f.xmm) <= FLT_MIN )
        f = float4(-n.x, -n.y, -n.z, f.w);

    pvPlane6[PLANE_LEFT]    = l;
    pvPlane6[PLANE_RIGHT]   = r;
    pvPlane6[PLANE_BOTTOM]  = b;
    pvPlane6[PLANE_TOP]     = t;
    pvPlane6[PLANE_NEAR]    = n;
    pvPlane6[PLANE_FAR]     = f;

    return bReversed;
}

void cFrustum::Setup(uint8_t ucNdcDepthRange, const float4x4& mMvp)
{
    MvpToPlanes(ucNdcDepthRange, mMvp, m_vPlane);

    m_mPlanesT.SetCol0(m_vPlane[PLANE_LEFT]);
    m_mPlanesT.SetCol1(m_vPlane[PLANE_RIGHT]);
    m_mPlanesT.SetCol2(m_vPlane[PLANE_BOTTOM]);
    m_mPlanesT.SetCol3(m_vPlane[PLANE_TOP]);
    m_mPlanesT.Transpose();

    for( uint32_t i = 0; i < PLANES_NUM; i++ )
        m_vMask[i] = _mm_cmpgt_ps(m_vPlane[i].xmm, xmm_zero);
}

void cFrustum::Translate(const float3& vPos)
{
    // NOTE: update of m_vMask is not required, because only m_vMask.w can be changed, but this component doesn't affect results

    for( uint32_t i = 0; i < PLANES_NUM; i++ )
        m_vPlane[i].w = Dot43(m_vPlane[i], vPos);
}

bool cFrustum::CheckSphere(const float3& center, float fRadius, uint32_t planes) const
{
    v4f p1 = xmm_setw1(center.xmm);

    for( uint32_t i = 0; i < planes; i++ )
    {
        float d = Dot44(m_vPlane[i].xmm, p1);

        if( d < -fRadius )
            return false;
    }

    return true;
}

bool cFrustum::CheckAabb(const float3& minv, const float3& maxv, uint32_t planes) const
{
    v4f min1 = xmm_setw1(minv.xmm);
    v4f max1 = xmm_setw1(maxv.xmm);

    for( uint32_t i = 0; i < planes; i++ )
    {
        v4f v = xmm_select(min1, max1, m_vMask[i]);
        v = xmm_dot44(m_vPlane[i].xmm, v);

        if( xmm_isnegative1_all(v) )
            return false;
    }

    return true;
}

bool cFrustum::CheckCapsule(const float3& capsule_start, const float3& capsule_axis, float capsule_radius, uint32_t planes) const
{
    // NOTE: https://github.com/toxygen/STA/blob/master/celestia-src/celmath/frustum.cpp

    float r2 = capsule_radius * capsule_radius;
    float3 capsule_end = capsule_start + capsule_axis;

    for( uint32_t i = 0; i < planes; i++ )
    {
        float signedDist0 = Dot43(m_vPlane[i], capsule_start);
        float signedDist1 = Dot43(m_vPlane[i], capsule_end);

        if( signedDist0 * signedDist1 > r2 )
        {
            if( Abs(signedDist0) <= Abs(signedDist1) )
            {
                if( signedDist0 < -capsule_radius )
                    return false;
            }
            else
            {
                if( signedDist1 < -capsule_radius )
                    return false;
            }
        }
    }

    return true;
}

bool cFrustum::CheckSphere_mask(const float3& center, float fRadius, uint32_t mask, uint32_t planes) const
{
    v4f p1 = xmm_setw1(center.xmm);

    for( uint32_t i = 0; i < planes; i++ )
    {
        if( !(mask & (1 << i)) )
        {
            float d = Dot44(m_vPlane[i].xmm, p1);

            if( d < -fRadius )
                return false;
        }
    }

    return true;
}

bool cFrustum::CheckAabb_mask(const float3& minv, const float3& maxv, uint32_t mask, uint32_t planes) const
{
    v4f min1 = xmm_setw1(minv.xmm);
    v4f max1 = xmm_setw1(maxv.xmm);

    for( uint32_t i = 0; i < planes; i++ )
    {
        if( !(mask & (1 << i)) )
        {
            v4f v = xmm_select(min1, max1, m_vMask[i]);
            v = xmm_dot44(m_vPlane[i].xmm, v);

            if( xmm_isnegative1_all(v) )
                return false;
        }
    }

    return true;
}

eClip cFrustum::CheckSphere_state(const float3& center, float fRadius, uint32_t planes) const
{
    v4f p1 = xmm_setw1(center.xmm);

    eClip clip = CLIP_IN;

    for( uint32_t i = 0; i < planes; i++ )
    {
        float d = Dot44(m_vPlane[i].xmm, p1);

        if( d < -fRadius )
            return CLIP_OUT;

        if( d < fRadius )
            clip = CLIP_PARTIAL;
    }

    return clip;
}

eClip cFrustum::CheckAabb_state(const float3& minv, const float3& maxv, uint32_t planes) const
{
    v4f min1 = xmm_setw1(minv.xmm);
    v4f max1 = xmm_setw1(maxv.xmm);

    eClip clip = CLIP_IN;

    for( uint32_t i = 0; i < planes; i++ )
    {
        v4f v = xmm_select(min1, max1, m_vMask[i]);
        v = xmm_dot44(m_vPlane[i].xmm, v);

        if( xmm_isnegative1_all(v) )
            return CLIP_OUT;

        v = xmm_select(max1, min1, m_vMask[i]);
        v = xmm_dot44(m_vPlane[i].xmm, v);

        if( xmm_isnegative1_all(v) )
            clip = CLIP_PARTIAL;
    }

    return clip;
}

eClip cFrustum::CheckCapsule_state(const float3& capsule_start, const float3& capsule_axis, float capsule_radius, uint32_t planes) const
{
    float r2 = capsule_radius * capsule_radius;
    float3 capsule_end = capsule_start + capsule_axis;

    uint32_t intersections = 0;

    for( uint32_t i = 0; i < planes; i++ )
    {
        float signedDist0 = Dot43(m_vPlane[i], capsule_start);
        float signedDist1 = Dot43(m_vPlane[i], capsule_end);

        if( signedDist0 * signedDist1 > r2 )
        {
            // Endpoints of capsule are on same side of plane;
            // test closest endpoint to see if it lies closer to the plane than radius

            if( Abs(signedDist0) <= Abs(signedDist1) )
            {
                if( signedDist0 < -capsule_radius )
                    return CLIP_OUT;
                else if( signedDist0 < capsule_radius )
                    intersections |= (1 << i);
            }
            else
            {
                if( signedDist1 < -capsule_radius )
                    return CLIP_OUT;
                else if( signedDist1 < capsule_radius )
                    intersections |= (1 << i);
            }
        }
        else
        {
            // Capsule endpoints are on different sides of the plane, so we have an intersection
            intersections |= (1 << i);
        }
    }

    return !intersections ? CLIP_IN : CLIP_PARTIAL;
}

eClip cFrustum::CheckSphere_mask_state(const float3& center, float fRadius, uint32_t& mask, uint32_t planes) const
{
    v4f p1 = xmm_setw1(center.xmm);

    eClip clip = CLIP_IN;

    for( uint32_t i = 0; i < planes; i++ )
    {
        if( !(mask & (1 << i)) )
        {
            float d = Dot44(m_vPlane[i].xmm, p1);

            if( d < -fRadius )
                return CLIP_OUT;

            if( d < fRadius )
                clip = CLIP_PARTIAL;
            else
                mask |= 1 << i;
        }
    }

    return clip;
}

eClip cFrustum::CheckAabb_mask_state(const float3& minv, const float3& maxv, uint32_t& mask, uint32_t planes) const
{
    v4f min1 = xmm_setw1(minv.xmm);
    v4f max1 = xmm_setw1(maxv.xmm);

    eClip result = CLIP_IN;

    for( uint32_t i = 0; i < planes; i++ )
    {
        if( !(mask & (1 << i)) )
        {
            v4f v = xmm_select(min1, max1, m_vMask[i]);
            v = xmm_dot44(m_vPlane[i].xmm, v);

            if( xmm_isnegative1_all(v) )
                return CLIP_OUT;

            v = xmm_select(max1, min1, m_vMask[i]);
            v = xmm_dot44(m_vPlane[i].xmm, v);

            if( xmm_isnegative1_all(v) )
                result = CLIP_PARTIAL;
            else
                mask |= 1 << i;
        }
    }

    return result;
}

/*============================================================================================================
                                            Double to float
============================================================================================================*/

float DoubleToGequal(double dValue)
{
    float fValue = (float)dValue;
    float fError = (float)(dValue - fValue);

    int32_t exponent = 0;
    frexp(fValue, &exponent);
    exponent = Max(exponent, 0);
    exponent = (int32_t)log10f(float(1 << exponent));

    float fStep = 1.0f / Pow(10.0f, float(7 - exponent));

    while( fError > 0.0f )
    {
        fValue += fStep;

        float fCurrError = float(dValue - fValue);

        if( fCurrError == fError )
            fStep += fStep;
        else
            fError = fCurrError;
    }

    return fValue;
}

float DoubleToLequal(double dValue)
{
    float fValue = (float)dValue;
    float fError = (float)(dValue - fValue);

    int32_t exponent = 0;
    frexp(fValue, &exponent);
    exponent = Max(exponent, 0);
    exponent = (int32_t)log10f(float(1 << exponent));

    float fStep = 1.0f / Pow(10.0f, float(7 - exponent));

    while( fError < 0.0f )
    {
        fValue -= fStep;

        float fCurrError = float(dValue - fValue);

        if( fCurrError == fError )
            fStep += fStep;
        else
            fError = fCurrError;
    }

    return fValue;
}

/*============================================================================================================
                                                Misc
============================================================================================================*/

void DecomposeProjection(uint8_t ucNdcOrigin, uint8_t ucNdcDepth, const float4x4& proj, uint32_t* puiFlags, float* pfSettings15, float* pfUnproject2, float* pfFrustum4, float* pfProject3, float* pfSafeNearZ)
{
    float4 vPlane[PLANES_NUM];
    bool bReversedZ = MvpToPlanes(ucNdcDepth, proj, vPlane);

    bool bIsOrtho = proj.a33 == 1.0f ? true : false;

    float fNearZ = -vPlane[PLANE_NEAR].w;
    float fFarZ = vPlane[PLANE_FAR].w;

    float x0, x1, y0, y1;
    bool bLeftHanded;

    if( bIsOrtho )
    {
        x0 = -vPlane[PLANE_LEFT].w;
        x1 = vPlane[PLANE_RIGHT].w;
        y0 = -vPlane[PLANE_BOTTOM].w;
        y1 = vPlane[PLANE_TOP].w;

        bLeftHanded = proj.a22 > 0.0f;
    }
    else
    {
        x0 = vPlane[PLANE_LEFT].z / vPlane[PLANE_LEFT].x;
        x1 = vPlane[PLANE_RIGHT].z / vPlane[PLANE_RIGHT].x;
        y0 = vPlane[PLANE_BOTTOM].z / vPlane[PLANE_BOTTOM].y;
        y1 = vPlane[PLANE_TOP].z / vPlane[PLANE_TOP].y;

        bLeftHanded = x0 > x1 && y0 > y1;
    }

    if( puiFlags )
    {
        *puiFlags = bIsOrtho ? PROJ_ORTHO : 0;
        *puiFlags |= bReversedZ ? PROJ_REVERSED_Z : 0;
        *puiFlags |= bLeftHanded ? PROJ_LEFT_HANDED : 0;
    }

    if( pfUnproject2 )
        Zbuffer::UnprojectZ(pfUnproject2, proj.a22, proj.a23, proj.a32);

    if( pfSafeNearZ )
    {
        *pfSafeNearZ = fNearZ - _DEPTH_EPS;

        if( !bIsOrtho )
        {
            float maxx = Max(Abs(x0), Abs(x1));
            float maxy = Max(Abs(y0), Abs(y1));

            *pfSafeNearZ *= Sqrt(maxx * maxx + maxy * maxy + 1.0f);
        }
    }

    if( pfProject3 )
    {
        // IMPORTANT: Rg - geometry radius, Rp - projected radius, Rn - projected normalized radius

        //      keep in mind:
        //          zp = -(mView * p).z
        //          zp_fix = mix(zp, 1.0, bIsOrtho), or
        //          zp_fix = (mViewProj * p).w

        //      project:
        //          Rn.x = Rg * pfProject3[0] / zp_fix
        //          Rn.y = Rg * pfProject3[1] / zp_fix
        //          Rp = 0.5 * viewport.w * Rn.x, or
        //          Rp = 0.5 * viewport.h * Rn.y, or
        //          Rp = Rg * K / zp_fix

        //
        //      unproject:
        //          Rn.x = 2.0 * Rp / viewport.w
        //          Rn.y = 2.0 * Rp / viewport.h
        //          Rg = Rn.x * zp_fix / pfProject3[0], or
        //          Rg = Rn.y * zp_fix / pfProject3[1], or
        //          Rg = Rp * zp_fix / K

        //          K = 0.5 * viewport.w * pfProject3[0] = 0.5 * viewport.h * pfProject3[1]

        float fProjectx = 2.0f / (x1 - x0);
        float fProjecty = 2.0f / (y1 - y0);

        pfProject3[0] = Abs(fProjectx);
        pfProject3[1] = Abs(fProjecty);
        pfProject3[2] = bIsOrtho ? 1.0f : 0.0f;
    }

    if( pfFrustum4 )
    {
        // IMPORTANT: view space position from screen space uv [0, 1]
        //          ray.xy = (pfFrustum4.zw * uv + pfFrustum4.xy) * mix(zDistanceNeg, -1.0, bIsOrtho)
        //          ray.z = 1.0 * zDistanceNeg

        pfFrustum4[0] = -x0;
        pfFrustum4[2] = x0 - x1;

        if( ucNdcOrigin == NDC_D3D )
        {
            pfFrustum4[1] = -y1;
            pfFrustum4[3] = y1 - y0;
        }
        else
        {
            pfFrustum4[1] = -y0;
            pfFrustum4[3] = y0 - y1;
        }
    }

    if( pfSettings15 )
    {
        // NOTE: swap is possible, because it is the last pass...

        if( bLeftHanded )
        {
            Swap(x0, x1);
            Swap(y0, y1);
        }

        float fAngleY0 = Atan(bIsOrtho ? 0.0f : y0);
        float fAngleY1 = Atan(bIsOrtho ? 0.0f : y1);
        float fAngleX0 = Atan(bIsOrtho ? 0.0f : x0);
        float fAngleX1 = Atan(bIsOrtho ? 0.0f : x1);

        float fAspect = (x1 - x0) / (y1 - y0);

        pfSettings15[PROJ_ZNEAR]        = fNearZ;
        pfSettings15[PROJ_ZFAR]         = fFarZ;
        pfSettings15[PROJ_ASPECT]       = fAspect;
        pfSettings15[PROJ_FOVX]         = fAngleX1 - fAngleX0;
        pfSettings15[PROJ_FOVY]         = fAngleY1 - fAngleY0;
        pfSettings15[PROJ_MINX]         = x0 * fNearZ;
        pfSettings15[PROJ_MAXX]         = x1 * fNearZ;
        pfSettings15[PROJ_MINY]         = y0 * fNearZ;
        pfSettings15[PROJ_MAXY]         = y1 * fNearZ;
        pfSettings15[PROJ_ANGLEMINX]    = fAngleX0;
        pfSettings15[PROJ_ANGLEMAXX]    = fAngleX1;
        pfSettings15[PROJ_ANGLEMINY]    = fAngleY0;
        pfSettings15[PROJ_ANGLEMAXY]    = fAngleY1;
        pfSettings15[PROJ_DIRX]         = (fAngleX0 + fAngleX1) * 0.5f;
        pfSettings15[PROJ_DIRY]         = (fAngleY0 + fAngleY1) * 0.5f;
    }
}

/*============================================================================================================
                                            Cubic filtering
============================================================================================================*/

float CubicFilter(float x, float i0, float i1, float i2, float i3)
{
    float x1 = x + 1.0f;
    float x2 = x1 * x1;
    float x3 = x2 * x1;
    float h = -x3 + 5.0f * x2 - 8.0f * x1 + 4.0f;
    float result = h * i0;

    x1 = x;
    x2 = x1 * x1;
    x3 = x2 * x1;
    h = x3 - 2.0f * x2 + 1.0f;
    result += h * i1;

    x1 = 1.0f - x;
    x2 = x1 * x1;
    x3 = x2 * x1;
    h = x3 - 2.0f * x2 + 1.0f;
    result += h * i2;

    x1 = 2.0f - x;
    x2 = x1 * x1;
    x3 = x2 * x1;
    h = -x3 + 5.0f * x2 - 8.0f * x1 + 4.0f;
    result += h * i3;

    return result;
}

// NOTE: AABB-triangle overlap test code by Tomas Akenine-Moller
//       http://fileadmin.cs.lth.se/cs/Personal/Tomas_Akenine-Moller/code/
//       SSE code from http://www.codercorner.com/blog/?p=1118

PLATFORM_INLINE uint32_t TestClassIII(const v4f& e0V, const v4f& v0V, const v4f& v1V, const v4f& v2V, const v4f& extents)
{
    v4f fe0ZYX_V = xmm_abs(e0V);

    v4f e0XZY_V = xmm_swizzle(e0V, 1, 2, 0, 3);
    v4f v0XZY_V = xmm_swizzle(v0V, 1, 2, 0, 3);
    v4f v1XZY_V = xmm_swizzle(v1V, 1, 2, 0, 3);
    v4f v2XZY_V = xmm_swizzle(v2V, 1, 2, 0, 3);
    v4f fe0XZY_V = xmm_swizzle(fe0ZYX_V, 1, 2, 0, 3);
    v4f extentsXZY_V = xmm_swizzle(extents, 1, 2, 0, 3);

    v4f radV = _mm_add_ps(_mm_mul_ps(extents, fe0XZY_V), _mm_mul_ps(extentsXZY_V, fe0ZYX_V));
    v4f p0V = _mm_sub_ps(_mm_mul_ps(v0V, e0XZY_V), _mm_mul_ps(v0XZY_V, e0V));
    v4f p1V = _mm_sub_ps(_mm_mul_ps(v1V, e0XZY_V), _mm_mul_ps(v1XZY_V, e0V));
    v4f p2V = _mm_sub_ps(_mm_mul_ps(v2V, e0XZY_V), _mm_mul_ps(v2XZY_V, e0V));

    v4f minV = _mm_min_ps(_mm_min_ps(p0V, p1V), p2V);
    v4f maxV = _mm_max_ps(_mm_max_ps(p0V, p1V), p2V);

    uint32_t test = _mm_movemask_ps(_mm_cmpgt_ps(minV, radV));
    radV = _mm_sub_ps(xmm_zero, radV);
    test |= _mm_movemask_ps(_mm_cmpgt_ps(radV, maxV));

    return test & 7;
}

bool IsOverlapBoxTriangle(const float3& boxcenter, const float3& extents, const float3& p0, const float3& p1, const float3& p2)
{
    v4f v0V = _mm_sub_ps(p0.xmm, boxcenter.xmm);
    v4f cV = xmm_abs(v0V);
    uint32_t test = _mm_movemask_ps(_mm_sub_ps(cV, extents.xmm));

    if( (test & 7) == 7 )
        return true;

    v4f v1V = _mm_sub_ps(p1.xmm, boxcenter.xmm);
    v4f v2V = _mm_sub_ps(p2.xmm, boxcenter.xmm);
    v4f minV = _mm_min_ps(v0V, v1V);
    minV = _mm_min_ps(minV, v2V);
    test = _mm_movemask_ps(_mm_cmpgt_ps(minV, extents.xmm));

    if( test & 7 )
        return false;

    v4f maxV = _mm_max_ps(v0V, v1V);
    maxV = _mm_max_ps(maxV, v2V);
    cV = _mm_sub_ps(xmm_zero, extents.xmm);
    test = _mm_movemask_ps(_mm_cmpgt_ps(cV, maxV));

    if( test & 7 )
        return false;

    v4f e0V = _mm_sub_ps(v1V, v0V);
    v4f e1V = _mm_sub_ps(v2V, v1V);
    v4f normalV = xmm_cross(e0V, e1V);
    v4f dV = xmm_dot33(normalV, v0V);

    v4f normalSignsV = _mm_and_ps(normalV, c_xmmSign);
    maxV = _mm_or_ps(extents.xmm, normalSignsV);

    v4f tmpV = xmm_dot33(normalV, maxV);
    test = _mm_movemask_ps(_mm_cmpgt_ps(dV, tmpV));

    if( test & 7 )
        return false;

    normalSignsV = _mm_xor_ps(normalSignsV, c_xmmSign);
    minV = _mm_or_ps(extents.xmm, normalSignsV);

    tmpV = xmm_dot33(normalV, minV);
    test = _mm_movemask_ps(_mm_cmpgt_ps(tmpV, dV));

    if( test & 7 )
        return false;

    if( TestClassIII(e0V, v0V, v1V, v2V, extents.xmm) )
        return false;

    if( TestClassIII(e1V, v0V, v1V, v2V, extents.xmm) )
        return false;

    v4f e2V = _mm_sub_ps(v0V, v2V);

    if( TestClassIII(e2V, v0V, v1V, v2V, extents.xmm) )
        return false;

    return true;
}

/*============================================================================================================
                            Barycentric ray-triangle test by Tomas Akenine-Moller
============================================================================================================*/

bool IsIntersectRayTriangle(const float3& origin, const float3& dir, const float3& v1, const float3& v2, const float3& v3, float3& out_tuv)
{
    // find vectors for two edges sharing vert0

    float3 e1 = v2 - v1;
    float3 e2 = v3 - v1;

    // begin calculating determinant - also used to calculate U parameter

    float3 pvec = Cross(dir, e2);

    // if determinant is near zero, ray lies in plane of triangle

    float det = Dot33(e1, pvec);

    if( det < -c_fEps )
        return false;

    // calculate distance from vert0 to ray origin

    float3 tvec = origin - v1;

    // calculate U parameter and test bounds

    float u = Dot33(tvec, pvec);

    if( u < 0.0f || u > det )
        return false;

    // prepare to test V parameter

    float3 qvec = Cross(tvec, e1);

    // calculate V parameter and test bounds

    float v = Dot33(dir, qvec);

    if( v < 0.0f || u + v > det )
        return false;

    // calculate t, scale parameters, ray intersects triangle

    out_tuv.x = Dot33(e2, qvec);
    out_tuv.y = u;          // v
    out_tuv.z = v;          // 1 - (u + v)

    float idet = 1.0f / det;
    out_tuv *= idet;

    return true;
}

bool IsIntersectRayTriangle(const float3& from, const float3& to, const float3& v1, const float3& v2, const float3& v3, float3& out_intersection, float3& out_normal)
{
    // find vectors for two edges sharing vert0

    float3 e1 = v2 - v1;
    float3 e2 = v3 - v1;

    // begin calculating determinant - also used to calculate U parameter
    float3 dir = to - from;
    float length = Length(dir);
    dir = Normalize(dir);

    float3 pvec = Cross(dir, e2);

    // if determinant is near zero, ray lies in plane of triangle

    float det = Dot33(e1, pvec);

    if( det < -c_fEps )
        return false;

    // calculate distance from vert0 to ray origin point "from"

    float3 tvec = from - v1;

    // calculate U parameter and test bounds

    float u = Dot33(tvec, pvec);

    if( u < 0.0f || u > det )
        return false;

    // prepare to test V parameter

    float3 qvec = Cross(tvec, e1);

    // calculate V parameter and test bounds

    float v = Dot33(dir, qvec);

    if( v < 0.0f || u + v > det )
        return false;

    // calculate t, scale parameters, ray intersects triangle

    float t = Dot33(e2, qvec) / det;

    if( t > length )
        return false;

    out_intersection = from + dir * t;
    out_normal = Normalize( Cross(e1, e2) );

    return true;
}

/*============================================================================================================
                                            Distribution & sampling
============================================================================================================*/

// http://www.cse.cuhk.edu.hk/~ttwong/papers/udpoint/udpoint.pdf

void Hammersley(float* pXyz, uint32_t n)
{
    for( uint32_t k = 0; k < n; k++ )
    {
        float t = 0.0f;
        float p = 0.5f;

        for( uint32_t kk = k; kk; p *= 0.5f, kk >>= 1 )
        {
            if( kk & 0x1 )
                t += p;
        }

        t = 2.0f * t - 1.0f;

        float phi = (k + 0.5f) / n;
        float phirad = phi * Pi(2.0f);
        float st = Sqrt(1.0f - t * t);

        *pXyz++ = st * Cos(phirad);
        *pXyz++ = st * Sin(phirad);
        *pXyz++ = t;
    }
}

/*======================================================================================================================
                                                        Numerical
=======================================================================================================================*/

uint32_t greatest_common_divisor(uint32_t a, uint32_t b)
{
    while( a && b )
    {
        if( a >= b )
            a = a % b;
        else
            b = b % a;
    }

    return a + b;
}

/*======================================================================================================================
                                            Transcendental functions (float)
=======================================================================================================================*/

// IMPORTANT: based on Sleef 2.80
// https://bitbucket.org/eschnett/vecmathlib/src
// http://shibatch.sourceforge.net/

#define Cf_PI4_A                    0.78515625f
#define Cf_PI4_B                    0.00024187564849853515625f
#define Cf_PI4_C                    3.7747668102383613586e-08f
#define Cf_PI4_D                    1.2816720341285448015e-12f

#define _xmm_vselecti(mask, x, y)   xmm_select(y, x, _mm_castsi128_ps(mask))
#define _xmm_vselect(mask, x, y)    xmm_select(y, x, mask)
#define _xmm_iselect(mask, x, y)    _mm_castps_si128(xmm_select(_mm_castsi128_ps(y), _mm_castsi128_ps(x), mask))

#define _xmm_mulsign(x, y)          _mm_xor_ps(x, _mm_and_ps(y, c_xmmSign))
#define _xmm_negatei(x)             _mm_sub_epi32(_mm_setzero_si128(), x)

#define _xmm_is_inf(x)              xmm_equal(xmm_abs(x), c_xmmInf)
#define _xmm_is_pinf(x)             xmm_equal(x, c_xmmInf)
#define _xmm_is_ninf(x)             xmm_equal(x, c_xmmInfMinus)
#define _xmm_is_nan(x)              xmm_notequal(x, x)
#define _xmm_is_inf2(x, y)          _mm_and_ps(_xmm_is_inf(x), _mm_or_ps(_mm_and_ps(x, c_xmmSign), y))

static const float c_f[] =
{
    1.0f / Pi(1.0f),
    0.00282363896258175373077393f,
    -0.0159569028764963150024414f,
    0.0425049886107444763183594f,
    -0.0748900920152664184570312f,
    0.106347933411598205566406f,
    -0.142027363181114196777344f,
    0.199926957488059997558594f,
    -0.333331018686294555664062f,
    Pi(0.5f),
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
    2.0f / Pi(1.0f),
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
    Pi(0.25f),
    -1.0f,
    0.5f,
    Pi(1.0f),
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

PLATFORM_INLINE v4f _xmm_is_inf_or_zero(const v4f& x)
{
    v4f t = xmm_abs(x);

    return _mm_or_ps(xmm_equal(t, xmm_zero), xmm_equal(t, c_xmmInf));
}

PLATFORM_INLINE v4f _xmm_atan2(const v4f& y, const v4f& x)
{
    v4i q = _xmm_iselect(_mm_cmplt_ps(x, xmm_zero), _mm_set1_epi32(-2), _mm_setzero_si128());
    v4f r = xmm_abs(x);

    v4f mask = _mm_cmplt_ps(r, y);
    q = _xmm_iselect(mask, _mm_add_epi32(q, _mm_set1_epi32(1)), q);
    v4f s = _xmm_vselect(mask, xmm_negate(r), y);
    v4f t = _mm_max_ps(r, y);

    s = _mm_div_ps(s, t);
    t = _mm_mul_ps(s, s);

    v4f u = _mm_broadcast_ss(c_f + 1);
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 2));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 3));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 4));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 5));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 6));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 7));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 8));

    t = xmm_madd(s, _mm_mul_ps(t, u), s);
    t = xmm_madd(_mm_cvtepi32_ps(q), _mm_broadcast_ss(c_f + 9), t);

    return t;
}

PLATFORM_INLINE v4i _xmm_logbp1(const v4f& d)
{
    v4f m = _mm_cmplt_ps(d, _mm_broadcast_ss(c_f + 10));
    v4f r = _xmm_vselect(m, _mm_mul_ps(_mm_broadcast_ss(c_f + 11), d), d);
    v4i q = _mm_and_si128(_mm_srli_epi32(_mm_castps_si128(r), 23), _mm_set1_epi32(0xff));
    q = _mm_sub_epi32(q, _xmm_iselect(m, _mm_set1_epi32(64 + 0x7e), _mm_set1_epi32(0x7e)));

    return q;
}

PLATFORM_INLINE v4f _xmm_ldexp(const v4f& x, const v4i& q)
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

v4f _mm_sin_ps(const v4f& x)
{
    v4i q = _mm_cvtps_epi32( _mm_mul_ps(x, _mm_broadcast_ss(c_f + 0)) );
    v4f u = _mm_cvtepi32_ps(q);

    v4f r = xmm_madd(u, _mm_broadcast_ss(c_f + 12), x);
    r = xmm_madd(u, _mm_broadcast_ss(c_f + 13), r);
    r = xmm_madd(u, _mm_broadcast_ss(c_f + 14), r);
    r = xmm_madd(u, _mm_broadcast_ss(c_f + 15), r);

    v4f s = _mm_mul_ps(r, r);

    r = _mm_castsi128_ps(_mm_xor_si128(_mm_and_si128(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1)), _mm_castps_si128(c_xmmSign)), _mm_castps_si128(r)));

    u = _mm_broadcast_ss(c_f + 16);
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 17));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 18));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 19));
    u = xmm_madd(s, _mm_mul_ps(u, r), r);

    u = _mm_or_ps(_xmm_is_inf(r), u);

    return u;
}

v4f _mm_cos_ps(const v4f& x)
{
    v4i q = _mm_cvtps_epi32(_mm_sub_ps(_mm_mul_ps(x, _mm_broadcast_ss(c_f + 0)), _mm_broadcast_ss(c_f + 42)));
    q = _mm_add_epi32(_mm_add_epi32(q, q), _mm_set1_epi32(1));

    v4f u = _mm_cvtepi32_ps(q);

    v4f r = xmm_madd(u, _mm_broadcast_ss(c_f + 20), x);
    r = xmm_madd(u, _mm_broadcast_ss(c_f + 21), r);
    r = xmm_madd(u, _mm_broadcast_ss(c_f + 22), r);
    r = xmm_madd(u, _mm_broadcast_ss(c_f + 23), r);

    v4f s = _mm_mul_ps(r, r);

    r = _mm_castsi128_ps(_mm_xor_si128(_mm_and_si128(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_setzero_si128()), _mm_castps_si128(c_xmmSign)), _mm_castps_si128(r)));

    u = _mm_broadcast_ss(c_f + 16);
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 17));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 18));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 19));
    u = xmm_madd(s, _mm_mul_ps(u, r), r);

    u = _mm_or_ps(_xmm_is_inf(r), u);

    return u;
}

v4f _mm_sincos_ps(v4f* pCos, const v4f& d)
{
    v4i q = _mm_cvtps_epi32(_mm_mul_ps(d, _mm_broadcast_ss(c_f + 24)));

    v4f s = d;

    v4f u = _mm_cvtepi32_ps(q);
    s = xmm_madd(u, _mm_broadcast_ss(c_f + 20), s);
    s = xmm_madd(u, _mm_broadcast_ss(c_f + 21), s);
    s = xmm_madd(u, _mm_broadcast_ss(c_f + 22), s);
    s = xmm_madd(u, _mm_broadcast_ss(c_f + 23), s);

    v4f t = s;

    s = _mm_mul_ps(s, s);

    u = _mm_broadcast_ss(c_f + 25);
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 26));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 27));
    u = _mm_mul_ps(_mm_mul_ps(u, s), t);

    v4f rx = _mm_add_ps(t, u);

    u = _mm_broadcast_ss(c_f + 28);
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 29));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 30));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 31));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 32));

    v4f ry = xmm_madd(s, u, _mm_broadcast_ss(c_f + 33));

    v4f m = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(0)));
    v4f rrx = _xmm_vselect(m, rx, ry);
    v4f rry = _xmm_vselect(m, ry, rx);

    m = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_set1_epi32(2)));
    rrx = _mm_xor_ps(_mm_and_ps(m, c_xmmSign), rrx);

    m = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(_mm_add_epi32(q, _mm_set1_epi32(1)), _mm_set1_epi32(2)), _mm_set1_epi32(2)));
    rry = _mm_xor_ps(_mm_and_ps(m, c_xmmSign), rry);

    m = _xmm_is_inf(d);

    *pCos = _mm_or_ps(m, rry);

    return _mm_or_ps(m, rrx);
}

v4f _mm_tan_ps(const v4f& x)
{
    v4i q = _mm_cvtps_epi32(_mm_mul_ps(x, _mm_broadcast_ss(c_f + 24)));
    v4f r = x;

    v4f u = _mm_cvtepi32_ps(q);
    r = xmm_madd(u, _mm_broadcast_ss(c_f + 20), r);
    r = xmm_madd(u, _mm_broadcast_ss(c_f + 21), r);
    r = xmm_madd(u, _mm_broadcast_ss(c_f + 22), r);
    r = xmm_madd(u, _mm_broadcast_ss(c_f + 23), r);

    v4f s = _mm_mul_ps(r, r);

    v4i m = _mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1));
    r = _mm_castsi128_ps(_mm_xor_si128(_mm_and_si128(m, _mm_castps_si128(c_xmmSign)), _mm_castps_si128(r)));

    u = _mm_broadcast_ss(c_f + 34);
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 35));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 36));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 37));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 38));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 39));

    u = xmm_madd(s, _mm_mul_ps(u, r), r);
    u = _xmm_vselecti(m, xmm_rcp(u), u);

    u = _mm_or_ps(_xmm_is_inf(r), u);

    return u;
}

v4f _mm_atan_ps(const v4f& d)
{
    v4i q = _xmm_iselect(_mm_cmplt_ps(d, xmm_zero), _mm_set1_epi32(2), _mm_setzero_si128());
    v4f s = xmm_abs(d);

    v4f mask = _mm_cmplt_ps(_mm_broadcast_ss(c_f + 33), s);
    q = _xmm_iselect(mask, _mm_add_epi32(q, _mm_set1_epi32(1)), q);
    s = _xmm_vselect(mask, xmm_rcp(s), s);

    v4f t = _mm_mul_ps(s, s);

    v4f u = _mm_broadcast_ss(c_f + 1);
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 2));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 3));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 4));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 5));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 6));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 7));
    u = xmm_madd(u, t, _mm_broadcast_ss(c_f + 8));

    t = xmm_madd(s, _mm_mul_ps(t, u), s);
    t = _xmm_vselecti(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1)), _mm_sub_ps(_mm_broadcast_ss(c_f + 9), t), t);

    t = _mm_castsi128_ps(_mm_xor_si128(_mm_and_si128(_mm_cmpeq_epi32(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_set1_epi32(2)), _mm_castps_si128(c_xmmSign)), _mm_castps_si128(t)));

    return t;
}

v4f _mm_atan2_ps(const v4f& y, const v4f& x)
{
    v4f r = _xmm_atan2(xmm_abs(y), x);

    r = _xmm_mulsign(r, x);
    r = _xmm_vselect(_xmm_is_inf_or_zero(x), _mm_sub_ps(_mm_broadcast_ss(c_f + 9), _xmm_is_inf2(x, _xmm_mulsign(_mm_broadcast_ss(c_f + 9), x))), r);
    r = _xmm_vselect(_xmm_is_inf(y), _mm_sub_ps(_mm_broadcast_ss(c_f + 9), _xmm_is_inf2(x, _xmm_mulsign(_mm_broadcast_ss(c_f + 40), x))), r);
    r = _xmm_vselect(_mm_cmpeq_ps(y, xmm_zero), _mm_xor_ps(_mm_cmpeq_ps(xmm_sign(x), _mm_broadcast_ss(c_f + 41)), _mm_broadcast_ss(c_f + 43)), r);

    r = _mm_or_ps(_mm_or_ps(_xmm_is_nan(x), _xmm_is_nan(y)), _xmm_mulsign(r, y));

    return r;
}

v4f _mm_asin_ps(const v4f& d)
{
    v4f x = _mm_add_ps(_mm_broadcast_ss(c_f + 33), d);
    v4f y = _mm_sub_ps(_mm_broadcast_ss(c_f + 33), d);
    x = _mm_mul_ps(x, y);
    x = xmm_sqrt(x);
    x = _mm_or_ps(_xmm_is_nan(x), _xmm_atan2(xmm_abs(d), x));

    return _xmm_mulsign(x, d);
}

v4f _mm_acos_ps(const v4f& d)
{
    v4f x = _mm_add_ps(_mm_broadcast_ss(c_f + 33), d);
    v4f y = _mm_sub_ps(_mm_broadcast_ss(c_f + 33), d);
    x = _mm_mul_ps(x, y);
    x = xmm_sqrt(x);
    x = _xmm_mulsign(_xmm_atan2(x, xmm_abs(d)), d);
    y = _mm_and_ps(_mm_cmplt_ps(d, xmm_zero), _mm_broadcast_ss(c_f + 43));
    x = _mm_add_ps(x, y);

    return x;
}

v4f _mm_log_ps(const v4f& d)
{
    v4f x = _mm_mul_ps(d, _mm_broadcast_ss(c_f + 44));
    v4i e = _xmm_logbp1(x);
    v4f m = _xmm_ldexp(d, _xmm_negatei(e));
    v4f r = x;

    x = _mm_div_ps(_mm_add_ps(_mm_broadcast_ss(c_f + 41), m), _mm_add_ps(_mm_broadcast_ss(c_f + 33), m));
    v4f x2 = _mm_mul_ps(x, x);

    v4f t = _mm_broadcast_ss(c_f + 45);
    t = xmm_madd(t, x2, _mm_broadcast_ss(c_f + 46));
    t = xmm_madd(t, x2, _mm_broadcast_ss(c_f + 47));
    t = xmm_madd(t, x2, _mm_broadcast_ss(c_f + 48));
    t = xmm_madd(t, x2, _mm_broadcast_ss(c_f + 49));

    x = xmm_madd(x, t, _mm_mul_ps(_mm_broadcast_ss(c_f + 50), _mm_cvtepi32_ps(e)));
    x = _xmm_vselect(_xmm_is_pinf(r), c_xmmInf, x);

    x = _mm_or_ps(_mm_cmpgt_ps(xmm_zero, r), x);
    x = _xmm_vselect(_mm_cmpeq_ps(r, xmm_zero), c_xmmInfMinus, x);

    return x;
}

v4f _mm_exp_ps(const v4f& d)
{
    v4i q = _mm_cvtps_epi32(_mm_mul_ps(d, _mm_broadcast_ss(c_f + 51)));

    v4f s = xmm_madd(_mm_cvtepi32_ps(q), _mm_broadcast_ss(c_f + 52), d);
    s = xmm_madd(_mm_cvtepi32_ps(q), _mm_broadcast_ss(c_f + 53), s);

    v4f u = _mm_broadcast_ss(c_f + 54);
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 55));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 56));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 57));
    u = xmm_madd(u, s, _mm_broadcast_ss(c_f + 58));

    u = _mm_add_ps(_mm_broadcast_ss(c_f + 33), xmm_madd(_mm_mul_ps(s, s), u, s));
    u = _xmm_ldexp(u, q);

    u = _mm_andnot_ps(_xmm_is_ninf(d), u);

    return u;
}

/*======================================================================================================================
                                            Transcendental functions (double)
=======================================================================================================================*/

#define Cd_PI4_A                        0.78539816290140151978
#define Cd_PI4_B                        4.9604678871439933374e-10
#define Cd_PI4_C                        1.1258708853173288931e-18
#define Cd_PI4_D                        1.7607799325916000908e-27

#define _ymm_mulsign(x, y)              _mm256_xor_pd(x, _mm256_and_pd(y, c_ymmSign))

#define _ymm_is_inf(x)                  ymm_equal(ymm_abs(x), c_ymmInf)
#define _ymm_is_pinf(x)                 ymm_equal(x, c_ymmInf)
#define _ymm_is_ninf(x)                 ymm_equal(x, c_ymmInfMinus)
#define _ymm_is_nan(x)                  ymm_notequal(x, x)
#define _ymm_is_inf2(x, y)              _mm256_and_pd(_ymm_is_inf(x), _mm256_or_pd(_mm256_and_pd(x, c_ymmSign), y))

static const double c_d[] =
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
    Pi(0.5),
    4.9090934652977266E-91,
    2.037035976334486E90,
    300+0x3fe,
    0x3fe,
    1.0 / Pi(1.0),
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
    2.0 / Pi(1.0),
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
    Pi(0.25),
    -1.0,
    Pi(1.0),
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

PLATFORM_INLINE v4d _ymm_is_inf_or_zero(const v4d& x)
{
    v4d t = ymm_abs(x);

    return _mm256_or_pd(ymm_equal(t, ymm_zero), ymm_equal(t, c_ymmInf));
}

#define _ymm_vselect(mask, x, y)        ymm_select(y, x, mask)

PLATFORM_INLINE v4i _ymm_selecti(const v4d& d0, const v4d& d1, const v4i& x, const v4i& y)
{
    __m128i mask = _mm256_cvtpd_epi32(_mm256_and_pd(ymm_less(d0, d1), _mm256_broadcast_sd(c_d + 0)));
    mask = _mm_cmpeq_epi32(mask, _mm_set1_epi32(1));

    return _xmm_iselect(_mm_castsi128_ps(mask), x, y);
}

PLATFORM_INLINE v4d _ymm_cmp_4i(const v4i& x, const v4i& y)
{
    v4i t = _mm_cmpeq_epi32(x, y);

    return _mm256_castsi256_pd( _mm256_cvtepi32_epi64(t) );
}

PLATFORM_INLINE v4d _ymm_atan2(const v4d& y, const v4d& x)
{
    v4i q = _ymm_selecti(x, ymm_zero, _mm_set1_epi32(-2), _mm_setzero_si128());
    v4d r = ymm_abs(x);

    q = _ymm_selecti(r, y, _mm_add_epi32(q, _mm_set1_epi32(1)), q);
    v4d p = ymm_less(r, y);
    v4d s = _ymm_vselect(p, ymm_negate(r), y);
    v4d t = _mm256_max_pd(r, y);

    s = _mm256_div_pd(s, t);
    t = _mm256_mul_pd(s, s);

    v4d u = _mm256_broadcast_sd(c_d + 1);
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 2));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 3));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 4));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 5));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 6));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 7));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 8));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 9));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 10));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 11));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 12));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 13));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 14));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 15));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 16));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 17));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 18));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 19));

    t = ymm_madd(s, _mm256_mul_pd(t, u), s);
    t = ymm_madd(_mm256_cvtepi32_pd(q), _mm256_broadcast_sd(c_d + 20), t);

    return t;
}

PLATFORM_INLINE v4i _ymm_logbp1(const v4d& d)
{
    v4d m = ymm_less(d, _mm256_broadcast_sd(c_d + 21));
    v4d t = _ymm_vselect(m, _mm256_mul_pd(_mm256_broadcast_sd(c_d + 22), d), d);
    v4i c = _mm256_cvtpd_epi32(_ymm_vselect(m, _mm256_broadcast_sd(c_d + 23), _mm256_broadcast_sd(c_d + 24)));
    v4i q = _mm_castpd_si128(_mm256_castpd256_pd128(t));
    q = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(q), xmm_zero, _MM_SHUFFLE(0,0,3,1)));
    v4i r = _mm_castpd_si128(_mm256_extractf128_pd(t, 1));
    r = _mm_castps_si128(_mm_shuffle_ps(xmm_zero, _mm_castsi128_ps(r), _MM_SHUFFLE(3,1,0,0)));
    q = _mm_or_si128(q, r);
    q = _mm_srli_epi32(q, 20);
    q = _mm_sub_epi32(q, c);

    return q;
}

PLATFORM_INLINE v4d _ymm_pow2i(const v4i& q)
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

PLATFORM_INLINE v4d _ymm_ldexp(const v4d& x, const v4i& q)
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

    return _mm256_mul_pd(_mm256_mul_pd(_mm256_mul_pd(_mm256_mul_pd(_mm256_mul_pd(x, y), y), y), y), _ymm_pow2i(t));
}

v4d _mm256_sin_pd(const v4d& d)
{
    v4i q = _mm256_cvtpd_epi32(_mm256_mul_pd(d, _mm256_broadcast_sd(c_d + 25)));

    v4d u = _mm256_cvtepi32_pd(q);

    v4d r = ymm_madd(u, _mm256_broadcast_sd(c_d + 26), d);
    r = ymm_madd(u, _mm256_broadcast_sd(c_d + 27), r);
    r = ymm_madd(u, _mm256_broadcast_sd(c_d + 28), r);
    r = ymm_madd(u, _mm256_broadcast_sd(c_d + 29), r);

    v4d s = _mm256_mul_pd(r, r);

    r = _mm256_xor_pd(_mm256_and_pd(_ymm_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1)), c_ymmSign), r);

    u = _mm256_broadcast_sd(c_d + 30);
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 31));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 32));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 33));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 34));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 35));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 36));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 37));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 38));

    u = ymm_madd(s, _mm256_mul_pd(u, r), r);

    return u;
}

v4d _mm256_cos_pd(const v4d& d)
{
    v4i q = _mm256_cvtpd_epi32(ymm_madd(d, _mm256_broadcast_sd(c_d + 25), _mm256_broadcast_sd(c_d + 39)));
    q = _mm_add_epi32(_mm_add_epi32(q, q), _mm_set1_epi32(1));

    v4d u = _mm256_cvtepi32_pd(q);

    v4d r = ymm_madd(u, _mm256_broadcast_sd(c_d + 40), d);
    r = ymm_madd(u, _mm256_broadcast_sd(c_d + 41), r);
    r = ymm_madd(u, _mm256_broadcast_sd(c_d + 42), r);
    r = ymm_madd(u, _mm256_broadcast_sd(c_d + 43), r);

    v4d s = _mm256_mul_pd(r, r);

    r = _mm256_xor_pd(_mm256_and_pd(_ymm_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_setzero_si128()), c_ymmSign), r);

    u = _mm256_broadcast_sd(c_d + 30);
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 31));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 32));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 33));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 34));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 35));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 36));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 37));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 38));

    u = ymm_madd(s, _mm256_mul_pd(u, r), r);

    return u;
}

v4d _mm256_sincos_pd(v4d* pCos, const v4d& d)
{
    v4i q = _mm256_cvtpd_epi32(_mm256_mul_pd(d, _mm256_broadcast_sd(c_d + 44)));
    v4d s = d;

    v4d u = _mm256_cvtepi32_pd(q);
    s = ymm_madd(u, _mm256_broadcast_sd(c_d + 40), s);
    s = ymm_madd(u, _mm256_broadcast_sd(c_d + 41), s);
    s = ymm_madd(u, _mm256_broadcast_sd(c_d + 42), s);
    s = ymm_madd(u, _mm256_broadcast_sd(c_d + 43), s);

    v4d t = s;

    s = _mm256_mul_pd(s, s);

    u = _mm256_broadcast_sd(c_d + 45);
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 46));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 47));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 48));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 49));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 50));
    u = _mm256_mul_pd(_mm256_mul_pd(u, s), t);

    v4d rx = _mm256_add_pd(t, u);

    u = _mm256_broadcast_sd(c_d + 51);
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 52));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 53));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 54));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 55));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 56));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 39));

    v4d ry = ymm_madd(s, u, _mm256_broadcast_sd(c_d + 0));

    v4d m = _ymm_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_setzero_si128());
    v4d rrx = _ymm_vselect(m, rx, ry);
    v4d rry = _ymm_vselect(m, ry, rx);

    m = _ymm_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_set1_epi32(2));
    rrx = _mm256_xor_pd(_mm256_and_pd(m, c_ymmSign), rrx);

    m = _ymm_cmp_4i(_mm_and_si128(_mm_add_epi32(q, _mm_set1_epi32(1)), _mm_set1_epi32(2)), _mm_set1_epi32(2));
    rry = _mm256_xor_pd(_mm256_and_pd(m, c_ymmSign), rry);

    m = _ymm_is_inf(d);
    *pCos = _mm256_or_pd(m, rry);

    return _mm256_or_pd(m, rrx);
}

v4d _mm256_tan_pd(const v4d& d)
{
    v4i q = _mm256_cvtpd_epi32(_mm256_mul_pd(d, _mm256_broadcast_sd(c_d + 44)));

    v4d u = _mm256_cvtepi32_pd(q);

    v4d x = ymm_madd(u, _mm256_broadcast_sd(c_d + 40), d);
    x = ymm_madd(u, _mm256_broadcast_sd(c_d + 41), x);
    x = ymm_madd(u, _mm256_broadcast_sd(c_d + 42), x);
    x = ymm_madd(u, _mm256_broadcast_sd(c_d + 43), x);

    v4d s = _mm256_mul_pd(x, x);

    v4d m = _ymm_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1));
    x = _mm256_xor_pd(_mm256_and_pd(m, c_ymmSign), x);

    u = _mm256_broadcast_sd(c_d + 84);
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 85));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 86));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 87));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 88));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 89));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 90));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 91));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 92));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 93));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 94));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 95));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 96));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 97));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 98));

    u = ymm_madd(s, _mm256_mul_pd(u, x), x);
    u = _ymm_vselect(m, ymm_rcp(u), u);

    u = _mm256_or_pd(_ymm_is_inf(d), u);

    return u;
}

v4d _mm256_atan_pd(const v4d& s)
{
    v4i q = _ymm_selecti(s, ymm_zero, _mm_set1_epi32(2), _mm_setzero_si128());
    v4d r = ymm_abs(s);

    q = _ymm_selecti(_mm256_broadcast_sd(c_d + 0), r, _mm_add_epi32(q, _mm_set1_epi32(1)), q);
    r = _ymm_vselect(ymm_less(_mm256_broadcast_sd(c_d + 0), r), ymm_rcp(r), r);

    v4d t = _mm256_mul_pd(r, r);

    v4d u = _mm256_broadcast_sd(c_d + 1);
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 2));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 3));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 4));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 5));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 6));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 7));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 8));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 9));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 10));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 11));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 12));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 13));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 14));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 15));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 16));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 17));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 18));
    u = ymm_madd(u, t, _mm256_broadcast_sd(c_d + 19));

    t = ymm_madd(r, _mm256_mul_pd(t, u), r);

    t = _ymm_vselect(_ymm_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(1)), _mm_set1_epi32(1)), _mm256_sub_pd(_mm256_broadcast_sd(c_d + 20), t), t);
    t = _mm256_xor_pd(_mm256_and_pd(_ymm_cmp_4i(_mm_and_si128(q, _mm_set1_epi32(2)), _mm_set1_epi32(2)), c_ymmSign), t);

    return t;
}

v4d _mm256_atan2_pd(const v4d& y, const v4d& x)
{
    v4d r = _ymm_atan2(ymm_abs(y), x);

    r = _ymm_mulsign(r, x);
    r = _ymm_vselect(_mm256_or_pd(_ymm_is_inf(x), ymm_equal(x, ymm_zero)), _mm256_sub_pd(_mm256_broadcast_sd(c_d + 20), _ymm_is_inf2(x, _ymm_mulsign(_mm256_broadcast_sd(c_d + 20), x))), r);
    r = _ymm_vselect(_ymm_is_inf(y), _mm256_sub_pd(_mm256_broadcast_sd(c_d + 20), _ymm_is_inf2(x, _ymm_mulsign(_mm256_broadcast_sd(c_d + 57), x))), r);
    r = _ymm_vselect(ymm_equal(y, ymm_zero), _mm256_and_pd(ymm_equal(ymm_sign(x), _mm256_broadcast_sd(c_d + 58)), _mm256_broadcast_sd(c_d + 59)), r);

    r = _mm256_or_pd(_mm256_or_pd(_ymm_is_nan(x), _ymm_is_nan(y)), _ymm_mulsign(r, y));
    return r;
}

v4d _mm256_asin_pd(const v4d& d)
{
    v4d x = _mm256_add_pd(_mm256_broadcast_sd(c_d + 0), d);
    v4d y = _mm256_sub_pd(_mm256_broadcast_sd(c_d + 0), d);
    x = _mm256_mul_pd(x, y);
    x = ymm_sqrt(x);
    x = _mm256_or_pd(_ymm_is_nan(x), _ymm_atan2(ymm_abs(d), x));

    return _ymm_mulsign(x, d);
}

v4d _mm256_acos_pd(const v4d& d)
{
    v4d x = _mm256_add_pd(_mm256_broadcast_sd(c_d + 0), d);
    v4d y = _mm256_sub_pd(_mm256_broadcast_sd(c_d + 0), d);
    x = _mm256_mul_pd(x, y);
    x = ymm_sqrt(x);
    x = _ymm_mulsign(_ymm_atan2(x, ymm_abs(d)), d);
    y = _mm256_and_pd(ymm_less(d, ymm_zero), _mm256_broadcast_sd(c_d + 59));
    x = _mm256_add_pd(x, y);

    return x;
}

v4d _mm256_log_pd(const v4d& d)
{
    v4i e = _ymm_logbp1(_mm256_mul_pd(d, _mm256_broadcast_sd(c_d + 60)));
    v4d m = _ymm_ldexp(d, _xmm_negatei(e));

    v4d x = _mm256_div_pd(_mm256_add_pd(_mm256_broadcast_sd(c_d + 58), m), _mm256_add_pd(_mm256_broadcast_sd(c_d + 0), m));
    v4d x2 = _mm256_mul_pd(x, x);

    v4d t = _mm256_broadcast_sd(c_d + 61);
    t = ymm_madd(t, x2, _mm256_broadcast_sd(c_d + 62));
    t = ymm_madd(t, x2, _mm256_broadcast_sd(c_d + 63));
    t = ymm_madd(t, x2, _mm256_broadcast_sd(c_d + 64));
    t = ymm_madd(t, x2, _mm256_broadcast_sd(c_d + 65));
    t = ymm_madd(t, x2, _mm256_broadcast_sd(c_d + 66));
    t = ymm_madd(t, x2, _mm256_broadcast_sd(c_d + 67));
    t = ymm_madd(t, x2, _mm256_broadcast_sd(c_d + 68));

    x = ymm_madd(x, t, _mm256_mul_pd(_mm256_broadcast_sd(c_d + 69), _mm256_cvtepi32_pd(e)));

    x = _ymm_vselect(_ymm_is_pinf(d), c_ymmInf, x);
    x = _mm256_or_pd(ymm_greater(ymm_zero, d), x);
    x = _ymm_vselect(ymm_equal(d, ymm_zero), c_ymmInfMinus, x);

    return x;
}

v4d _mm256_exp_pd(const v4d& d)
{
    v4i q = _mm256_cvtpd_epi32(_mm256_mul_pd(d, _mm256_broadcast_sd(c_d + 70)));

    v4d s = ymm_madd(_mm256_cvtepi32_pd(q), _mm256_broadcast_sd(c_d + 71), d);
    s = ymm_madd(_mm256_cvtepi32_pd(q), _mm256_broadcast_sd(c_d + 72), s);

    v4d u = _mm256_broadcast_sd(c_d + 73);
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 74));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 75));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 76));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 77));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 78));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 79));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 80));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 81));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 82));
    u = ymm_madd(u, s, _mm256_broadcast_sd(c_d + 83));

    u = _mm256_add_pd(_mm256_broadcast_sd(c_d + 0), ymm_madd(_mm256_mul_pd(s, s), u, s));

    u = _ymm_ldexp(u, q);

    u = _mm256_andnot_pd(_ymm_is_ninf(d), u);

    return u;
}

#ifdef MATH_NAMESPACE
}
#endif
