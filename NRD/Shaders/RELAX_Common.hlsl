/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "STL.hlsl"
#include "NRD.hlsl"

NRI_RESOURCE(SamplerState, gNearestClamp, s, 0, 0);
NRI_RESOURCE(SamplerState, gNearestMirror, s, 1, 0);
NRI_RESOURCE(SamplerState, gLinearClamp, s, 2, 0);
NRI_RESOURCE(SamplerState, gLinearMirror, s, 3, 0);

// Settings
#define NORMAL_ROUGHNESS_BITS			        11, 11, 10

// Unpack normal, roughness and depth from uint32x2 with the following layout:
// - x: [NORMAL_ROUGHNESS_BITS#1 + NORMAL_ROUGHNESS_BITS#2 normal] | [NORMAL_ROUGHNESS_BITS#3 roughness]
// - y: 32 bit depth
void UnpackNormalRoughnessDepth(out float3 normal, out float roughness, out float depth, uint2 packed)
{
    float3 t = STL::Packing::UintToRgba(packed.x, NORMAL_ROUGHNESS_BITS).xyz;
    normal = STL::Packing::DecodeUnitVector(t.xy);
    roughness = t.z;
    depth = asfloat(packed.y);
}

// Pack normal, roughness and depth to uint32x2 with the following layout:
// - x: [NORMAL_ROUGHNESS_BITS#1 + NORMAL_ROUGHNESS_BITS#2 normal] | [NORMAL_ROUGHNESS_BITS#3 roughness]
// - y: 32 bit depth
uint2 PackNormalRoughnessDepth(float3 normal, float roughness, float depth)
{
    float3 t;
    t.xy = STL::Packing::EncodeUnitVector(normal);
    t.z = roughness;

    uint2 packed;
    packed.x = STL::Packing::RgbaToUint(t.xyzz, NORMAL_ROUGHNESS_BITS);
    packed.y = asuint(depth);

    return packed;
}

// Transform an RGB color in Rec.709 to CIE XYZ
float3 RGBtoXYZ_Rec709(float3 c)
{
    static const float3x3 M =
    {
        0.4123907992659595, 0.3575843393838780, 0.1804807884018343,
        0.2126390058715104, 0.7151686787677559, 0.0721923153607337,
        0.0193308187155918, 0.1191947797946259, 0.9505321522496608
    };
    return mul(M, c);
}

// Transforms an XYZ color to RGB in Rec.709
float3 XYZtoRGB_Rec709(float3 c)
{
    static const float3x3 M =
    {
        3.240969941904522, -1.537383177570094, -0.4986107602930032,
        -0.9692436362808803, 1.875967501507721, 0.04155505740717569,
        0.05563007969699373, -0.2039769588889765, 1.056971514242878
    };
    return mul(M, c);
}


/** Encode an RGB color into a 32-bit LogLuv HDR format.
    The supported luminance range is roughly 10^-6..10^6 in 0.17% steps.

    The log-luminance is encoded with 14 bits and chroma with 9 bits each.
    This was empirically more accurate than using 8 bit chroma.
    Black (all zeros) is handled exactly.
*/
uint EncodeLogLuvHDR(float3 color)
{
    // Convert RGB to XYZ.
    float3 XYZ = RGBtoXYZ_Rec709(color);

    // Encode log2(Y) over the range [-20,20) in 14 bits (no sign bit).
    // TODO: Fast path that uses the bits from the fp32 representation directly.
    float logY = 409.6 * (log2(XYZ.y) + 20.0); // -inf if Y==0
    uint Le = (uint)clamp(logY, 0.0, 16383.0);

    // Early out if zero luminance to avoid NaN in chroma computation.
    // Note Le==0 if Y < 9.55e-7. We'll decode that as exactly zero.
    if (Le == 0) return 0;

    // Compute chroma (u,v) values by:
    //  x = X / (X + Y + Z)
    //  y = Y / (X + Y + Z)
    //  u = 4x / (-2x + 12y + 3)
    //  v = 9y / (-2x + 12y + 3)
    //
    // These expressions can be refactored to avoid a division by:
    //  u = 4X / (-2X + 12Y + 3(X + Y + Z))
    //  v = 9Y / (-2X + 12Y + 3(X + Y + Z))
    //
    float invDenom = 1.0 / (-2.0 * XYZ.x + 12.0 * XYZ.y + 3.0 * (XYZ.x + XYZ.y + XYZ.z));
    float2 uv = float2(4.0, 9.0) * XYZ.xy * invDenom;

    // Encode chroma (u,v) in 9 bits each.
    // The gamut of perceivable uv values is roughly [0,0.62], so scale by 820 to get 9-bit values.
    uint2 uve = (uint2)clamp(820.0 * uv, 0.0, 511.0);

    return (Le << 18) | (uve.x << 9) | uve.y;
}

/** Decode an RGB color stored in a 32-bit LogLuv HDR format.
    See encodeLogLuvHDR() for details.
*/
float3 DecodeLogLuvHDR(uint packedColor)
{
    // Decode luminance Y from encoded log-luminance.
    uint Le = packedColor >> 18;
    if (Le == 0) return 0;

    float logY = (float(Le) + 0.5) / 409.6 - 20.0;
    float Y = pow(2.0, logY);

    // Decode normalized chromaticity xy from encoded chroma (u,v).
    //
    //  x = 9u / (6u - 16v + 12)
    //  y = 4v / (6u - 16v + 12)
    //
    uint2 uve = uint2(packedColor >> 9, packedColor) & 0x1ff;
    float2 uv = (float2(uve)+0.5) / 820.0;

    float invDenom = 1.0 / (6.0 * uv.x - 16.0 * uv.y + 12.0);
    float2 xy = float2(9.0, 4.0) * uv * invDenom;

    // Convert chromaticity to XYZ and back to RGB.
    //  X = Y / y * x
    //  Z = Y / y * (1 - x - y)
    //
    float s = Y / xy.y;
    float3 XYZ = { s * xy.x, Y, s * (1.0 - xy.x - xy.y) };

    // Convert back to RGB and clamp to avoid out-of-gamut colors.
    return max(XYZtoRGB_Rec709(XYZ), 0.0);
}

// Pack 2 RGB colors to uint2 using LogLuv encoding
uint2 PackSpecularAndDiffuseToLogLuvUint2(float3 specular, float3 diffuse)
{
    return uint2(EncodeLogLuvHDR(specular), EncodeLogLuvHDR(diffuse));
}

// Unpack 2 RGB colors from uint2 using LogLuv decoding
void UnpackSpecularAndDiffuseFromLogLuvUint2(out float3 specular, out float3 diffuse, uint2 packed)
{
    specular = DecodeLogLuvHDR(packed.x);
    diffuse = DecodeLogLuvHDR(packed.y);
}

// Filtering helpers
float4 BicubicSampleCatmullRomFloat4UsingBilinear(Texture2D<float4> tex, SamplerState samp, float2 samplePos, float2 invViewSize)
{
    float2 tc = floor(samplePos - 0.5) + 0.5;
    float2 f = saturate(samplePos - tc);

    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float c = 0.5; // Sharpness: 0.5 is standard for Catmull-Rom
    float2 w0 = -c * f3 + 2.0 * c * f2 - c * f;
    float2 w1 = (2.0 - c) * f3 - (3.0 - c) * f2 + 1.0;
    float2 w2 = -(2.0 - c) * f3 + (3.0 - 2.0 * c) * f2 + c * f;
    float2 w3 = c * f3 - c * f2;
    float2 w12 = w1 + w2;

    float2 tc0 = (tc - 1) * invViewSize;
    float2 tc12 = (tc + w2 / w12) * invViewSize;
    float2 tc3 = (tc + 2) * invViewSize;

    float4 result =
        tex.SampleLevel(samp, float2(tc0.x, tc12.y), 0).rgba * (w0.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc0.y), 0).rgba * (w12.x * w0.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0).rgba * (w12.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc3.y), 0).rgba * (w12.x * w3.y) +
        tex.SampleLevel(samp, float2(tc3.x, tc12.y), 0).rgba * (w3.x * w12.y);
    return result / ((w0.x * w12.y) + (w12.x * w0.y) + (w12.x * w12.y) + (w12.x * w3.y) + (w3.x * w12.y));
}

float2 BicubicSampleCatmullRomFloat2UsingBilinear(Texture2D<float2> tex, SamplerState samp, float2 samplePos, float2 invViewSize)
{
    float2 tc = floor(samplePos - 0.5) + 0.5;
    float2 f = saturate(samplePos - tc);

    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float c = 0.5; // Sharpness: 0.5 is standard for Catmull-Rom
    float2 w0 = -c * f3 + 2.0 * c * f2 - c * f;
    float2 w1 = (2.0 - c) * f3 - (3.0 - c) * f2 + 1.0;
    float2 w2 = -(2.0 - c) * f3 + (3.0 - 2.0 * c) * f2 + c * f;
    float2 w3 = c * f3 - c * f2;
    float2 w12 = w1 + w2;

    float2 tc0 = (tc - 1) * invViewSize;
    float2 tc12 = (tc + w2 / w12) * invViewSize;
    float2 tc3 = (tc + 2) * invViewSize;

    float2 result =
        tex.SampleLevel(samp, float2(tc0.x, tc12.y), 0).rg * (w0.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc0.y), 0).rg * (w12.x * w0.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0).rg * (w12.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc3.y), 0).rg * (w12.x * w3.y) +
        tex.SampleLevel(samp, float2(tc3.x, tc12.y), 0).rg * (w3.x * w12.y);
    return result / ((w0.x * w12.y) + (w12.x * w0.y) + (w12.x * w12.y) + (w12.x * w3.y) + (w3.x * w12.y));
}

float4 BicubicSampleCatmullRomFloat4(Texture2D<float4> tex, float2 samplePos)
{
    float2 tc = floor(samplePos - 0.5) + 0.5;
    float2 f = saturate(samplePos - tc);

    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float c = 0.5; // Sharpness: 0.5 is standard for Catmull-Rom
    float2 w0 = -c * f3 + 2.0 * c * f2 - c * f;
    float2 w1 = (2.0 - c) * f3 - (3.0 - c) * f2 + 1.0;
    float2 w2 = -(2.0 - c) * f3 + (3.0 - 2.0 * c) * f2 + c * f;
    float2 w3 = c * f3 - c * f2;
    float2 w12 = w1 + w2;

    int2 origin = int2(floor(samplePos - 0.5) + 0.5);
    int2 tc0 = origin - int2(1, 1);
    int2 tc1 = origin;
    int2 tc2 = origin + int2(1, 1);
    int2 tc3 = origin + int2(2, 2);

    float4 result = tex[int2(tc1.x, tc0.y)].rgba * w1.x * w0.y + tex[int2(tc2.x, tc0.y)].rgba * w2.x * w0.y +

        tex[int2(tc0.x, tc1.y)].rgba * w0.x * w1.y + tex[int2(tc1.x, tc1.y)].rgba * w1.x * w1.y +
        tex[int2(tc2.x, tc1.y)].rgba * w2.x * w1.y + tex[int2(tc3.x, tc1.y)].rgba * w3.x * w1.y +

        tex[int2(tc0.x, tc2.y)].rgba * w0.x * w2.y + tex[int2(tc1.x, tc2.y)].rgba * w1.x * w2.y +
        tex[int2(tc2.x, tc2.y)].rgba * w2.x * w2.y + tex[int2(tc3.x, tc2.y)].rgba * w3.x * w2.y +

        tex[int2(tc1.x, tc3.y)].rgba * w1.x * w3.y + tex[int2(tc2.x, tc3.y)].rgba * w2.x * w3.y;

    return result / (1.0 - w0.x * w0.y - w3.x * w0.y - w0.x * w3.y - w3.x * w3.y);
}

void BicubicSampleCatmullRomFromPackedLogLuvX2(out float3 outSpecular, out float3 outDiffuse, Texture2D<uint2> tex, float2 samplePos)
{
    float2 tc = floor(samplePos - 0.5) + 0.5;
    float2 f = saturate(samplePos - tc);

    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float c = 0.5; // Sharpness: 0.5 is standard for Catmull-Rom
    float2 w0 = -c * f3 + 2.0 * c * f2 - c * f;
    float2 w1 = (2.0 - c) * f3 - (3.0 - c) * f2 + 1.0;
    float2 w2 = -(2.0 - c) * f3 + (3.0 - 2.0 * c) * f2 + c * f;
    float2 w3 = c * f3 - c * f2;
    float2 w12 = w1 + w2;

    int2 origin = int2(floor(samplePos - 0.5) + 0.5);
    int2 tc0 = origin - int2(1, 1);
    int2 tc1 = origin;
    int2 tc2 = origin + int2(1, 1);
    int2 tc3 = origin + int2(2, 2);

    outSpecular = 0;
    outDiffuse = 0;
    float3 specular;
    float3 diffuse;

    // 1st row
    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc1.x, tc0.y)]);
    outSpecular += specular * w1.x * w0.y;
    outDiffuse += diffuse * w1.x * w0.y;

    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc2.x, tc0.y)]);
    outSpecular += specular * w2.x * w0.y;
    outDiffuse += diffuse * w2.x * w0.y;

    // 2nd row
    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc0.x, tc1.y)]);
    outSpecular += specular * w0.x * w1.y;
    outDiffuse += diffuse * w0.x * w1.y;

    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc1.x, tc1.y)]);
    outSpecular += specular * w1.x * w1.y;
    outDiffuse += diffuse * w1.x * w1.y;

    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc2.x, tc1.y)]);
    outSpecular += specular * w2.x * w1.y;
    outDiffuse += diffuse * w2.x * w1.y;

    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc3.x, tc1.y)]);
    outSpecular += specular * w3.x * w1.y;
    outDiffuse += diffuse * w3.x * w1.y;

    // 3rd row
    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc0.x, tc2.y)]);
    outSpecular += specular * w0.x * w2.y;
    outDiffuse += diffuse * w0.x * w2.y;

    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc1.x, tc2.y)]);
    outSpecular += specular * w1.x * w2.y;
    outDiffuse += diffuse * w1.x * w2.y;

    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc2.x, tc2.y)]);
    outSpecular += specular * w2.x * w2.y;
    outDiffuse += diffuse * w2.x * w2.y;

    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc3.x, tc2.y)]);
    outSpecular += specular * w3.x * w2.y;
    outDiffuse += diffuse * w3.x * w2.y;

    // 4th row
    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc1.x, tc3.y)]);
    outSpecular += specular * w1.x * w3.y;
    outDiffuse += diffuse * w1.x * w3.y;

    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, tex[int2(tc2.x, tc3.y)]);
    outSpecular += specular * w2.x * w3.y;
    outDiffuse += diffuse * w2.x * w3.y;

    float norm = 1.0 / (1.0 - w0.x * w0.y - w3.x * w0.y - w0.x * w3.y - w3.x * w3.y);
    outSpecular *= norm;
    outDiffuse *= norm;
    outSpecular = max(0, outSpecular);
    outDiffuse = max(0, outDiffuse);
}

void LinearInterpolationWithBinaryWeightsFromPackedLogLuvX2(out float3 outSpecular,
    out float3 outDiffuse,
    Texture2D<uint2> tex,
    int2 bilinearOrigin,
    float2 bilinearWeights,
    float4 binaryWeights,
    float interpolatedBinaryWeight)
{
    float3 specular00, specular10, specular01, specular11;
    float3 diffuse00, diffuse10, diffuse01, diffuse11;

    UnpackSpecularAndDiffuseFromLogLuvUint2(specular00, diffuse00, tex[bilinearOrigin + int2(0, 0)]);
    UnpackSpecularAndDiffuseFromLogLuvUint2(specular10, diffuse10, tex[bilinearOrigin + int2(1, 0)]);
    UnpackSpecularAndDiffuseFromLogLuvUint2(specular01, diffuse01, tex[bilinearOrigin + int2(0, 1)]);
    UnpackSpecularAndDiffuseFromLogLuvUint2(specular11, diffuse11, tex[bilinearOrigin + int2(1, 1)]);

    specular00 *= binaryWeights.x;
    specular10 *= binaryWeights.y;
    specular01 *= binaryWeights.z;
    specular11 *= binaryWeights.w;

    diffuse00 *= binaryWeights.x;
    diffuse10 *= binaryWeights.y;
    diffuse01 *= binaryWeights.z;
    diffuse11 *= binaryWeights.w;

    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;
    outSpecular = STL::Filtering::ApplyBilinearFilter(specular00, specular10, specular01, specular11, bilinear) / interpolatedBinaryWeight;
    outDiffuse = STL::Filtering::ApplyBilinearFilter(diffuse00, diffuse10, diffuse01, diffuse11, bilinear) / interpolatedBinaryWeight;
}

float LinearInterpolationWithBinaryWeightsFloat(Texture2D<float> tex, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
{
    float s00 = tex[bilinearOrigin + int2(0, 0)].r;
    float s10 = tex[bilinearOrigin + int2(1, 0)].r;
    float s01 = tex[bilinearOrigin + int2(0, 1)].r;
    float s11 = tex[bilinearOrigin + int2(1, 1)].r;
    s00 *= binaryWeights.x;
    s10 *= binaryWeights.y;
    s01 *= binaryWeights.z;
    s11 *= binaryWeights.w;

    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;

    float r = STL::Filtering::ApplyBilinearFilter(s00, s10, s01, s11, bilinear);
    r /= interpolatedBinaryWeight;
    return r;
}

float2 LinearInterpolationWithBinaryWeightsFloat2(Texture2D<float2> tex, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
{
    float2 s00 = tex[bilinearOrigin + int2(0, 0)].rg;
    float2 s10 = tex[bilinearOrigin + int2(1, 0)].rg;
    float2 s01 = tex[bilinearOrigin + int2(0, 1)].rg;
    float2 s11 = tex[bilinearOrigin + int2(1, 1)].rg;
    s00 *= binaryWeights.x;
    s10 *= binaryWeights.y;
    s01 *= binaryWeights.z;
    s11 *= binaryWeights.w;

    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;

    float2 r = STL::Filtering::ApplyBilinearFilter(s00, s10, s01, s11, bilinear);
    r /= interpolatedBinaryWeight;
    return r;
}

float4 LinearInterpolationWithBinaryWeightsFloat4(Texture2D<float4> tex, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
{
    float4 s00 = tex[bilinearOrigin + int2(0, 0)].rgba;
    float4 s10 = tex[bilinearOrigin + int2(1, 0)].rgba;
    float4 s01 = tex[bilinearOrigin + int2(0, 1)].rgba;
    float4 s11 = tex[bilinearOrigin + int2(1, 1)].rgba;
    s00 *= binaryWeights.x;
    s10 *= binaryWeights.y;
    s01 *= binaryWeights.z;
    s11 *= binaryWeights.w;

    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;

    float4 r = STL::Filtering::ApplyBilinearFilter(s00, s10, s01, s11, bilinear);
    r /= interpolatedBinaryWeight;
    return r;
}

float LinearInterpolationWithBinaryWeightsImmediateFloat(float s00, float s10, float s01, float s11, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
{
    s00 *= binaryWeights.x;
    s10 *= binaryWeights.y;
    s01 *= binaryWeights.z;
    s11 *= binaryWeights.w;

    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;

    float r = STL::Filtering::ApplyBilinearFilter(s00, s10, s01, s11, bilinear);
    r /= interpolatedBinaryWeight;
    return r;
}

float3 LinearInterpolationWithBinaryWeightsImmediateFloat3(float3 s00, float3 s10, float3 s01, float3 s11, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
{
    s00 *= binaryWeights.x;
    s10 *= binaryWeights.y;
    s01 *= binaryWeights.z;
    s11 *= binaryWeights.w;

    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;

    float3 r = STL::Filtering::ApplyBilinearFilter(s00, s10, s01, s11, bilinear);
    r /= interpolatedBinaryWeight;
    return r;
}
