/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE(cbuffer, globalConstants, b, 0, 0)
{
    float4x4            gClipToWorld;
    float4x4            gClipToView;

    int2                gResolution;
    float2              gInvViewSize;

    float               gDisocclusionFixEdgeStoppingZFraction;
    float               gDisocclusionFixEdgeStoppingNormalPower;
    float               gMaxRadius;
    int                 gFramesToFix;
};

#include "RELAX_Common.hlsl"

// Inputs
NRI_RESOURCE(Texture2D<uint2>,  gSpecularAndDiffuseIlluminationLogLuv, t, 0, 0);
NRI_RESOURCE(Texture2D<uint2>,  gSpecularAndDiffuseIlluminationResponsiveLogLuv, t, 1, 0);
NRI_RESOURCE(Texture2D<float2>, gSpecularAndDiffuse2ndMoments, t, 2, 0);
NRI_RESOURCE(Texture2D<float>,  gHistoryLength, t, 3, 0);
NRI_RESOURCE(Texture2D<uint2>,  gNormalRoughnessDepth, t, 4, 0);

// Outputs
NRI_RESOURCE(RWTexture2D<uint2>,  gOutSpecularAndDiffuseIlluminationLogLuv, u, 0, 0);
NRI_RESOURCE(RWTexture2D<uint2>,  gOutSpecularAndDiffuseIlluminationResponsiveLogLuv, u, 1, 0);
NRI_RESOURCE(RWTexture2D<float2>, gOutSpecularAndDiffuse2ndMoments, u, 2, 0);

// Helper functions
float getLinearZFromDepth(int2 ipos, float depth)
{
    float2 uv = (float2(ipos)+0.5) * gInvViewSize.xy;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1);
    float4 viewPos = mul(gClipToView, clipPos);
    viewPos.z /= viewPos.w;
    return viewPos.z;
}

float3 getCurrentWorldPos(int2 pixelPos, float depth)
{
    float2 uv = ((float2)pixelPos + float2(0.5, 0.5)) * gInvViewSize;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1);
    float4 worldPos = mul(gClipToWorld, clipPos);
    return worldPos.xyz / worldPos.w;
}

float edgeStoppingDepth(float3 centerWorldPos, float3 centerNormal, float3 sampleWorldPos, float centerLinearZ)
{
    return (abs(dot(centerWorldPos - sampleWorldPos, centerNormal) / centerLinearZ) > gDisocclusionFixEdgeStoppingZFraction) ? 0.0 : 1.0;
}

float edgeStoppingNormal(float3 centerNormal, float3 pointNormal)
{
    return pow(max(0.01,dot(centerNormal, pointNormal)), max(gDisocclusionFixEdgeStoppingNormalPower, 0.01));
}


float GetRadius(float numFramesInHistory)
{
    return gMaxRadius / (numFramesInHistory + 1.0);
}


//
// Main
//

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 ipos = int2(dispatchThreadId.xy);

	// Getting data at the center
    float historyLength = gHistoryLength[ipos].r;
    uint2 illuminationPacked = gSpecularAndDiffuseIlluminationLogLuv[ipos];
    uint2 illuminationResponsivePacked = gSpecularAndDiffuseIlluminationResponsiveLogLuv[ipos];
    float2 centerSpecularAndDiffuse2ndMoments = gSpecularAndDiffuse2ndMoments[ipos];

    // Early out if no disocclusion detected
    if (historyLength > gFramesToFix)
    {
        gOutSpecularAndDiffuseIlluminationLogLuv[ipos] = illuminationPacked;
        gOutSpecularAndDiffuseIlluminationResponsiveLogLuv[ipos] = illuminationResponsivePacked;
        gOutSpecularAndDiffuse2ndMoments[ipos] = centerSpecularAndDiffuse2ndMoments;
        return;
    }

    // Unpacking the rest of center data
    float3 centerSpecularIlluminationResponsive;
    float3 centerDiffuseIlluminationResponsive;
    UnpackSpecularAndDiffuseFromLogLuvUint2(centerSpecularIlluminationResponsive, centerDiffuseIlluminationResponsive, illuminationResponsivePacked);

    float3 centerNormal;
    float centerRoughnessDontCare;
    float centerDepth;
    UnpackNormalRoughnessDepth(centerNormal, centerRoughnessDontCare, centerDepth, gNormalRoughnessDepth[ipos]);

    float centerLinearZ = getLinearZFromDepth(ipos, centerDepth);
    float3 centerWorldPos = getCurrentWorldPos(ipos, centerDepth);

    // Running sparse cross-bilateral filter
    float3 specularIlluminationSum = centerSpecularIlluminationResponsive;
    float3 diffuseIlluminationSum = centerDiffuseIlluminationResponsive;
    float2 specularAndDiffuse2ndMomentsSum = centerSpecularAndDiffuse2ndMoments;

    float wSum = 1;



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
        float3 sampleSpecularIllumination;
        float3 sampleDiffuseIllumination;
        UnpackSpecularAndDiffuseFromLogLuvUint2(sampleSpecularIllumination, sampleDiffuseIllumination, gSpecularAndDiffuseIlluminationResponsiveLogLuv[samplePosInt]);

        float2 sampleSpecularAndDiffuse2ndMoments = gSpecularAndDiffuse2ndMoments[samplePosInt];

        float3 sampleNormal;
        float sampleRoughnessDontCare;
        float sampleDepth;
        UnpackNormalRoughnessDepth(sampleNormal, sampleRoughnessDontCare, sampleDepth, gNormalRoughnessDepth[samplePosInt]);

        // Edge stopping functions:
        // ..normal
        float w = edgeStoppingNormal(centerNormal, sampleNormal);

        // ..depth
        float3 sampleWorldPos = getCurrentWorldPos(samplePosInt, sampleDepth);
        w *= edgeStoppingDepth(centerWorldPos, centerNormal, sampleWorldPos, centerLinearZ);

        // Summing up the result
        specularIlluminationSum += sampleSpecularIllumination * w;
        diffuseIlluminationSum += sampleDiffuseIllumination * w;
        specularAndDiffuse2ndMomentsSum += sampleSpecularAndDiffuse2ndMoments * w;

        wSum += w;
    }

    float3 outSpecularIllumination = specularIlluminationSum / wSum;
    float3 outDiffuseIllumination = diffuseIlluminationSum / wSum;
    float2 outSpecularAndDiffuse2ndMoments = specularAndDiffuse2ndMomentsSum / wSum;

    uint2 outIlluminationPacked = PackSpecularAndDiffuseToLogLuvUint2(outSpecularIllumination, outDiffuseIllumination);

    // Writing out the results
    gOutSpecularAndDiffuseIlluminationLogLuv[ipos] = outIlluminationPacked;
    gOutSpecularAndDiffuseIlluminationResponsiveLogLuv[ipos] = outIlluminationPacked;
    gOutSpecularAndDiffuse2ndMoments[ipos] = outSpecularAndDiffuse2ndMoments;
}
