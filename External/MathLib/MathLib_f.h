#pragma once

//======================================================================================================================
//                                                      float2
//======================================================================================================================

class float2
{
    public:

        union
        {
            struct
            {
                v2i mm;
            };

            struct
            {
                float pv[COORD_2D];
            };

            struct
            {
                float x, y;
            };
        };

    public:

        // NOTE: constructors

        PLATFORM_INLINE float2()
        {
        }

        PLATFORM_INLINE float2(float a, float b) : x(a), y(b)
        {
        }

        PLATFORM_INLINE float2(float a) : x(a), y(a)
        {
        }

        PLATFORM_INLINE float2(const float2& v) : x(v.x), y(v.y)
        {
        }

        // NOTE: set

        PLATFORM_INLINE void Set0()
        {
            Set(0.0f);
        }

        PLATFORM_INLINE void Set(const float2& v)
        {
            x = v.x;
            y = v.y;
        }

        PLATFORM_INLINE void Set(float a, float b)
        {
            x = a;
            y = b;
        }

        PLATFORM_INLINE void Set(float xy)
        {
            x = y = xy;
        }

        PLATFORM_INLINE void operator = (const float2& v)
        {
            Set(v);
        }

        // NOTE: compare

        PLATFORM_INLINE bool operator == (const float2& v) const
        {
            return x == v.x && y == v.y;
        }

        PLATFORM_INLINE bool operator != (const float2& v) const
        {
            return x != v.x || y != v.y;
        }

        // NOTE: arithmetic

        PLATFORM_INLINE float2 operator - () const
        {
            return float2(-x, -y);
        }

        PLATFORM_INLINE float2 operator + (const float2& v) const
        {
            return float2(x + v.x, y + v.y);
        }

        PLATFORM_INLINE float2 operator - (const float2& v) const
        {
            return float2(x - v.x, y - v.y);
        }

        PLATFORM_INLINE float2 operator / (float u) const
        {
            DEBUG_Assert( u != 0.0f );

            float ou = 1.0f / u;

            return float2(x * ou, y * ou);
        }

        PLATFORM_INLINE float2 operator * (float v) const
        {
            return float2(x * v, y * v);
        }

        PLATFORM_INLINE float2 operator * (const float2& v) const
        {
            return float2(x * v.x, y * v.y);
        }

        PLATFORM_INLINE float2 operator / (const float2& v) const
        {
            return float2(x / v.x, y / v.y);
        }

        PLATFORM_INLINE void operator /= (const float2& v)
        {
            DEBUG_Assert( v.x != 0.0f );
            DEBUG_Assert( v.y != 0.0f );

            x /= v.x;
            y /= v.y;
        }

        PLATFORM_INLINE void operator /= (float v)
        {
            DEBUG_Assert( v != 0.0f );

            float rv = 1.0f / v;

            x *= rv;
            y *= rv;
        }

        PLATFORM_INLINE void operator *= (const float2& v)
        {
            x *= v.x;
            y *= v.y;
        }

        PLATFORM_INLINE void operator *= (float v)
        {
            x *= v;
            y *= v;
        }

        PLATFORM_INLINE void operator += (const float2& v)
        {
            x += v.x;
            y += v.y;
        }

        PLATFORM_INLINE void operator -= (const float2& v)
        {
            x -= v.x;
            y -= v.y;
        }

        PLATFORM_INLINE friend float2 operator * (float s, const float2& v)
        {
            return float2(v.x * s, v.y * s);
        }

        // NOTE: swizzle

        PLATFORM_INLINE float2 xx() const { return float2(x, x); }
        PLATFORM_INLINE float2 xy() const { return float2(x, y); }
        PLATFORM_INLINE float2 yx() const { return float2(y, x); }
        PLATFORM_INLINE float2 yy() const { return float2(y, y); }
};

PLATFORM_INLINE float Dot22(const float2& a, const float2& b)
{
    return a.x * b.x + a.y * b.y;
}

PLATFORM_INLINE float LengthSquared(const float2& x)
{
    return Dot22(x, x);
}

PLATFORM_INLINE float Length(const float2& x)
{
    return Sqrt( LengthSquared(x) );
}

PLATFORM_INLINE float2 Normalize(const float2& x)
{
    return x / Length(x);
}

PLATFORM_INLINE float2 Perpendicular(const float2& a)
{
    return float2(-a.y, a.x);
}

PLATFORM_INLINE float2 Rotate(const float2& v, float angle)
{
    float sa = Sin(angle);
    float ca = Cos(angle);

    float2 p;
    p.x = ca * v.x + sa * v.y;
    p.y = ca * v.y - sa * v.x;

    return p;
}

//======================================================================================================================
//                                                      float3
//======================================================================================================================

class float3
{
    public:

        static const float3 one;

    public:

        union
        {
            struct
            {
                v4f xmm;
            };

            struct
            {
                float pv[COORD_3D];
            };

            struct
            {
                float x, y, z;
            };
        };

    public:

        // NOTE: constructors

        PLATFORM_INLINE float3()
        {
        }

        PLATFORM_INLINE float3(float a, float b, float c)
        {
            v4f x1 = _mm_load_ss(&a);
            v4f y1 = _mm_load_ss(&b);
            v4f z1 = _mm_load_ss(&c);
            v4f xy = _mm_unpacklo_ps(x1, y1);

            xmm = _mm_movelh_ps(xy, z1);
        }

        PLATFORM_INLINE float3(float a) : xmm( _mm_broadcast_ss(&a) )
        {
        }

        PLATFORM_INLINE float3(const float* v3)
        {
            xmm = float3(v3[0], v3[1], v3[2]).xmm;
        }

        PLATFORM_INLINE float3(const v4f& vec) : xmm(vec)
        {
        }

        // NOTE: set

        PLATFORM_INLINE void Set0()
        {
            xmm = v4f_zero;
        }

        PLATFORM_INLINE void operator = (const float3& vec)
        {
            xmm = vec.xmm;
        }

        // NOTE: compare

        PLATFORM_INLINE bool operator == (const float3& v) const
        {
            v4f r = v4f_equal(xmm, v.xmm);

            return v4f_test3_all(r);
        }

        PLATFORM_INLINE bool operator != (const float3& v) const
        {
            v4f r = v4f_notequal(xmm, v.xmm);

            return v4f_test3_any(r);
        }

        // NOTE: arithmetic

        PLATFORM_INLINE float3 operator - () const
        {
            return v4f_negate(xmm);
        }

        PLATFORM_INLINE float3 operator + (const float3& v) const
        {
            return _mm_add_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE float3 operator - (const float3& v) const
        {
            return _mm_sub_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE float3 operator * (const float3& v) const
        {
            return _mm_mul_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE float3 operator / (const float3& v) const
        {
            DEBUG_Assert( v4f_not0_all(v.xmm) );

            return _mm_div_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE void operator += (const float3& v)
        {
            xmm = _mm_add_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE void operator -= (const float3& v)
        {
            xmm = _mm_sub_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE void operator *= (const float3& v)
        {
            xmm = _mm_mul_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE void operator /= (const float3& v)
        {
            DEBUG_Assert( v4f_not0_all(v.xmm) );

            xmm = _mm_div_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE void operator *= (float s)
        {
            xmm = _mm_mul_ps(xmm, _mm_broadcast_ss(&s));
        }

        PLATFORM_INLINE void operator /= (float s)
        {
            DEBUG_Assert( s != 0.0f );

            xmm = _mm_div_ps(xmm, _mm_broadcast_ss(&s));
        }

        PLATFORM_INLINE float3 operator / (float s) const
        {
            DEBUG_Assert( s != 0.0f );

            return _mm_div_ps(xmm, _mm_broadcast_ss(&s));
        }

        PLATFORM_INLINE float3 operator * (float s) const
        {
            return _mm_mul_ps(xmm, _mm_broadcast_ss(&s));
        }

        // NOTE: misc

        PLATFORM_INLINE bool IsZero() const
        {
            v4f r = v4f_equal(xmm, v4f_zero);

            return v4f_test3_all(r);
        }

        static PLATFORM_INLINE float3 Zero()
        {
            return v4f_zero;
        }

        // NOTE: swizzle

        PLATFORM_INLINE float3 xxx() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_X, 0); }
        PLATFORM_INLINE float3 xxy() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_Y, 0); }
        PLATFORM_INLINE float3 xxz() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_Z, 0); }
        PLATFORM_INLINE float3 xyx() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_X, 0); }
        PLATFORM_INLINE float3 xyy() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_Y, 0); }
        PLATFORM_INLINE float3 xyz() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_Z, 0); }
        PLATFORM_INLINE float3 xzx() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_X, 0); }
        PLATFORM_INLINE float3 xzy() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_Y, 0); }
        PLATFORM_INLINE float3 xzz() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_Z, 0); }
        PLATFORM_INLINE float3 yxx() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_X, 0); }
        PLATFORM_INLINE float3 yxy() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_Y, 0); }
        PLATFORM_INLINE float3 yxz() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_Z, 0); }
        PLATFORM_INLINE float3 yyx() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_X, 0); }
        PLATFORM_INLINE float3 yyy() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_Y, 0); }
        PLATFORM_INLINE float3 yyz() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_Z, 0); }
        PLATFORM_INLINE float3 yzx() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_X, 0); }
        PLATFORM_INLINE float3 yzy() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_Y, 0); }
        PLATFORM_INLINE float3 yzz() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_Z, 0); }
        PLATFORM_INLINE float3 zxx() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_X, 0); }
        PLATFORM_INLINE float3 zxy() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_Y, 0); }
        PLATFORM_INLINE float3 zxz() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_Z, 0); }
        PLATFORM_INLINE float3 zyx() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_X, 0); }
        PLATFORM_INLINE float3 zyy() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_Y, 0); }
        PLATFORM_INLINE float3 zyz() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_Z, 0); }
        PLATFORM_INLINE float3 zzx() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_X, 0); }
        PLATFORM_INLINE float3 zzy() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_Y, 0); }
        PLATFORM_INLINE float3 zzz() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_Z, 0); }
};

PLATFORM_INLINE float Dot33(const float3& a, const float3& b)
{
    v4f r = v4f_dot33(a.xmm, b.xmm);

    return v4f_get_x(r);
}

PLATFORM_INLINE float LengthSquared(const float3& x)
{
    v4f r = v4f_dot33(x.xmm, x.xmm);

    return v4f_get_x(r);
}

PLATFORM_INLINE float Length(const float3& x)
{
    v4f r = v4f_length(x.xmm);

    return v4f_get_x(r);
}

PLATFORM_INLINE float3 Normalize(const float3& x)
{
    return v4f_normalize(x.xmm);
}

PLATFORM_INLINE float3 Cross(const float3& x, const float3& y)
{
    return v4f_cross(x.xmm, y.xmm);
}

PLATFORM_INLINE float3 Rcp(const float3& x)
{
    return v4f_rcp( v4f_setw1(x.xmm) );
}

PLATFORM_INLINE float3 Ceil(const float3& x)
{
    return v4f_ceil(x.xmm);
}

PLATFORM_INLINE float3 Madd(const float3& a, const float3& b, const float3& c)
{
    return v4f_madd(a.xmm, b.xmm, c.xmm);
}

PLATFORM_INLINE float3 GetPerpendicularVector(const float3& N)
{
    float3 T = float3(N.z, -N.x, N.y);
    T -= N * Dot33(T, N);

    return Normalize(T);
}

//======================================================================================================================
//                                                      float4
//======================================================================================================================

class float4
{
    public:

        union
        {
            struct
            {
                v4f xmm;
            };

            struct
            {
                float pv[COORD_4D];
            };

            struct
            {
                float x, y, z, w;
            };
        };

    public:

        // NOTE: constructors

        PLATFORM_INLINE float4()
        {
        }

        PLATFORM_INLINE float4(float a) : xmm( _mm_broadcast_ss(&a) )
        {
        }

        PLATFORM_INLINE float4(float a, float b, float c, float d) : xmm( v4f_set(a, b, c, d) )
        {
        }

        PLATFORM_INLINE float4(float a, float b, float c) : xmm( v4f_set(a, b, c, 1.0f) )
        {
        }

        PLATFORM_INLINE float4(const float* v4) : xmm( _mm_loadu_ps(v4) )
        {
        }

        PLATFORM_INLINE float4(const float3& vec)
        {
            xmm = v4f_setw1(vec.xmm);
        }

        PLATFORM_INLINE float4(const v4f& m) : xmm(m)
        {
        }

        // NOTE: set

        PLATFORM_INLINE void Set0001()
        {
            xmm = c_v4f_0001;
        }

        PLATFORM_INLINE void Set0()
        {
            xmm = v4f_zero;
        }

        PLATFORM_INLINE void operator = (const float4& vec)
        {
            xmm = vec.xmm;
        }

        // NOTE: compare

        PLATFORM_INLINE bool operator == (const float4& v) const
        {
            v4f r = v4f_equal(xmm, v.xmm);

            return v4f_test4_all(r);
        }

        PLATFORM_INLINE bool operator != (const float4& v) const
        {
            v4f r = v4f_notequal(xmm, v.xmm);

            return v4f_test4_any(r);
        }

        // NOTE: arithmetic

        PLATFORM_INLINE float4 operator - () const
        {
            return v4f_negate(xmm);
        }

        PLATFORM_INLINE float4 operator + (const float4& v) const
        {
            return _mm_add_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE float4 operator - (const float4& v) const
        {
            return _mm_sub_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE float4 operator * (const float4& v) const
        {
            return _mm_mul_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE float4 operator / (const float4& v) const
        {
            DEBUG_Assert( v4f_not0_all(v.xmm) );

            return _mm_div_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE void operator += (const float4& v)
        {
            xmm = _mm_add_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE void operator -= (const float4& v)
        {
            xmm = _mm_sub_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE void operator *= (const float4& v)
        {
            xmm = _mm_mul_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE void operator /= (const float4& v)
        {
            DEBUG_Assert( v4f_not0_all(v.xmm) );

            xmm = _mm_div_ps(xmm, v.xmm);
        }

        PLATFORM_INLINE void operator *= (float s)
        {
            xmm = _mm_mul_ps(xmm, _mm_broadcast_ss(&s));
        }

        PLATFORM_INLINE void operator /= (float s)
        {
            DEBUG_Assert( s != 0.0f );

            xmm = _mm_div_ps(xmm, _mm_broadcast_ss(&s));
        }

        PLATFORM_INLINE float4 operator / (float s) const
        {
            DEBUG_Assert( s != 0.0f );

            return _mm_div_ps(xmm, _mm_broadcast_ss(&s));
        }

        PLATFORM_INLINE float4 operator * (float s) const
        {
            return _mm_mul_ps(xmm, _mm_broadcast_ss(&s));
        }

        // NOTE: misc

        PLATFORM_INLINE const float3& To3d() const
        {
            return (float3&)xmm;
        }

        PLATFORM_INLINE bool IsZero() const
        {
            v4f r = v4f_equal(xmm, v4f_zero);

            return v4f_test4_all(r);
        }

        // NOTE: swizzle

        PLATFORM_INLINE float4 xxxx() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 xxxy() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 xxxz() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 xxxw() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 xxyx() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 xxyy() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 xxyz() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 xxyw() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 xxzx() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 xxzy() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 xxzz() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 xxzw() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 xxwx() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 xxwy() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 xxwz() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 xxww() const { return v4f_swizzle(xmm, COORD_X, COORD_X, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 xyxx() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 xyxy() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 xyxz() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 xyxw() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 xyyx() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 xyyy() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 xyyz() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 xyyw() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 xyzx() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 xyzy() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 xyzz() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 xyzw() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 xywx() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 xywy() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 xywz() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 xyww() const { return v4f_swizzle(xmm, COORD_X, COORD_Y, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 xzxx() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 xzxy() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 xzxz() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 xzxw() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 xzyx() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 xzyy() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 xzyz() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 xzyw() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 xzzx() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 xzzy() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 xzzz() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 xzzw() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 xzwx() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 xzwy() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 xzwz() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 xzww() const { return v4f_swizzle(xmm, COORD_X, COORD_Z, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 xwxx() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 xwxy() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 xwxz() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 xwxw() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 xwyx() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 xwyy() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 xwyz() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 xwyw() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 xwzx() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 xwzy() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 xwzz() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 xwzw() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 xwwx() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 xwwy() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 xwwz() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 xwww() const { return v4f_swizzle(xmm, COORD_X, COORD_W, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 yxxx() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 yxxy() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 yxxz() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 yxxw() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 yxyx() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 yxyy() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 yxyz() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 yxyw() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 yxzx() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 yxzy() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 yxzz() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 yxzw() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 yxwx() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 yxwy() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 yxwz() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 yxww() const { return v4f_swizzle(xmm, COORD_Y, COORD_X, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 yyxx() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 yyxy() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 yyxz() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 yyxw() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 yyyx() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 yyyy() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 yyyz() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 yyyw() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 yyzx() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 yyzy() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 yyzz() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 yyzw() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 yywx() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 yywy() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 yywz() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 yyww() const { return v4f_swizzle(xmm, COORD_Y, COORD_Y, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 yzxx() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 yzxy() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 yzxz() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 yzxw() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 yzyx() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 yzyy() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 yzyz() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 yzyw() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 yzzx() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 yzzy() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 yzzz() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 yzzw() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 yzwx() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 yzwy() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 yzwz() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 yzww() const { return v4f_swizzle(xmm, COORD_Y, COORD_Z, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 ywxx() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 ywxy() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 ywxz() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 ywxw() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 ywyx() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 ywyy() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 ywyz() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 ywyw() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 ywzx() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 ywzy() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 ywzz() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 ywzw() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 ywwx() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 ywwy() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 ywwz() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 ywww() const { return v4f_swizzle(xmm, COORD_Y, COORD_W, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 zxxx() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 zxxy() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 zxxz() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 zxxw() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 zxyx() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 zxyy() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 zxyz() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 zxyw() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 zxzx() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 zxzy() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 zxzz() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 zxzw() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 zxwx() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 zxwy() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 zxwz() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 zxww() const { return v4f_swizzle(xmm, COORD_Z, COORD_X, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 zyxx() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 zyxy() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 zyxz() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 zyxw() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 zyyx() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 zyyy() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 zyyz() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 zyyw() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 zyzx() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 zyzy() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 zyzz() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 zyzw() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 zywx() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 zywy() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 zywz() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 zyww() const { return v4f_swizzle(xmm, COORD_Z, COORD_Y, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 zzxx() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 zzxy() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 zzxz() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 zzxw() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 zzyx() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 zzyy() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 zzyz() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 zzyw() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 zzzx() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 zzzy() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 zzzz() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 zzzw() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 zzwx() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 zzwy() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 zzwz() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 zzww() const { return v4f_swizzle(xmm, COORD_Z, COORD_Z, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 zwxx() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 zwxy() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 zwxz() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 zwxw() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 zwyx() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 zwyy() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 zwyz() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 zwyw() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 zwzx() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 zwzy() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 zwzz() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 zwzw() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 zwwx() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 zwwy() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 zwwz() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 zwww() const { return v4f_swizzle(xmm, COORD_Z, COORD_W, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 wxxx() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 wxxy() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 wxxz() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 wxxw() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 wxyx() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 wxyy() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 wxyz() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 wxyw() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 wxzx() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 wxzy() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 wxzz() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 wxzw() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 wxwx() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 wxwy() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 wxwz() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 wxww() const { return v4f_swizzle(xmm, COORD_W, COORD_X, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 wyxx() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 wyxy() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 wyxz() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 wyxw() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 wyyx() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 wyyy() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 wyyz() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 wyyw() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 wyzx() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 wyzy() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 wyzz() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 wyzw() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 wywx() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 wywy() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 wywz() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 wyww() const { return v4f_swizzle(xmm, COORD_W, COORD_Y, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 wzxx() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 wzxy() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 wzxz() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 wzxw() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 wzyx() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 wzyy() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 wzyz() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 wzyw() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 wzzx() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 wzzy() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 wzzz() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 wzzw() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 wzwx() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 wzwy() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 wzwz() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 wzww() const { return v4f_swizzle(xmm, COORD_W, COORD_Z, COORD_W, COORD_W); }
        PLATFORM_INLINE float4 wwxx() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_X, COORD_X); }
        PLATFORM_INLINE float4 wwxy() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_X, COORD_Y); }
        PLATFORM_INLINE float4 wwxz() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_X, COORD_Z); }
        PLATFORM_INLINE float4 wwxw() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_X, COORD_W); }
        PLATFORM_INLINE float4 wwyx() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_Y, COORD_X); }
        PLATFORM_INLINE float4 wwyy() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_Y, COORD_Y); }
        PLATFORM_INLINE float4 wwyz() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_Y, COORD_Z); }
        PLATFORM_INLINE float4 wwyw() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_Y, COORD_W); }
        PLATFORM_INLINE float4 wwzx() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_Z, COORD_X); }
        PLATFORM_INLINE float4 wwzy() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_Z, COORD_Y); }
        PLATFORM_INLINE float4 wwzz() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_Z, COORD_Z); }
        PLATFORM_INLINE float4 wwzw() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_Z, COORD_W); }
        PLATFORM_INLINE float4 wwwx() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_W, COORD_X); }
        PLATFORM_INLINE float4 wwwy() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_W, COORD_Y); }
        PLATFORM_INLINE float4 wwwz() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_W, COORD_Z); }
        PLATFORM_INLINE float4 wwww() const { return v4f_swizzle(xmm, COORD_W, COORD_W, COORD_W, COORD_W); }
};

PLATFORM_INLINE float Dot44(const float4& a, const float4& b)
{
    v4f r = v4f_dot44(a.xmm, b.xmm);

    return v4f_get_x(r);
}

PLATFORM_INLINE float Dot43(const float4& a, const float3& b)
{
    v4f r = v4f_dot43(a.xmm, b.xmm);

    return v4f_get_x(r);
}

PLATFORM_INLINE float4 Rcp(const float4& x)
{
    return v4f_rcp(x.xmm);
}

PLATFORM_INLINE float4 Ceil(const float4& x)
{
    return v4f_ceil(x.xmm);
}

PLATFORM_INLINE float4 Madd(const float4& a, const float4& b, const float4& c)
{
    return v4f_madd(a.xmm, b.xmm, c.xmm);
}

//======================================================================================================================
//                                                      float4x4
//======================================================================================================================

class float4x4
{
    public:

        // IMPORTANT: store - "column-major", math - "row-major" (vector is column)

        union
        {
            struct
            {
                v4f col0;
                v4f col1;
                v4f col2;
                v4f col3;
            };

            struct
            {
                float a16[16];
            };

            struct
            {
                struct
                {
                    float a00, a10, a20, a30;
                };

                struct
                {
                    float a01, a11, a21, a31;
                };

                struct
                {
                    float a02, a12, a22, a32;
                };

                struct
                {
                    float a03, a13, a23, a33;
                };
            };
        };

    public:

        // NOTE: constructors

        PLATFORM_INLINE float4x4()
        {
        }

        PLATFORM_INLINE float4x4
        (
            float m00, float m01, float m02, float m03,
            float m10, float m11, float m12, float m13,
            float m20, float m21, float m22, float m23,
            float m30, float m31, float m32, float m33
        ) :
            col0( v4f_set(m00, m10, m20, m30) )
            , col1( v4f_set(m01, m11, m21, m31) )
            , col2( v4f_set(m02, m12, m22, m32) )
            , col3( v4f_set(m03, m13, m23, m33) )
        {
        }

        PLATFORM_INLINE float4x4(const float4& c0, const float4& c1, const float4& c2, const float4& c3) :
            col0( c0.xmm )
            , col1( c1.xmm )
            , col2( c2.xmm )
            , col3( c3.xmm )
        {
        }

        PLATFORM_INLINE float4x4(const float4x4& m) :
            col0( m.col0 )
            , col1( m.col1 )
            , col2( m.col2 )
            , col3( m.col3 )
        {
        }

        // NOTE: set

        static PLATFORM_INLINE float4x4 Identity()
        {
            return float4x4(
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            );
        }

        PLATFORM_INLINE void SetCol0(const float4& v)
        {
            col0 = v.xmm;
        }

        PLATFORM_INLINE void SetCol1(const float4& v)
        {
            col1 = v.xmm;
        }

        PLATFORM_INLINE void SetCol2(const float4& v)
        {
            col2 = v.xmm;
        }

        PLATFORM_INLINE void SetCol3(const float4& v)
        {
            col3 = v.xmm;
        }

        PLATFORM_INLINE void SetCol0_0(const float3& v)
        {
            col0 = v4f_setw0(v.xmm);
        }

        PLATFORM_INLINE void SetCol1_0(const float3& v)
        {
            col1 = v4f_setw0(v.xmm);
        }

        PLATFORM_INLINE void SetCol2_0(const float3& v)
        {
            col2 = v4f_setw0(v.xmm);
        }

        PLATFORM_INLINE void SetCol3_1(const float3& v)
        {
            col3 = v4f_setw1(v.xmm);
        }

        PLATFORM_INLINE void SetCol0(float x, float y, float z, float w)
        {
            col0 = v4f_set(x, y, z, w);
        }

        PLATFORM_INLINE void SetCol1(float x, float y, float z, float w)
        {
            col1 = v4f_set(x, y, z, w);
        }

        PLATFORM_INLINE void SetCol2(float x, float y, float z, float w)
        {
            col2 = v4f_set(x, y, z, w);
        }

        PLATFORM_INLINE void SetCol3(float x, float y, float z, float w)
        {
            col3 = v4f_set(x, y, z, w);
        }

        PLATFORM_INLINE void operator = (const float4x4& m)
        {
            col0 = m.col0;
            col1 = m.col1;
            col2 = m.col2;
            col3 = m.col3;
        }

        // NOTE: compare

        PLATFORM_INLINE bool operator == (const float4x4& m) const
        {
            return float4(col0) == m.col0 && float4(col1) == m.col1 && float4(col2) == m.col2 && float4(col3) == m.col3;
        }

        PLATFORM_INLINE bool operator != (const float4x4& m) const
        {
            return float4(col0) != m.col0 || float4(col1) != m.col1 || float4(col2) != m.col2 || float4(col3) != m.col3;
        }

        // NOTE: misc

        // IMPORTANT: col0-col2 can be used as m * axis(X/Y/Z), BUT:
        // only when translation = {0}, in other case w-component
        // must be zeroed!

        PLATFORM_INLINE const float4& GetCol0() const
        {
            return (float4&)col0;
        }

        PLATFORM_INLINE const float4& GetCol1() const
        {
            return (float4&)col1;
        }

        PLATFORM_INLINE const float4& GetCol2() const
        {
            return (float4&)col2;
        }

        PLATFORM_INLINE const float4& GetCol3() const
        {
            return (float4&)col3;
        }

        PLATFORM_INLINE float4 GetRow0() const
        {
            return float4(a00, a01, a02, a03);
        }

        PLATFORM_INLINE float4 GetRow1() const
        {
            return float4(a10, a11, a12, a13);
        }

        PLATFORM_INLINE float4 GetRow2() const
        {
            return float4(a20, a21, a22, a23);
        }

        PLATFORM_INLINE float4 GetRow3() const
        {
            return float4(a30, a31, a32, a33);
        }

        PLATFORM_INLINE float GetNdcDepth(float z) const
        {
            float a = a22 * z + a23;
            float b = a32 * z + a33;

            return a / b;
        }

        PLATFORM_INLINE float3 GetRotationYPR() const
        {
            float3 r;
            r.x = Atan(-a01, a11);
            r.y = Asin(a21);
            r.z = Atan(-a20, a22);

            return r;
        }

        PLATFORM_INLINE float4 GetQuaternion() const
        {
            // Pain in the ass for mirror transformations...
            // TODO: replace with http://www.iri.upc.edu/files/scidoc/2068-Accurate-Computation-of-Quaternions-from-Rotation-Matrices.pdf

            float4 q;

            float tr = a00 + a11 + a22;
            if (tr > 0.0f)
                q = float4( a12 - a21, a20 - a02, a01 - a10, tr + 1.0f);
            else if (a00 > a11 && a00 > a22)
                q = float4( 1.0f + a00 - a11 - a22, a10 + a01, a20 + a02, a12 - a21 );
            else if (a11 > a22)
                q = float4( a10 + a01, 1.0f + a11 - a00 - a22, a21 + a12, a20 - a02 );
            else
                q = float4( a20 + a02, a21 + a12, 1.0f + a22 - a00 - a11, a01 - a10 );

            q *= Rsqrt( Dot44(q, q) );

            return q;
        }

        PLATFORM_INLINE float3 GetScale() const
        {
            float3 scale = float3
            (
                v4f_get_x(v4f_length(col0)),
                v4f_get_x(v4f_length(col1)),
                v4f_get_x(v4f_length(col2))
            );

            return scale;
        }

        PLATFORM_INLINE void SetTranslation(const float3& p)
        {
            col3 = v4f_setw1(p.xmm);
        }

        PLATFORM_INLINE void AddTranslation(const float3& p)
        {
            col3 = _mm_add_ps(col3, v4f_setw0(p.xmm));
        }

        PLATFORM_INLINE void PreTranslation(const float3& p);

        PLATFORM_INLINE void AddScale(const float3& scale)
        {
            col0 = _mm_mul_ps(col0, scale.xmm);
            col1 = _mm_mul_ps(col1, scale.xmm);
            col2 = _mm_mul_ps(col2, scale.xmm);
        }

        PLATFORM_INLINE void WorldToView(uint32_t uiProjFlags = 0)
        {
            /*
            float4x4 rot;
            rot.SetupByRotationX(c_fHalfPi);
            *this = (*this) * rot;
            InvertOrtho();
            */

            Swap(col1, col2);

            if( (uiProjFlags & PROJ_LEFT_HANDED) == 0 )
                col2 = v4f_negate(col2);

            Transpose3x4();
        }

        PLATFORM_INLINE void ViewToWorld(uint32_t uiProjFlags = 0)
        {
            Transpose3x4();

            if( (uiProjFlags & PROJ_LEFT_HANDED) == 0 )
                col2 = v4f_negate(col2);

            Swap(col1, col2);
        }

        PLATFORM_INLINE bool IsRightHanded() const
        {
            float3 v1 = Cross(col0, col1);

            return Dot33(v1, col2) > 0.0f;
        }

        // NOTE: arithmetic

        PLATFORM_INLINE float4x4 operator * (const float4x4& m) const
        {
            float4x4 r;

            v4f r1 = _mm_mul_ps(v4f_swizzle(m.col0, 0, 0, 0, 0), col0);
            v4f r2 = _mm_mul_ps(v4f_swizzle(m.col1, 0, 0, 0, 0), col0);

            r1 = v4f_madd(v4f_swizzle(m.col0, 1, 1, 1, 1), col1, r1);
            r2 = v4f_madd(v4f_swizzle(m.col1, 1, 1, 1, 1), col1, r2);
            r1 = v4f_madd(v4f_swizzle(m.col0, 2, 2, 2, 2), col2, r1);
            r2 = v4f_madd(v4f_swizzle(m.col1, 2, 2, 2, 2), col2, r2);
            r1 = v4f_madd(v4f_swizzle(m.col0, 3, 3, 3, 3), col3, r1);
            r2 = v4f_madd(v4f_swizzle(m.col1, 3, 3, 3, 3), col3, r2);

            r.col0 = r1;
            r.col1 = r2;

            r1 = _mm_mul_ps(v4f_swizzle(m.col2, 0, 0, 0, 0), col0);
            r2 = _mm_mul_ps(v4f_swizzle(m.col3, 0, 0, 0, 0), col0);

            r1 = v4f_madd(v4f_swizzle(m.col2, 1, 1, 1, 1), col1, r1);
            r2 = v4f_madd(v4f_swizzle(m.col3, 1, 1, 1, 1), col1, r2);
            r1 = v4f_madd(v4f_swizzle(m.col2, 2, 2, 2, 2), col2, r1);
            r2 = v4f_madd(v4f_swizzle(m.col3, 2, 2, 2, 2), col2, r2);
            r1 = v4f_madd(v4f_swizzle(m.col2, 3, 3, 3, 3), col3, r1);
            r2 = v4f_madd(v4f_swizzle(m.col3, 3, 3, 3, 3), col3, r2);

            r.col2 = r1;
            r.col3 = r2;

            return r;
        }

        PLATFORM_INLINE float4 operator * (const float4& v) const
        {
            v4f r = _mm_mul_ps(v4f_swizzle(v.xmm, 0, 0, 0, 0), col0);
            r = v4f_madd(v4f_swizzle(v.xmm, 1, 1, 1, 1), col1, r);
            r = v4f_madd(v4f_swizzle(v.xmm, 2, 2, 2, 2), col2, r);
            r = v4f_madd(v4f_swizzle(v.xmm, 3, 3, 3, 3), col3, r);

            return r;
        }

        PLATFORM_INLINE float3 operator * (const float3& v) const
        {
            v4f r = v4f_madd(v4f_swizzle(v.xmm, 0, 0, 0, 0), col0, col3);
            r = v4f_madd(v4f_swizzle(v.xmm, 1, 1, 1, 1), col1, r);
            r = v4f_madd(v4f_swizzle(v.xmm, 2, 2, 2, 2), col2, r);

            return r;
        }

        PLATFORM_INLINE void TransposeTo(float4x4& m) const
        {
            v4f xmm0 = v4f_Ax_Bx_Ay_By(col0, col1);
            v4f xmm1 = v4f_Ax_Bx_Ay_By(col2, col3);
            v4f xmm2 = v4f_Az_Bz_Aw_Bw(col0, col1);
            v4f xmm3 = v4f_Az_Bz_Aw_Bw(col2, col3);

            m.col0 = v4f_Axy_Bxy(xmm0, xmm1);
            m.col1 = v4f_Azw_Bzw(xmm1, xmm0);
            m.col2 = v4f_Axy_Bxy(xmm2, xmm3);
            m.col3 = v4f_Azw_Bzw(xmm3, xmm2);
        }

        PLATFORM_INLINE void Transpose()
        {
            v4f xmm0 = v4f_Ax_Bx_Ay_By(col0, col1);
            v4f xmm1 = v4f_Ax_Bx_Ay_By(col2, col3);
            v4f xmm2 = v4f_Az_Bz_Aw_Bw(col0, col1);
            v4f xmm3 = v4f_Az_Bz_Aw_Bw(col2, col3);

            col0 = v4f_Axy_Bxy(xmm0, xmm1);
            col1 = v4f_Azw_Bzw(xmm1, xmm0);
            col2 = v4f_Axy_Bxy(xmm2, xmm3);
            col3 = v4f_Azw_Bzw(xmm3, xmm2);
        }

        PLATFORM_INLINE void Transpose3x4()
        {
            v4f xmm0 = v4f_Ax_Bx_Ay_By(col0, col1);
            v4f xmm1 = v4f_Ax_Bx_Ay_By(col2, col3);
            v4f xmm2 = v4f_Az_Bz_Aw_Bw(col0, col1);
            v4f xmm3 = v4f_Az_Bz_Aw_Bw(col2, col3);

            col0 = v4f_Axy_Bxy(xmm0, xmm1);
            col1 = v4f_Azw_Bzw(xmm1, xmm0);
            col2 = v4f_Axy_Bxy(xmm2, xmm3);
        }

        PLATFORM_INLINE void Invert()
        {
            // NOTE: http://forum.devmaster.net/t/sse-mat4-inverse/16799

            v4f Fac0;
            {
                v4f Swp0a = v4f_shuffle(col3, col2, 3, 3, 3, 3);
                v4f Swp0b = v4f_shuffle(col3, col2, 2, 2, 2, 2);

                v4f Swp00 = v4f_shuffle(col2, col1, 2, 2, 2, 2);
                v4f Swp01 = v4f_swizzle(Swp0a, 0, 0, 0, 2);
                v4f Swp02 = v4f_swizzle(Swp0b, 0, 0, 0, 2);
                v4f Swp03 = v4f_shuffle(col2, col1, 3, 3, 3, 3);

                v4f Mul00 = _mm_mul_ps(Swp00, Swp01);

                Fac0 = v4f_nmadd(Swp02, Swp03, Mul00);
            }

            v4f Fac1;
            {
                v4f Swp0a = v4f_shuffle(col3, col2, 3, 3, 3, 3);
                v4f Swp0b = v4f_shuffle(col3, col2, 1, 1, 1, 1);

                v4f Swp00 = v4f_shuffle(col2, col1, 1, 1, 1, 1);
                v4f Swp01 = v4f_swizzle(Swp0a, 0, 0, 0, 2);
                v4f Swp02 = v4f_swizzle(Swp0b, 0, 0, 0, 2);
                v4f Swp03 = v4f_shuffle(col2, col1, 3, 3, 3, 3);

                v4f Mul00 = _mm_mul_ps(Swp00, Swp01);

                Fac1 = v4f_nmadd(Swp02, Swp03, Mul00);
            }

            v4f Fac2;
            {
                v4f Swp0a = v4f_shuffle(col3, col2, 2, 2, 2, 2);
                v4f Swp0b = v4f_shuffle(col3, col2, 1, 1, 1, 1);

                v4f Swp00 = v4f_shuffle(col2, col1, 1, 1, 1, 1);
                v4f Swp01 = v4f_swizzle(Swp0a, 0, 0, 0, 2);
                v4f Swp02 = v4f_swizzle(Swp0b, 0, 0, 0, 2);
                v4f Swp03 = v4f_shuffle(col2, col1, 2, 2, 2, 2);

                v4f Mul00 = _mm_mul_ps(Swp00, Swp01);

                Fac2 = v4f_nmadd(Swp02, Swp03, Mul00);
            }

            v4f Fac3;
            {
                v4f Swp0a = v4f_shuffle(col3, col2, 3, 3, 3, 3);
                v4f Swp0b = v4f_shuffle(col3, col2, 0, 0, 0, 0);

                v4f Swp00 = v4f_shuffle(col2, col1, 0, 0, 0, 0);
                v4f Swp01 = v4f_swizzle(Swp0a, 0, 0, 0, 2);
                v4f Swp02 = v4f_swizzle(Swp0b, 0, 0, 0, 2);
                v4f Swp03 = v4f_shuffle(col2, col1, 3, 3, 3, 3);

                v4f Mul00 = _mm_mul_ps(Swp00, Swp01);

                Fac3 = v4f_nmadd(Swp02, Swp03, Mul00);
            }

            v4f Fac4;
            {
                v4f Swp0a = v4f_shuffle(col3, col2, 2, 2, 2, 2);
                v4f Swp0b = v4f_shuffle(col3, col2, 0, 0, 0, 0);

                v4f Swp00 = v4f_shuffle(col2, col1, 0, 0, 0, 0);
                v4f Swp01 = v4f_swizzle(Swp0a, 0, 0, 0, 2);
                v4f Swp02 = v4f_swizzle(Swp0b, 0, 0, 0, 2);
                v4f Swp03 = v4f_shuffle(col2, col1, 2, 2, 2, 2);

                v4f Mul00 = _mm_mul_ps(Swp00, Swp01);

                Fac4 = v4f_nmadd(Swp02, Swp03, Mul00);
            }

            v4f Fac5;
            {
                v4f Swp0a = v4f_shuffle(col3, col2, 1, 1, 1, 1);
                v4f Swp0b = v4f_shuffle(col3, col2, 0, 0, 0, 0);

                v4f Swp00 = v4f_shuffle(col2, col1, 0, 0, 0, 0);
                v4f Swp01 = v4f_swizzle(Swp0a, 0, 0, 0, 2);
                v4f Swp02 = v4f_swizzle(Swp0b, 0, 0, 0, 2);
                v4f Swp03 = v4f_shuffle(col2, col1, 1, 1, 1, 1);

                v4f Mul00 = _mm_mul_ps(Swp00, Swp01);

                Fac5 = v4f_nmadd(Swp02, Swp03, Mul00);
            }

            v4f SignA = _mm_set_ps( 1.0f,-1.0f, 1.0f,-1.0f);
            v4f SignB = _mm_set_ps(-1.0f, 1.0f,-1.0f, 1.0f);

            v4f Temp0 = v4f_shuffle(col1, col0, 0, 0, 0, 0);
            v4f Vec0 = v4f_swizzle(Temp0, 0, 2, 2, 2);

            v4f Temp1 = v4f_shuffle(col1, col0, 1, 1, 1, 1);
            v4f Vec1 = v4f_swizzle(Temp1, 0, 2, 2, 2);

            v4f Temp2 = v4f_shuffle(col1, col0, 2, 2, 2, 2);
            v4f Vec2 = v4f_swizzle(Temp2, 0, 2, 2, 2);

            v4f Temp3 = v4f_shuffle(col1, col0, 3, 3, 3, 3);
            v4f Vec3 = v4f_swizzle(Temp3, 0, 2, 2, 2);

            v4f Mul0 = _mm_mul_ps(Vec1, Fac0);
            v4f Mul1 = _mm_mul_ps(Vec0, Fac0);
            v4f Mul2 = _mm_mul_ps(Vec0, Fac1);
            v4f Mul3 = _mm_mul_ps(Vec0, Fac2);

            v4f Sub0 = v4f_nmadd(Vec2, Fac1, Mul0);
            v4f Sub1 = v4f_nmadd(Vec2, Fac3, Mul1);
            v4f Sub2 = v4f_nmadd(Vec1, Fac3, Mul2);
            v4f Sub3 = v4f_nmadd(Vec1, Fac4, Mul3);

            v4f Add0 = v4f_madd(Vec3, Fac2, Sub0);
            v4f Add1 = v4f_madd(Vec3, Fac4, Sub1);
            v4f Add2 = v4f_madd(Vec3, Fac5, Sub2);
            v4f Add3 = v4f_madd(Vec2, Fac5, Sub3);

            v4f Inv0 = _mm_mul_ps(SignB, Add0);
            v4f Inv1 = _mm_mul_ps(SignA, Add1);
            v4f Inv2 = _mm_mul_ps(SignB, Add2);
            v4f Inv3 = _mm_mul_ps(SignA, Add3);

            v4f Row0 = v4f_shuffle(Inv0, Inv1, 0, 0, 0, 0);
            v4f Row1 = v4f_shuffle(Inv2, Inv3, 0, 0, 0, 0);
            v4f Row2 = v4f_shuffle(Row0, Row1, 0, 2, 0, 2);

            v4f Det0 = v4f_dot44(col0, Row2);
            v4f Rcp0 = v4f_rcp(Det0);
            //v4f Rcp0 = _mm_div_ps(_mm_set1_ps(1.0f), Det0);

            col0 = _mm_mul_ps(Inv0, Rcp0);
            col1 = _mm_mul_ps(Inv1, Rcp0);
            col2 = _mm_mul_ps(Inv2, Rcp0);
            col3 = _mm_mul_ps(Inv3, Rcp0);
        }

        PLATFORM_INLINE void InvertOrtho();

        // NOTE: special sets

        PLATFORM_INLINE void SetupByQuaternion(const float4& q)
        {
            float qxx = q.x * q.x;
            float qyy = q.y * q.y;
            float qzz = q.z * q.z;
            float qxz = q.x * q.z;
            float qxy = q.x * q.y;
            float qyz = q.y * q.z;
            float qwx = q.w * q.x;
            float qwy = q.w * q.y;
            float qwz = q.w * q.z;

            a00 = 1.0f - 2.0f * (qyy +  qzz);
            a01 = 2.0f * (qxy + qwz);
            a02 = 2.0f * (qxz - qwy);

            a10 = 2.0f * (qxy - qwz);
            a11 = 1.0f - 2.0f * (qxx +  qzz);
            a12 = 2.0f * (qyz + qwx);

            a20 = 2.0f * (qxz + qwy);
            a21 = 2.0f * (qyz - qwx);
            a22 = 1.0f - 2.0f * (qxx +  qyy);

            a30 = 0.0f;
            a31 = 0.0f;
            a32 = 0.0f;

            col3 = c_v4f_0001;
        }

        PLATFORM_INLINE void SetupByRotationX(float angleX)
        {
            float ct = Cos(angleX);
            float st = Sin(angleX);

            SetCol0(1.0f, 0.0f, 0.0f, 0.0f);
            SetCol1(0.0f, ct, st, 0.0f);
            SetCol2(0.0f, -st, ct, 0.0f);

            col3 = c_v4f_0001;
        }

        PLATFORM_INLINE void SetupByRotationY(float angleY)
        {
            float ct = Cos(angleY);
            float st = Sin(angleY);

            SetCol0(ct, 0.0f, -st, 0.0f);
            SetCol1(0.0f, 1.0f, 0.0f, 0.0f);
            SetCol2(st, 0.0f, ct, 0.0f);

            col3 = c_v4f_0001;
        }

        PLATFORM_INLINE void SetupByRotationZ(float angleZ)
        {
            float ct = Cos(angleZ);
            float st = Sin(angleZ);

            SetCol0(ct, st, 0.0f, 0.0f);
            SetCol1(-st, ct, 0.0f, 0.0f);
            SetCol2(0.0f, 0.0f, 1.0f, 0.0f);

            col3 = c_v4f_0001;
        }

        PLATFORM_INLINE void SetupByRotationYPR(float fYaw, float fPitch, float fRoll)
        {
            // NOTE: "yaw-pitch-roll" rotation
            //       yaw - around Z (object "down-up" axis)
            //       pitch - around X (object "left-right" axis)
            //       roll - around Y (object "backward-forward" axis)

            /*
            float4x4 rot;
            rot.SetupByRotationY(fRoll);
            *this = rot;
            rot.SetupByRotationX(fPitch);
            *this = rot * (*this);
            rot.SetupByRotationZ(fYaw);
            *this = rot * (*this);
            */

            float4 angles(fYaw, fPitch, fRoll, 0.0f);

            float4 c;
            float4 s = _mm_sincos_ps(&c.xmm, angles.xmm);

            a00 = c.x * c.z - s.x * s.y * s.z;
            a10 = s.x * c.z + c.x * s.y * s.z;
            a20 = -c.y * s.z;
            a30 = 0.0f;

            a01 = -s.x * c.y;
            a11 = c.x * c.y;
            a21 = s.y;
            a31 = 0.0f;

            a02 = c.x * s.z + c.z * s.x * s.y;
            a12 = s.x * s.z - c.x * s.y * c.z;
            a22 = c.y * c.z;
            a32 = 0.0f;

            col3 = c_v4f_0001;
        }

        PLATFORM_INLINE void SetupByRotation(float theta, const float3& v)
        {
            float ct = Cos(theta);
            float st = Sin(theta);

            SetupByRotation(st, ct, v);
        }

        PLATFORM_INLINE void SetupByRotation(float st, float ct, const float3& v)
        {
            float xx = v.x * v.x;
            float yy = v.y * v.y;
            float zz = v.z * v.z;
            float xy = v.x * v.y;
            float xz = v.x * v.z;
            float yz = v.y * v.z;
            float ctxy = ct * xy;
            float ctxz = ct * xz;
            float ctyz = ct * yz;
            float sty = st * v.y;
            float stx = st * v.x;
            float stz = st * v.z;

            a00 = xx + ct * (1.0f - xx);
            a01 = xy - ctxy - stz;
            a02 = xz - ctxz + sty;

            a10 = xy - ctxy + stz;
            a11 = yy + ct * (1.0f - yy);
            a12 = yz - ctyz - stx;

            a20 = xz - ctxz - sty;
            a21 = yz - ctyz + stx;
            a22 = zz + ct * (1.0f - zz);

            a30 = 0.0f;
            a31 = 0.0f;
            a32 = 0.0f;

            col3 = c_v4f_0001;
        }

        PLATFORM_INLINE void SetupByRotation(const float3& z, const float3& d)
        {
            /*
            // NOTE: same as

            float3 axis = Cross(z, d);
            float angle = Acos( Dot33(z, d) );

            SetupByRotation(angle, axis);
            */

            float3 w = Cross(z, d);
            float c = Dot33(z, d);
            float k = (1.0f - c) / (1.0f - c * c);

            float hxy = w.x * w.y * k;
            float hxz = w.x * w.z * k;
            float hyz = w.y * w.z * k;

            a00 = c + w.x * w.x * k;
            a01 = hxy - w.z;
            a02 = hxz + w.y;

            a10 = hxy + w.z;
            a11 = c + w.y * w.y * k;
            a12 = hyz - w.x;

            a20 = hxz - w.y;
            a21 = hyz + w.x;
            a22 = c + w.z * w.z * k;

            a30 = 0.0f;
            a31 = 0.0f;
            a32 = 0.0f;

            col3 = c_v4f_0001;
        }

        PLATFORM_INLINE void SetupByTranslation(const float3& p)
        {
            SetCol0(1.0f, 0.0f, 0.0f, 0.0f);
            SetCol1(0.0f, 1.0f, 0.0f, 0.0f);
            SetCol2(0.0f, 0.0f, 1.0f, 0.0f);
            SetCol3_1(p);
        }

        PLATFORM_INLINE void SetupByScale(const float3& scale)
        {
            SetCol0(scale.x, 0.0f, 0.0f, 0.0f);
            SetCol1(0.0f, scale.y, 0.0f, 0.0f);
            SetCol2(0.0f, 0.0f, scale.z, 0.0f);

            col3 = c_v4f_0001;
        }

        PLATFORM_INLINE void SetupByLookAt(const float3& vForward)
        {
            float3 y = Normalize(vForward);
            float3 z = GetPerpendicularVector(y);
            float3 x = Cross(y, z);

            SetCol0_0(x);
            SetCol1_0(y);
            SetCol2_0(z);

            col3 = c_v4f_0001;
        }

        PLATFORM_INLINE void SetupByLookAt(const float3& vForward, const float3& vRight)
        {
            float3 y = Normalize(vForward);
            float3 z = Normalize( Cross(vRight, y) );
            float3 x = Cross(y, z);

            SetCol0_0(x);
            SetCol1_0(y);
            SetCol2_0(z);

            col3 = c_v4f_0001;
        }

        PLATFORM_INLINE void SetupByOrthoProjection(float left, float right, float bottom, float top, float zNear, float zFar, uint32_t uiProjFlags = 0)
        {
            DEBUG_Assert( left < right );
            DEBUG_Assert( bottom < top );

            float rWidth = 1.0f / (right - left);
            float rHeight = 1.0f / (top - bottom);
            float rDepth = 1.0f / (zFar - zNear);

            a00 = 2.0f * rWidth;
            a01 = 0.0f;
            a02 = 0.0f;
            a03 = -(right + left) * rWidth;

            a10 = 0.0f;
            a11 = 2.0f * rHeight;
            a12 = 0.0f;
            a13 = -(top + bottom) * rHeight;

            a20 = 0.0f;
            a21 = 0.0f;
            a22 = -2.0f * rDepth;
            a23 = -(zFar + zNear) * rDepth;

            a30 = 0.0f;
            a31 = 0.0f;
            a32 = 0.0f;
            a33 = 1.0f;

            bool bReverseZ = (uiProjFlags & PROJ_REVERSED_Z) != 0;

            a22 = Zbuffer::ModifyProjZ(bReverseZ, a22, a32);
            a23 = Zbuffer::ModifyProjZ(bReverseZ, a23, a33);

            if( uiProjFlags & PROJ_LEFT_HANDED )
                col2 = v4f_negate(col2);
        }

        PLATFORM_INLINE void SetupByFrustum(float left, float right, float bottom, float top, float zNear, float zFar, uint32_t uiProjFlags = 0)
        {
            DEBUG_Assert( left < right );
            DEBUG_Assert( bottom < top );

            float rWidth = 1.0f / (right - left);
            float rHeight = 1.0f / (top - bottom);
            float rDepth = 1.0f / (zNear - zFar);

            a00 = 2.0f * zNear * rWidth;
            a01 = 0.0f;
            a02 = (right + left) * rWidth;
            a03 = 0.0f;

            a10 = 0.0f;
            a11 = 2.0f * zNear * rHeight;
            a12 = (top + bottom) * rHeight;
            a13 = 0.0f;

            a20 = 0.0f;
            a21 = 0.0f;
            a22 = (zFar + zNear) * rDepth;
            a23 = 2.0f * zFar * zNear * rDepth;

            a30 = 0.0f;
            a31 = 0.0f;
            a32 = -1.0f;
            a33 = 0.0f;

            bool bReverseZ = (uiProjFlags & PROJ_REVERSED_Z) != 0;

            a22 = Zbuffer::ModifyProjZ(bReverseZ, a22, a32);
            a23 = Zbuffer::ModifyProjZ(bReverseZ, a23, a33);

            if( uiProjFlags & PROJ_LEFT_HANDED )
                col2 = v4f_negate(col2);
        }

        PLATFORM_INLINE void SetupByFrustumInf(float left, float right, float bottom, float top, float zNear, uint32_t uiProjFlags = 0)
        {
            DEBUG_Assert( left < right );
            DEBUG_Assert( bottom < top );

            float rWidth = 1.0f / (right - left);
            float rHeight = 1.0f / (top - bottom);

            a00 = 2.0f * zNear * rWidth;
            a01 = 0.0f;
            a02 = (right + left) * rWidth;
            a03 = 0.0f;

            a10 = 0.0f;
            a11 = 2.0f * zNear * rHeight;
            a12 = (top + bottom) * rHeight;
            a13 = 0.0f;

            a20 = 0.0f;
            a21 = 0.0f;
            a22 = -1.0f;
            a23 = -2.0f * zNear;

            a30 = 0.0f;
            a31 = 0.0f;
            a32 = -1.0f;
            a33 = 0.0f;

            bool bReverseZ = (uiProjFlags & PROJ_REVERSED_Z) != 0;

            a22 = Zbuffer::ModifyProjZ(bReverseZ, a22, a32);
            a23 = Zbuffer::ModifyProjZ(bReverseZ, a23, a33);

            if( uiProjFlags & PROJ_LEFT_HANDED )
                col2 = v4f_negate(col2);
        }

        PLATFORM_INLINE void SetupByHalfFovy(float halfFovy, float aspect, float zNear, float zFar, uint32_t uiProjFlags = 0)
        {
            float ymax = zNear * Tan(halfFovy);
            float xmax = ymax * aspect;

            SetupByFrustum(-xmax, xmax, -ymax, ymax, zNear, zFar, uiProjFlags);
        }

        PLATFORM_INLINE void SetupByHalfFovyInf(float halfFovy, float aspect, float zNear, uint32_t uiProjFlags = 0)
        {
            float ymax = zNear * Tan(halfFovy);
            float xmax = ymax * aspect;

            SetupByFrustumInf(-xmax, xmax, -ymax, ymax, zNear, uiProjFlags);
        }

        PLATFORM_INLINE void SetupByHalfFovx(float halfFovx, float aspect, float zNear, float zFar, uint32_t uiProjFlags = 0)
        {
            float xmax = zNear * Tan(halfFovx);
            float ymax = xmax / aspect;

            SetupByFrustum(-xmax, xmax, -ymax, ymax, zNear, zFar, uiProjFlags);
        }

        PLATFORM_INLINE void SetupByHalfFovxInf(float halfFovx, float aspect, float zNear, uint32_t uiProjFlags = 0)
        {
            float xmax = zNear * Tan(halfFovx);
            float ymax = xmax / aspect;

            SetupByFrustumInf(-xmax, xmax, -ymax, ymax, zNear, uiProjFlags);
        }

        PLATFORM_INLINE void SetupByAngles(float angleMinx, float angleMaxx, float angleMiny, float angleMaxy, float zNear, float zFar, uint32_t uiProjFlags = 0)
        {
            float xmin = Tan(angleMinx) * zNear;
            float xmax = Tan(angleMaxx) * zNear;
            float ymin = Tan(angleMiny) * zNear;
            float ymax = Tan(angleMaxy) * zNear;

            SetupByFrustum(xmin, xmax, ymin, ymax, zNear, zFar, uiProjFlags);
        }

        PLATFORM_INLINE void SetupByAnglesInf(float angleMinx, float angleMaxx, float angleMiny, float angleMaxy, float zNear, uint32_t uiProjFlags = 0)
        {
            float xmin = Tan(angleMinx) * zNear;
            float xmax = Tan(angleMaxx) * zNear;
            float ymin = Tan(angleMiny) * zNear;
            float ymax = Tan(angleMaxy) * zNear;

            SetupByFrustumInf(xmin, xmax, ymin, ymax, zNear, uiProjFlags);
        }

        PLATFORM_INLINE void SubsampleProjection(float dx, float dy, uint32_t viewportWidth, uint32_t viewportHeight)
        {
            // NOTE: dx/dy in range [-1; 1]

            a02 += dx / float(viewportWidth);
            a12 += dy / float(viewportHeight);
        }

        PLATFORM_INLINE bool IsProjectionValid() const
        {
            // Do not check a20 and a21 to allow off-centered projections
            // Do not check a22 to allow reverse infinite projections

            return
            (
                (a00 != 0.0f && a10 == 0.0f && a20 == 0.0f && a30 == 0.0f)
                && (a01 == 0.0f && a11 != 0.0f && a21 == 0.0f && a31 == 0.0f)
                && (a32 == 1.0f || a32 == -1.0f)
                && (a03 == 0.0f && a13 == 0.0f && a23 != 0.0f && a33 == 0.0f)
            );
        }
};

//======================================================================================================================
//                                                      cBoxf
//======================================================================================================================

class cBoxf
{
    public:

        float3 vMin;
        float3 vMax;

    public:

        PLATFORM_INLINE cBoxf()
        {
            Clear();
        }

        PLATFORM_INLINE cBoxf(const float3& v)
        {
            vMin = v;
            vMax = v;
        }

        PLATFORM_INLINE cBoxf(const float3& minv, const float3& maxv)
        {
            vMin = minv;
            vMax = maxv;
        }

        PLATFORM_INLINE void Clear()
        {
            vMin = float3(c_v4f_Inf);
            vMax = float3(c_v4f_InfMinus);
        }

        PLATFORM_INLINE bool IsValid() const
        {
            v4f r = v4f_less(vMin.xmm, vMax.xmm);

            return v4f_test3_all(r);
        }

        PLATFORM_INLINE float3 GetCenter() const
        {
            return (vMin + vMax) * 0.5f;
        }

        PLATFORM_INLINE float GetRadius() const
        {
            return Length(vMax - vMin) * 0.5f;
        }

        PLATFORM_INLINE void Scale(float fScale)
        {
            fScale *= 0.5f;

            float k1 = 0.5f + fScale;
            float k2 = 0.5f - fScale;

            float3 a = vMin * k1 + vMax * k2;
            float3 b = vMax * k1 + vMin * k2;

            vMin = a;
            vMax = b;
        }

        PLATFORM_INLINE void Enlarge(const float3& vBorder)
        {
            vMin -= vBorder;
            vMax += vBorder;
        }

        PLATFORM_INLINE void Add(const float3& v)
        {
            vMin = _mm_min_ps(vMin.xmm, v.xmm);
            vMax = _mm_max_ps(vMax.xmm, v.xmm);
        }

        PLATFORM_INLINE void Add(const cBoxf& b)
        {
            vMin = _mm_min_ps(vMin.xmm, b.vMin.xmm);
            vMax = _mm_max_ps(vMax.xmm, b.vMax.xmm);
        }

        PLATFORM_INLINE float DistanceSquared(const float3& from) const
        {
            v4f p = v4f_clamp(from.xmm, vMin.xmm, vMax.xmm);
            p = _mm_sub_ps(p, from.xmm);
            p = v4f_dot33(p, p);

            return v4f_get_x(p);
        }

        PLATFORM_INLINE float Distance(const float3& from) const
        {
            v4f p = v4f_clamp(from.xmm, vMin.xmm, vMax.xmm);
            p = _mm_sub_ps(p, from.xmm);
            p = v4f_length(p);

            return v4f_get_x(p);
        }

        PLATFORM_INLINE bool IsIntersectWith(const cBoxf& b) const
        {
            v4f r = v4f_less(vMax.xmm, b.vMin.xmm);
            r = _mm_or_ps(r, v4f_greater(vMin.xmm, b.vMax.xmm));

            return v4f_test3_none(r);
        }

        // NOTE: intersection state 'b' vs 'this'

        PLATFORM_INLINE eClip GetIntersectionState(const cBoxf& b) const
        {
            if( !IsIntersectWith(b) )
                return CLIP_OUT;

            v4f r = v4f_less(vMin.xmm, b.vMin.xmm);
            r = _mm_and_ps(r, v4f_greater(vMax.xmm, b.vMax.xmm));

            return v4f_test3_all(r) ? CLIP_IN : CLIP_PARTIAL;
        }

        PLATFORM_INLINE bool IsContain(const float3& p) const
        {
            v4f r = v4f_less(p.xmm, vMin.xmm);
            r = _mm_or_ps(r, v4f_greater(p.xmm, vMax.xmm));

            return v4f_test3_none(r);
        }

        PLATFORM_INLINE bool IsContainSphere(const float3& center, float radius) const
        {
            v4f r = _mm_broadcast_ss(&radius);
            v4f t = _mm_sub_ps(vMin.xmm, r);
            t = v4f_less(center.xmm, t);

            if( v4f_test3_any(t) )
                return false;

            t = _mm_add_ps(vMax.xmm, r);
            t = v4f_greater(center.xmm, t);

            if( v4f_test3_any(t) )
                return false;

            return true;
        }

        PLATFORM_INLINE uint32_t GetIntersectionBits(const cBoxf& b) const
        {
            v4f r = v4f_gequal(b.vMin.xmm, vMin.xmm);
            uint32_t bits = v4f_bits3(r);

            r = v4f_lequal(b.vMax.xmm, vMax.xmm);
            bits |= v4f_bits3(r) << 3;

            return bits;
        }

        PLATFORM_INLINE uint32_t IsContain(const float3& p, uint32_t bits) const
        {
            v4f r = v4f_gequal(p.xmm, vMin.xmm);
            bits |= v4f_bits3(r);

            r = v4f_lequal(p.xmm, vMax.xmm);
            bits |= v4f_bits3(r) << 3;

            return bits;
        }

        PLATFORM_INLINE bool IsIntersectWith(const float3& vRayPos, const float3& vRayDir, float* out_fTmin, float* out_fTmax) const
        {
            // NOTE: http://tavianator.com/2011/05/fast-branchless-raybounding-box-intersections/

            // IMPORTANT: store '1 / ray_dir' and filter INFs out!

            v4f t1 = _mm_div_ps(_mm_sub_ps(vMin.xmm, vRayPos.xmm), vRayDir.xmm);
            v4f t2 = _mm_div_ps(_mm_sub_ps(vMax.xmm, vRayPos.xmm), vRayDir.xmm);

            v4f vmin = _mm_min_ps(t1, t2);
            v4f vmax = _mm_max_ps(t1, t2);

            // NOTE: hmax.xxx
            v4f tmin = _mm_max_ps(vmin, v4f_swizzle(vmin, COORD_Y, COORD_Z, COORD_X, 0));
            tmin = _mm_max_ps(tmin, v4f_swizzle(vmin, COORD_Z, COORD_X, COORD_Y, 0));

            // NOTE: hmin.xxx
            v4f tmax = _mm_min_ps(vmax, v4f_swizzle(vmax, COORD_Y, COORD_Z, COORD_X, 0));
            tmax = _mm_min_ps(tmax, v4f_swizzle(vmax, COORD_Z, COORD_X, COORD_Y, 0));

            v4f_store_x(out_fTmin, tmin);
            v4f_store_x(out_fTmax, tmax);

            v4f cmp = v4f_gequal(tmax, tmin);

            return (v4f_bits4(cmp) & v4f_mask(1, 0, 0, 0)) == v4f_mask(1, 0, 0, 0);
        }
};

//======================================================================================================================
//                                                      Misc
//======================================================================================================================

PLATFORM_INLINE float3 Reflect(const float3& v, const float3& n)
{
    // NOTE: slow
    // return v - n * Dot33(n, v) * 2;

    v4f dot0 = v4f_dot33(n.xmm, v.xmm);
    dot0 = _mm_mul_ps(dot0, _mm_set1_ps(2.0f));

    return v4f_nmadd(n.xmm, dot0, v.xmm);
}

PLATFORM_INLINE float3 Refract(const float3& v, const float3& n, float eta)
{
    // NOTE: slow
    /*
    float dot = Dot33(v, n);
    float k = 1 - eta * eta * (1 - dot * dot);

    if( k < 0 )
        return 0

    return v * eta - n * (eta * dot + Sqrt(k));
    */

    v4f eta0 = _mm_broadcast_ss(&eta);
    v4f dot0 = v4f_dot33(n.xmm, v.xmm);
    v4f mul0 = _mm_mul_ps(eta0, eta0);
    v4f sub0 = v4f_nmadd(dot0, dot0, c_v4f_1111);
    v4f sub1 = v4f_nmadd(mul0, sub0, c_v4f_1111);

    if( v4f_isnegative4_all(sub1) )
        return v4f_zero;

    v4f mul5 = _mm_mul_ps(eta0, v.xmm);
    v4f mul3 = _mm_mul_ps(eta0, dot0);
    v4f sqt0 = v4f_sqrt(sub1);
    v4f add0 = _mm_add_ps(mul3, sqt0);

    return v4f_nmadd(add0, n.xmm, mul5);
}

PLATFORM_INLINE bool IsPointsNear(const float3& p1, const float3& p2, float eps = c_fEps)
{
    v4f r = _mm_sub_ps(p1.xmm, p2.xmm);
    r = v4f_abs(r);
    r = v4f_lequal(r, _mm_broadcast_ss(&eps));

    return v4f_test3_all(r);
}

PLATFORM_INLINE float3 Rotate(const float4x4& m, const float3& v)
{
    v4f r = _mm_mul_ps(v4f_swizzle(v.xmm, 0, 0, 0, 0), m.col0);
    r = v4f_madd(v4f_swizzle(v.xmm, 1, 1, 1, 1), m.col1, r);
    r = v4f_madd(v4f_swizzle(v.xmm, 2, 2, 2, 2), m.col2, r);
    r = v4f_setw0(r);

    return r;
}

PLATFORM_INLINE void float4x4::PreTranslation(const float3& p)
{
    v4f r = Rotate(*this, p.xmm).xmm;
    col3 = _mm_add_ps(col3, r);
}

PLATFORM_INLINE void float4x4::InvertOrtho()
{
    Transpose3x4();

    col3 = Rotate(*this, col3).xmm;
    col3 = v4f_negate(col3);

    col0 = v4f_setw0(col0);
    col1 = v4f_setw0(col1);
    col2 = v4f_setw0(col2);
    col3 = v4f_setw1(col3);
}

PLATFORM_INLINE float3 RotateAbs(const float4x4& m, const float3& v)
{
    v4f col0_abs = v4f_abs(m.col0);
    v4f col1_abs = v4f_abs(m.col1);
    v4f col2_abs = v4f_abs(m.col2);

    v4f r = _mm_mul_ps(v4f_swizzle(v.xmm, 0, 0, 0, 0), col0_abs);
    r = v4f_madd(v4f_swizzle(v.xmm, 1, 1, 1, 1), col1_abs, r);
    r = v4f_madd(v4f_swizzle(v.xmm, 2, 2, 2, 2), col2_abs, r);

    return r;
}

PLATFORM_INLINE void TransformAabb(const float4x4& mTransform, const cBoxf& src, cBoxf& dst)
{
    float3 center = (src.vMin + src.vMax) * 0.5f;
    float3 extends = src.vMax - center;

    center = mTransform * center;
    extends = RotateAbs(mTransform, extends);

    dst.vMin = center - extends;
    dst.vMax = center + extends;
}

PLATFORM_INLINE float3 Project(const float3& v, const float4x4& m)
{
    float4 clip = (m * v).xmm;
    clip /= clip.w;

    return clip.To3d();
}
