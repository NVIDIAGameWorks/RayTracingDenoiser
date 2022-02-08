/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

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
    float3 centerWorldPos = GetCurrentWorldPos(ipos, centerViewZ);

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

        bool isInside = all(samplePosInt >= int2(0, 0)) && all(samplePosInt < int2(gRectSize));
        if ((i == 0) && (j == 0)) continue;

        float3 sampleNormal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[samplePosInt]).rgb;

        float sampleViewZ = gViewZFP16[samplePosInt] / NRD_FP16_VIEWZ_SCALE;

        // Edge stopping functions:
        // ..normal
        float diffuseW = getDiffuseNormalWeight(centerNormal, sampleNormal);

        // ..geometry
        float3 sampleWorldPos = GetCurrentWorldPos(samplePosInt, sampleViewZ);
        float geometryWeight = GetPlaneDistanceWeight(
                                    centerWorldPos,
                                    centerNormal,
                                    gIsOrtho == 0 ? centerViewZ : 1.0,
                                    sampleWorldPos,
                                    gDepthThreshold);
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