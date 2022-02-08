/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// Debug

#define SIGMA_REFERENCE                                 0 // works better with 16-bit precision
#define SIGMA_SHOW_TILES                                0

// Misc

#define PackShadow( s )                                 STL::Math::Sqrt01( s )
#define UnpackShadow( s )                               ( s * s )

// TODO: shadow unpacking is less trivial
// 2.0 - closer to reference (dictated by encoding)
// 2.0 - s.x - looks better
#if 0
    #define UnpackShadowSpecial( s )                    STL::Math::Pow01( s, 2.0 - s.x * ( 1 - SIGMA_REFERENCE ) )
#else
    #define UnpackShadowSpecial( s )                    UnpackShadow( s )
#endif

// TODO: move code below to STL.hlsl

float2 FilterBicubic(float2 size, float2 uv, out float4 uv_10_00, out float4 uv_11_01)
{
    const float4 c1 = float4( 3.0,  0.0,  1.0, 4.0);
    const float4 c2 = float4(-1.0,  3.0, -3.0, 1.0);
    const float4 c3 = float4( 3.0, -6.0, -3.0, 0.0);
    const float k = 1.0 / 6.0;

    float4 dxdy = -c1.zyyz / size.xyxy;

    float2 f = frac(uv.xy * size - 0.5);
    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float3 xw, yw;
    float4 phi;

    phi = k * (c2.xyzw * f3.xxxx + c3.xyxw * f2.xxxx + c3.zwxw * f.xxxx + c1.zwzy);
    xw.xy = c2.ww + c2.wx * f.xx + c2.xw * phi.yw / (phi.xz + phi.yw);
    xw.z = phi.x + phi.y;

    phi = k * (c2.xyzw * f3.yyyy + c3.xyxw * f2.yyyy + c3.zwxw * f.yyyy + c1.zwzy);
    yw.xy = c2.ww + c2.wx * f.yy + c2.xw * phi.yw / (phi.xz + phi.yw);
    yw.z = phi.x + phi.y;

    uv_10_00 = uv.xyxy + c2.wwxx * xw.xxyy * dxdy.xyxy;
    uv_11_01 = uv_10_00 + yw.xxxx * dxdy.zwzw;

    uv_10_00 -= yw.yyyy * dxdy.zwzw;

    return float2(yw.z, xw.z);
}

float TextureCubic(Texture2D<float> tex, float2 uv)
{
    uint w, h;
    tex.GetDimensions( w, h );
    float2 size = float2( w, h );

    float4 uv_10_00, uv_11_01;
    float2 t = FilterBicubic(size, uv.xy, uv_10_00, uv_11_01);

    float c00 = tex.SampleLevel(gLinearClamp, uv_10_00.zw, 0);
    float c10 = tex.SampleLevel(gLinearClamp, uv_10_00.xy, 0);
    float c01 = tex.SampleLevel(gLinearClamp, uv_11_01.zw, 0);
    float c11 = tex.SampleLevel(gLinearClamp, uv_11_01.xy, 0);

    c00 = lerp(c00, c01, t.x);
    c10 = lerp(c10, c11, t.x);

    return lerp(c00, c10, t.y);
}

float TextureSmooth(Texture2D<float> tex, float2 uv)
{
    // https://www.iquilezles.org/www/articles/texture/texture.htm
    uint w, h;
    tex.GetDimensions( w, h );
    float2 size = float2( w, h );

    uv = uv * size + 0.5;
    float2 iuv = floor( uv );
    float2 fuv = frac( uv );
    uv = iuv + fuv * fuv * ( 3.0 - 2.0 * fuv );
    uv = ( uv - 0.5 ) / size;

    return tex.SampleLevel( gLinearClamp, uv, 0 );
}