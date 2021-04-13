/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

float3 RELAX_BackEnd_UnpackRadiance( float3 compressedRadiance, float linearRoughness = 1.0 )
{
    float exposure = _NRD_GetColorCompressionExposure( linearRoughness );
    float lum = _NRD_Luminance( compressedRadiance );
    float3 radiance = compressedRadiance / max( 1.0 - lum * exposure, NRD_EPS );
    return radiance;
}

// Unpack normal, roughness and depth from uint32x2 with the following layout:
// - x: [RELAX_NORMAL_ROUGHNESS_BITS#1 + RELAX_NORMAL_ROUGHNESS_BITS#2 normal] | [RELAX_NORMAL_ROUGHNESS_BITS#3 roughness]
// - y: 32 bit depth
void UnpackNormalRoughnessDepth(out float3 normal, out float roughness, out float depth, uint2 packed)
{
    float3 t = STL::Packing::UintToRgba(packed.x, RELAX_NORMAL_ROUGHNESS_BITS).xyz;
    normal = STL::Packing::DecodeUnitVector(t.xy);
    roughness = t.z;
    depth = asfloat(packed.y);
}

// Pack normal, roughness and depth to uint32x2 with the following layout:
// - x: [RELAX_NORMAL_ROUGHNESS_BITS#1 + RELAX_NORMAL_ROUGHNESS_BITS#2 normal] | [RELAX_NORMAL_ROUGHNESS_BITS#3 roughness]
// - y: 32 bit depth
uint2 PackNormalRoughnessDepth(float3 normal, float roughness, float depth)
{
    float3 t;
    t.xy = STL::Packing::EncodeUnitVector(normal);
    t.z = roughness;

    uint2 packed;
    packed.x = STL::Packing::RgbaToUint(t.xyzz, RELAX_NORMAL_ROUGHNESS_BITS);
    packed.y = asuint(depth);

    return packed;
}

// Pack 2 RGB colors to uint2 using LogLuv encoding
uint2 PackSpecularAndDiffuseToLogLuvUint2(float3 specular, float3 diffuse)
{
    return uint2(STL::Color::LinearToLogLuv(specular), STL::Color::LinearToLogLuv(diffuse));
}

// Unpack 2 RGB colors from uint2 using LogLuv decoding
void UnpackSpecularAndDiffuseFromLogLuvUint2(out float3 specular, out float3 diffuse, uint2 packed)
{
    specular = STL::Color::LogLuvToLinear(packed.x);
    diffuse = STL::Color::LogLuvToLinear(packed.y);
}

// Filtering helpers
float4 BicubicFloat4(Texture2D<float4> tex, SamplerState samp, float2 samplePos, float2 invViewSize)
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

    float2 tc0 = (tc - 1.0) * invViewSize;
    float2 tc12 = (tc + w2 / w12) * invViewSize;
    float2 tc3 = (tc + 2.0) * invViewSize;

    float4 result =
        tex.SampleLevel(samp, float2(tc0.x, tc12.y), 0).rgba * (w0.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc0.y), 0).rgba * (w12.x * w0.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0).rgba * (w12.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc3.y), 0).rgba * (w12.x * w3.y) +
        tex.SampleLevel(samp, float2(tc3.x, tc12.y), 0).rgba * (w3.x * w12.y);
    return result / ((w0.x * w12.y) + (w12.x * w0.y) + (w12.x * w12.y) + (w12.x * w3.y) + (w3.x * w12.y));
}

float2 BicubicFloat2(Texture2D<float2> tex, SamplerState samp, float2 samplePos, float2 invViewSize)
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

    float2 tc0 = (tc - 1.0) * invViewSize;
    float2 tc12 = (tc + w2 / w12) * invViewSize;
    float2 tc3 = (tc + 2.0) * invViewSize;

    float2 result =
        tex.SampleLevel(samp, float2(tc0.x, tc12.y), 0).rg * (w0.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc0.y), 0).rg * (w12.x * w0.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0).rg * (w12.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc3.y), 0).rg * (w12.x * w3.y) +
        tex.SampleLevel(samp, float2(tc3.x, tc12.y), 0).rg * (w3.x * w12.y);
    return result / ((w0.x * w12.y) + (w12.x * w0.y) + (w12.x * w12.y) + (w12.x * w3.y) + (w3.x * w12.y));
}

void BilinearWithBinaryWeightsLogLuvX2(out float3 outSpecular,
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

float BilinearWithBinaryWeightsFloat(Texture2D<float> tex, SamplerState samp, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
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

float2 BilinearWithBinaryWeightsFloat2(Texture2D<float2> tex, SamplerState samp, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
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

float4 BilinearWithBinaryWeightsFloat4(Texture2D<float4> tex, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
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

float BilinearWithBinaryWeightsImmediateFloat(float s00, float s10, float s01, float s11, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
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

float3 BilinearWithBinaryWeightsImmediateFloat3(float3 s00, float3 s10, float3 s01, float3 s11, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
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

float3 GetVirtualWorldPos(float3 worldPos, float3 V, float NoV, float roughness, float hitDist)
{
    float f = STL::ImportanceSampling::GetSpecularDominantFactor(NoV, roughness, RELAX_SPEC_DOMINANT_DIRECTION);
    float3 virtualWorldPos = worldPos - V * hitDist * f;

    return virtualWorldPos;
}

