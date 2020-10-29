/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef STL_H
#define STL_H

#define STL_VERSION_MAJOR 1
#define STL_VERSION_MINOR 2

// Settings
#define STL_SIGN_DEFAULT                            STL_SIGN_FAST
#define STL_SQRT_DEFAULT                            STL_SQRT_SAFE
#define STL_RSQRT_DEFAULT                           STL_POSITIVE_RSQRT_ACCURATE_SAFE
#define STL_POSITIVE_RCP_DEFAULT                    STL_POSITIVE_RCP_ACCURATE_SAFE
#define STL_LUMINANCE_DEFAULT                       STL_LUMINANCE_BT601
#define STL_RNG_DEFAULT                             STL_RNG_MANTISSA_BITS
#define STL_BAYER_DEFAULT                           STL_BAYER_REVERSEBITS
#define STL_RF0_DIELECTRICS                         0.04
#define STL_GTR_GAMMA                               1.5
#define STL_SPECULAR_DOMINANT_DIRECTION_DEFAULT     STL_SPECULAR_DOMINANT_DIRECTION_APPROX

#define compiletime

namespace STL
{
    static const float FLT_MIN = 1e-15;

    //=======================================================================================================================
    // MATH
    //=======================================================================================================================

    namespace Math
    {
        // Pi
        #define _Pi( x ) radians( 180.0 * x )

        float Pi( float x )
        { return _Pi( x ); }

        float2 Pi( float2 x )
        { return _Pi( x ); }

        float3 Pi( float3 x )
        { return _Pi( x ); }

        float4 Pi( float4 x )
        { return _Pi( x ); }

        // Radians to degrees
        #define _RadToDeg( x ) ( x * 180.0 / Pi( 1.0 ) )

        float RadToDeg( float x )
        { return _RadToDeg( x ); }

        float2 RadToDeg( float2 x )
        { return _RadToDeg( x ); }

        float3 RadToDeg( float3 x )
        { return _RadToDeg( x ); }

        float4 RadToDeg( float4 x )
        { return _RadToDeg( x ); }

        // Degrees to radians
        #define _DegToRad( x ) ( x * Pi( 1.0 ) / 180.0 )

        float DegToRad( float x )
        { return _DegToRad( x ); }

        float2 DegToRad( float2 x )
        { return _DegToRad( x ); }

        float3 DegToRad( float3 x )
        { return _DegToRad( x ); }

        float4 DegToRad( float4 x )
        { return _DegToRad( x ); }

        // Swap two values
        #define _Swap( x, y ) x ^= y; y ^= x; x ^= y

        void Swap( inout uint x, inout uint y )
        { _Swap( x, y ); }

        void Swap( inout uint2 x, inout uint2 y )
        { _Swap( x, y ); }

        void Swap( inout uint3 x, inout uint3 y )
        { _Swap( x, y ); }

        void Swap( inout uint4 x, inout uint4 y )
        { _Swap( x, y ); }

        void Swap( inout float x, inout float y )
        { float t = x; x = y; y = t; }

        void Swap( inout float2 x, inout float2 y )
        { float2 t = x; x = y; y = t; }

        void Swap( inout float3 x, inout float3 y )
        { float3 t = x; x = y; y = t; }

        void Swap( inout float4 x, inout float4 y )
        { float4 t = x; x = y; y = t; }

        // LinearStep
        // REQUIREMENT: a < b
        #define _LinearStep( a, b, x ) saturate( ( x - a ) / ( b - a ) )

        float LinearStep( float a, float b, float x )
        { return _LinearStep( a, b, x ); }

        float2 LinearStep( float2 a, float2 b, float2 x )
        { return _LinearStep( a, b, x ); }

        float3 LinearStep( float3 a, float3 b, float3 x )
        { return _LinearStep( a, b, x ); }

        float4 LinearStep( float4 a, float4 b, float4 x )
        { return _LinearStep( a, b, x ); }

        // SmoothStep
        // REQUIREMENT: a < b
        #define _SmoothStep01( x ) ( x * x * ( 3.0 - 2.0 * x ) )

        float SmoothStep( float a, float b, float x )
        { x = _LinearStep( a, b, x ); return _SmoothStep01( x ); }

        float2 SmoothStep( float2 a, float2 b, float2 x )
        { x = _LinearStep( a, b, x ); return _SmoothStep01( x ); }

        float3 SmoothStep( float3 a, float3 b, float3 x )
        { x = _LinearStep( a, b, x ); return _SmoothStep01( x ); }

        float4 SmoothStep( float4 a, float4 b, float4 x )
        { x = _LinearStep( a, b, x ); return _SmoothStep01( x ); }

        // SmootherStep
        // https://en.wikipedia.org/wiki/Smoothstep
        // REQUIREMENT: a < b
        #define _SmootherStep01( x ) ( x * x * x * ( x * ( x * 6.0 - 15.0 ) + 10.0 ) )

        float SmootherStep( float a, float b, float x )
        { x = _LinearStep( a, b, x ); return _SmootherStep01( x ); }

        float2 SmootherStep( float2 a, float2 b, float2 x )
        { x = _LinearStep( a, b, x ); return _SmootherStep01( x ); }

        float3 SmootherStep( float3 a, float3 b, float3 x )
        { x = _LinearStep( a, b, x ); return _SmootherStep01( x ); }

        float4 SmootherStep( float4 a, float4 b, float4 x )
        { x = _LinearStep( a, b, x ); return _SmootherStep01( x ); }

        // Sign
        #define STL_SIGN_BUILTIN 0
        #define STL_SIGN_FAST 1
        #define _Sign( x ) ( step( 0.0, x ) * 2.0 - 1.0 )

        float Sign( float x, compiletime const uint mode = STL_SIGN_DEFAULT )
        { return mode == STL_SIGN_FAST ? _Sign( x ) : sign( x ); }

        float2 Sign( float2 x, compiletime const uint mode = STL_SIGN_DEFAULT )
        { return mode == STL_SIGN_FAST ? _Sign( x ) : sign( x ); }

        float3 Sign( float3 x, compiletime const uint mode = STL_SIGN_DEFAULT )
        { return mode == STL_SIGN_FAST ? _Sign( x ) : sign( x ); }

        float4 Sign( float4 x, compiletime const uint mode = STL_SIGN_DEFAULT )
        { return mode == STL_SIGN_FAST ? _Sign( x ) : sign( x ); }

        // Pow
        float Pow( float x, float y )
        { return pow( abs( x ), y ); }

        float2 Pow( float2 x, float y )
        { return pow( abs( x ), y ); }

        float2 Pow( float2 x, float2 y )
        { return pow( abs( x ), y ); }

        float3 Pow( float3 x, float y )
        { return pow( abs( x ), y ); }

        float3 Pow( float3 x, float3 y )
        { return pow( abs( x ), y ); }

        float4 Pow( float4 x, float y )
        { return pow( abs( x ), y ); }

        float4 Pow( float4 x, float4 y )
        { return pow( abs( x ), y ); }

        // Pow for values in range [0; 1]
        float Pow01( float x, float y )
        { return pow( saturate( x ), y ); }

        float2 Pow01( float2 x, float y )
        { return pow( saturate( x ), y ); }

        float2 Pow01( float2 x, float2 y )
        { return pow( saturate( x ), y ); }

        float3 Pow01( float3 x, float y )
        { return pow( saturate( x ), y ); }

        float3 Pow01( float3 x, float3 y )
        { return pow( saturate( x ), y ); }

        float4 Pow01( float4 x, float y )
        { return pow( saturate( x ), y ); }

        float4 Pow01( float4 x, float4 y )
        { return pow( saturate( x ), y ); }

        // Sqrt
        #define STL_SQRT_BUILTIN 0
        #define STL_SQRT_SAFE 1

        float Sqrt( float x, compiletime const uint mode = STL_SQRT_DEFAULT )
        { return sqrt( mode == STL_SQRT_SAFE ? max( x, 0 ) : x ); }

        float2 Sqrt( float2 x, compiletime const uint mode = STL_SQRT_DEFAULT )
        { return sqrt( mode == STL_SQRT_SAFE ? max( x, 0 ) : x ); }

        float3 Sqrt( float3 x, compiletime const uint mode = STL_SQRT_DEFAULT )
        { return sqrt( mode == STL_SQRT_SAFE ? max( x, 0 ) : x ); }

        float4 Sqrt( float4 x, compiletime const uint mode = STL_SQRT_DEFAULT )
        { return sqrt( mode == STL_SQRT_SAFE ? max( x, 0 ) : x ); }

        // Sqrt for values in range [0; 1]
        float Sqrt01( float x )
        { return sqrt( saturate( x ) ); }

        float2 Sqrt01( float2 x )
        { return sqrt( saturate( x ) ); }

        float3 Sqrt01( float3 x )
        { return sqrt( saturate( x ) ); }

        float4 Sqrt01( float4 x )
        { return sqrt( saturate( x ) ); }

        // 1 / Sqrt
        #define STL_POSITIVE_RSQRT_BUILTIN 0
        #define STL_POSITIVE_RSQRT_BUILTIN_SAFE 1
        #define STL_POSITIVE_RSQRT_ACCURATE 2
        #define STL_POSITIVE_RSQRT_ACCURATE_SAFE 3

        float Rsqrt( float x, compiletime const uint mode = STL_RSQRT_DEFAULT )
        {
            if ( mode <= STL_POSITIVE_RSQRT_BUILTIN_SAFE )
                return rsqrt( mode == STL_POSITIVE_RSQRT_BUILTIN ? x : max( x, FLT_MIN ) );

            return 1.0 / sqrt( mode == STL_POSITIVE_RSQRT_ACCURATE ? x : max( x, FLT_MIN ) );
        }

        float2 Rsqrt( float2 x, compiletime const uint mode = STL_RSQRT_DEFAULT )
        {
            if ( mode <= STL_POSITIVE_RSQRT_BUILTIN_SAFE )
                return rsqrt( mode == STL_POSITIVE_RSQRT_BUILTIN ? x : max( x, FLT_MIN ) );

            return 1.0 / sqrt( mode == STL_POSITIVE_RSQRT_ACCURATE ? x : max( x, FLT_MIN ) );
        }

        float3 Rsqrt( float3 x, compiletime const uint mode = STL_RSQRT_DEFAULT )
        {
            if ( mode <= STL_POSITIVE_RSQRT_BUILTIN_SAFE )
                return rsqrt( mode == STL_POSITIVE_RSQRT_BUILTIN ? x : max( x, FLT_MIN ) );

            return 1.0 / sqrt( mode == STL_POSITIVE_RSQRT_ACCURATE ? x : max( x, FLT_MIN ) );
        }

        float4 Rsqrt( float4 x, compiletime const uint mode = STL_RSQRT_DEFAULT )
        {
            if ( mode <= STL_POSITIVE_RSQRT_BUILTIN_SAFE )
                return rsqrt( mode == STL_POSITIVE_RSQRT_BUILTIN ? x : max( x, FLT_MIN ) );

            return 1.0 / sqrt( mode == STL_POSITIVE_RSQRT_ACCURATE ? x : max( x, FLT_MIN ) );
        }

        // Acos(x) (approximate)
        // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiJhY29zKHgpIiwiY29sb3IiOiIjMDAwMDAwIn0seyJ0eXBlIjowLCJlcSI6InNxcnQoMS14KSpzcXJ0KDIpIiwiY29sb3IiOiIjRjIwQzBDIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiMiJdLCJzaXplIjpbMTE1MCw5MDBdfV0-
        #define _AcosApprox( x ) ( sqrt( 2.0 ) * sqrt( saturate( 1.0 - x ) ) )

        float AcosApprox( float x )
        { return _AcosApprox( x ); }

        float2 AcosApprox( float2 x )
        { return _AcosApprox( x ); }

        float3 AcosApprox( float3 x )
        { return _AcosApprox( x ); }

        float4 AcosApprox( float4 x )
        { return _AcosApprox( x ); }

        // 1 / positive
        #define STL_POSITIVE_RCP_BUILTIN 0
        #define STL_POSITIVE_RCP_BUILTIN_SAFE 1
        #define STL_POSITIVE_RCP_ACCURATE 2
        #define STL_POSITIVE_RCP_ACCURATE_SAFE 3

        float PositiveRcp( float x, compiletime const uint mode = STL_POSITIVE_RCP_DEFAULT )
        {
            if ( mode <= STL_POSITIVE_RCP_BUILTIN_SAFE )
                return rcp( mode == STL_POSITIVE_RCP_BUILTIN ? x : max( x, FLT_MIN ) );

            return 1.0 / ( mode == STL_POSITIVE_RCP_ACCURATE ? x : max( x, FLT_MIN ) );
        }

        float2 PositiveRcp( float2 x, compiletime const uint mode = STL_POSITIVE_RCP_DEFAULT )
        {
            if ( mode <= STL_POSITIVE_RCP_BUILTIN_SAFE )
                return rcp( mode == STL_POSITIVE_RCP_BUILTIN ? x : max( x, FLT_MIN ) );

            return 1.0 / ( mode == STL_POSITIVE_RCP_ACCURATE ? x : max( x, FLT_MIN ) );
        }

        float3 PositiveRcp( float3 x, compiletime const uint mode = STL_POSITIVE_RCP_DEFAULT )
        {
            if ( mode <= STL_POSITIVE_RCP_BUILTIN_SAFE )
                return rcp( mode == STL_POSITIVE_RCP_BUILTIN ? x : max( x, FLT_MIN ) );

            return 1.0 / ( mode == STL_POSITIVE_RCP_ACCURATE ? x : max( x, FLT_MIN ) );
        }

        float4 PositiveRcp( float4 x, compiletime const uint mode = STL_POSITIVE_RCP_DEFAULT )
        {
            if ( mode <= STL_POSITIVE_RCP_BUILTIN_SAFE )
                return rcp( mode == STL_POSITIVE_RCP_BUILTIN ? x : max( x, FLT_MIN ) );

            return 1.0 / ( mode == STL_POSITIVE_RCP_ACCURATE ? x : max( x, FLT_MIN ) );
        }

        // LengthSquared
        float LengthSquared( float2 v )
        { return dot( v, v ); }

        float LengthSquared( float3 v )
        { return dot( v, v ); }

        float LengthSquared( float4 v )
        { return dot( v, v ); }

        // Bit operations
        uint ReverseBits4( uint x )
        {
            x = ( ( x & 0x5 ) << 1 ) | ( ( x & 0xA ) >> 1 );
            x = ( ( x & 0x3 ) << 2 ) | ( ( x & 0xC ) >> 2 );

            return x;
        }

        uint ReverseBits8( uint x )
        {
            x = ( ( x & 0x55 ) << 1 ) | ( ( x & 0xAA ) >> 1 );
            x = ( ( x & 0x33 ) << 2 ) | ( ( x & 0xCC ) >> 2 );
            x = ( ( x & 0x0F ) << 4 ) | ( ( x & 0xF0 ) >> 4 );

            return x;
        }

        uint ReverseBits16( uint x )
        {
            x = ( ( x & 0x5555 ) << 1 ) | ( ( x & 0xAAAA ) >> 1 );
            x = ( ( x & 0x3333 ) << 2 ) | ( ( x & 0xCCCC ) >> 2 );
            x = ( ( x & 0x0F0F ) << 4 ) | ( ( x & 0xF0F0 ) >> 4 );
            x = ( ( x & 0x00FF ) << 8 ) | ( ( x & 0xFF00 ) >> 8 );

            return x;
        }

        uint ReverseBits32( uint x )
        {
            x = ( x << 16 ) | ( x >> 16 );
            x = ( ( x & 0x55555555 ) << 1 ) | ( ( x & 0xAAAAAAAA ) >> 1 );
            x = ( ( x & 0x33333333 ) << 2 ) | ( ( x & 0xCCCCCCCC ) >> 2 );
            x = ( ( x & 0x0F0F0F0F ) << 4 ) | ( ( x & 0xF0F0F0F0 ) >> 4 );
            x = ( ( x & 0x00FF00FF ) << 8 ) | ( ( x & 0xFF00FF00 ) >> 8 );

            return x;
        }

        uint CompactBits( uint x )
        {
            x &= 0x55555555;
            x = ( x ^ ( x >> 1 ) ) & 0x33333333;
            x = ( x ^ ( x >> 2 ) ) & 0x0F0F0F0F;
            x = ( x ^ ( x >> 4 ) ) & 0x00FF00FF;
            x = ( x ^ ( x >> 8 ) ) & 0x0000FFFF;

            return x;
        }
    };

    //=======================================================================================================================
    // GEOMETRY
    //=======================================================================================================================

    namespace Geometry
    {
        float2 GetPerpendicular( float2 v )
        {
            return float2( -v.y, v.x );
        }

        float4 GetRotator( float angle )
        {
            float ca = cos( angle );
            float sa = sin( angle );

            return float4( ca, sa, -sa, ca );
        }

        float4 GetRotator( float sa, float ca )
        { return float4( ca, sa, -sa, ca ); }

        float3x3 GetRotator( float3 axis, float angle )
        {
            float sa = sin( angle );
            float ca = cos( angle );
            float one_ca = 1.0 - ca;

            float3 a = sa * axis;
            float3 b = one_ca * axis.xyx * axis.yzz;

            float3 t1 = one_ca * ( axis * axis ) + ca;
            float3 t2 = b.xyz - a.zxy;
            float3 t3 = b.zxy + a.yzx;

            return float3x3
            (
                t1.x, t2.x, t3.x,
                t3.y, t1.y, t2.y,
                t2.z, t3.z, t1.z
            );
        }

        float4 CombineRotators( float4 r1, float4 r2 )
        { return r1.xyxy * r2.xxzz + r1.zwzw * r2.yyww; }

        float2 RotateVector( float4 rotator, float2 v )
        { return v.x * rotator.xz + v.y * rotator.yw; }

        float3 RotateVector( float4x4 m, float3 v )
        { return mul( ( float3x3 )m, v ); }

        float3 RotateVector( float3x3 m, float3 v )
        { return mul( m, v ); }

        float2 RotateVectorInverse( float4 rotator, float2 v )
        { return v.x * rotator.xy + v.y * rotator.zw; }

        float3 RotateVectorInverse( float4x4 m, float3 v )
        { return mul( ( float3x3 )transpose( m ), v ); }

        float3 RotateVectorInverse( float3x3 m, float3 v )
        { return mul( transpose( m ), v ); }

        float3 AffineTransform( float4x4 m, float3 p )
        { return mul( m, float4( p, 1.0 ) ).xyz; }

        float3 AffineTransform( float3x4 m, float3 p )
        { return mul( m, float4( p, 1.0 ) ); }

        float3 AffineTransform( float4x4 m, float4 p )
        { return mul( m, p ).xyz; }

        float4 ProjectiveTransform( float4x4 m, float3 p )
        { return mul( m, float4( p, 1.0 ) ); }

        float4 ProjectiveTransform( float4x4 m, float4 p )
        { return mul( m, p ); }

        float3 GetPerpendicularVector( float3 N )
        {
            float3 T = float3( N.z, -N.x, N.y );
            T -= N * dot( T, N );

            return normalize( T );
        }

        // http://marc-b-reynolds.github.io/quaternions/2016/07/06/Orthonormal.html
        float3x3 GetBasis( float3 N )
        {
            float sz = Math::Sign( N.z );
            float a  = 1.0 / ( sz + N.z );
            float ya = N.y * a;
            float b  = N.x * ya;
            float c  = N.x * sz;

            float3 T = float3( c * N.x * a - 1.0, sz * b, c );
            float3 B = float3( b, N.y * ya - sz, N.y );

            // Note: due to the quaternion formulation, the generated frame is rotated by 180 degrees,
            // s.t. if N = (0, 0, 1), then T = (-1, 0, 0) and B = (0, -1, 0).
            return float3x3( T, B, N );
        }

        float2 GetBarycentricCoords( float3 p, float3 a, float3 b, float3 c )
        {
            float3 v0 = b - a;
            float3 v1 = c - a;
            float3 v2 = p - a;

            float d00 = dot( v0, v0 );
            float d01 = dot( v0, v1 );
            float d11 = dot( v1, v1 );
            float d20 = dot( v2, v0 );
            float d21 = dot( v2, v1 );

            float2 barys;
            barys.x = d11 * d20 - d01 * d21;
            barys.y = d00 * d21 - d01 * d20;

            float invDenom = 1.0 / ( d00 * d11 - d01 * d01 );

            return barys * invDenom;
        }

        float DistanceAttenuation( float dist, float Rmax )
        {
            // [Brian Karis 2013, "Real Shading in Unreal Engine 4 ( course notes )"]
            float falloff = dist / Rmax;
            falloff *= falloff;
            falloff = saturate( 1.0 - falloff * falloff );
            falloff *= falloff;

            float atten = falloff;
            atten *= Math::PositiveRcp( dist * dist + 1.0 );

            return atten;
        }

        float3 UnpackLocalNormal( float2 localNormal, bool isUnorm = true )
        {
            float3 n;
            n.xy = isUnorm ? ( localNormal * ( 255.0 / 127.0 ) - 1.0 ) : localNormal;
            n.z = Math::Sqrt01( 1.0 - Math::LengthSquared( n.xy ) );

            return n;
        }

        float3 TransformLocalNormal( float2 localNormal, float4 T, float3 N )
        {
            float3 n = UnpackLocalNormal( localNormal );
            float3 B = cross( N, T.xyz ); // TODO: potentially "normalize" is needed here

            return normalize( T.xyz * n.x + B * n.y * T.w + N * n.z );
        }

        float BodyAngle( float cosHalfAngle )
        {
            return Math::Pi( 2.0 ) * ( 1.0 - cosHalfAngle );
        }

        float3 ReconstructViewPosition( float2 uv, float4 cameraFrustum, float viewZ = 1.0, float isOrtho = 0.0 )
        {
            float3 p;
            p.xy = uv * cameraFrustum.zw + cameraFrustum.xy;
            p.xy *= viewZ * ( 1.0 - abs( isOrtho ) ) + isOrtho; // isOrtho = { 0 - perspective, -1 - right handed ortho, 1 - left handed ortho }
            p.z = viewZ;

            return p;
        }

        float2 GetScreenUv( float4x4 worldToClip, float3 X )
        {
            float4 clip = Geometry::ProjectiveTransform( worldToClip, X );
            float2 uv = ( clip.xy / clip.w ) * float2( 0.5, -0.5 ) + 0.5;
            uv = clip.w < 0.0 ? 99999.0 : uv;

            return uv;
        }

        #define STL_SCREEN_MOTION 0
        #define STL_WORLD_MOTION 1

        float2 GetPrevUvFromMotion( float2 uv, float3 X, float4x4 worldToClipPrev, float3 motionVector, compiletime const uint motionType = STL_WORLD_MOTION )
        {
            float3 Xprev = X + motionVector;
            float2 uvPrev = GetScreenUv( worldToClipPrev, Xprev );

            [flatten]
            if( motionType == STL_SCREEN_MOTION )
                uvPrev = uv + motionVector.xy;

            return uvPrev;
        }
    };

    //=======================================================================================================================
    // COLOR
    //=======================================================================================================================

    namespace Color
    {
        // Transformations
        // https://en.wikipedia.org/wiki/Relative_luminance
        #define STL_LUMINANCE_BT601 0
        #define STL_LUMINANCE_BT709 1

        float Luminance( float3 linearColor, compiletime const uint mode = STL_LUMINANCE_DEFAULT )
        {
            return dot( linearColor, mode == STL_LUMINANCE_BT601 ? float3( 0.2990, 0.5870, 0.1140 ) : float3( 0.2126, 0.7152, 0.0722 ) );
        }

        float3 Saturation( float3 color, float amount )
        {
            float luma = Luminance( color );

            return lerp( color, luma, amount );
        }

        // Spaces
        float3 LinearToGamma( float3 color, float gamma = 2.2 )
        {
            return Math::Pow01( color, 1.0 / gamma );
        }

        float3 GammaToLinear( float3 color, float gamma = 2.2 )
        {
            return Math::Pow01( color, gamma );
        }

        float3 LinearToSrgb( float3 color )
        {
            const float4 consts = float4( 1.055, 0.41666, -0.055, 12.92 );
            color = saturate( color );

            return lerp( consts.x * Math::Pow( color, consts.yyy ) + consts.zzz, consts.w * color, color < 0.0031308 );
        }

        float3 SrgbToLinear( float3 color )
        {
            const float4 consts = float4( 1.0 / 12.92, 1.0 / 1.055, 0.055 / 1.055, 2.4 );
            color = saturate( color );

            return lerp( color * consts.x, Math::Pow( color * consts.y + consts.zzz, consts.www ), color > 0.04045 );
        }

        // https://en.wikipedia.org/wiki/High-dynamic-range_video#Perceptual_Quantizer
        // https://nick-shaw.github.io/cinematiccolor/common-rgb-color-spaces.html
        #define pq_m1 0.1593017578125
        #define pq_m2 78.84375
        #define pq_c1 0.8359375
        #define pq_c2 18.8515625
        #define pq_c3 18.6875
        #define pq_C 10000.0

        float3 LinearToPq( float3 color )
        {
            float3 L = color / pq_C;
            float3 Lm = Math::Pow( L, pq_m1 );
            float3 N = ( pq_c1 + pq_c2 * Lm ) * Math::PositiveRcp( 1.0 + pq_c3 * Lm );

            return Math::Pow( N, pq_m2 );
        }

        float3 PqToLinear( float3 color )
        {
            float3 Np = Math::Pow( color, 1.0 / pq_m2 );
            float3 L = Np - pq_c1;
            L *= Math::PositiveRcp( pq_c2 - pq_c3 * Np );
            L = Math::Pow( L, 1.0 / pq_m1 );

            return L * pq_C;
        }

        float3 LinearToYCoCg( float3 color )
        {
            float Co = color.x - color.z;
            float t = color.z + Co * 0.5;
            float Cg = color.y - t;
            float Y = t + Cg * 0.5;

            // TODO: useful, but not needed in many cases
            Y = max( Y, 0.0 );

            return float3( Y, Co, Cg );
        }

        float3 YCoCgToLinear( float3 color )
        {
            // TODO: useful, but not needed in many cases
            color.x = max( color.x, 0.0 );

            float t = color.x - color.z * 0.5;
            float g = color.z + t;
            float b = t - color.y * 0.5;
            float r = b + color.y;
            float3 res = float3( r, g, b );

            return res;
        }

        // HDR
        float3 Compress( float3 color, float exposure = 1.0 )
        {
            float luma = Luminance( color );

            return color * Math::PositiveRcp( 1.0 + luma * exposure );
        }

        float3 Decompress( float3 color, float exposure = 1.0 )
        {
            float luma = Luminance( color );

            return color * Math::PositiveRcp( 1.0 - luma * exposure );
        }

        float3 HdrToLinear( float3 colorMulExposure )
        {
            float3 x0 = colorMulExposure * 0.38317;
            float3 x1 = GammaToLinear( 1.0 - exp( -colorMulExposure ) );
            float3 color = lerp( x0, x1, step( 1.413, colorMulExposure ) );

            return saturate( color );
        }

        float3 LinearToHdr( float3 color )
        {
            float3 x0 = color / 0.38317;
            float3 x1 = -log( max( 1.0 - LinearToGamma( color ), 1e-6 ) );
            float3 colorMulExposure = lerp( x0, x1, step( 1.413, x0 ) );

            return colorMulExposure;
        }

        float3 HdrToGamma( float3 colorMulExposure )
        {
            float3 x0 = LinearToGamma( colorMulExposure * 0.38317 );
            float3 x1 = 1.0 - exp( -colorMulExposure );

            x0 = lerp( x0, x1, step( 1.413, colorMulExposure ) );

            return saturate( x0 );
        }

        float3 _UnchartedCurve( float3 color )
        {
            float A = 0.22; // Shoulder Strength
            float B = 0.3;  // Linear Strength
            float C = 0.1;  // Linear Angle
            float D = 0.2;  // Toe Strength
            float E = 0.01; // Toe Numerator
            float F = 0.3;  // Toe Denominator

            return saturate( ( ( color * ( A * color + C * B ) + D * E ) / ( color * ( A * color + B ) + D * F ) ) - ( E / F ) );
        }

        float3 HdrToLinear_Uncharted( float3 color )
        {
            // John Hable's Uncharted 2 filmic tone map (http://filmicgames.com/archives/75)
            return saturate( _UnchartedCurve( color ) / _UnchartedCurve( 11.2 ).x );
        }

        float3 HdrToLinear_Aces( float3 color )
        {
            // Cancel out the pre-exposure mentioned in https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
            color *= 0.6;

            float A = 2.51;
            float B = 0.03;
            float C = 2.43;
            float D = 0.59;
            float E = 0.14;

            return saturate( ( color * ( A * color + B ) ) * Math::PositiveRcp( color * ( C * color + D ) + E ) );
        }

        // Blending functions
        float4 BlendSoft( float4 a, float4 b )
        {
            float4 t = 1.0 - 2.0 * b;
            float4 c = ( 2.0 * b + a * t ) * a;
            float4 d = 2.0 * a * ( 1.0 - b ) - Math::Sqrt( a ) * t;
            bool4 res = b > 0.5;

            return lerp( c, d, res );
        }

        float4 BlendDarken( float4 a, float4 b )
        {
            bool4 res = a > b;

            return lerp( a, b, res );
        }

        float4 BlendDifference( float4 a, float4 b )
        {
            return abs( a - b );
        }

        float4 BlendScreen( float4 a, float4 b )
        {
            return a + b * ( 1.0 - a );
        }

        float4 BlendOverlay( float4 a, float4 b )
        {
            bool4 res = a > 0.5;
            float4 c = 2.0 * a * b;
            float4 d = 2.0 * BlendScreen( a, b ) - 1.0;

            return lerp( c, d, res );
        }

        // Misc
        float3 ColorizeLinear( float x )
        {
            x = saturate( x );

            float3 color;
            if( x < 0.25 )
                color = lerp( float3( 0, 0, 0 ), float3( 0, 0, 1 ), Math::SmoothStep( 0.00, 0.25, x ) );
            else if ( x < 0.50 )
                color = lerp( float3( 0, 0, 1 ), float3( 0, 1, 0 ), Math::SmoothStep( 0.25, 0.50, x ) );
            else if ( x < 0.75 )
                color = lerp( float3( 0, 1, 0 ), float3( 1, 1, 0 ), Math::SmoothStep( 0.50, 0.75, x ) );
            else
                color = lerp( float3( 1, 1, 0 ), float3( 1, 0, 0 ), Math::SmoothStep( 0.75, 1.00, x ) );

            return color;
        }

        // https://www.shadertoy.com/view/ls2Bz1
        float3 ColorizeZucconi( float x )
        {
            // Original solution converts visible wavelengths of light (400-700 nm) (represented as x = [0; 1]) to RGB colors
            x = saturate( x ) * 0.85;

            const float3 c1 = float3( 3.54585104, 2.93225262, 2.41593945 );
            const float3 x1 = float3( 0.69549072, 0.49228336, 0.27699880 );
            const float3 y1 = float3( 0.02312639, 0.15225084, 0.52607955 );

            float3 t = c1 * ( x - x1 );
            float3 a = saturate( 1.0 - t * t - y1 );

            const float3 c2 = float3( 3.90307140, 3.21182957, 3.96587128 );
            const float3 x2 = float3( 0.11748627, 0.86755042, 0.66077860 );
            const float3 y2 = float3( 0.84897130, 0.88445281, 0.73949448 );

            float3 k = c2 * ( x - x2 );
            float3 b = saturate( 1.0 - k * k - y2 );

            return saturate( a + b );
        }
    };

    //=======================================================================================================================
    // PACKING
    //=======================================================================================================================

    namespace Packing
    {
        // TODO: add signed packing / unpacking

        // Unsigned packing
        uint RgbaToUint( float4 c, compiletime const uint Rbits, compiletime const uint Gbits = 0, compiletime const uint Bbits = 0, compiletime const uint Abits = 0 )
        {
            const uint Rmask = ( 1 << Rbits ) - 1;
            const uint Gmask = ( 1 << Gbits ) - 1;
            const uint Bmask = ( 1 << Bbits ) - 1;
            const uint Amask = ( 1 << Abits ) - 1;
            const uint Gshift = Rbits;
            const uint Bshift = Gshift + Gbits;
            const uint Ashift = Bshift + Bbits;
            const float4 scale = float4( Rmask, Gmask, Bmask, Amask );

            uint4 p = uint4( saturate( c ) * scale + 0.5 );
            p.yzw <<= uint3( Gshift, Bshift, Ashift );
            p.xy |= p.zw;

            return p.x | p.y;
        }

        // Unsigned unpacking
        float4 UintToRgba( uint p, compiletime const uint Rbits, compiletime const uint Gbits = 0, compiletime const uint Bbits = 0, compiletime const uint Abits = 0 )
        {
            const uint Rmask = ( 1 << Rbits ) - 1;
            const uint Gmask = ( 1 << Gbits ) - 1;
            const uint Bmask = ( 1 << Bbits ) - 1;
            const uint Amask = ( 1 << Abits ) - 1;
            const uint Gshift = Rbits;
            const uint Bshift = Gshift + Gbits;
            const uint Ashift = Bshift + Bbits;
            const float4 scale = 1.0 / max( float4( Rmask, Gmask, Bmask, Amask ), 1.0 );

            uint4 c = p >> uint4( 0, Gshift, Bshift, Ashift );
            c.xyz &= uint3( Rmask, Gmask, Bmask );

            return float4( c ) * scale;
        }

        // Half float
        uint Rg16fToUint( float2 c )
        {
            return ( f32tof16( c.y ) << 16 ) | f32tof16( c.x );
        }

        float2 UintToRg16f( uint p )
        {
            float2 c;
            c.x = f16tof32( p );
            c.y = f16tof32( p >> 16 );

            return c;
        }

        // RGBE - shared exponent, positive values only (Ward 1984)
        uint EncodeRgbe( float3 c )
        {
            float sharedExp = ceil( log2( max( max( c.x, c.y ), c.z ) ) );
            float4 p = float4( c * exp2( -sharedExp ), ( sharedExp + 128.0 ) / 255.0 );

            return RgbaToUint( p, 8, 8, 8, 8 );
        }

        float3 DecodeRgbe( uint p )
        {
            float4 c = UintToRgba( p, 8, 8, 8, 8 );

            return c.xyz * exp2( c.w * 255.0 - 128.0 );
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
        float2 EncodeUnitVector( float3 v, compiletime const bool bSigned = false )
        {
            v /= abs( v.x ) + abs( v.y ) + abs( v.z );
            v.xy = v.z >= 0.0 ? v.xy : ( 1.0 - abs( v.yx ) ) * Math::Sign( v.xy );

            return bSigned ? v.xy : saturate( v.xy * 0.5 + 0.5 );
        }

        float3 DecodeUnitVector( float2 p, compiletime const bool bSigned = false, compiletime const bool bNormalize = true )
        {
            p = bSigned ? p : ( p * 2.0 - 1.0 );

            // https://twitter.com/Stubbesaurus/status/937994790553227264
            float3 n = float3( p.xy, 1.0 - abs( p.x ) - abs( p.y ) );
            float t = saturate( -n.z );
            n.xy += n.xy >= 0.0 ? -t : t;

            return bNormalize ? normalize( n ) : n;
        }
    };

    //=======================================================================================================================
    // FILTERING
    //=======================================================================================================================

    namespace Filtering
    {
        float GetModifiedRoughnessFromNormalVariance( float linearRoughness, float3 nonNormalizedAverageNormal )
        {
            // https://blog.selfshadow.com/publications/s2013-shading-course/rad/s2013_pbs_rad_notes.pdf (page 20)
            float l = length( nonNormalizedAverageNormal );
            float kappa = saturate( 1.0 - l * l ) * Math::PositiveRcp( l * ( 3.0 - l * l ) );

            return Math::Sqrt01( linearRoughness * linearRoughness + kappa );
        }

        // Mipmap level
        float GetMipmapLevel( float duvdxMulTexSizeSq, float duvdyMulTexSizeSq, compiletime const float maxAnisotropy = 1.0 )
        {
            // duvdtMulTexSizeSq = ||dUV/dt * TexSize|| ^ 2

            float Pmax = max( duvdxMulTexSizeSq, duvdyMulTexSizeSq );

            if ( maxAnisotropy > 1.0 )
            {
                float Pmin = min( duvdxMulTexSizeSq, duvdyMulTexSizeSq );
                float N = min( Pmax * Math::PositiveRcp( Pmin ), maxAnisotropy );
                Pmax *= Math::PositiveRcp( N );
            }

            float mip = 0.5 * log2( Pmax );

            return mip; // Yes, no clamping to 0 here!
        }

        // Nearest filter (for nearest sampling)
        struct Nearest
        {
            float2 origin;
        };

        Nearest GetNearestFilter( float2 uv, float2 texSize )
        {
            float2 t = uv * texSize;

            Nearest result;
            result.origin = floor( t );

            return result;
        }

        // Bilinear filter (for nearest sampling)
        struct Bilinear
        {
            float2 origin;
            float2 weights;
        };

        Bilinear GetBilinearFilter( float2 uv, float2 texSize )
        {
            float2 t = uv * texSize - 0.5;

            Bilinear result;
            result.origin = floor( t );
            result.weights = t - result.origin;

            return result;
        }

        float ApplyBilinearFilter( float s00, float s10, float s01, float s11, Bilinear f )
        { return lerp( lerp( s00, s10, f.weights.x ), lerp( s01, s11, f.weights.x ), f.weights.y ); }

        float2 ApplyBilinearFilter( float2 s00, float2 s10, float2 s01, float2 s11, Bilinear f )
        { return lerp( lerp( s00, s10, f.weights.x ), lerp( s01, s11, f.weights.x ), f.weights.y ); }

        float3 ApplyBilinearFilter( float3 s00, float3 s10, float3 s01, float3 s11, Bilinear f )
        { return lerp( lerp( s00, s10, f.weights.x ), lerp( s01, s11, f.weights.x ), f.weights.y ); }

        float4 ApplyBilinearFilter( float4 s00, float4 s10, float4 s01, float4 s11, Bilinear f )
        { return lerp( lerp( s00, s10, f.weights.x ), lerp( s01, s11, f.weights.x ), f.weights.y ); }

        float4 GetBilinearCustomWeights( Bilinear f, float4 customWeights )
        {
            float2 oneMinusWeights = 1.0 - f.weights;

            float4 weights = customWeights;
            weights.x *= oneMinusWeights.x * oneMinusWeights.y;
            weights.y *= f.weights.x * oneMinusWeights.y;
            weights.z *= oneMinusWeights.x * f.weights.y;
            weights.w *= f.weights.x * f.weights.y;

            return weights;
        }

        #define _ApplyBilinearCustomWeights( s00, s10, s01, s11, w, normalize ) ( ( s00 * w.x + s10 * w.y + s01 * w.z + s11 * w.w ) * ( normalize ? Math::PositiveRcp( dot( w, 1.0 ) ) : 1.0 ) )

        float ApplyBilinearCustomWeights( float s00, float s10, float s01, float s11, float4 w, compiletime const bool normalize = true )
        { return _ApplyBilinearCustomWeights( s00, s10, s01, s11, w, normalize ); }

        float2 ApplyBilinearCustomWeights( float2 s00, float2 s10, float2 s01, float2 s11, float4 w, compiletime const bool normalize = true )
        { return _ApplyBilinearCustomWeights( s00, s10, s01, s11, w, normalize ); }

        float4 ApplyBilinearCustomWeights( float4 s00, float4 s10, float4 s01, float4 s11, float4 w, compiletime const bool normalize = true )
        { return _ApplyBilinearCustomWeights( s00, s10, s01, s11, w, normalize ); }

        // Catmull-Rom (for nearest sampling)
        struct CatmullRom
        {
            float2 origin;
            float2 weights[4];
        };

        CatmullRom GetCatmullRomFilter( float2 uv, float2 texSize )
        {
            float2 tci = uv * texSize;
            float2 tc = floor( tci - 0.5 ) + 0.5;
            float2 f = tci - tc;

            CatmullRom result;
            result.origin = tc - 1.5;
            result.weights[ 0 ] = f * ( -0.5 + f * ( 1.0 - 0.5 * f ) );
            result.weights[ 1 ] = 1.0 + f * f * ( -2.5 + 1.5 * f );
            result.weights[ 2 ] = f * ( 0.5 + f * ( 2.0 - 1.5 *f ) );
            result.weights[ 3 ] = f * f * ( -0.5 + 0.5 * f );

            return result;
        }

        float4 ApplyCatmullRomFilterWithCustomWeights( CatmullRom filter, float4 s00, float4 s10, float4 s20, float4 s30, float4 s01, float4 s11, float4 s21, float4 s31, float4 s02, float4 s12, float4 s22, float4 s32, float4 s03, float4 s13, float4 s23, float4 s33, float4 w0, float4 w1, float4 w2, float4 w3 )
        {
            /*
            s00 * w0.x   s10 * w0.y   s20 * w1.x   s30 * w1.y
            s01 * w0.z   s11 * w0.w   s21 * w1.z   s31 * w1.w
            s02 * w2.x   s12 * w2.y   s22 * w3.x   s32 * w3.y
            s03 * w2.z   s13 * w2.w   s23 * w3.z   s33 * w3.w
            */

            float w = w0.x * filter.weights[ 0 ].x * filter.weights[ 0 ].y;
            float4 color = s00 * w;
            float sum = w;

            w = w0.y * filter.weights[ 1 ].x * filter.weights[ 0 ].y;
            color += s10 * w;
            sum += w;

            w = w1.x * filter.weights[ 2 ].x * filter.weights[ 0 ].y;
            color += s20 * w;
            sum += w;

            w = w1.y * filter.weights[ 3 ].x * filter.weights[ 0 ].y;
            color += s30 * w;
            sum += w;


            w = w0.z * filter.weights[ 0 ].x * filter.weights[ 1 ].y;
            color += s01 * w;
            sum += w;

            w = w0.w * filter.weights[ 1 ].x * filter.weights[ 1 ].y;
            color += s11 * w;
            sum += w;

            w = w1.z * filter.weights[ 2 ].x * filter.weights[ 1 ].y;
            color += s21 * w;
            sum += w;

            w = w1.w * filter.weights[ 3 ].x * filter.weights[ 1 ].y;
            color += s31 * w;
            sum += w;


            w = w2.x * filter.weights[ 0 ].x * filter.weights[ 2 ].y;
            color += s02 * w;
            sum += w;

            w = w2.y * filter.weights[ 1 ].x * filter.weights[ 2 ].y;
            color += s12 * w;
            sum += w;

            w = w3.x * filter.weights[ 2 ].x * filter.weights[ 2 ].y;
            color += s22 * w;
            sum += w;

            w = w3.y * filter.weights[ 3 ].x * filter.weights[ 2 ].y;
            color += s32 * w;
            sum += w;


            w = w2.z * filter.weights[ 0 ].x * filter.weights[ 3 ].y;
            color += s03 * w;
            sum += w;

            w = w2.w * filter.weights[ 1 ].x * filter.weights[ 3 ].y;
            color += s13 * w;
            sum += w;

            w = w3.z * filter.weights[ 2 ].x * filter.weights[ 3 ].y;
            color += s23 * w;
            sum += w;

            w = w3.w * filter.weights[ 3 ].x * filter.weights[ 3 ].y;
            color += s33 * w;
            sum += w;

            return color * Math::PositiveRcp( sum );
        }

        // Blur 1D
        // offsets = { -offsets.xy, offsets.zw, 0, offsets.xy, offsets.zw };
        float4 GetBlurOffsets1D( float2 directionDivTexSize )
        { return float2( 1.7229, 3.8697 ).xxyy * directionDivTexSize.xyxy; }

        // Blur 2D
        // offsets = { 0, offsets.xy, offsets.zw, offsets.wx, offsets.yz };
        float4 GetBlurOffsets2D( float2 invTexSize )
        { return float4( 0.4, 0.9, -0.4, -0.9 ) * invTexSize.xyxy; }
    };

    //=======================================================================================================================
    // Random number generation
    //=======================================================================================================================

    namespace Rng
    {
        static uint2 g_Seed;

        #define STL_RNG_UTOF 0
        #define STL_RNG_MANTISSA_BITS 1

        void Initialize( uint2 samplePos, uint frameIndex, uint spinNum = 16 )
        {
            uint2 tile = samplePos & 511;

            g_Seed.x = tile.y * 512 + tile.x;
            g_Seed.y = frameIndex;

            uint s = 0;
            [unroll]
            for( uint n = 0; n < spinNum; n++ )
            {
                s += 0x9e3779b9;
                g_Seed.x += ( ( g_Seed.y << 4 ) + 0xa341316c ) ^ ( g_Seed.y + s ) ^ ( ( g_Seed.y >> 5 ) + 0xc8013ea4 );
                g_Seed.y += ( ( g_Seed.x << 4 ) + 0xad90777d ) ^ ( g_Seed.x + s ) ^ ( ( g_Seed.x >> 5 ) + 0x7e95761e );
            }
        }

        uint2 GetUint2( )
        {
            // http://en.wikipedia.org/wiki/Linear_congruential_generator
            g_Seed = 1664525u * g_Seed + 1013904223u;

            return g_Seed;
        }

        // RESULT: [0; 1)
        float2 GetFloat2( compiletime const uint mode = STL_RNG_DEFAULT )
        {
            uint2 r = GetUint2( );

            if ( mode == STL_RNG_MANTISSA_BITS )
                return 2.0 - asfloat( ( r >> 9 ) | 0x3F800000 );

            return float2( r >> 8 ) * ( 1.0 / float( 1 << 24 ) );
        }

        float4 GetFloat4( compiletime const uint mode = STL_RNG_DEFAULT )
        {
            uint4 r;
            r.xy = GetUint2( );
            r.zw = GetUint2( );

            if ( mode == STL_RNG_MANTISSA_BITS )
                return 2.0 - asfloat( ( r >> 9 ) | 0x3F800000 );

            return float4( r >> 8 ) * ( 1.0 / float( 1 << 24 ) );
        }
    };

    //=======================================================================================================================
    // SEQUENCE
    //=======================================================================================================================

    namespace Sequence
    {
        // https://en.wikipedia.org/wiki/Ordered_dithering
        #define STL_BAYER_LINEAR 0
        #define STL_BAYER_REVERSEBITS 1

        // RESULT: [0; 15]
        uint Bayer4x4ui( uint2 samplePos, uint frameIndex, compiletime const uint mode = STL_BAYER_DEFAULT )
        {
            uint2 samplePosWrap = samplePos & 3;
            uint a = 2068378560 * ( 1 - ( samplePosWrap.x >> 1 ) ) + 1500172770 * ( samplePosWrap.x >> 1 );
            uint b = ( samplePosWrap.y + ( ( samplePosWrap.x & 1 ) << 2 ) ) << 2;

            uint sampleOffset = mode == STL_BAYER_REVERSEBITS ? Math::ReverseBits4( frameIndex ) : frameIndex;

            return ( ( a >> b ) + sampleOffset ) & 0xF;
        }

        // RESULT: [0; 1)
        float Bayer4x4( uint2 samplePos, uint frameIndex, compiletime const uint mode = STL_BAYER_DEFAULT )
        {
            uint bayer = Bayer4x4ui( samplePos, frameIndex, mode );

            return float( bayer ) / 16.0;
        }

        // https://en.wikipedia.org/wiki/Low-discrepancy_sequence
        // RESULT: [0; 1)
        float2 Hammersley2D( uint index, float sampleCount )
        {
            float x = float( index ) / sampleCount;
            float y = float( Math::ReverseBits32( index ) ) * 2.3283064365386963e-10;

            return float2( x, y );
        }

        // https://en.wikipedia.org/wiki/Z-order_curve
        uint2 Morton2D( uint index )
        {
            return uint2( Math::CompactBits( index ), Math::CompactBits( index >> 1 ) );
        }

        // Checkerboard pattern
        uint CheckerBoard( uint2 samplePos, uint frameIndex )
        {
            uint a = samplePos.x ^ samplePos.y;

            return ( a ^ frameIndex ) & 0x1;
        }
    };

    //=======================================================================================================================
    // ImportanceSampling
    //=======================================================================================================================

    namespace ImportanceSampling
    {
        float GetDiffuseProbability( float3 albedo, float3 Rf0 )
        {
            float lumAlbedo = Color::Luminance( albedo );
            float lumRf0 = Color::Luminance( Rf0 );
            float probability = lumAlbedo * Math::PositiveRcp( lumAlbedo + lumRf0 );

            return saturate( probability );
        }

        // Defines a cone angle, where micro-normals are distributed
        float GetSpecularLobeHalfAngle( float linearRoughness, float percentOfVolume = 0.75 )
        {
            float m = linearRoughness * linearRoughness;

            // Comparison of two methods: http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiJhdGFuKDAuNzUqeCp4LygxLTAuNzUpKSoxODAvMy4xNDE1OTIiLCJjb2xvciI6IiMxOUYwMEUifSx7InR5cGUiOjAsImVxIjoiMTgwKngqeC8oMSt4KngpIiwiY29sb3IiOiIjRkYwMDJCIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiMTgwIl19XQ--
            #if 1
                // https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf (page 72)
                // TODO: % of NDF volume - is it the trimming factor from VNDF sampling?
                return atan( m * percentOfVolume / ( 1.0 - percentOfVolume ) );
            #else
                return Math::DegToRad( 180.0 ) * m / ( 1.0 + m );
            #endif
        }

        float3 CorrectDirectionToInfiniteSource( float3 N, float3 L, float3 V, float tanOfAngularSize )
        {
            float3 R = reflect( -V, N );
            float3 centerToRay = L - dot( L, R ) * R;
            float3 closestPoint = centerToRay * saturate( tanOfAngularSize * Math::Rsqrt( Math::LengthSquared( centerToRay ) ) );

            return normalize( L - closestPoint );
        }

        // https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf (page 69)
        #define STL_SPECULAR_DOMINANT_DIRECTION_G1      0
        #define STL_SPECULAR_DOMINANT_DIRECTION_G2      1
        #define STL_SPECULAR_DOMINANT_DIRECTION_APPROX  2

        float4 GetSpecularDominantDirection( float3 N, float3 V, float linearRoughness, compiletime const uint mode = STL_SPECULAR_DOMINANT_DIRECTION_DEFAULT )
        {
            float NoV = abs( dot( N, V ) );

            float f;
            if( mode == STL_SPECULAR_DOMINANT_DIRECTION_G2 )
            {
                float a = 0.298475 * log( 39.4115 - 39.0029 * linearRoughness );
                f = Math::Pow01( 1.0 - NoV, 10.8649 ) * ( 1.0 - a ) + a;
            }
            else if( mode == STL_SPECULAR_DOMINANT_DIRECTION_G1 )
                f = 0.298475 * NoV * log( 39.4115 - 39.0029 * linearRoughness ) + ( 0.385503 - 0.385503 * NoV ) * log( 13.1567 - 12.2848 * linearRoughness );
            else
            {
                float s = 1.0 - linearRoughness;
                f = s * ( Math::Sqrt01( s ) + linearRoughness );
            }

            float3 R = reflect( -V, N );
            float3 lobeAxis = normalize( lerp( N, R, f ) );

            return float4( lobeAxis, f );
        }

        //=================================================================================
        // Uniform-distribution
        //=================================================================================

        namespace Uniform
        {
            float GetInversePDF( )
            {
                return Math::Pi( 2.0 );
            }

            float3 GetRay( float2 rnd )
            {
                float cosTheta = rnd.y;

                float sinTheta = Math::Sqrt01( 1.0 - cosTheta * cosTheta );
                float phi = rnd.x * Math::Pi( 2.0 );

                float3 ray;
                ray.x = sinTheta * cos( phi );
                ray.y = sinTheta * sin( phi );
                ray.z = cosTheta;

                return ray;
            }
        }

        //=================================================================================
        // Cosine-distribution
        //=================================================================================

        namespace Cosine
        {
            float GetInversePDF( float NoL = 1.0 ) // default can be useful to handle NoL cancelation ( PDF's NoL cancels throughput's NoL )
            {
                return Math::Pi( 1.0 ) * Math::PositiveRcp( NoL );
            }

            float3 GetRay( float2 rnd )
            {
                float cosTheta = Math::Sqrt01( rnd.y );

                float sinTheta = Math::Sqrt01( 1.0 - cosTheta * cosTheta );
                float phi = rnd.x * Math::Pi( 2.0 );

                float3 ray;
                ray.x = sinTheta * cos( phi );
                ray.y = sinTheta * sin( phi );
                ray.z = cosTheta;

                return ray;
            }
        }

        //=================================================================================
        // GGX
        //=================================================================================

        namespace GGX
        {
            float GetInversePDF( float D, float NoH, float VoH )
            {
                return 4.0 * VoH * Math::PositiveRcp( D * NoH );
            }

            float3 GetRay( float2 rnd, float linearRoughness )
            {
                float m = linearRoughness * linearRoughness;
                float m2 = m * m;
                float t = ( m2 - 1.0 ) * rnd.y + 1.0;
                float cosThetaSq = ( 1.0 - rnd.y ) * Math::PositiveRcp( t );
                float sinTheta = Math::Sqrt01( 1.0 - cosThetaSq );
                float phi = rnd.x * Math::Pi( 2.0 );

                float3 ray;
                ray.x = sinTheta * cos( phi );
                ray.y = sinTheta * sin( phi );
                ray.z = Math::Sqrt01( cosThetaSq );

                return ray;
            }
        }

        //=================================================================================
        // GGX-VNDF
        // http://jcgt.org/published/0007/04/01/paper.pdf
        //=================================================================================

        namespace VNDF
        {
            float GetInversePDF( float D, float VoH )
            {
                return 4.0 * VoH * Math::PositiveRcp( D );
            }

            float3 GetRay( float2 rnd, float2 linearRoughness, float3 Vlocal, float trimFactor = 1.0 )
            {
                const float EPS = 1e-7;

                // TODO: instead of using 2 roughness values introduce "anisotropy" parameter
                // https://blog.selfshadow.com/publications/s2013-shading-course/rad/s2013_pbs_rad_notes.pdf (page 3)

                float2 m = linearRoughness * linearRoughness;

                // Section 3.2: transforming the view direction to the hemisphere configuration
                float3 Vh = normalize( float3( m * Vlocal.xy, Vlocal.z ) );

                // Section 4.1: orthonormal basis (with special case if cross product is zero)
                float lensq = dot( Vh.xy, Vh.xy );
                float3 T1 = lensq > EPS ? float3( -Vh.y, Vh.x, 0.0 ) * rsqrt( lensq ) : float3( 1.0, 0.0, 0.0 );
                float3 T2 = cross( Vh, T1 );

                // Section 4.2: parameterization of the projected area
                // trimFactor: 1 - full lobe, 0 - true mirror
                float r = Math::Sqrt01( rnd.x * trimFactor );
                float phi = rnd.y * Math::Pi( 2.0 );
                float t1 = r * cos( phi );
                float t2 = r * sin( phi );
                float s = 0.5 * ( 1.0 + Vh.z );
                t2 = ( 1.0 - s ) * Math::Sqrt01( 1.0 - t1 * t1 ) + s * t2;

                // Section 4.3: reprojection onto hemisphere
                float3 Nh = t1 * T1 + t2 * T2 + Math::Sqrt01( 1.0 - t1 * t1 - t2 * t2 ) * Vh;

                // Section 3.4: transforming the normal back to the ellipsoid configuration
                float3 Ne = normalize( float3( m * Nh.xy, max( Nh.z, EPS ) ) );

                return Ne;
            }
        }
    };

    //=======================================================================================================================
    // BRDF
    //=======================================================================================================================

    /*
    LINKS:
    https://google.github.io/filament/Filament.html#materialsystem/specularbrdf
    https://knarkowicz.wordpress.com/2014/12/27/analytical-dfg-term-for-ibl/
    https://blog.selfshadow.com/publications/
    */

    // "roughness" (aka "alpha", aka "m") = specular or real roughness
    // "linearRoughness" (aka "perceptual roughness", aka "artistic roughness") = sqrt( roughness )
    // G1 = G1(V, m) is % visible in one direction
    // G2 = G2(L, V, m) is % visible in two directions (in practice, derived from G1)
    // G2(uncorellated) = G1(L, m) * G1(V, m)
    // Specular BRDF = F * D * G2 / (4.0 * NoV * NoL)

    namespace BRDF
    {
        float Pow5( float x )
        { return Math::Pow01( 1.0 - x, 5.0 ); }

        void ConvertDiffuseMetalnessToAlbedoRf0( float3 diffuseColor, float metalness, out float3 albedo, out float3 Rf0 )
        {
            // TODO: ideally, STL_RF0_DIELECTRICS needs to be replaced with reflectance "STL_RF0_DIELECTRICS = 0.16 * reflectance * reflectance"
            // see https://google.github.io/filament/Filament.html#toc4.8
            // see https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf (page 13)
            albedo = diffuseColor * saturate( 1.0 - metalness );
            Rf0 = lerp( STL_RF0_DIELECTRICS, diffuseColor, metalness );
        }

        float FresnelTerm_Shadowing( float3 Rf0 )
        {
            // UE4: anything less than 2% is physically impossible and is instead considered to be shadowing
            return saturate( Color::Luminance( Rf0 ) / 0.02 );
        }

        //======================================================================================================================
        // Diffuse terms
        //======================================================================================================================

        float DiffuseTerm_Lambert( float linearRoughness, float NoL, float NoV, float VoH )
        {
            float d = 1.0;

            return d / Math::Pi( 1.0 );
        }

        // [Burley 2012, "Physically-Based Shading at Disney"]
        float DiffuseTerm_Burley( float linearRoughness, float NoL, float NoV, float VoH )
        {
            float f = 2.0 * VoH * VoH * linearRoughness - 0.5;
            float FdV = f * Pow5( NoV ) + 1.0;
            float FdL = f * Pow5( NoL ) + 1.0;
            float d = FdV * FdL;

            return d / Math::Pi( 1.0 );
        }

        // [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
        float DiffuseTerm_OrenNayar( float linearRoughness, float NoL, float NoV, float VoH )
        {
            float m = linearRoughness * linearRoughness;
            float m2 = m * m;
            float VoL = 2.0 * VoH - 1.0;
            float c1 = 1.0 - 0.5 * m2 / ( m2 + 0.33 );
            float cosri = VoL - NoV * NoL;
            float a = cosri >= 0.0 ? saturate( NoL * Math::PositiveRcp( NoV ) ) : NoL;
            float c2 = 0.45 * m2 / ( m2 + 0.09 ) * cosri * a;
            float d = NoL * c1 + c2;

            return d / Math::Pi( 1.0 );
        }

        float DiffuseTerm( float linearRoughness, float NoL, float NoV, float VoH )
        {
            // DiffuseTerm_Lambert
            // DiffuseTerm_Burley
            // DiffuseTerm_OrenNayar

            return DiffuseTerm_Burley( linearRoughness, NoL, NoV, VoH );
        }

        //======================================================================================================================
        // Distribution terms - how the microfacet normal distributed around a given direction
        //======================================================================================================================

        // [Blinn 1977, "Models of light reflection for computer synthesized pictures"]
        float DistributionTerm_Blinn( float linearRoughness, float NoH )
        {
            float m = linearRoughness * linearRoughness;
            float m2 = m * m;
            float alpha = 2.0 * Math::PositiveRcp( m2 ) - 2.0;
            float norm = ( alpha + 2.0 ) / 2.0;
            float d = norm * Math::Pow01( NoH, alpha );

            return d / Math::Pi( 1.0 );
        }

        // [Beckmann 1963, "The scattering of electromagnetic waves from rough surfaces"]
        float DistributionTerm_Beckmann( float linearRoughness, float NoH )
        {
            float m = linearRoughness * linearRoughness;
            float m2 = m * m;
            float b = NoH * NoH;
            float a = m2 * b;
            float d = exp( ( b - 1.0 ) * Math::PositiveRcp( a ) ) * Math::PositiveRcp( a * b );

            return d / Math::Pi( 1.0 );
        }

        // GGX / Trowbridge-Reitz, [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
        float DistributionTerm_GGX( float linearRoughness, float NoH )
        {
            float m = linearRoughness * linearRoughness;
            float m2 = m * m;

            #if 1
                float t = 1.0 - NoH * NoH * ( 0.99999994 - m2 );
                float a = max( m, 1e-6 ) / t;
            #else
                float t = ( NoH * m2 - NoH ) * NoH + 1.0;
                float a = m * Math::PositiveRcp( t );
            #endif

            float d = a * a;

            return d / Math::Pi( 1.0 );
        }

        // Generalized Trowbridge-Reitz, [Burley 2012, "Physically-Based Shading at Disney"]
        float DistributionTerm_GTR( float linearRoughness, float NoH )
        {
            float m = linearRoughness * linearRoughness;
            float m2 = m * m;
            float t = ( NoH * m2 - NoH ) * NoH + 1.0;

            float t1 = Math::Pow01( t, -STL_GTR_GAMMA );
            float t2 = 1.0 - Math::Pow01( m2, -( STL_GTR_GAMMA - 1.0 ) );
            float d = ( STL_GTR_GAMMA - 1.0 ) * ( m2 * t1 - t1 ) * Math::PositiveRcp( t2 );

            return d / Math::Pi( 1.0 );
        }

        float DistributionTerm( float linearRoughness, float NoH )
        {
            // DistributionTerm_Blinn
            // DistributionTerm_Beckmann
            // DistributionTerm_GGX
            // DistributionTerm_GTR

            return DistributionTerm_GGX( linearRoughness, NoH );
        }

        //======================================================================================================================
        // Geometry terms - how much the microfacet is blocked by other microfacet
        //======================================================================================================================

        // Known as "G1"
        float GeometryTerm_Smith( float linearRoughness, float NoVL )
        {
            float m = linearRoughness * linearRoughness;
            float m2 = m * m;
            float a = NoVL + Math::Sqrt01( ( NoVL - m2 * NoVL ) * NoVL + m2 );

            return 2.0 * NoVL * Math::PositiveRcp( a );
        }

        //======================================================================================================================
        // Geometry terms - how much the microfacet is blocked by other microfacet
        // G(mod) = G / ( 4.0 * NoV * NoL ): BRDF = F * D * G / (4 * NoV * NoL) => BRDF = F * D * Gmod
        //======================================================================================================================

        float GeometryTermMod_Implicit( float linearRoughness, float NoL, float NoV, float VoH, float NoH )
        {
            return 0.25;
        }

        // [Neumann 1999, "Compact metallic reflectance models"]
        float GeometryTermMod_Neumann( float linearRoughness, float NoL, float NoV, float VoH, float NoH )
        {
            return 0.25 * Math::PositiveRcp( max( NoL, NoV ) );
        }

        // [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
        float GeometryTermMod_Schlick( float linearRoughness, float NoL, float NoV, float VoH, float NoH )
        {
            float m = linearRoughness * linearRoughness;

            // original form
            //float k = m * sqrt( 2.0 / Math::Pi( 1.0 ) );

            // UE4: tuned to match GGX [Karis]
            float k = m * 0.5;

            float a = NoL * ( 1.0 - k ) + k;
            float b = NoV * ( 1.0 - k ) + k;

            return 0.25 / max( a * b, 1e-6 );
        }

        // [Smith 1967, "Geometrical shadowing of a random rough surface"]
        // https://twvideo01.ubm-us.net/o1/vault/gdc2017/Presentations/Hammon_Earl_PBR_Diffuse_Lighting.pdf
        // Known as "G2 height correlated"
        float GeometryTermMod_SmithCorrelated( float linearRoughness, float NoL, float NoV, float VoH, float NoH )
        {
            float m = linearRoughness * linearRoughness;
            float m2 = m * m;
            float a = NoV * Math::Sqrt01( ( NoL - m2 * NoL ) * NoL + m2 );
            float b = NoL * Math::Sqrt01( ( NoV - m2 * NoV ) * NoV + m2 );

            return 0.5 * Math::PositiveRcp( a + b );
        }

        // Smith term for GGX modified by Disney to be less "hot" for small roughness values
        // [Burley 2012, "Physically-Based Shading at Disney"]
        // Known as "G2 = G1( NoL ) * G1( NoV )"
        float GeometryTermMod_SmithUncorrelated( float linearRoughness, float NoL, float NoV, float VoH, float NoH )
        {
            float m = linearRoughness * linearRoughness;
            float m2 = m * m;
            float a = NoL + Math::Sqrt01( ( NoL - m2 * NoL ) * NoL + m2 );
            float b = NoV + Math::Sqrt01( ( NoV - m2 * NoV ) * NoV + m2 );

            return Math::PositiveRcp( a * b );
        }

        // [Cook and Torrance 1982, "A Reflectance Model for Computer Graphics"]
        float GeometryTermMod_CookTorrance( float linearRoughness, float NoL, float NoV, float VoH, float NoH )
        {
            float k = 2.0 * NoH / VoH;
            float a = min( k * NoV, k * NoL );

            return saturate( a ) * 0.25 * Math::PositiveRcp( NoV * NoL );
        }

        float GeometryTermMod( float linearRoughness, float NoL, float NoV, float VoH, float NoH )
        {
            // GeometryTermMod_Implicit
            // GeometryTermMod_Neumann
            // GeometryTermMod_Schlick
            // GeometryTermMod_SmithCorrelated
            // GeometryTermMod_SmithUncorrelated
            // GeometryTermMod_CookTorrance

            return GeometryTermMod_SmithCorrelated( linearRoughness, NoL, NoV, VoH, NoH );
        }

        //======================================================================================================================
        // Fresnel terms - the amount of light that reflects from a mirror surface given its index of refraction
        //======================================================================================================================

        float3 FresnelTerm_None( float3 Rf0, float VoNH )
        {
            return Rf0;
        }

        // [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
        float3 FresnelTerm_Schlick( float3 Rf0, float VoNH )
        {
            return Rf0 + ( 1.0 - Rf0 ) * Pow5( VoNH );
        }

        float3 FresnelTerm_Fresnel( float3 Rf0, float VoNH )
        {
            float3 nu = Math::Sqrt01( Rf0 );
            nu = ( 1.0 + nu ) * Math::PositiveRcp( 1.0 - nu );

            float k = VoNH * VoNH - 1.0;
            float3 g = sqrt( nu * nu + k );
            float3 a = ( g - VoNH ) / ( g + VoNH );
            float3 c = ( g * VoNH + k ) / ( g * VoNH - k );

            return 0.5 * a * a * ( c * c + 1.0 );
        }

        float3 FresnelTerm( float3 Rf0, float VoNH )
        {
            // FresnelTerm_None
            // FresnelTerm_Schlick
            // FresnelTerm_Fresnel

            return FresnelTerm_Schlick( Rf0, VoNH ) * FresnelTerm_Shadowing( Rf0 );
        }

        //======================================================================================================================
        // Environment terms ( integrated over hemisphere )
        //======================================================================================================================

        // Approximate Models For Physically Based Rendering by Angelo Pesce and Michael Iwanicki
        // http://miciwan.com/SIGGRAPH2015/course_notes_wip.pdf
        float3 EnvironmentTerm_Pesce( float3 Rf0, float NoV, float linearRoughness )
        {
            float m = linearRoughness * linearRoughness;
            float a = 7.0 * NoV + 4.0 * m;
            float bias = exp2( -a );

            float b = min( linearRoughness, 0.739 + 0.323 * NoV ) - 0.434;
            float scale = 1.0 - bias - m * max( bias, b );

            bias *= FresnelTerm_Shadowing( Rf0 );

            return saturate( Rf0 * scale + bias );
        }

        // Shlick's approximation for Ross BRDF - makes Fresnel converge to less than 1.0 when NoV is low
        // https://hal.inria.fr/inria-00443630/file/article-1.pdf
        float3 EnvironmentTerm_Ross( float3 Rf0, float NoV, float linearRoughness )
        {
            float m = linearRoughness * linearRoughness;
            return Rf0 + ( 1.0 - Rf0 ) * Math::Pow01( 1.0 - NoV, 5.0 * exp( -2.69 * m ) ) / ( 1.0 + 22.7 * Math::Pow01( m, 1.5 ) );
        }

        float3 EnvironmentTerm( float3 Rf0, float NoV, float linearRoughness )
        {
            // EnvironmentTerm_Pesce
            // EnvironmentTerm_Ross

            return EnvironmentTerm_Ross( Rf0, NoV, linearRoughness );
        }

        //======================================================================================================================
        // Direct lighting
        //======================================================================================================================

        void DirectLighting( float3 N, float3 L, float3 V, float3 Rf0, float linearRoughness, out float3 Cdiff, out float3 Cspec )
        {
            float3 H = normalize( L + V );

            float NoL = saturate( dot( N, L ) );
            float NoH = saturate( dot( N, H ) );
            float VoH = saturate( dot( V, H ) );

            // Due to normal mapping can easily be < 0, it blows up GeometryTerm (Smith)
            float NoV = abs( dot( N, V ) );

            float D = DistributionTerm( linearRoughness, NoH );
            float G = GeometryTermMod( linearRoughness, NoL, NoV, VoH, NoH );
            float3 F = FresnelTerm( Rf0, VoH );
            float Kdiff = DiffuseTerm( linearRoughness, NoL, NoV, VoH );

            Cspec = F * D * G * NoL;
            Cdiff = ( 1.0 - F ) * Kdiff * NoL;

            Cspec = saturate( Cspec );
        }
    };

    //=======================================================================================================================
    // SPHERICAL HARMONICS
    //=======================================================================================================================

    struct SH1
    {
        float c0;
        float3 c1;
        float2 chroma;
    };

    namespace SphericalHarmonics
    {
        SH1 ConvertToSecondOrder( float3 color, float3 direction )
        {
            float3 YCoCg = Color::LinearToYCoCg( color );

            SH1 sh;
            sh.c0 = 0.282095 * YCoCg.x;
            sh.c1 = 0.488603 * YCoCg.x * direction;
            sh.chroma = YCoCg.yz;

            return sh;
        }

        float3 ExtractColor( SH1 sh )
        {
            float Y = sh.c0 / 0.282095;

            return Color::YCoCgToLinear( float3( Y, sh.chroma ) );
        }

        float3 ExtractDirection( SH1 sh )
        {
            return sh.c1 * Math::Rsqrt( Math::LengthSquared( sh.c1 ) );
        }

        // https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/gdc2018-precomputedgiobalilluminationinfrostbite.pdf
        float3 ResolveColorToDiffuse( SH1 sh, float3 N, float cosHalfAngle = 0.0 )
        {
            float d = dot( sh.c1, N );
            float Y = 1.023326 * d + 0.886226 * sh.c0;

            // Ignore negative values
            Y = max( Y, 0 );

            // Pages 45-53 ( Y *= 2.0 - hemisphere, Y *= 4.0 - sphere )
            Y *= Geometry::BodyAngle( cosHalfAngle ) / Math::Pi( 1.0 );

            // Corrected color-reproduction
            float modifier = 0.282095 * Y * Math::PositiveRcp( sh.c0 );
            float2 CoCg = sh.chroma * saturate( modifier );

            return Color::YCoCgToLinear( float3( Y, CoCg ) );
        }
    };
}

#endif

/*
Changelog:
v1.1
- removed bicubic filter
- added Catmull-Rom filter with custom weights
- removed "STL::" inside the namespace
- added tone mapping curves
- added more specular dominant direction calculation variants

v1.2
- fixed messed up "roughness" and "linearRoughness" entities
*/
