/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "NRD.hlsl"
#include "STL.hlsl"
#include "RELAX_Config.hlsl"

NRI_RESOURCE(cbuffer, globalConstants, b, 0, 0)
{
    int2  gResolution;
    float gColorBoxSigmaScale;
    float gSpecularAntiLagSigmaScale;
    float gSpecularAntiLagPower;
    float gDiffuseAntiLagSigmaScale;
    float gDiffuseAntiLagPower;
}

#include "NRD_Common.hlsl"
#include "RELAX_Common.hlsl"

#define THREAD_GROUP_SIZE 16
#define SKIRT 2

// Inputs
NRI_RESOURCE(Texture2D<uint2>, gSpecularAndDiffuseIlluminationLogLuv, t, 0, 0);
NRI_RESOURCE(Texture2D<uint2>, gSpecularAndDiffuseResponsiveIlluminationLogLuv, t, 1, 0);
NRI_RESOURCE(Texture2D<float2>, gSpecularAndDiffuseHistoryLength, t, 2, 0);

// Outputs
NRI_RESOURCE(RWTexture2D<uint2>, gOutSpecularAndDiffuseIlluminationLogLuv, u, 0, 0);
NRI_RESOURCE(RWTexture2D<float2>, gOutSpecularAndDiffuseHistoryLength, u, 1, 0);

groupshared uint3 sharedPackedSpecularDiffuse[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

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

uint3 pack(float3 specular, float3 diffuse)
{
    uint3 result;
    result.r = f32tof16(specular.r) | f32tof16(specular.g) << 16;
    result.g = f32tof16(specular.b) | f32tof16(diffuse.r) << 16;
    result.b = f32tof16(diffuse.g) | f32tof16(diffuse.b) << 16;
    return result;
}

void unpack(uint3 packed, out float3 specular, out float3 diffuse)
{
    specular.r = f16tof32(packed.r);
    specular.g = f16tof32(packed.r >> 16);
    specular.b = f16tof32(packed.g);
    diffuse.r = f16tof32(packed.g >> 16);
    diffuse.g = f16tof32(packed.b);
    diffuse.b = f16tof32(packed.b >> 16);
}

void runHistoryClamping(int2 pixelPos, int2 sharedMemoryIndex, float3 specular, float3 diffuse, float2 historyLength, out float3 outSpecular, out float3 outDiffuse, out float2 outHistoryLength)
{

    float3 specularYCoCg = LinearToYCoCg(specular);
    float3 diffuseYCoCg = LinearToYCoCg(diffuse);

    float3 specularFirstMoment = 0;
    float3 specularSecondMoment = 0;
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

            float3 specularP;
            float3 diffuseP;
            unpack(sharedPackedSpecularDiffuse[sharedMemoryIndexP.y][sharedMemoryIndexP.x], specularP, diffuseP);

            specularFirstMoment += specularP;
            specularSecondMoment += specularP * specularP;

            diffuseFirstMoment += diffuseP;
            diffuseSecondMoment += diffuseP * diffuseP;
        }
    }

    specularFirstMoment /= 25.0;
    specularSecondMoment /= 25.0;

    diffuseFirstMoment /= 25.0;
    diffuseSecondMoment /= 25.0;

    // Calculating color boxes for specular and diffuse signals
    float3 specularSigma = sqrt(max(0.0f, specularSecondMoment - specularFirstMoment * specularFirstMoment));
    float3 specularColorMin = specularFirstMoment - gColorBoxSigmaScale * specularSigma;
    float3 specularColorMax = specularFirstMoment + gColorBoxSigmaScale * specularSigma;

    float3 diffuseSigma = sqrt(max(0.0f, diffuseSecondMoment - diffuseFirstMoment * diffuseFirstMoment));
    float3 diffuseColorMin = diffuseFirstMoment - gColorBoxSigmaScale * diffuseSigma;
    float3 diffuseColorMax = diffuseFirstMoment + gColorBoxSigmaScale * diffuseSigma;

    // Expanding specular and diffuse color boxes with color of the center pixel for specular and diffuse signals
    // to avoid introducing bias
    float3 specularCenter;
    float3 diffuseCenter;
    unpack(sharedPackedSpecularDiffuse[sharedMemoryIndex.y][sharedMemoryIndex.x], specularCenter, diffuseCenter);

    specularColorMin = min(specularColorMin, specularCenter);
    specularColorMax = max(specularColorMax, specularCenter);
    diffuseColorMin = min(diffuseColorMin, diffuseCenter);
    diffuseColorMax = max(diffuseColorMax, diffuseCenter);

    // Calculating color boxes for antilag
    float3 specularColorMinForAntilag = specularFirstMoment - gSpecularAntiLagSigmaScale * specularSigma;
    float3 specularColorMaxForAntilag = specularFirstMoment + gSpecularAntiLagSigmaScale * specularSigma;
    float3 diffuseColorMinForAntilag = diffuseFirstMoment - gDiffuseAntiLagSigmaScale * diffuseSigma;
    float3 diffuseColorMaxForAntilag = diffuseFirstMoment + gDiffuseAntiLagSigmaScale * diffuseSigma;

    float3 specularYCoCgClampedForAntilag = clamp(specularYCoCg, specularColorMinForAntilag, specularColorMaxForAntilag);
    float3 diffuseYCoCgClampedForAntilag = clamp(diffuseYCoCg, diffuseColorMinForAntilag, diffuseColorMaxForAntilag);

    float3 specularDiffYCoCg = abs(specularYCoCgClampedForAntilag - specularYCoCg);
    float3 specularDiffYCoCgScaled = (specularYCoCg.r != 0) ? specularDiffYCoCg / (specularYCoCg.r) : 0;
    float specularAntilagAmount = gSpecularAntiLagPower * sqrt(dot(specularDiffYCoCgScaled, specularDiffYCoCgScaled));

    float3 diffuseDiffYCoCg = abs(diffuseYCoCgClampedForAntilag - diffuseYCoCg);
    float3 diffuseDiffYCoCgScaled = (diffuseYCoCg.r != 0) ? diffuseDiffYCoCg / (diffuseYCoCg.r) : 0;
    float diffuseAntilagAmount = gDiffuseAntiLagPower * sqrt(dot(diffuseDiffYCoCgScaled, diffuseDiffYCoCgScaled));

    outHistoryLength = historyLength;
    outHistoryLength.x = historyLength.x / (1.0 + specularAntilagAmount);
    outHistoryLength.y = historyLength.y / (1.0 + diffuseAntilagAmount);
    outHistoryLength = max(outHistoryLength, 1.0);

    // Color clamping
    specularYCoCg = clamp(specularYCoCg, specularColorMin, specularColorMax);
    outSpecular = YCoCgToLinear(specularYCoCg);

    diffuseYCoCg = clamp(diffuseYCoCg, diffuseColorMin, diffuseColorMax);
    outDiffuse = YCoCgToLinear(diffuseYCoCg);
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void NRD_CS_MAIN(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
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
    float3 diffuseResponsive = 0;

    if (xx >= 0 && yy >= 0 && xx < gResolution.x && yy < gResolution.y)
    {
        UnpackSpecularAndDiffuseFromLogLuvUint2(specularResponsive, diffuseResponsive, gSpecularAndDiffuseResponsiveIlluminationLogLuv[int2(xx, yy)]);
    }
    sharedPackedSpecularDiffuse[oy][ox] = pack(LinearToYCoCg(specularResponsive), LinearToYCoCg(diffuseResponsive));

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    specularResponsive = 0;
    diffuseResponsive = 0;

    if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if (xx >= 0 && yy >= 0 && xx < (int)gResolution.x && yy < (int)gResolution.y)
        {
            UnpackSpecularAndDiffuseFromLogLuvUint2(specularResponsive, diffuseResponsive, gSpecularAndDiffuseResponsiveIlluminationLogLuv[int2(xx, yy)]);
        }
        sharedPackedSpecularDiffuse[oy][ox] = pack(LinearToYCoCg(specularResponsive), LinearToYCoCg(diffuseResponsive));
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();
    // Shared memory is populated with responsive history now and can be used for history clamping

    // Reading normal history and history length
    float3 specular;
    float3 diffuse;
    UnpackSpecularAndDiffuseFromLogLuvUint2(specular, diffuse, gSpecularAndDiffuseIlluminationLogLuv[dispatchThreadId.xy]);

    // Reading history length
    float2 historyLength = gSpecularAndDiffuseHistoryLength[dispatchThreadId.xy] * 255.0;

    // Running history clamping
    uint2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT, SKIRT);
    float3 clampedSpecular;
    float3 clampedDiffuse;
    float2 adjustedHistoryLength;
    runHistoryClamping(dispatchThreadId.xy, sharedMemoryIndex, specular, diffuse, historyLength, clampedSpecular, clampedDiffuse, adjustedHistoryLength);

    // Writing out the results
    gOutSpecularAndDiffuseIlluminationLogLuv[dispatchThreadId.xy] = PackSpecularAndDiffuseToLogLuvUint2(clampedSpecular, clampedDiffuse);
    gOutSpecularAndDiffuseHistoryLength[dispatchThreadId.xy] = adjustedHistoryLength / 255.0;
}
