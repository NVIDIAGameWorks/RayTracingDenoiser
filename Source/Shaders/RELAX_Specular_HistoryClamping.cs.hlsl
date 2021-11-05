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
#include "RELAX_Specular_HistoryClamping.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

#define THREAD_GROUP_SIZE 16
#define SKIRT 2

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float3 sharedSpecular[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

// Helper functions

void runHistoryClamping(int2 pixelPos, int2 sharedMemoryIndex, float3 specular, out float3 outSpecular)
{

    float3 specularYCoCg = STL::Color::LinearToYCoCg(specular);

    float3 specularFirstMoment = 0;
    float3 specularSecondMoment = 0;

    [unroll]
    for (int dy = -2; dy <= 2; dy++)
    {
        [unroll]
        for (int dx = -2; dx <= 2; dx++)
        {
            uint2 sharedMemoryIndexP = sharedMemoryIndex + int2(dx, dy);
            int2 p = pixelPos + int2(dx, dy);
            if (p.x <= 0 || p.y <= 0 || p.x >= gResolution.x || p.y >= gResolution.y) sharedMemoryIndexP = sharedMemoryIndex;

            float3 specularP = sharedSpecular[sharedMemoryIndexP.y][sharedMemoryIndexP.x];

            specularFirstMoment += specularP;
            specularSecondMoment += specularP * specularP;
        }
    }

    specularFirstMoment /= 25.0;
    specularSecondMoment /= 25.0;

    // Calculating color boxes for specular signal
    float3 specularSigma = sqrt(max(0.0f, specularSecondMoment - specularFirstMoment * specularFirstMoment));
    float3 specularColorMin = specularFirstMoment - gColorBoxSigmaScale * specularSigma;
    float3 specularColorMax = specularFirstMoment + gColorBoxSigmaScale * specularSigma;

    // Expanding specular color boxes with color of the center pixel for specular signal
    // to avoid introducing bias
    float3 specularCenter = sharedSpecular[sharedMemoryIndex.y][sharedMemoryIndex.x];

    specularColorMin = min(specularColorMin, specularCenter);
    specularColorMax = max(specularColorMax, specularCenter);

    // Color clamping
    specularYCoCg = clamp(specularYCoCg, specularColorMin, specularColorMax);
    outSpecular = STL::Color::YCoCgToLinear(specularYCoCg);
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

    float3 specularResponsive = 0;

    if (xx >= 0 && yy >= 0 && xx < gResolution.x && yy < gResolution.y)
    {
        specularResponsive = gSpecularResponsiveIllumination[int2(xx, yy)].rgb;
    }
    sharedSpecular[oy][ox] = STL::Color::LinearToYCoCg(specularResponsive);

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    specularResponsive = 0;

    if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if (xx >= 0 && yy >= 0 && xx < (int)gResolution.x && yy < (int)gResolution.y)
        {
            specularResponsive = gSpecularResponsiveIllumination[int2(xx, yy)].rgb;
        }
        sharedSpecular[oy][ox] = STL::Color::LinearToYCoCg(specularResponsive);
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();
    // Shared memory is populated with responsive history now and can be used for history clamping

    // Reading normal history and history length
    float4 specularAnd2ndMoment = gSpecularIllumination[dispatchThreadId.xy];

    // Reading history length
    float historyLength = gSpecularHistoryLength[dispatchThreadId.xy];

    // Running history clamping
    uint2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT, SKIRT);
    float3 clampedSpecular;
    runHistoryClamping(dispatchThreadId.xy, sharedMemoryIndex, specularAnd2ndMoment.rgb, clampedSpecular);

    // Writing out the results
    gOutSpecularIllumination[dispatchThreadId.xy] = float4(clampedSpecular, specularAnd2ndMoment.a);
    gOutSpecularHistoryLength[dispatchThreadId.xy] = historyLength;
}
