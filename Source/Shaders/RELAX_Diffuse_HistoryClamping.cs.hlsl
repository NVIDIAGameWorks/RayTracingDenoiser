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
#include "RELAX_Diffuse_HistoryClamping.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

#define THREAD_GROUP_SIZE 16
#define SKIRT 2

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float3 sharedDiffuse[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

void runHistoryClamping(int2 pixelPos, int2 sharedMemoryIndex, float3 diffuse, out float3 outDiffuse)
{

    float3 diffuseYCoCg = STL::Color::LinearToYCoCg(diffuse);

    float3 diffuseFirstMoment = 0;
    float3 diffuseSecondMoment = 0;

    [unroll]
    for (int dy = -2; dy <= 2; dy++)
    {
        [unroll]
        for (int dx = -2; dx <= 2; dx++)
        {
            uint2 sharedMemoryIndexP = sharedMemoryIndex + int2(dx, dy);
            int2 p = pixelPos + int2(dx, dy);
            if (p.x <= 0 || p.y <= 0 || p.x >= gResolution.x || p.y >= gResolution.y) sharedMemoryIndexP = sharedMemoryIndex;

            float3 diffuseP = sharedDiffuse[sharedMemoryIndexP.y][sharedMemoryIndexP.x];

            diffuseFirstMoment += diffuseP;
            diffuseSecondMoment += diffuseP * diffuseP;
        }
    }

    diffuseFirstMoment /= 25.0;
    diffuseSecondMoment /= 25.0;

    // Calculating color boxes for diffuse signal
    float3 diffuseSigma = sqrt(max(0.0f, diffuseSecondMoment - diffuseFirstMoment * diffuseFirstMoment));
    float3 diffuseColorMin = diffuseFirstMoment - gColorBoxSigmaScale * diffuseSigma;
    float3 diffuseColorMax = diffuseFirstMoment + gColorBoxSigmaScale * diffuseSigma;

    // Expanding diffuse color boxes with color of the center pixel for diffuse signal
    // to avoid introducing bias
    float3 diffuseCenter = sharedDiffuse[sharedMemoryIndex.y][sharedMemoryIndex.x];

    diffuseColorMin = min(diffuseColorMin, diffuseCenter);
    diffuseColorMax = max(diffuseColorMax, diffuseCenter);

    // Color clamping
    diffuseYCoCg = clamp(diffuseYCoCg, diffuseColorMin, diffuseColorMax);
    outDiffuse = STL::Color::YCoCgToLinear(diffuseYCoCg);
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadId, uint3 groupId : SV_GroupId)
{
    // Populating shared memory
    uint linearThreadIndex = groupThreadId.y * THREAD_GROUP_SIZE + groupThreadId.x;
    uint newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    uint newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    uint blockXStart = groupId.x * THREAD_GROUP_SIZE;
    uint blockYStart = groupId.y * THREAD_GROUP_SIZE;

    // First stage
    uint ox = newIdxX;
    uint oy = newIdxY;
    int xx = blockXStart + newIdxX - SKIRT;
    int yy = blockYStart + newIdxY - SKIRT;

    float3 diffuseResponsive = 0;

    if (xx >= 0 && yy >= 0 && xx < gResolution.x && yy < gResolution.y)
    {
        diffuseResponsive = gDiffuseResponsiveIllumination[int2(xx, yy)].rgb;
    }
    sharedDiffuse[oy][ox] = STL::Color::LinearToYCoCg(diffuseResponsive);

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    diffuseResponsive = 0;

    if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if (xx >= 0 && yy >= 0 && xx < (int)gResolution.x && yy < (int)gResolution.y)
        {
            diffuseResponsive = gDiffuseResponsiveIllumination[int2(xx, yy)].rgb;
        }
        sharedDiffuse[oy][ox] = STL::Color::LinearToYCoCg(diffuseResponsive);
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();
    // Shared memory is populated with responsive history now and can be used for history clamping

    // Reading normal history
    float4 diffuseAnd2ndMoment = gDiffuseIllumination[dispatchThreadId.xy];

    // Reading history length
    float historyLength = gDiffuseHistoryLength[dispatchThreadId.xy];

    // Running history clamping
    uint2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT, SKIRT);
    float3 clampedDiffuse;
    float adjustedHistoryLength;
    runHistoryClamping(dispatchThreadId.xy, sharedMemoryIndex, diffuseAnd2ndMoment.rgb, clampedDiffuse);

    // Writing out the results
    gOutDiffuseIllumination[dispatchThreadId.xy] = float4(clampedDiffuse, diffuseAnd2ndMoment.a);
    gOutDiffuseHistoryLength[dispatchThreadId.xy] = historyLength;
}
