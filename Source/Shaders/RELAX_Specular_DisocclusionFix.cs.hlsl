/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsl"
#include "STL.hlsl"
#include "RELAX_Specular_DisocclusionFix.resources.hlsl"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsl"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

// Helper functions
float3 getCurrentWorldPos(int2 pixelPos, float depth)
{
    float2 uv = ((float2)pixelPos + float2(0.5, 0.5)) * gInvRectSize * 2.0 - 1.0;
    return depth * (gFrustumForward.xyz + gFrustumRight.xyz * uv.x - gFrustumUp.xyz * uv.y);
}

float getGeometryWeight(float3 centerWorldPos, float3 centerNormal, float3 sampleWorldPos, float centerLinearZ)
{
    float distanceToCenterPointPlane = abs(dot(sampleWorldPos - centerWorldPos, centerNormal));
    return (distanceToCenterPointPlane / (centerLinearZ + 1e-6) > gDisocclusionThreshold) ? 0.0 : 1.0;
}

float GetRadius(float numFramesInHistory)
{
    return gMaxRadius / (numFramesInHistory + 1.0);
}

float getSpecularLobeHalfAngle(float roughness)
{
    // Defines a cone angle, where micro-normals are distributed
    float r2 = roughness * roughness;
    float r3 = roughness * r2;
    return 3.141592 * r2 / (1.0 + 0.5*r2 + r3);
}

float2 getNormalWeightParams(float roughness, float numFramesInHistory)
{
    // This is the main parameter - cone angle
    float angle = 0.33*getSpecularLobeHalfAngle(roughness);
    return float2(angle, 1.0);
}

float getSpecularRWeight(float2 params0, float3 v0, float3 v)
{
    float cosa = saturate(dot(v0, v));
    float a = STL::Math::AcosApprox(cosa);
    a = 1.0 - STL::Math::SmoothStep(0.0, params0.x, a);
    return saturate(1.0 + (a - 1.0) * params0.y);
}

//
// Main
//

[numthreads(8, 8, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 ipos = int2(dispatchThreadId.xy);

    // Getting data at the center
    float3 centerNormal;
    float centerRoughness;
    float centerLinearZ;
    UnpackNormalRoughnessDepth(centerNormal, centerRoughness, centerLinearZ, gNormalRoughnessDepth[ipos]);

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerLinearZ > gDenoisingRange)
    {
        return;
    }

    float historyLength = 255.0 * gHistoryLength[ipos]; 
    uint illuminationPacked = gSpecularIlluminationLogLuv[ipos];
    uint illuminationResponsivePacked = gSpecularIlluminationResponsiveLogLuv[ipos];
    float centerSpecular2ndMoment = gSpecular2ndMoment[ipos];

    // Pass through the input data if no disocclusion detected
    [branch]
    if (historyLength > gFramesToFix)
    {
        gOutSpecularIlluminationLogLuv[ipos] = illuminationPacked;
        gOutSpecularIlluminationResponsiveLogLuv[ipos] = illuminationResponsivePacked;
        gOutSpecular2ndMoment[ipos] = centerSpecular2ndMoment;
        return;
    }

    // Unpacking the rest of center data
    float3 centerSpecularIlluminationResponsive = STL::Color::LogLuvToLinear(illuminationResponsivePacked);

    float3 centerWorldPos = getCurrentWorldPos(ipos, centerLinearZ);
    float3 centerV = normalize(centerWorldPos);
    float3 centerR = reflect(centerV, centerNormal);
    float2 normalWeightParams = getNormalWeightParams(centerRoughness, historyLength);

    // Running sparse cross-bilateral filter
    float3 specularIlluminationSum = centerSpecularIlluminationResponsive;
    float specular2ndMoment = centerSpecular2ndMoment;

    float specularWSum = 1;

    float r = GetRadius(historyLength);

    [unroll]
    for (int j = -2; j <= 2; j++)
    [unroll]
    for (int i = -2; i <= 2; i++)
    {
        int dx = (int)(i * r);
        int dy = (int)(j * r);

        int2 samplePosInt = (int2)ipos + int2(dx, dy);

        if ((samplePosInt.x < 0) || (samplePosInt.x >= gResolution.x)) continue;
        if ((samplePosInt.y < 0) || (samplePosInt.y >= gResolution.y)) continue;
        if ((i == 0) && (j == 0)) continue;

        // Sampling data at the sample location
        // Since disocclusion fix works where there is no / small history, there is no / not much difference between
        // responsive and standard illumination, so we'll blur the responsive illumination for fix disocclusions in both normal and responsive illumination
        float3 sampleSpecularIllumination = STL::Color::LogLuvToLinear(gSpecularIlluminationResponsiveLogLuv[samplePosInt]);

        float sampleSpecular2ndMoment = gSpecular2ndMoment[samplePosInt];

        float3 sampleNormal;
        float sampleRoughnessDontCare;
        float sampleLinearZ;
        UnpackNormalRoughnessDepth(sampleNormal, sampleRoughnessDontCare, sampleLinearZ, gNormalRoughnessDepth[samplePosInt]);

        // Edge stopping functions:
        // ..geometry
        float3 sampleWorldPos = getCurrentWorldPos(samplePosInt, sampleLinearZ);
        float geometryWeight = getGeometryWeight(centerWorldPos, centerNormal, sampleWorldPos, centerLinearZ);
        float specularW = geometryWeight;

        // ..specular lobe
        float3 sampleV = normalize(sampleWorldPos);
        float3 sampleR = reflect(sampleV, sampleNormal);
        specularW *= getSpecularRWeight(normalWeightParams, sampleR, centerR);

        // Summing up the result
        specularIlluminationSum += sampleSpecularIllumination * specularW;
        specular2ndMoment += sampleSpecular2ndMoment * specularW;

        specularWSum += specularW;
    }

    float3 outSpecularIllumination = specularIlluminationSum / specularWSum;
    float outSpecular2ndMoment = specular2ndMoment / specularWSum;

    uint outIlluminationPacked = STL::Color::LinearToLogLuv(outSpecularIllumination);

    // Writing out the results
    gOutSpecularIlluminationLogLuv[ipos] = outIlluminationPacked;
    gOutSpecularIlluminationResponsiveLogLuv[ipos] = outIlluminationPacked;
    gOutSpecular2ndMoment[ipos] = outSpecular2ndMoment;
}
