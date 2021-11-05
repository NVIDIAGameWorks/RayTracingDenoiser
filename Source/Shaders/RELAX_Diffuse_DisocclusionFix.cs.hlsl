/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"
#include "RELAX_Diffuse_DisocclusionFix.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

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

float getDiffuseNormalWeight(float3 centerNormal, float3 pointNormal)
{
    return pow(max(0.01, dot(centerNormal, pointNormal)), max(gDisocclusionFixEdgeStoppingNormalPower, 0.01));
}

float GetRadius(float numFramesInHistory)
{
    return gMaxRadius / (numFramesInHistory + 1.0);
}

//
// Main
//

[numthreads(8, 8, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint3 dispatchThreadId : SV_DispatchThreadId)
{
    const int2 ipos = int2(dispatchThreadId.xy);

    float centerViewZ = gViewZFP16[ipos] / NRD_FP16_VIEWZ_SCALE;

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerViewZ > gDenoisingRange)
    {
        return;
    }

    float historyLength = 255.0 * gDiffuseHistoryLength[ipos]; 
    float4 diffuseIlluminationAnd2ndMoment = gDiffuseIllumination[ipos];
    float4 diffuseIlluminationResponsive = gDiffuseIlluminationResponsive[ipos];

    // Pass through the input data if no disocclusion detected
    [branch]
    if (historyLength > gFramesToFix)
    {
        gOutDiffuseIllumination[ipos] = diffuseIlluminationAnd2ndMoment;
        gOutDiffuseIlluminationResponsive[ipos] = diffuseIlluminationResponsive;
        return;
    }

    // Unpacking the rest of center data
    float3 centerNormal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[ipos]).rgb;
    float3 centerWorldPos = getCurrentWorldPos(ipos, centerViewZ);

    // Running sparse cross-bilateral filter
    float4 diffuseIlluminationAnd2ndMomentSum = diffuseIlluminationAnd2ndMoment;

    float diffuseWSum = 1;

    float r = GetRadius(historyLength);

    [unroll]
    for (int j = -2; j <= 2; j++)
        [unroll]
    for (int i = -2; i <= 2; i++)
    {
        int dx = (int)(i * r);
        int dy = (int)(j * r);

        int2 samplePosInt = (int2)ipos + int2(dx, dy);

        bool isInside = all(samplePosInt >= int2(0, 0)) && all(samplePosInt < gResolution);
        if ((i == 0) && (j == 0)) continue;

        float3 sampleNormal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[samplePosInt]).rgb;

        float sampleViewZ = gViewZFP16[samplePosInt] / NRD_FP16_VIEWZ_SCALE;

        // Edge stopping functions:
        // ..normal
        float diffuseW = getDiffuseNormalWeight(centerNormal, sampleNormal);

        // ..geometry
        float3 sampleWorldPos = getCurrentWorldPos(samplePosInt, sampleViewZ);
        float geometryWeight = getGeometryWeight(centerWorldPos, centerNormal, sampleWorldPos, centerViewZ);
        diffuseW *= geometryWeight;
        diffuseW *= isInside ? 1.0 : 0;

        // Summing up diffuse result
        if (diffuseW > 1e-4)
        {
            float4 sampleDiffuseIlluminationAnd2ndMoment = gDiffuseIllumination[samplePosInt];
            diffuseIlluminationAnd2ndMomentSum += sampleDiffuseIlluminationAnd2ndMoment * diffuseW;
            diffuseWSum += diffuseW;
        }

    }

    float4 outDiffuseIlluminationAnd2ndMoment = diffuseIlluminationAnd2ndMomentSum / diffuseWSum;

    // Writing out the results
    gOutDiffuseIllumination[ipos] = outDiffuseIlluminationAnd2ndMoment;
    gOutDiffuseIlluminationResponsive[ipos] = outDiffuseIlluminationAnd2ndMoment;
}