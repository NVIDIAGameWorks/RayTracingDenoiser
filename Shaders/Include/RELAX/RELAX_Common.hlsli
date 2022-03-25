/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

float4 UnpackPrevNormalRoughness(float4 packedData)
{
    float4 result;
    result.rgb = normalize(packedData.rgb * 2.0 - 1.0);
    result.a = packedData.a;

    return result;
}

float4 PackPrevNormalRoughness(float4 normalRoughness)
{
    float4 result;
    result.rgb = normalRoughness.xyz * 0.5 + 0.5;
    result.a = normalRoughness.a;

    return result;
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

void BicubicFloat4x2(out float4 result1, out float4 result2, Texture2D<float4> tex1, Texture2D<float4> tex2, SamplerState samp, float2 samplePos, float2 invViewSize)
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

    result1 =
        tex1.SampleLevel(samp, float2(tc0.x, tc12.y), 0).rgba * (w0.x * w12.y) +
        tex1.SampleLevel(samp, float2(tc12.x, tc0.y), 0).rgba * (w12.x * w0.y) +
        tex1.SampleLevel(samp, float2(tc12.x, tc12.y), 0).rgba * (w12.x * w12.y) +
        tex1.SampleLevel(samp, float2(tc12.x, tc3.y), 0).rgba * (w12.x * w3.y) +
        tex1.SampleLevel(samp, float2(tc3.x, tc12.y), 0).rgba * (w3.x * w12.y);

    result2 =
        tex2.SampleLevel(samp, float2(tc0.x, tc12.y), 0).rgba * (w0.x * w12.y) +
        tex2.SampleLevel(samp, float2(tc12.x, tc0.y), 0).rgba * (w12.x * w0.y) +
        tex2.SampleLevel(samp, float2(tc12.x, tc12.y), 0).rgba * (w12.x * w12.y) +
        tex2.SampleLevel(samp, float2(tc12.x, tc3.y), 0).rgba * (w12.x * w3.y) +
        tex2.SampleLevel(samp, float2(tc3.x, tc12.y), 0).rgba * (w3.x * w12.y);
    float norm = 1.0 / ((w0.x * w12.y) + (w12.x * w0.y) + (w12.x * w12.y) + (w12.x * w3.y) + (w3.x * w12.y));

    result1 *= norm;
    result2 *= norm;
}

float BilinearWithBinaryWeightsFloat(Texture2D<float> tex, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
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

float2 BilinearWithBinaryWeightsFloat2(Texture2D<float2> tex, int2 bilinearOrigin, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
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

float BilinearWithBinaryWeightsImmediateFloat(float s00, float s10, float s01, float s11, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
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

float3 BilinearWithBinaryWeightsImmediateFloat3(float3 s00, float3 s10, float3 s01, float3 s11, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
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

float4 BilinearWithBinaryWeightsImmediateFloat4(float4 s00, float4 s10, float4 s01, float4 s11, float2 bilinearWeights, float4 binaryWeights, float interpolatedBinaryWeight)
{
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

float3 GetCurrentWorldPosFromPixelPos(int2 pixelPos, float viewZ)
{
    float2 clipSpaceXY = ((float2)pixelPos + float2(0.5, 0.5)) * gInvRectSize * 2.0 - 1.0;

    return (gOrthoMode == 0) ?
        viewZ * (gFrustumForward.xyz + gFrustumRight.xyz * clipSpaceXY.x - gFrustumUp.xyz * clipSpaceXY.y) :
        viewZ * gFrustumForward.xyz + gFrustumRight.xyz * clipSpaceXY.x - gFrustumUp.xyz * clipSpaceXY.y;
}

float3 GetCurrentWorldPosFromClipSpaceXY(float2 clipSpaceXY, float viewZ)
{
    return (gOrthoMode == 0) ?
        viewZ * (gFrustumForward.xyz + gFrustumRight.xyz * clipSpaceXY.x - gFrustumUp.xyz * clipSpaceXY.y) :
        viewZ * gFrustumForward.xyz + gFrustumRight.xyz * clipSpaceXY.x - gFrustumUp.xyz * clipSpaceXY.y;
}

float3 GetPreviousWorldPosFromPixelPos(int2 pixelPos, float viewZ)
{
    float2 clipSpaceXY = ((float2)pixelPos + float2(0.5, 0.5)) * (1.0 / gRectSizePrev) * 2.0 - 1.0;

    return (gOrthoMode == 0) ?
        viewZ * (gPrevFrustumForward.xyz + gPrevFrustumRight.xyz * clipSpaceXY.x - gPrevFrustumUp.xyz * clipSpaceXY.y) :
        viewZ * gPrevFrustumForward.xyz + gPrevFrustumRight.xyz * clipSpaceXY.x - gPrevFrustumUp.xyz * clipSpaceXY.y;
}

float3 GetPreviousWorldPosFromClipSpaceXY(float2 clipSpaceXY, float viewZ)
{
    return (gOrthoMode == 0) ?
        viewZ * (gPrevFrustumForward.xyz + gPrevFrustumRight.xyz * clipSpaceXY.x - gPrevFrustumUp.xyz * clipSpaceXY.y) :
        viewZ * gPrevFrustumForward.xyz + gPrevFrustumRight.xyz * clipSpaceXY.x - gPrevFrustumUp.xyz * clipSpaceXY.y;
}

float GetPlaneDistanceWeight(float3 centerWorldPos, float3 centerNormal, float centerViewZ, float3 sampleWorldPos, float threshold)
{
    float distanceToCenterPointPlane = abs(dot(sampleWorldPos - centerWorldPos, centerNormal));

    return 1.0 - STL::Math::SmoothStep(threshold, threshold * 2.0, distanceToCenterPointPlane / centerViewZ);
}

float2 GetNormalWeightParams_ATrous(float roughness, float numFramesInHistory, float specularReprojectionConfidence, float normalEdgeStoppingRelaxation, float specularLobeAngleFraction)
{
    // Relaxing normal weights if not enough frames in history
    // and if specular reprojection confidence is low
    float relaxation = saturate(numFramesInHistory / 5.0);
    relaxation *= lerp(1.0, specularReprojectionConfidence, normalEdgeStoppingRelaxation);
    float f = 0.9 + 0.1 * relaxation;

    // This is the main parameter - cone angle
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle(roughness, specularLobeAngleFraction);

    // Increasing angle ~10x to relax rejection of the neighbors if specular reprojection confidence is low
    angle *= 10.0 - 9.0 * relaxation;
    angle = min(STL::Math::Pi(0.5), angle);

    return float2(angle, f);
}

float GetSpecularNormalWeight_ATrous(float2 params0, float specularLobeAngleSlack, float3 n0, float3 n)
{
    float cosa = saturate(dot(n0, n));
    float a = STL::Math::AcosApprox(cosa);
    params0.x += specularLobeAngleSlack;
    a = 1.0 - STL::Math::SmoothStep(0.0, params0.x, a);
    return saturate(1.0 + (a - 1.0) * params0.y);
}

float GetNormalWeightParams(float roughness, float angleFraction = 0.75)
{
    // This is the main parameter - cone angle
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle(roughness, angleFraction);
    angle = 1.0 / max(angle, NRD_ENCODING_ERRORS.x);

    return angle;
}

float GetNormalWeight(float params0, float3 n0, float3 n)
{
    float cosa = saturate(dot(n0, n));
    float angle = STL::Math::AcosApprox(cosa);

    return _ComputeWeight(float2(params0, -0.001), angle);
}

float GetEncodingAwareNormalWeight(float3 Ncurr, float3 Nprev, float maxAngle)
{
    float a = 1.0 / maxAngle;
    float cosa = saturate(dot(Ncurr, Nprev));
    float d = STL::Math::AcosApprox(cosa);

    return STL::Math::SmoothStep01(1.0 - (d - RELAX_NORMAL_ULP) * a);
}

float2 GetRoughnessWeightParams(float roughness, float percentOfRoughness = 0.05)
{
    // IMPORTANT: too small values of "percentOfRoughness" can ruin contact shadowing even if neighboring roughness is absolutely same due to re-packing imprecision problems.
    float a = 1.0 / (roughness * percentOfRoughness * 0.99 + 0.01);
    float b = roughness * a;

    return float2(a, -b);
}

float GetNormHitDist(float hitDist, float viewZ, float4 hitDistParams = float4(3.0, 0.1, 10.0, -25.0), float linearRoughness = 1.0)
{
    float f = _REBLUR_GetHitDistanceNormalization(viewZ, hitDistParams, linearRoughness);

    return saturate(hitDist / f);
}

float2 GetHitDistanceWeightParams(float normHitDist, float nonLinearAccumSpeed, float roughness = 1.0)
{
    float threshold = exp2(-17.0 * roughness * roughness); // TODO: not in line with other weights
    float scale = lerp(threshold, 1.0, nonLinearAccumSpeed);

    float a = rcp(normHitDist * scale * 0.99 + 0.01);
    float b = normHitDist * a;

    return float2(a, -b);
}

float EstimateCurvature(float3 Ni, float3 Vi, float3 N, float3 X)
{
    float3 Xi = 0 + Vi * dot(X - 0, N) / dot(Vi, N);
    float3 edge = Xi - X;
    float curvature = dot(Ni - N, edge) * rsqrt(STL::Math::LengthSquared(edge));

    return curvature;
}

// Altered (2,0) Pade approximation for exp(), good for negative arguments, max error 0.04 at a = -0.721
float exp_approx(float a)
{
    return 1.0 / (1.0 - a + a *a);
}