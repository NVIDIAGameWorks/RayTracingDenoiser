#pragma once

//======================================================================================================================
//                                                      double3
//======================================================================================================================

class double3
{
    public:

        union
        {
            struct
            {
                v4d ymm;
            };

            struct
            {
                double pv[COORD_3D];
            };

            struct
            {
                double x, y, z;
            };
        };

    public:

        // NOTE: constructors

        PLATFORM_INLINE double3()
        {
        }

        PLATFORM_INLINE double3(double a, double b, double c) : ymm( v4d_set(a, b, c, 0.0) )
        {
        }

        PLATFORM_INLINE double3(double a) : ymm( _mm256_broadcast_sd(&a) )
        {
        }

        PLATFORM_INLINE double3(const double* v3)
        {
            ymm = double3(v3[0], v3[1], v3[2]).ymm;
        }

        PLATFORM_INLINE double3(const v4d& vec) : ymm(vec)
        {
        }

        // NOTE: set

        PLATFORM_INLINE void Set0()
        {
            ymm = v4d_zero;
        }

        PLATFORM_INLINE void operator = (const double3& vec)
        {
            ymm = vec.ymm;
        }

        // NOTE: compare

        PLATFORM_INLINE bool operator == (const double3& v) const
        {
            v4d r = v4d_equal(ymm, v.ymm);

            return v4d_test3_all(r);
        }

        PLATFORM_INLINE bool operator != (const double3& v) const
        {
            v4d r = v4d_notequal(ymm, v.ymm);

            return v4d_test3_any(r);
        }

        // NOTE: arithmetic

        PLATFORM_INLINE double3 operator - () const
        {
            return v4d_negate(ymm);
        }

        PLATFORM_INLINE double3 operator + (const double3& v) const
        {
            return _mm256_add_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE double3 operator - (const double3& v) const
        {
            return _mm256_sub_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE double3 operator * (const double3& v) const
        {
            return _mm256_mul_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE double3 operator / (const double3& v) const
        {
            DEBUG_Assert( v4d_not0_all(v.ymm) );

            return _mm256_div_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE void operator += (const double3& v)
        {
            ymm = _mm256_add_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE void operator -= (const double3& v)
        {
            ymm = _mm256_sub_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE void operator *= (const double3& v)
        {
            ymm = _mm256_mul_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE void operator /= (const double3& v)
        {
            DEBUG_Assert( v4d_not0_all(v.ymm) );

            ymm = _mm256_div_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE void operator *= (double s)
        {
            ymm = _mm256_mul_pd(ymm, _mm256_broadcast_sd(&s));
        }

        PLATFORM_INLINE void operator /= (double s)
        {
            DEBUG_Assert( s != 0.0 );

            ymm = _mm256_div_pd(ymm, _mm256_broadcast_sd(&s));
        }

        PLATFORM_INLINE double3 operator / (double s) const
        {
            DEBUG_Assert( s != 0.0 );

            return _mm256_div_pd(ymm, _mm256_broadcast_sd(&s));
        }

        PLATFORM_INLINE double3 operator * (double s) const
        {
            return _mm256_mul_pd(ymm, _mm256_broadcast_sd(&s));
        }

        // NOTE: misc

        PLATFORM_INLINE bool IsZero() const
        {
            v4d r = v4d_equal(ymm, v4d_zero);

            return v4d_test3_all(r);
        }

        static PLATFORM_INLINE double3 Zero()
        {
            return v4d_zero;
        }

        // NOTE: swizzle

        PLATFORM_INLINE double3 xxx() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_X, 0); }
        PLATFORM_INLINE double3 xxy() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_Y, 0); }
        PLATFORM_INLINE double3 xxz() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_Z, 0); }
        PLATFORM_INLINE double3 xyx() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_X, 0); }
        PLATFORM_INLINE double3 xyy() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_Y, 0); }
        PLATFORM_INLINE double3 xyz() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_Z, 0); }
        PLATFORM_INLINE double3 xzx() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_X, 0); }
        PLATFORM_INLINE double3 xzy() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_Y, 0); }
        PLATFORM_INLINE double3 xzz() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_Z, 0); }
        PLATFORM_INLINE double3 yxx() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_X, 0); }
        PLATFORM_INLINE double3 yxy() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_Y, 0); }
        PLATFORM_INLINE double3 yxz() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_Z, 0); }
        PLATFORM_INLINE double3 yyx() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_X, 0); }
        PLATFORM_INLINE double3 yyy() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_Y, 0); }
        PLATFORM_INLINE double3 yyz() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_Z, 0); }
        PLATFORM_INLINE double3 yzx() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_X, 0); }
        PLATFORM_INLINE double3 yzy() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_Y, 0); }
        PLATFORM_INLINE double3 yzz() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_Z, 0); }
        PLATFORM_INLINE double3 zxx() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_X, 0); }
        PLATFORM_INLINE double3 zxy() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_Y, 0); }
        PLATFORM_INLINE double3 zxz() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_Z, 0); }
        PLATFORM_INLINE double3 zyx() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_X, 0); }
        PLATFORM_INLINE double3 zyy() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_Y, 0); }
        PLATFORM_INLINE double3 zyz() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_Z, 0); }
        PLATFORM_INLINE double3 zzx() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_X, 0); }
        PLATFORM_INLINE double3 zzy() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_Y, 0); }
        PLATFORM_INLINE double3 zzz() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_Z, 0); }
};

PLATFORM_INLINE double Dot33(const double3& a, const double3& b)
{
    v4d r = v4d_dot33(a.ymm, b.ymm);

    return v4d_get_x(r);
}

PLATFORM_INLINE double LengthSquared(const double3& x)
{
    v4d r = v4d_dot33(x.ymm, x.ymm);

    return v4d_get_x(r);
}

PLATFORM_INLINE double Length(const double3& x)
{
    v4d r = v4d_length(x.ymm);

    return v4d_get_x(r);
}

PLATFORM_INLINE double3 Normalize(const double3& x)
{
    return v4d_normalize(x.ymm);
}

PLATFORM_INLINE double3 Cross(const double3& x, const double3& y)
{
    return v4d_cross(x.ymm, y.ymm);
}

PLATFORM_INLINE double3 Rcp(const double3& x)
{
    return v4d_rcp( v4d_setw1(x.ymm) );
}

PLATFORM_INLINE double3 Ceil(const double3& x)
{
    return v4d_ceil(x.ymm);
}

PLATFORM_INLINE double3 Madd(const double3& a, const double3& b, const double3& c)
{
    return v4d_madd(a.ymm, b.ymm, c.ymm);
}

PLATFORM_INLINE double3 GetPerpendicularVector(const double3& N)
{
    double3 T = double3(N.z, -N.x, N.y);
    T -= N * Dot33(T, N);

    return Normalize(T);
}

//======================================================================================================================
//                                                      double4
//======================================================================================================================

class double4
{
    public:

        union
        {
            struct
            {
                v4d ymm;
            };

            struct
            {
                double pv[COORD_4D];
            };

            struct
            {
                double x, y, z, w;
            };
        };

    public:

        // NOTE: constructors

        PLATFORM_INLINE double4()
        {
        }

        PLATFORM_INLINE double4(double a) : ymm( _mm256_broadcast_sd(&a) )
        {
        }

        PLATFORM_INLINE double4(double a, double b, double c, double d) : ymm( v4d_set(a, b, c, d) )
        {
        }

        PLATFORM_INLINE double4(const double* v4) : ymm( _mm256_loadu_pd(v4) )
        {
        }

        PLATFORM_INLINE double4(const v4d& m) : ymm(m)
        {
        }

        // NOTE: set

        PLATFORM_INLINE void Set0001()
        {
            ymm = c_v4d_0001;
        }

        PLATFORM_INLINE void Set0()
        {
            ymm = v4d_zero;
        }

        PLATFORM_INLINE void operator = (const double4& vec)
        {
            ymm = vec.ymm;
        }

        // NOTE: compare

        PLATFORM_INLINE bool operator == (const double4& v) const
        {
            v4d r = v4d_equal(ymm, v.ymm);

            return v4d_test4_all(r);
        }

        PLATFORM_INLINE bool operator != (const double4& v) const
        {
            v4d r = v4d_notequal(ymm, v.ymm);

            return v4d_test4_any(r);
        }

        // NOTE: arithmetic

        PLATFORM_INLINE double4 operator - () const
        {
            return v4d_negate(ymm);
        }

        PLATFORM_INLINE double4 operator + (const double4& v) const
        {
            return _mm256_add_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE double4 operator - (const double4& v) const
        {
            return _mm256_sub_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE double4 operator * (const double4& v) const
        {
            return _mm256_mul_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE double4 operator / (const double4& v) const
        {
            DEBUG_Assert( v4d_not0_all(v.ymm) );

            return _mm256_div_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE void operator += (const double4& v)
        {
            ymm = _mm256_add_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE void operator -= (const double4& v)
        {
            ymm = _mm256_sub_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE void operator *= (const double4& v)
        {
            ymm = _mm256_mul_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE void operator /= (const double4& v)
        {
            DEBUG_Assert( v4d_not0_all(v.ymm) );

            ymm = _mm256_div_pd(ymm, v.ymm);
        }

        PLATFORM_INLINE void operator *= (double s)
        {
            ymm = _mm256_mul_pd(ymm, _mm256_broadcast_sd(&s));
        }

        PLATFORM_INLINE void operator /= (double s)
        {
            DEBUG_Assert( s != 0.0 );

            ymm = _mm256_div_pd(ymm, _mm256_broadcast_sd(&s));
        }

        PLATFORM_INLINE double4 operator / (double s) const
        {
            DEBUG_Assert( s != 0.0 );

            return _mm256_div_pd(ymm, _mm256_broadcast_sd(&s));
        }

        PLATFORM_INLINE double4 operator * (double s) const
        {
            return _mm256_mul_pd(ymm, _mm256_broadcast_sd(&s));
        }

        // NOTE: misc

        PLATFORM_INLINE const double3& To3d() const
        {
            return (double3&)ymm;
        }

        PLATFORM_INLINE bool IsZero() const
        {
            v4d r = v4d_equal(ymm, v4d_zero);

            return v4d_test4_all(r);
        }

        // NOTE: swizzle

        PLATFORM_INLINE double4 xxxx() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 xxxy() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 xxxz() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 xxxw() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 xxyx() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 xxyy() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 xxyz() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 xxyw() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 xxzx() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 xxzy() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 xxzz() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 xxzw() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 xxwx() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 xxwy() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 xxwz() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 xxww() const { return v4d_swizzle(ymm, COORD_X, COORD_X, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 xyxx() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 xyxy() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 xyxz() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 xyxw() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 xyyx() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 xyyy() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 xyyz() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 xyyw() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 xyzx() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 xyzy() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 xyzz() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 xyzw() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 xywx() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 xywy() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 xywz() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 xyww() const { return v4d_swizzle(ymm, COORD_X, COORD_Y, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 xzxx() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 xzxy() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 xzxz() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 xzxw() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 xzyx() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 xzyy() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 xzyz() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 xzyw() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 xzzx() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 xzzy() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 xzzz() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 xzzw() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 xzwx() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 xzwy() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 xzwz() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 xzww() const { return v4d_swizzle(ymm, COORD_X, COORD_Z, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 xwxx() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 xwxy() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 xwxz() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 xwxw() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 xwyx() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 xwyy() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 xwyz() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 xwyw() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 xwzx() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 xwzy() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 xwzz() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 xwzw() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 xwwx() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 xwwy() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 xwwz() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 xwww() const { return v4d_swizzle(ymm, COORD_X, COORD_W, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 yxxx() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 yxxy() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 yxxz() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 yxxw() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 yxyx() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 yxyy() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 yxyz() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 yxyw() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 yxzx() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 yxzy() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 yxzz() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 yxzw() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 yxwx() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 yxwy() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 yxwz() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 yxww() const { return v4d_swizzle(ymm, COORD_Y, COORD_X, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 yyxx() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 yyxy() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 yyxz() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 yyxw() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 yyyx() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 yyyy() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 yyyz() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 yyyw() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 yyzx() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 yyzy() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 yyzz() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 yyzw() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 yywx() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 yywy() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 yywz() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 yyww() const { return v4d_swizzle(ymm, COORD_Y, COORD_Y, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 yzxx() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 yzxy() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 yzxz() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 yzxw() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 yzyx() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 yzyy() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 yzyz() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 yzyw() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 yzzx() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 yzzy() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 yzzz() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 yzzw() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 yzwx() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 yzwy() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 yzwz() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 yzww() const { return v4d_swizzle(ymm, COORD_Y, COORD_Z, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 ywxx() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 ywxy() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 ywxz() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 ywxw() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 ywyx() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 ywyy() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 ywyz() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 ywyw() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 ywzx() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 ywzy() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 ywzz() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 ywzw() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 ywwx() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 ywwy() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 ywwz() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 ywww() const { return v4d_swizzle(ymm, COORD_Y, COORD_W, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 zxxx() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 zxxy() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 zxxz() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 zxxw() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 zxyx() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 zxyy() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 zxyz() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 zxyw() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 zxzx() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 zxzy() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 zxzz() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 zxzw() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 zxwx() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 zxwy() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 zxwz() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 zxww() const { return v4d_swizzle(ymm, COORD_Z, COORD_X, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 zyxx() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 zyxy() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 zyxz() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 zyxw() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 zyyx() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 zyyy() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 zyyz() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 zyyw() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 zyzx() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 zyzy() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 zyzz() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 zyzw() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 zywx() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 zywy() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 zywz() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 zyww() const { return v4d_swizzle(ymm, COORD_Z, COORD_Y, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 zzxx() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 zzxy() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 zzxz() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 zzxw() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 zzyx() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 zzyy() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 zzyz() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 zzyw() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 zzzx() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 zzzy() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 zzzz() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 zzzw() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 zzwx() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 zzwy() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 zzwz() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 zzww() const { return v4d_swizzle(ymm, COORD_Z, COORD_Z, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 zwxx() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 zwxy() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 zwxz() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 zwxw() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 zwyx() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 zwyy() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 zwyz() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 zwyw() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 zwzx() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 zwzy() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 zwzz() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 zwzw() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 zwwx() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 zwwy() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 zwwz() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 zwww() const { return v4d_swizzle(ymm, COORD_Z, COORD_W, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 wxxx() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 wxxy() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 wxxz() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 wxxw() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 wxyx() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 wxyy() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 wxyz() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 wxyw() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 wxzx() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 wxzy() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 wxzz() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 wxzw() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 wxwx() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 wxwy() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 wxwz() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 wxww() const { return v4d_swizzle(ymm, COORD_W, COORD_X, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 wyxx() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 wyxy() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 wyxz() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 wyxw() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 wyyx() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 wyyy() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 wyyz() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 wyyw() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 wyzx() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 wyzy() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 wyzz() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 wyzw() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 wywx() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 wywy() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 wywz() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 wyww() const { return v4d_swizzle(ymm, COORD_W, COORD_Y, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 wzxx() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 wzxy() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 wzxz() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 wzxw() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 wzyx() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 wzyy() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 wzyz() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 wzyw() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 wzzx() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 wzzy() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 wzzz() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 wzzw() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 wzwx() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 wzwy() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 wzwz() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 wzww() const { return v4d_swizzle(ymm, COORD_W, COORD_Z, COORD_W, COORD_W); }
        PLATFORM_INLINE double4 wwxx() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_X, COORD_X); }
        PLATFORM_INLINE double4 wwxy() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_X, COORD_Y); }
        PLATFORM_INLINE double4 wwxz() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_X, COORD_Z); }
        PLATFORM_INLINE double4 wwxw() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_X, COORD_W); }
        PLATFORM_INLINE double4 wwyx() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_Y, COORD_X); }
        PLATFORM_INLINE double4 wwyy() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_Y, COORD_Y); }
        PLATFORM_INLINE double4 wwyz() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_Y, COORD_Z); }
        PLATFORM_INLINE double4 wwyw() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_Y, COORD_W); }
        PLATFORM_INLINE double4 wwzx() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_Z, COORD_X); }
        PLATFORM_INLINE double4 wwzy() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_Z, COORD_Y); }
        PLATFORM_INLINE double4 wwzz() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_Z, COORD_Z); }
        PLATFORM_INLINE double4 wwzw() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_Z, COORD_W); }
        PLATFORM_INLINE double4 wwwx() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_W, COORD_X); }
        PLATFORM_INLINE double4 wwwy() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_W, COORD_Y); }
        PLATFORM_INLINE double4 wwwz() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_W, COORD_Z); }
        PLATFORM_INLINE double4 wwww() const { return v4d_swizzle(ymm, COORD_W, COORD_W, COORD_W, COORD_W); }
};

PLATFORM_INLINE double Dot44(const double4& a, const double4& b)
{
    v4d r = v4d_dot44(a.ymm, b.ymm);

    return v4d_get_x(r);
}

PLATFORM_INLINE double Dot43(const double4& a, const double3& b)
{
    v4d r = v4d_dot43(a.ymm, b.ymm);

    return v4d_get_x(r);
}

PLATFORM_INLINE double4 Rcp(const double4& x)
{
    return v4d_rcp(x.ymm);
}

PLATFORM_INLINE double4 Ceil(const double4& x)
{
    return v4d_ceil(x.ymm);
}

PLATFORM_INLINE double4 Madd(const double4& a, const double4& b, const double4& c)
{
    return v4d_madd(a.ymm, b.ymm, c.ymm);
}

//======================================================================================================================
//                                                      double4x4
//======================================================================================================================

class double4x4
{
    public:

        // IMPORTANT: store - "column-major", math - "row-major" (vector is column)

        union
        {
            struct
            {
                v4d col0;
                v4d col1;
                v4d col2;
                v4d col3;
            };

            struct
            {
                double a16[16];
            };

            struct
            {
                struct
                {
                    double a00, a10, a20, a30;
                };

                struct
                {
                    double a01, a11, a21, a31;
                };

                struct
                {
                    double a02, a12, a22, a32;
                };

                struct
                {
                    double a03, a13, a23, a33;
                };
            };
        };

    public:

        // NOTE: constructors

        PLATFORM_INLINE double4x4()
        {
        }

        PLATFORM_INLINE double4x4
        (
            double m00, double m01, double m02, double m03,
            double m10, double m11, double m12, double m13,
            double m20, double m21, double m22, double m23,
            double m30, double m31, double m32, double m33
        ) :
            col0( v4d_set(m00, m10, m20, m30) )
            , col1( v4d_set(m01, m11, m21, m31) )
            , col2( v4d_set(m02, m12, m22, m32) )
            , col3( v4d_set(m03, m13, m23, m33) )
        {
        }

        PLATFORM_INLINE double4x4(const double4& c0, const double4& c1, const double4& c2, const double4& c3) :
            col0( c0.ymm )
            , col1( c1.ymm )
            , col2( c2.ymm )
            , col3( c3.ymm )
        {
        }

        PLATFORM_INLINE double4x4(const double4x4& m) :
            col0( m.col0 )
            , col1( m.col1 )
            , col2( m.col2 )
            , col3( m.col3 )
        {
        }

        // NOTE: set

        PLATFORM_INLINE double4x4 Identity()
        {
            return double4x4(
                1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0
            );
        }

        PLATFORM_INLINE void SetCol0(const double4& v)
        {
            col0 = v.ymm;
        }

        PLATFORM_INLINE void SetCol1(const double4& v)
        {
            col1 = v.ymm;
        }

        PLATFORM_INLINE void SetCol2(const double4& v)
        {
            col2 = v.ymm;
        }

        PLATFORM_INLINE void SetCol3(const double4& v)
        {
            col3 = v.ymm;
        }

        PLATFORM_INLINE void SetCol0_0(const double3& v)
        {
            col0 = v4d_setw0(v.ymm);
        }

        PLATFORM_INLINE void SetCol1_0(const double3& v)
        {
            col1 = v4d_setw0(v.ymm);
        }

        PLATFORM_INLINE void SetCol2_0(const double3& v)
        {
            col2 = v4d_setw0(v.ymm);
        }

        PLATFORM_INLINE void SetCol3_1(const double3& v)
        {
            col3 = v4d_setw1(v.ymm);
        }

        PLATFORM_INLINE void SetCol0(double x, double y, double z, double w)
        {
            col0 = v4d_set(x, y, z, w);
        }

        PLATFORM_INLINE void SetCol1(double x, double y, double z, double w)
        {
            col1 = v4d_set(x, y, z, w);
        }

        PLATFORM_INLINE void SetCol2(double x, double y, double z, double w)
        {
            col2 = v4d_set(x, y, z, w);
        }

        PLATFORM_INLINE void SetCol3(double x, double y, double z, double w)
        {
            col3 = v4d_set(x, y, z, w);
        }

        PLATFORM_INLINE void operator = (const double4x4& m)
        {
            col0 = m.col0;
            col1 = m.col1;
            col2 = m.col2;
            col3 = m.col3;
        }

        // NOTE: compare

        PLATFORM_INLINE bool operator == (const double4x4& m) const
        {
            return double4(col0) == m.col0 && double4(col1) == m.col1 && double4(col2) == m.col2 && double4(col3) == m.col3;
        }

        PLATFORM_INLINE bool operator != (const double4x4& m) const
        {
            return double4(col0) != m.col0 || double4(col1) != m.col1 || double4(col2) != m.col2 || double4(col3) != m.col3;
        }

        // NOTE: misc

        // IMPORTANT: col0-col2 can be used as m * axis(X/Y/Z), BUT:
        // only when translation = {0}, in other case w-component
        // must be zeroed!

        PLATFORM_INLINE const double4& GetCol0() const
        {
            return (double4&)col0;
        }

        PLATFORM_INLINE const double4& GetCol1() const
        {
            return (double4&)col1;
        }

        PLATFORM_INLINE const double4& GetCol2() const
        {
            return (double4&)col2;
        }

        PLATFORM_INLINE const double4& GetCol3() const
        {
            return (double4&)col3;
        }

        PLATFORM_INLINE double4 GetRow0() const
        {
            return double4(a00, a01, a02, a03);
        }

        PLATFORM_INLINE double4 GetRow1() const
        {
            return double4(a10, a11, a12, a13);
        }

        PLATFORM_INLINE double4 GetRow2() const
        {
            return double4(a20, a21, a22, a23);
        }

        PLATFORM_INLINE double4 GetRow3() const
        {
            return double4(a30, a31, a32, a33);
        }

        PLATFORM_INLINE double GetNdcDepth(double z) const
        {
            double a = a22 * z + a23;
            double b = a32 * z + a33;

            return a / b;
        }

        PLATFORM_INLINE double3 GetRotationYPR() const
        {
            double3 r;
            r.x = Atan(-a01, a11);
            r.y = Asin(a21);
            r.z = Atan(-a20, a22);

            return r;
        }

        PLATFORM_INLINE double4 GetQuaternion() const
        {
            // Pain in the ass for mirror transformations...
            // TODO: replace with http://www.iri.upc.edu/files/scidoc/2068-Accurate-Computation-of-Quaternions-from-Rotation-Matrices.pdf

            double4 q;

            double tr = a00 + a11 + a22;
            if (tr > 0.0f)
                q = double4( a12 - a21, a20 - a02, a01 - a10, tr + 1.0);
            else if (a00 > a11 && a00 > a22)
                q = double4( 1.0 + a00 - a11 - a22, a10 + a01, a20 + a02, a12 - a21 );
            else if (a11 > a22)
                q = double4( a10 + a01, 1.0 + a11 - a00 - a22, a21 + a12, a20 - a02 );
            else
                q = double4( a20 + a02, a21 + a12, 1.0 + a22 - a00 - a11, a01 - a10 );

            q *= Rsqrt( Dot44(q, q) );

            return q;
        }

        PLATFORM_INLINE double3 GetScale() const
        {
            double3 scale = double3
            (
                v4d_get_x(v4d_length(col0)),
                v4d_get_x(v4d_length(col1)),
                v4d_get_x(v4d_length(col2))
            );

            return scale;
        }

        PLATFORM_INLINE void SetTranslation(const double3& p)
        {
            col3 = v4d_setw1(p.ymm);
        }

        PLATFORM_INLINE void AddTranslation(const double3& p)
        {
            col3 = _mm256_add_pd(col3, v4d_setw0(p.ymm));
        }

        PLATFORM_INLINE void PreTranslation(const double3& p);

        PLATFORM_INLINE void AddScale(const double3& scale)
        {
            col0 = _mm256_mul_pd(col0, scale.ymm);
            col1 = _mm256_mul_pd(col1, scale.ymm);
            col2 = _mm256_mul_pd(col2, scale.ymm);
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
                col2 = v4d_negate(col2);

            Transpose3x4();
        }

        PLATFORM_INLINE void ViewToWorld(uint32_t uiProjFlags = 0)
        {
            Transpose3x4();

            if( (uiProjFlags & PROJ_LEFT_HANDED) == 0 )
                col2 = v4d_negate(col2);

            Swap(col1, col2);
        }

        PLATFORM_INLINE bool IsLeftHanded() const
        {
            double3 v1 = Cross(col0, col1);

            return Dot33(v1, col2) < 0.0f;
        }

        // NOTE: arithmetic

        PLATFORM_INLINE double4x4 operator * (const double4x4& m) const
        {
            double4x4 r;

            v4d r1 = _mm256_mul_pd(v4d_swizzle(m.col0, 0, 0, 0, 0), col0);
            v4d r2 = _mm256_mul_pd(v4d_swizzle(m.col1, 0, 0, 0, 0), col0);

            r1 = v4d_madd(v4d_swizzle(m.col0, 1, 1, 1, 1), col1, r1);
            r2 = v4d_madd(v4d_swizzle(m.col1, 1, 1, 1, 1), col1, r2);
            r1 = v4d_madd(v4d_swizzle(m.col0, 2, 2, 2, 2), col2, r1);
            r2 = v4d_madd(v4d_swizzle(m.col1, 2, 2, 2, 2), col2, r2);
            r1 = v4d_madd(v4d_swizzle(m.col0, 3, 3, 3, 3), col3, r1);
            r2 = v4d_madd(v4d_swizzle(m.col1, 3, 3, 3, 3), col3, r2);

            r.col0 = r1;
            r.col1 = r2;

            r1 = _mm256_mul_pd(v4d_swizzle(m.col2, 0, 0, 0, 0), col0);
            r2 = _mm256_mul_pd(v4d_swizzle(m.col3, 0, 0, 0, 0), col0);

            r1 = v4d_madd(v4d_swizzle(m.col2, 1, 1, 1, 1), col1, r1);
            r2 = v4d_madd(v4d_swizzle(m.col3, 1, 1, 1, 1), col1, r2);
            r1 = v4d_madd(v4d_swizzle(m.col2, 2, 2, 2, 2), col2, r1);
            r2 = v4d_madd(v4d_swizzle(m.col3, 2, 2, 2, 2), col2, r2);
            r1 = v4d_madd(v4d_swizzle(m.col2, 3, 3, 3, 3), col3, r1);
            r2 = v4d_madd(v4d_swizzle(m.col3, 3, 3, 3, 3), col3, r2);

            r.col2 = r1;
            r.col3 = r2;

            return r;
        }

        PLATFORM_INLINE double4 operator * (const double4& v) const
        {
            v4d r = _mm256_mul_pd(v4d_swizzle(v.ymm, 0, 0, 0, 0), col0);
            r = v4d_madd(v4d_swizzle(v.ymm, 1, 1, 1, 1), col1, r);
            r = v4d_madd(v4d_swizzle(v.ymm, 2, 2, 2, 2), col2, r);
            r = v4d_madd(v4d_swizzle(v.ymm, 3, 3, 3, 3), col3, r);

            return r;
        }

        PLATFORM_INLINE double3 operator * (const double3& v) const
        {
            v4d r = v4d_madd(v4d_swizzle(v.ymm, 0, 0, 0, 0), col0, col3);
            r = v4d_madd(v4d_swizzle(v.ymm, 1, 1, 1, 1), col1, r);
            r = v4d_madd(v4d_swizzle(v.ymm, 2, 2, 2, 2), col2, r);

            return r;
        }

        PLATFORM_INLINE void TransposeTo(double4x4& m) const
        {
            v4d ymm0 = v4d_Ax_Bx_Az_Bz(col0, col1);
            v4d ymm1 = v4d_Ax_Bx_Az_Bz(col2, col3);
            v4d ymm2 = v4d_Ay_By_Aw_Bw(col0, col1);
            v4d ymm3 = v4d_Ay_By_Aw_Bw(col2, col3);

            m.col0 = v4d_Axy_Bxy(ymm0, ymm1);
            m.col1 = v4d_Axy_Bxy(ymm2, ymm3);
            m.col2 = v4d_Azw_Bzw(ymm1, ymm0);
            m.col3 = v4d_Azw_Bzw(ymm3, ymm2);
        }

        PLATFORM_INLINE void Transpose()
        {
            v4d ymm0 = v4d_Ax_Bx_Az_Bz(col0, col1);
            v4d ymm1 = v4d_Ax_Bx_Az_Bz(col2, col3);
            v4d ymm2 = v4d_Ay_By_Aw_Bw(col0, col1);
            v4d ymm3 = v4d_Ay_By_Aw_Bw(col2, col3);

            col0 = v4d_Axy_Bxy(ymm0, ymm1);
            col1 = v4d_Axy_Bxy(ymm2, ymm3);
            col2 = v4d_Azw_Bzw(ymm1, ymm0);
            col3 = v4d_Azw_Bzw(ymm3, ymm2);
        }

        PLATFORM_INLINE void Transpose3x4()
        {
            v4d ymm0 = v4d_Ax_Bx_Az_Bz(col0, col1);
            v4d ymm1 = v4d_Ax_Bx_Az_Bz(col2, col3);
            v4d ymm2 = v4d_Ay_By_Aw_Bw(col0, col1);
            v4d ymm3 = v4d_Ay_By_Aw_Bw(col2, col3);

            col0 = v4d_Axy_Bxy(ymm0, ymm1);
            col1 = v4d_Axy_Bxy(ymm2, ymm3);
            col2 = v4d_Azw_Bzw(ymm1, ymm0);
        }

        PLATFORM_INLINE void Invert()
        {
            // NOTE: http://forum.devmaster.net/t/sse-mat4-inverse/16799

            v4d Fac0;
            {
                v4d Swp0a = v4d_shuffle(col3, col2, 3, 3, 3, 3);
                v4d Swp0b = v4d_shuffle(col3, col2, 2, 2, 2, 2);

                v4d Swp00 = v4d_shuffle(col2, col1, 2, 2, 2, 2);
                v4d Swp01 = v4d_swizzle(Swp0a, 0, 0, 0, 2);
                v4d Swp02 = v4d_swizzle(Swp0b, 0, 0, 0, 2);
                v4d Swp03 = v4d_shuffle(col2, col1, 3, 3, 3, 3);

                v4d Mul00 = _mm256_mul_pd(Swp00, Swp01);

                Fac0 = v4d_nmadd(Swp02, Swp03, Mul00);
            }

            v4d Fac1;
            {
                v4d Swp0a = v4d_shuffle(col3, col2, 3, 3, 3, 3);
                v4d Swp0b = v4d_shuffle(col3, col2, 1, 1, 1, 1);

                v4d Swp00 = v4d_shuffle(col2, col1, 1, 1, 1, 1);
                v4d Swp01 = v4d_swizzle(Swp0a, 0, 0, 0, 2);
                v4d Swp02 = v4d_swizzle(Swp0b, 0, 0, 0, 2);
                v4d Swp03 = v4d_shuffle(col2, col1, 3, 3, 3, 3);

                v4d Mul00 = _mm256_mul_pd(Swp00, Swp01);

                Fac1 = v4d_nmadd(Swp02, Swp03, Mul00);
            }

            v4d Fac2;
            {
                v4d Swp0a = v4d_shuffle(col3, col2, 2, 2, 2, 2);
                v4d Swp0b = v4d_shuffle(col3, col2, 1, 1, 1, 1);

                v4d Swp00 = v4d_shuffle(col2, col1, 1, 1, 1, 1);
                v4d Swp01 = v4d_swizzle(Swp0a, 0, 0, 0, 2);
                v4d Swp02 = v4d_swizzle(Swp0b, 0, 0, 0, 2);
                v4d Swp03 = v4d_shuffle(col2, col1, 2, 2, 2, 2);

                v4d Mul00 = _mm256_mul_pd(Swp00, Swp01);

                Fac2 = v4d_nmadd(Swp02, Swp03, Mul00);
            }

            v4d Fac3;
            {
                v4d Swp0a = v4d_shuffle(col3, col2, 3, 3, 3, 3);
                v4d Swp0b = v4d_shuffle(col3, col2, 0, 0, 0, 0);

                v4d Swp00 = v4d_shuffle(col2, col1, 0, 0, 0, 0);
                v4d Swp01 = v4d_swizzle(Swp0a, 0, 0, 0, 2);
                v4d Swp02 = v4d_swizzle(Swp0b, 0, 0, 0, 2);
                v4d Swp03 = v4d_shuffle(col2, col1, 3, 3, 3, 3);

                v4d Mul00 = _mm256_mul_pd(Swp00, Swp01);

                Fac3 = v4d_nmadd(Swp02, Swp03, Mul00);
            }

            v4d Fac4;
            {
                v4d Swp0a = v4d_shuffle(col3, col2, 2, 2, 2, 2);
                v4d Swp0b = v4d_shuffle(col3, col2, 0, 0, 0, 0);

                v4d Swp00 = v4d_shuffle(col2, col1, 0, 0, 0, 0);
                v4d Swp01 = v4d_swizzle(Swp0a, 0, 0, 0, 2);
                v4d Swp02 = v4d_swizzle(Swp0b, 0, 0, 0, 2);
                v4d Swp03 = v4d_shuffle(col2, col1, 2, 2, 2, 2);

                v4d Mul00 = _mm256_mul_pd(Swp00, Swp01);

                Fac4 = v4d_nmadd(Swp02, Swp03, Mul00);
            }

            v4d Fac5;
            {
                v4d Swp0a = v4d_shuffle(col3, col2, 1, 1, 1, 1);
                v4d Swp0b = v4d_shuffle(col3, col2, 0, 0, 0, 0);

                v4d Swp00 = v4d_shuffle(col2, col1, 0, 0, 0, 0);
                v4d Swp01 = v4d_swizzle(Swp0a, 0, 0, 0, 2);
                v4d Swp02 = v4d_swizzle(Swp0b, 0, 0, 0, 2);
                v4d Swp03 = v4d_shuffle(col2, col1, 1, 1, 1, 1);

                v4d Mul00 = _mm256_mul_pd(Swp00, Swp01);

                Fac5 = v4d_nmadd(Swp02, Swp03, Mul00);
            }

            v4d SignA = _mm256_set_pd( 1.0f,-1.0f, 1.0f,-1.0f);
            v4d SignB = _mm256_set_pd(-1.0f, 1.0f,-1.0f, 1.0f);

            v4d Temp0 = v4d_shuffle(col1, col0, 0, 0, 0, 0);
            v4d Vec0 = v4d_swizzle(Temp0, 0, 2, 2, 2);

            v4d Temp1 = v4d_shuffle(col1, col0, 1, 1, 1, 1);
            v4d Vec1 = v4d_swizzle(Temp1, 0, 2, 2, 2);

            v4d Temp2 = v4d_shuffle(col1, col0, 2, 2, 2, 2);
            v4d Vec2 = v4d_swizzle(Temp2, 0, 2, 2, 2);

            v4d Temp3 = v4d_shuffle(col1, col0, 3, 3, 3, 3);
            v4d Vec3 = v4d_swizzle(Temp3, 0, 2, 2, 2);

            v4d Mul0 = _mm256_mul_pd(Vec1, Fac0);
            v4d Mul1 = _mm256_mul_pd(Vec0, Fac0);
            v4d Mul2 = _mm256_mul_pd(Vec0, Fac1);
            v4d Mul3 = _mm256_mul_pd(Vec0, Fac2);

            v4d Sub0 = v4d_nmadd(Vec2, Fac1, Mul0);
            v4d Sub1 = v4d_nmadd(Vec2, Fac3, Mul1);
            v4d Sub2 = v4d_nmadd(Vec1, Fac3, Mul2);
            v4d Sub3 = v4d_nmadd(Vec1, Fac4, Mul3);

            v4d Add0 = v4d_madd(Vec3, Fac2, Sub0);
            v4d Add1 = v4d_madd(Vec3, Fac4, Sub1);
            v4d Add2 = v4d_madd(Vec3, Fac5, Sub2);
            v4d Add3 = v4d_madd(Vec2, Fac5, Sub3);

            v4d Inv0 = _mm256_mul_pd(SignB, Add0);
            v4d Inv1 = _mm256_mul_pd(SignA, Add1);
            v4d Inv2 = _mm256_mul_pd(SignB, Add2);
            v4d Inv3 = _mm256_mul_pd(SignA, Add3);

            v4d Row0 = v4d_shuffle(Inv0, Inv1, 0, 0, 0, 0);
            v4d Row1 = v4d_shuffle(Inv2, Inv3, 0, 0, 0, 0);
            v4d Row2 = v4d_shuffle(Row0, Row1, 0, 2, 0, 2);

            v4d Det0 = v4d_dot44(col0, Row2);
            v4d Rcp0 = v4d_rcp(Det0);

            col0 = _mm256_mul_pd(Inv0, Rcp0);
            col1 = _mm256_mul_pd(Inv1, Rcp0);
            col2 = _mm256_mul_pd(Inv2, Rcp0);
            col3 = _mm256_mul_pd(Inv3, Rcp0);
        }

        PLATFORM_INLINE void InvertOrtho();

        // NOTE: special sets

        PLATFORM_INLINE void SetupByQuaternion(const double4& q)
        {
            double qxx = q.x * q.x;
            double qyy = q.y * q.y;
            double qzz = q.z * q.z;
            double qxz = q.x * q.z;
            double qxy = q.x * q.y;
            double qyz = q.y * q.z;
            double qwx = q.w * q.x;
            double qwy = q.w * q.y;
            double qwz = q.w * q.z;

            a00 = 1.0 - 2.0 * (qyy +  qzz);
            a01 = 2.0 * (qxy + qwz);
            a02 = 2.0 * (qxz - qwy);

            a10 = 2.0 * (qxy - qwz);
            a11 = 1.0 - 2.0 * (qxx +  qzz);
            a12 = 2.0 * (qyz + qwx);

            a20 = 2.0 * (qxz + qwy);
            a21 = 2.0 * (qyz - qwx);
            a22 = 1.0 - 2.0 * (qxx +  qyy);

            a30 = 0.0;
            a31 = 0.0;
            a32 = 0.0;

            col3 = c_v4d_0001;
        }

        PLATFORM_INLINE void SetupByRotationX(double angleX)
        {
            double ct = Cos(angleX);
            double st = Sin(angleX);

            SetCol0(1.0, 0.0, 0.0, 0.0);
            SetCol1(0.0, ct, st, 0.0);
            SetCol2(0.0, -st, ct, 0.0);

            col3 = c_v4d_0001;
        }

        PLATFORM_INLINE void SetupByRotationY(double angleY)
        {
            double ct = Cos(angleY);
            double st = Sin(angleY);

            SetCol0(ct, 0.0, -st, 0.0);
            SetCol1(0.0, 1.0, 0.0, 0.0);
            SetCol2(st, 0.0, ct, 0.0);

            col3 = c_v4d_0001;
        }

        PLATFORM_INLINE void SetupByRotationZ(double angleZ)
        {
            double ct = Cos(angleZ);
            double st = Sin(angleZ);

            SetCol0(ct, st, 0.0, 0.0);
            SetCol1(-st, ct, 0.0, 0.0);
            SetCol2(0.0, 0.0, 1.0, 0.0);

            col3 = c_v4d_0001;
        }

        PLATFORM_INLINE void SetupByRotationYPR(double fYaw, double fPitch, double fRoll)
        {
            // NOTE: "yaw-pitch-roll" rotation
            //       yaw - around Z (object "down-up" axis)
            //       pitch - around X (object "left-right" axis)
            //       roll - around Y (object "backward-forward" axis)

            /*
            double4x4 rot;
            rot.SetupByRotationY(fRoll);
            *this = rot;
            rot.SetupByRotationX(fPitch);
            *this = rot * (*this);
            rot.SetupByRotationZ(fYaw);
            *this = rot * (*this);
            */

            double4 angles(fYaw, fPitch, fRoll, 0.0);

            double4 c;
            double4 s = _mm256_sincos_pd(&c.ymm, angles.ymm);

            a00 = c.x * c.z - s.x * s.y * s.z;
            a10 = s.x * c.z + c.x * s.y * s.z;
            a20 = -c.y * s.z;
            a30 = 0.0;

            a01 = -s.x * c.y;
            a11 = c.x * c.y;
            a21 = s.y;
            a31 = 0.0;

            a02 = c.x * s.z + c.z * s.x * s.y;
            a12 = s.x * s.z - c.x * s.y * c.z;
            a22 = c.y * c.z;
            a32 = 0.0;

            col3 = c_v4d_0001;
        }

        PLATFORM_INLINE void SetupByRotation(double theta, const double3& v)
        {
            double ct = Cos(theta);
            double st = Sin(theta);

            SetupByRotation(st, ct, v);
        }

        PLATFORM_INLINE void SetupByRotation(double st, double ct, const double3& v)
        {
            double xx = v.x * v.x;
            double yy = v.y * v.y;
            double zz = v.z * v.z;
            double xy = v.x * v.y;
            double xz = v.x * v.z;
            double yz = v.y * v.z;
            double ctxy = ct * xy;
            double ctxz = ct * xz;
            double ctyz = ct * yz;
            double sty = st * v.y;
            double stx = st * v.x;
            double stz = st * v.z;

            a00 = xx + ct * (1.0 - xx);
            a01 = xy - ctxy - stz;
            a02 = xz - ctxz + sty;

            a10 = xy - ctxy + stz;
            a11 = yy + ct * (1.0 - yy);
            a12 = yz - ctyz - stx;

            a20 = xz - ctxz - sty;
            a21 = yz - ctyz + stx;
            a22 = zz + ct * (1.0 - zz);

            a30 = 0.0;
            a31 = 0.0;
            a32 = 0.0;

            col3 = c_v4d_0001;
        }

        PLATFORM_INLINE void SetupByRotation(const double3& z, const double3& d)
        {
            /*
            // NOTE: same as

            double3 axis = Cross(z, d);
            double angle = Acos( Dot33(z, d) );

            SetupByRotation(angle, axis);
            */

            double3 w = Cross(z, d);
            double c = Dot33(z, d);
            double k = (1.0 - c) / (1.0 - c * c);

            double hxy = w.x * w.y * k;
            double hxz = w.x * w.z * k;
            double hyz = w.y * w.z * k;

            a00 = c + w.x * w.x * k;
            a01 = hxy - w.z;
            a02 = hxz + w.y;

            a10 = hxy + w.z;
            a11 = c + w.y * w.y * k;
            a12 = hyz - w.x;

            a20 = hxz - w.y;
            a21 = hyz + w.x;
            a22 = c + w.z * w.z * k;

            a30 = 0.0;
            a31 = 0.0;
            a32 = 0.0;

            col3 = c_v4d_0001;
        }

        PLATFORM_INLINE void SetupByTranslation(const double3& p)
        {
            SetCol0(1.0, 0.0, 0.0, 0.0);
            SetCol1(0.0, 1.0, 0.0, 0.0);
            SetCol2(0.0, 0.0, 1.0, 0.0);
            SetCol3_1(p);
        }

        PLATFORM_INLINE void SetupByScale(const double3& scale)
        {
            SetCol0(scale.x, 0.0, 0.0, 0.0);
            SetCol1(0.0, scale.y, 0.0, 0.0);
            SetCol2(0.0, 0.0, scale.z, 0.0);

            col3 = c_v4d_0001;
        }

        PLATFORM_INLINE void SetupByLookAt(const double3& vForward)
        {
            double3 y = Normalize(vForward);
            double3 z = GetPerpendicularVector(y);
            double3 x = Cross(y, z);

            SetCol0_0(x);
            SetCol1_0(y);
            SetCol2_0(z);

            col3 = c_v4d_0001;
        }

        PLATFORM_INLINE void SetupByLookAt(const double3& vForward, const double3& vRight)
        {
            double3 y = Normalize(vForward);
            double3 z = Normalize( Cross(vRight, y) );
            double3 x = Cross(y, z);

            SetCol0_0(x);
            SetCol1_0(y);
            SetCol2_0(z);

            col3 = c_v4d_0001;
        }

        PLATFORM_INLINE void SetupByOrthoProjection(double left, double right, double bottom, double top, double zNear, double zFar, uint32_t uiProjFlags = 0)
        {
            DEBUG_Assert( left < right );
            DEBUG_Assert( bottom < top );

            double rWidth = 1.0 / (right - left);
            double rHeight = 1.0 / (top - bottom);
            double rDepth = 1.0 / (zFar - zNear);

            a00 = 2.0 * rWidth;
            a01 = 0.0;
            a02 = 0.0;
            a03 = -(right + left) * rWidth;

            a10 = 0.0;
            a11 = 2.0 * rHeight;
            a12 = 0.0;
            a13 = -(top + bottom) * rHeight;

            a20 = 0.0;
            a21 = 0.0;
            a22 = -2.0 * rDepth;
            a23 = -(zFar + zNear) * rDepth;

            a30 = 0.0;
            a31 = 0.0;
            a32 = 0.0;
            a33 = 1.0;

            bool bReverseZ = (uiProjFlags & PROJ_REVERSED_Z) != 0;

            a22 = Zbuffer::ModifyProjZ(bReverseZ, a22, a32);
            a23 = Zbuffer::ModifyProjZ(bReverseZ, a23, a33);

            if( uiProjFlags & PROJ_LEFT_HANDED )
                col2 = v4d_negate(col2);
        }

        PLATFORM_INLINE void SetupByFrustum(double left, double right, double bottom, double top, double zNear, double zFar, uint32_t uiProjFlags = 0)
        {
            DEBUG_Assert( left < right );
            DEBUG_Assert( bottom < top );

            double rWidth = 1.0 / (right - left);
            double rHeight = 1.0 / (top - bottom);
            double rDepth = 1.0 / (zNear - zFar);

            a00 = 2.0 * zNear * rWidth;
            a01 = 0.0;
            a02 = (right + left) * rWidth;
            a03 = 0.0;

            a10 = 0.0;
            a11 = 2.0 * zNear * rHeight;
            a12 = (top + bottom) * rHeight;
            a13 = 0.0;

            a20 = 0.0;
            a21 = 0.0;
            a22 = (zFar + zNear) * rDepth;
            a23 = 2.0 * zFar * zNear * rDepth;

            a30 = 0.0;
            a31 = 0.0;
            a32 = -1.0;
            a33 = 0.0;

            bool bReverseZ = (uiProjFlags & PROJ_REVERSED_Z) != 0;

            a22 = Zbuffer::ModifyProjZ(bReverseZ, a22, a32);
            a23 = Zbuffer::ModifyProjZ(bReverseZ, a23, a33);

            if( uiProjFlags & PROJ_LEFT_HANDED )
                col2 = v4d_negate(col2);
        }

        PLATFORM_INLINE void SetupByFrustumInf(double left, double right, double bottom, double top, double zNear, uint32_t uiProjFlags = 0)
        {
            DEBUG_Assert( left < right );
            DEBUG_Assert( bottom < top );

            double rWidth = 1.0 / (right - left);
            double rHeight = 1.0 / (top - bottom);

            a00 = 2.0 * zNear * rWidth;
            a01 = 0.0;
            a02 = (right + left) * rWidth;
            a03 = 0.0;

            a10 = 0.0;
            a11 = 2.0 * zNear * rHeight;
            a12 = (top + bottom) * rHeight;
            a13 = 0.0;

            a20 = 0.0;
            a21 = 0.0;
            a22 = -1.0;
            a23 = -2.0 * zNear;

            a30 = 0.0;
            a31 = 0.0;
            a32 = -1.0;
            a33 = 0.0;

            bool bReverseZ = (uiProjFlags & PROJ_REVERSED_Z) != 0;

            a22 = Zbuffer::ModifyProjZ(bReverseZ, a22, a32);
            a23 = Zbuffer::ModifyProjZ(bReverseZ, a23, a33);

            if( uiProjFlags & PROJ_LEFT_HANDED )
                col2 = v4d_negate(col2);
        }

        PLATFORM_INLINE void SetupByHalfFovy(double halfFovy, double aspect, double zNear, double zFar, uint32_t uiProjFlags = 0)
        {
            double ymax = zNear * Tan(halfFovy);
            double xmax = ymax * aspect;

            SetupByFrustum(-xmax, xmax, -ymax, ymax, zNear, zFar, uiProjFlags);
        }

        PLATFORM_INLINE void SetupByHalfFovyInf(double halfFovy, double aspect, double zNear, uint32_t uiProjFlags = 0)
        {
            double ymax = zNear * Tan(halfFovy);
            double xmax = ymax * aspect;

            SetupByFrustumInf(-xmax, xmax, -ymax, ymax, zNear, uiProjFlags);
        }

        PLATFORM_INLINE void SetupByHalfFovx(double halfFovx, double aspect, double zNear, double zFar, uint32_t uiProjFlags = 0)
        {
            double xmax = zNear * Tan(halfFovx);
            double ymax = xmax / aspect;

            SetupByFrustum(-xmax, xmax, -ymax, ymax, zNear, zFar, uiProjFlags);
        }

        PLATFORM_INLINE void SetupByHalfFovxInf(double halfFovx, double aspect, double zNear, uint32_t uiProjFlags = 0)
        {
            double xmax = zNear * Tan(halfFovx);
            double ymax = xmax / aspect;

            SetupByFrustumInf(-xmax, xmax, -ymax, ymax, zNear, uiProjFlags);
        }

        PLATFORM_INLINE void SetupByAngles(double angleMinx, double angleMaxx, double angleMiny, double angleMaxy, double zNear, double zFar, uint32_t uiProjFlags = 0)
        {
            double xmin = Tan(angleMinx) * zNear;
            double xmax = Tan(angleMaxx) * zNear;
            double ymin = Tan(angleMiny) * zNear;
            double ymax = Tan(angleMaxy) * zNear;

            SetupByFrustum(xmin, xmax, ymin, ymax, zNear, zFar, uiProjFlags);
        }

        PLATFORM_INLINE void SetupByAnglesInf(double angleMinx, double angleMaxx, double angleMiny, double angleMaxy, double zNear, uint32_t uiProjFlags = 0)
        {
            double xmin = Tan(angleMinx) * zNear;
            double xmax = Tan(angleMaxx) * zNear;
            double ymin = Tan(angleMiny) * zNear;
            double ymax = Tan(angleMaxy) * zNear;

            SetupByFrustumInf(xmin, xmax, ymin, ymax, zNear, uiProjFlags);
        }

        PLATFORM_INLINE void SubsampleProjection(double dx, double dy, uint32_t viewportWidth, uint32_t viewportHeight)
        {
            // NOTE: dx/dy in range [-1; 1]

            a02 += dx / double(viewportWidth);
            a12 += dy / double(viewportHeight);
        }

        PLATFORM_INLINE bool IsProjectionValid() const
        {
            // Do not check a20 and a21 to allow off-centered projections
            // Do not check a22 to allow reverse infinite projections

            return
            (
                (a00 != 0.0 && a10 == 0.0 && a20 == 0.0 && a30 == 0.0)
                && (a01 == 0.0 && a11 != 0.0 && a21 == 0.0 && a31 == 0.0)
                && (a32 == 1.0 || a32 == -1.0)
                && (a03 == 0.0 && a13 == 0.0 && a23 != 0.0 && a33 == 0.0)
            );
        }
};

//======================================================================================================================
//                                                      cBoxd
//======================================================================================================================

class cBoxd
{
    public:

        double3 vMin;
        double3 vMax;

    public:

        PLATFORM_INLINE cBoxd()
        {
            Clear();
        }

        PLATFORM_INLINE cBoxd(const double3& v)
        {
            vMin = v;
            vMax = v;
        }

        PLATFORM_INLINE cBoxd(const double3& minv, const double3& maxv)
        {
            vMin = minv;
            vMax = maxv;
        }

        PLATFORM_INLINE void Clear()
        {
            vMin = double3(c_v4d_Inf);
            vMax = double3(c_v4d_InfMinus);
        }

        PLATFORM_INLINE bool IsValid() const
        {
            v4d r = v4d_less(vMin.ymm, vMax.ymm);

            return v4d_test3_all(r);
        }

        PLATFORM_INLINE double3 GetCenter() const
        {
            return (vMin + vMax) * 0.5;
        }

        PLATFORM_INLINE double GetRadius() const
        {
            return Length(vMax - vMin) * 0.5;
        }

        PLATFORM_INLINE void Scale(double fScale)
        {
            fScale *= 0.5;

            double k1 = 0.5 + fScale;
            double k2 = 0.5 - fScale;

            double3 a = vMin * k1 + vMax * k2;
            double3 b = vMax * k1 + vMin * k2;

            vMin = a;
            vMax = b;
        }

        PLATFORM_INLINE void Add(const double3& v)
        {
            vMin = _mm256_min_pd(vMin.ymm, v.ymm);
            vMax = _mm256_max_pd(vMax.ymm, v.ymm);
        }

        PLATFORM_INLINE void Add(const cBoxd& b)
        {
            vMin = _mm256_min_pd(vMin.ymm, b.vMin.ymm);
            vMax = _mm256_max_pd(vMax.ymm, b.vMax.ymm);
        }

        PLATFORM_INLINE double DistanceSquared(const double3& from) const
        {
            v4d p = v4d_clamp(from.ymm, vMin.ymm, vMax.ymm);
            p = _mm256_sub_pd(p, from.ymm);
            p = v4d_dot33(p, p);

            return v4d_get_x(p);
        }

        PLATFORM_INLINE double Distance(const double3& from) const
        {
            v4d p = v4d_clamp(from.ymm, vMin.ymm, vMax.ymm);
            p = _mm256_sub_pd(p, from.ymm);
            p = v4d_length(p);

            return v4d_get_x(p);
        }

        PLATFORM_INLINE bool IsIntersectWith(const cBoxd& b) const
        {
            v4d r = v4d_less(vMax.ymm, b.vMin.ymm);
            r = _mm256_or_pd(r, v4d_greater(vMin.ymm, b.vMax.ymm));

            return v4d_test3_none(r);
        }

        // NOTE: intersection state 'b' vs 'this'

        PLATFORM_INLINE eClip GetIntersectionState(const cBoxd& b) const
        {
            if( !IsIntersectWith(b) )
                return CLIP_OUT;

            v4d r = v4d_less(vMin.ymm, b.vMin.ymm);
            r = _mm256_and_pd(r, v4d_greater(vMax.ymm, b.vMax.ymm));

            return v4d_test3_all(r) ? CLIP_IN : CLIP_PARTIAL;
        }

        PLATFORM_INLINE bool IsContain(const double3& p) const
        {
            v4d r = v4d_less(p.ymm, vMin.ymm);
            r = _mm256_or_pd(r, v4d_greater(p.ymm, vMax.ymm));

            return v4d_test3_none(r);
        }

        PLATFORM_INLINE bool IsContainSphere(const double3& center, double radius) const
        {
            v4d r = _mm256_broadcast_sd(&radius);
            v4d t = _mm256_sub_pd(vMin.ymm, r);
            t = v4d_less(center.ymm, t);

            if( v4d_test3_any(t) )
                return false;

            t = _mm256_add_pd(vMax.ymm, r);
            t = v4d_greater(center.ymm, t);

            if( v4d_test3_any(t) )
                return false;

            return true;
        }

        PLATFORM_INLINE uint32_t GetIntersectionBits(const cBoxd& b) const
        {
            v4d r = v4d_gequal(b.vMin.ymm, vMin.ymm);
            uint32_t bits = v4d_bits3(r);

            r = v4d_lequal(b.vMax.ymm, vMax.ymm);
            bits |= v4d_bits3(r) << 3;

            return bits;
        }

        PLATFORM_INLINE uint32_t IsContain(const double3& p, uint32_t bits) const
        {
            v4d r = v4d_gequal(p.ymm, vMin.ymm);
            bits |= v4d_bits3(r);

            r = v4d_lequal(p.ymm, vMax.ymm);
            bits |= v4d_bits3(r) << 3;

            return bits;
        }

        PLATFORM_INLINE bool IsIntersectWith(const double3& vRayPos, const double3& vRayDir, double* out_fTmin, double* out_fTmax) const
        {
            // NOTE: http://tavianator.com/2011/05/fast-branchless-raybounding-box-intersections/

            // IMPORTANT: store '1 / ray_dir' and filter INFs out!

            v4d t1 = _mm256_div_pd(_mm256_sub_pd(vMin.ymm, vRayPos.ymm), vRayDir.ymm);
            v4d t2 = _mm256_div_pd(_mm256_sub_pd(vMax.ymm, vRayPos.ymm), vRayDir.ymm);

            v4d vmin = _mm256_min_pd(t1, t2);
            v4d vmax = _mm256_max_pd(t1, t2);

            // NOTE: hmax.xxx
            v4d tmin = _mm256_max_pd(vmin, v4d_swizzle(vmin, COORD_Y, COORD_Z, COORD_X, 0));
            tmin = _mm256_max_pd(tmin, v4d_swizzle(vmin, COORD_Z, COORD_X, COORD_Y, 0));

            // NOTE: hmin.xxx
            v4d tmax = _mm256_min_pd(vmax, v4d_swizzle(vmax, COORD_Y, COORD_Z, COORD_X, 0));
            tmax = _mm256_min_pd(tmax, v4d_swizzle(vmax, COORD_Z, COORD_X, COORD_Y, 0));

            v4d_store_x(out_fTmin, tmin);
            v4d_store_x(out_fTmax, tmax);

            v4d cmp = v4d_gequal(tmax, tmin);

            return (v4d_bits4(cmp) & v4d_mask(1, 0, 0, 0)) == v4d_mask(1, 0, 0, 0);
        }
};

//======================================================================================================================
//                                                      Misc
//======================================================================================================================

PLATFORM_INLINE double3 Reflect(const double3& v, const double3& n)
{
    // NOTE: slow
    // return v - n * Dot33(n, v) * 2;

    v4d dot0 = v4d_dot33(n.ymm, v.ymm);
    dot0 = _mm256_mul_pd(dot0, _mm256_set1_pd(2.0));

    return v4d_nmadd(n.ymm, dot0, v.ymm);
}

PLATFORM_INLINE double3 Refract(const double3& v, const double3& n, double eta)
{
    // NOTE: slow
    /*
    float dot = Dot33(v, n);
    float k = 1 - eta * eta * (1 - dot * dot);

    if( k < 0 )
    return 0

    return v * eta - n * (eta * dot + Sqrt(k));
    */

    v4d eta0 = _mm256_broadcast_sd(&eta);
    v4d dot0 = v4d_dot33(n.ymm, v.ymm);
    v4d mul0 = _mm256_mul_pd(eta0, eta0);
    v4d sub0 = v4d_nmadd(dot0, dot0, c_v4d_1111);
    v4d sub1 = v4d_nmadd(mul0, sub0, c_v4d_1111);

    if( v4d_isnegative4_all(sub1) )
        return v4d_zero;

    v4d mul5 = _mm256_mul_pd(eta0, v.ymm);
    v4d mul3 = _mm256_mul_pd(eta0, dot0);
    v4d sqt0 = v4d_sqrt(sub1);
    v4d add0 = _mm256_add_pd(mul3, sqt0);

    return v4d_nmadd(add0, n.ymm, mul5);
}

PLATFORM_INLINE bool IsPointsNear(const double3& p1, const double3& p2, double eps = c_dEps)
{
    v4d r = _mm256_sub_pd(p1.ymm, p2.ymm);
    r = v4d_abs(r);
    r = v4d_lequal(r, _mm256_broadcast_sd(&eps));

    return v4d_test3_all(r);
}

PLATFORM_INLINE double3 Rotate(const double4x4& m, const double3& v)
{
    v4d r = _mm256_mul_pd(v4d_swizzle(v.ymm, 0, 0, 0, 0), m.col0);
    r = v4d_madd(v4d_swizzle(v.ymm, 1, 1, 1, 1), m.col1, r);
    r = v4d_madd(v4d_swizzle(v.ymm, 2, 2, 2, 2), m.col2, r);
    r = v4d_setw0(r);

    return r;
}

PLATFORM_INLINE void double4x4::PreTranslation(const double3& p)
{
    v4d r = Rotate(*this, p.ymm).ymm;
    col3 = _mm256_add_pd(col3, r);
}

PLATFORM_INLINE void double4x4::InvertOrtho()
{
    Transpose3x4();

    col3 = Rotate(*this, col3).ymm;
    col3 = v4d_negate(col3);

    col0 = v4d_setw0(col0);
    col1 = v4d_setw0(col1);
    col2 = v4d_setw0(col2);
    col3 = v4d_setw1(col3);
}

PLATFORM_INLINE double3 RotateAbs(const double4x4& m, const double3& v)
{
    v4d col0_abs = v4d_abs(m.col0);
    v4d col1_abs = v4d_abs(m.col1);
    v4d col2_abs = v4d_abs(m.col2);

    v4d r = _mm256_mul_pd(v4d_swizzle(v.ymm, 0, 0, 0, 0), col0_abs);
    r = v4d_madd(v4d_swizzle(v.ymm, 1, 1, 1, 1), col1_abs, r);
    r = v4d_madd(v4d_swizzle(v.ymm, 2, 2, 2, 2), col2_abs, r);

    return r;
}

PLATFORM_INLINE void TransformAabb(const double4x4& mTransform, const cBoxd& src, cBoxd& dst)
{
    double3 center = (src.vMin + src.vMax) * 0.5;
    double3 extends = src.vMax - center;

    center = mTransform * center;
    extends = RotateAbs(mTransform, extends);

    dst.vMin = center - extends;
    dst.vMax = center + extends;
}

PLATFORM_INLINE double3 Project(const double3& v, const double4x4& m)
{
    double4 clip = (m * v).ymm;
    clip /= clip.w;

    return clip.To3d();
}
