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
#include "RELAX_Specular_HistoryClamping.resources.hlsl"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsl"

#define THREAD_GROUP_SIZE 16
#define SKIRT 2

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared uint2 sharedPackedSpecular[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

// Helper functions
float3 LinearToYCoCg(float3 color)
{
    float Co = color.x - color.z;
    float t = color.z + Co * 0.5;
    float Cg = color.y - t;
    float Y = t + Cg * 0.5;

    return float3(Y, Co, Cg);
}

float3 YCoCgToLinear(float3 color)
{
    float t = color.x - color.z * 0.5;
    float g = color.z + t;
    float b = t - color.y * 0.5;
    float r = b + color.y;
    float3 res = float3(r, g, b);

    // Ignore negative values ( minor deviations are possible )
    res = max(res, 0);

    return res;
}

uint2 pack(float3 specular)
{
    uint2 result;
    result.r = f32tof16(specular.r) | f32tof16(specular.g) << 16;
    result.g = f32tof16(specular.b);
    return result;
}

void unpack(uint2 packedData, out float3 specular)
{
    specular.r = f16tof32(packedData.r);
    specular.g = f16tof32(packedData.r >> 16);
    specular.b = f16tof32(packedData.g);
}

void runHistoryClamping(int2 pixelPos, int2 sharedMemoryIndex, float3 specular,float historyLength, out float3 outSpecular, out float outHistoryLength)
{

    float3 specularYCoCg = LinearToYCoCg(specular);

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

            float3 specularP;
            float3 diffuseP;
            unpack(sharedPackedSpecular[sharedMemoryIndexP.y][sharedMemoryIndexP.x], specularP);

            specularFirstMoment += specularP;
            specularSecondMoment += specularP * specularP;
        }
    }

    specularFirstMoment /= 25.0;
    specularSecondMoment /= 25.0;

    // Calculating color boxes for specular and diffuse signals
    float3 specularSigma = sqrt(max(0.0f, specularSecondMoment - specularFirstMoment * specularFirstMoment));
    float3 specularColorMin = specularFirstMoment - gColorBoxSigmaScale * specularSigma;
    float3 specularColorMax = specularFirstMoment + gColorBoxSigmaScale * specularSigma;

    // Expanding specular and diffuse color boxes with color of the center pixel for specular and diffuse signals
    // to avoid introducing bias
    float3 specularCenter;
    unpack(sharedPackedSpecular[sharedMemoryIndex.y][sharedMemoryIndex.x], specularCenter);

    specularColorMin = min(specularColorMin, specularCenter);
    specularColorMax = max(specularColorMax, specularCenter);

    // Calculating color boxes for antilag
    float3 specularColorMinForAntilag = specularFirstMoment - gSpecularAntiLagSigmaScale * specularSigma;
    float3 specularColorMaxForAntilag = specularFirstMoment + gSpecularAntiLagSigmaScale * specularSigma;

    float3 specularYCoCgClampedForAntilag = clamp(specularYCoCg, specularColorMinForAntilag, specularColorMaxForAntilag);

    float3 specularDiffYCoCg = abs(specularYCoCgClampedForAntilag - specularYCoCg);
    float3 specularDiffYCoCgScaled = (specularYCoCg.r != 0) ? specularDiffYCoCg / (specularYCoCg.r) : 0;
    float specularAntilagAmount = gSpecularAntiLagPower * sqrt(dot(specularDiffYCoCgScaled, specularDiffYCoCgScaled));

    outHistoryLength = historyLength / (1.0 + specularAntilagAmount);
    outHistoryLength = max(outHistoryLength, 1.0);

    // Color clamping
    specularYCoCg = clamp(specularYCoCg, specularColorMin, specularColorMax);
    outSpecular = YCoCgToLinear(specularYCoCg);
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
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
        specularResponsive = STL::Color::LogLuvToLinear(gSpecularResponsiveIlluminationLogLuv[int2(xx, yy)]);
    }
    sharedPackedSpecular[oy][ox] = pack(LinearToYCoCg(specularResponsive));

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
            specularResponsive = STL::Color::LogLuvToLinear(gSpecularResponsiveIlluminationLogLuv[int2(xx, yy)]);
        }
        sharedPackedSpecular[oy][ox] = pack(LinearToYCoCg(specularResponsive));
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();
    // Shared memory is populated with responsive history now and can be used for history clamping

    // Reading normal history and history length
    float3 specular = STL::Color::LogLuvToLinear(gSpecularIlluminationLogLuv[dispatchThreadId.xy]);

    // Reading history length
    float historyLength = gHistoryLength[dispatchThreadId.xy] * 255.0;

    // Running history clamping
    uint2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT, SKIRT);
    float3 clampedSpecular;
    float adjustedHistoryLength;
    runHistoryClamping(dispatchThreadId.xy, sharedMemoryIndex, specular, historyLength, clampedSpecular, adjustedHistoryLength);

    // Writing out the results
    gOutSpecularIlluminationLogLuv[dispatchThreadId.xy] = STL::Color::LinearToLogLuv(clampedSpecular);
    gOutHistoryLength[dispatchThreadId.xy] = adjustedHistoryLength / 255.0;
}
