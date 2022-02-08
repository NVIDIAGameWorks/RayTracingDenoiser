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
#include "RELAX_DiffuseSpecular_HistoryClamping.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

#define THREAD_GROUP_SIZE 16
#define SKIRT 2

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared uint3 sharedPackedSpecularDiffuse[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

uint3 pack(float3 specular, float3 diffuse)
{
    uint3 result;
    result.r = f32tof16(specular.r) | f32tof16(specular.g) << 16;
    result.g = f32tof16(specular.b) | f32tof16(diffuse.r) << 16;
    result.b = f32tof16(diffuse.g) | f32tof16(diffuse.b) << 16;
    return result;
}

void unpack(uint3 packedData, out float3 specular, out float3 diffuse)
{
    specular.r = f16tof32(packedData.r);
    specular.g = f16tof32(packedData.r >> 16);
    specular.b = f16tof32(packedData.g);
    diffuse.r = f16tof32(packedData.g >> 16);
    diffuse.g = f16tof32(packedData.b);
    diffuse.b = f16tof32(packedData.b >> 16);
}

void runHistoryClamping(int2 pixelPos, int2 sharedMemoryIndex, float3 specular, float3 diffuse, out float3 outSpecular, out float3 outDiffuse)
{
    float3 specularYCoCg = STL::Color::LinearToYCoCg(specular);
    float3 diffuseYCoCg = STL::Color::LinearToYCoCg(diffuse);

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
            if (any(p < int2(0,0)) || any(p >= (int2)gRectSize)) sharedMemoryIndexP = sharedMemoryIndex;

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

    // Color clamping
    specularYCoCg = clamp(specularYCoCg, specularColorMin, specularColorMax);
    outSpecular = STL::Color::YCoCgToLinear(specularYCoCg);

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

    float3 specularResponsive = 0;
    float3 diffuseResponsive = 0;

    if (xx >= 0 && yy >= 0 && xx < (int)gRectSize.x && yy < (int)gRectSize.y)
    {
        specularResponsive = gSpecularIlluminationResponsive[int2(xx, yy)].rgb;
        diffuseResponsive = gDiffuseIlluminationResponsive[int2(xx, yy)].rgb;
    }
    sharedPackedSpecularDiffuse[oy][ox] = pack(STL::Color::LinearToYCoCg(specularResponsive), STL::Color::LinearToYCoCg(diffuseResponsive));

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
        if (xx >= 0 && yy >= 0 && xx < (int)gRectSize.x && yy < (int)gRectSize.y)
        {
            specularResponsive = gSpecularIlluminationResponsive[int2(xx, yy)].rgb;
            diffuseResponsive = gDiffuseIlluminationResponsive[int2(xx, yy)].rgb;
        }
        sharedPackedSpecularDiffuse[oy][ox] = pack(STL::Color::LinearToYCoCg(specularResponsive), STL::Color::LinearToYCoCg(diffuseResponsive));
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();
    // Shared memory is populated with responsive history now and can be used for history clamping

    // Reading normal history
    float4 specularIlluminationAnd2ndMoment = gSpecularIllumination[dispatchThreadId.xy];
    float4 diffuseIlluminationAnd2ndMoment = gDiffuseIllumination[dispatchThreadId.xy];

    // Reading history length
    float2 historyLength = gSpecularAndDiffuseHistoryLength[dispatchThreadId.xy];

    // Running history clamping
    uint2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT, SKIRT);
    float3 clampedSpecular;
    float3 clampedDiffuse;
    float2 adjustedHistoryLength;
    runHistoryClamping(dispatchThreadId.xy, sharedMemoryIndex, specularIlluminationAnd2ndMoment.rgb, diffuseIlluminationAnd2ndMoment.rgb, clampedSpecular, clampedDiffuse);

    // Writing out the results
    gOutSpecularIllumination[dispatchThreadId.xy] =float4(clampedSpecular, specularIlluminationAnd2ndMoment.a);
    gOutDiffuseIllumination[dispatchThreadId.xy] = float4(clampedDiffuse, diffuseIlluminationAnd2ndMoment.a);
    gOutSpecularAndDiffuseHistoryLength[dispatchThreadId.xy] = historyLength;
}
