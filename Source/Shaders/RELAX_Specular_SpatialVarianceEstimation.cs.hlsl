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
#include "RELAX_Specular_SpatialVarianceEstimation.resources.hlsl"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsl"

#define THREAD_GROUP_SIZE 16
#define SKIRT 2

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared uint2 sharedPackedIllumination1stMoment         [THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
groupshared uint4 sharedPackedNormalRoughnessDepth2ndMoment [THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

// Unpacking from LogLuv to RGB is expensive, so let's do it once,
// at the stage of populating the shared memory
uint2 packIllumination1stMoment(uint specularIlluminationLogLuv)
{
    float3 specularIllum = STL::Color::LogLuvToLinear(specularIlluminationLogLuv);
    uint2 result;
    result.r = f32tof16(specularIllum.r) | f32tof16(specularIllum.g) << 16;
    result.g = f32tof16(specularIllum.b) | f32tof16(STL::Color::Luminance(specularIllum.rgb)) << 16;
    return result;
}

void unpackIllumination1stMoment(uint2 packedData, out float3 specularIllum, out float specular1stMoment)
{
    specularIllum.r = f16tof32(packedData.r);
    specularIllum.g = f16tof32(packedData.r >> 16);
    specularIllum.b = f16tof32(packedData.g);
    specular1stMoment = f16tof32(packedData.g >> 16);
}

uint4 packNormalRoughnessDepth2ndMoment(uint2 packedNormalRoughnessDepth, float specular2ndMoment)
{
    uint4 result;
    result.rg = packedNormalRoughnessDepth.rg;
    result.b = f32tof16(specular2ndMoment);
    result.a = 0;
    return result;
}

void unpackNormalRoughnessDepth2ndMoment(uint4 packedData, out float3 normal, out float roughness, out float depth, out float specular2ndMoment)
{
    UnpackNormalRoughnessDepth(normal, roughness, depth, packedData.rg);
    specular2ndMoment = f16tof32(packedData.b);
}

void unpack2ndMoment(uint4 packed, out float specular2ndMoment)
{
    specular2ndMoment = f16tof32(packed.b);
}

float computeNormalWeight(float3 normalCenter, float3 normalP, float phiNormal)
{
    return phiNormal == 0.0f ? 1.0f : pow(saturate(dot(normalCenter, normalP)), phiNormal);
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    const int2 ipos = dispatchThreadId.xy;

    // Populating shared memory
    uint linearThreadIndex = groupThreadId.y * THREAD_GROUP_SIZE + groupThreadId.x;
    uint newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    uint newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    uint blockXStart = groupId.x * THREAD_GROUP_SIZE;
    uint blockYStart = groupId.y * THREAD_GROUP_SIZE;

    // First stage
    int ox = newIdxX;
    int oy = newIdxY;
    int xx = blockXStart + newIdxX - SKIRT;
    int yy = blockYStart + newIdxY - SKIRT;

    uint2 packedIllumination1stMoment = 0;
    float specular2ndMoment = 0;
    uint2 packedNormalRoughnessDepth = 0;

    if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
    {
        packedIllumination1stMoment = packIllumination1stMoment(gSpecularIlluminationLogLuv[int2(xx,yy)]);
        specular2ndMoment = gSpecular2ndMoment[int2(xx, yy)];
        packedNormalRoughnessDepth = gNormalRoughnessDepth[int2(xx, yy)];
    }
    sharedPackedIllumination1stMoment[oy][ox] = packedIllumination1stMoment;
    sharedPackedNormalRoughnessDepth2ndMoment[oy][ox] = packNormalRoughnessDepth2ndMoment(packedNormalRoughnessDepth, specular2ndMoment);

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    packedIllumination1stMoment = 0;
    specular2ndMoment = 0;
    packedNormalRoughnessDepth = 0;

    if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
        {
            packedIllumination1stMoment = packIllumination1stMoment(gSpecularIlluminationLogLuv[int2(xx, yy)]);
            specular2ndMoment = gSpecular2ndMoment[int2(xx, yy)];
            packedNormalRoughnessDepth = gNormalRoughnessDepth[int2(xx, yy)];
        }
        sharedPackedIllumination1stMoment[oy][ox] = packedIllumination1stMoment;
        sharedPackedNormalRoughnessDepth2ndMoment[oy][ox] = packNormalRoughnessDepth2ndMoment(packedNormalRoughnessDepth, specular2ndMoment);
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    //
    // Shared memory is populated now and can be used for filtering
    //

    int2 sharedMemoryCenterIndex = groupThreadId.xy + int2(SKIRT, SKIRT);

    float historyLength = 255.0*gHistoryLength[ipos];

    float3 centerSpecularIllumination;
    float centerSpecular1stMoment;
    unpackIllumination1stMoment(sharedPackedIllumination1stMoment[sharedMemoryCenterIndex.y][sharedMemoryCenterIndex.x],
                                centerSpecularIllumination,
                                centerSpecular1stMoment);

    if (historyLength >= float(gHistoryThreshold))
    {
        // If we have enough temporal history available,
        // we pass illumination data unmodified
        // and calculate variance based on temporally accumulated moments
        float specular2ndMoment;
        unpack2ndMoment(sharedPackedNormalRoughnessDepth2ndMoment[sharedMemoryCenterIndex.y][sharedMemoryCenterIndex.x], specular2ndMoment);

        float specularVariance = specular2ndMoment - centerSpecular1stMoment * centerSpecular1stMoment;

        gOutSpecularIlluminationAndVariance[ipos] = float4(centerSpecularIllumination, specularVariance);
    }
    else
    {
        float3 centerNormal;
        float centerRoughnessDontCare;
        float centerDepth;
        float centerSpecular2ndMoment;
        uint4 centerPackedNormalRoughnessDepth2ndMoment = sharedPackedNormalRoughnessDepth2ndMoment[sharedMemoryCenterIndex.y][sharedMemoryCenterIndex.x];
        unpackNormalRoughnessDepth2ndMoment(centerPackedNormalRoughnessDepth2ndMoment,
                                            centerNormal,
                                            centerRoughnessDontCare,
                                            centerDepth,
                                            centerSpecular2ndMoment);

        // Early out if linearZ is beyond denoising range
        [branch]
        if (centerDepth > gDenoisingRange)
        {
            return;
        }

        float sumWSpecularIllumination = 0;
        float3 sumSpecularIllumination = 0;

        float sumSpecular1stMoment = 0;
        float sumSpecular2ndMoment = 0;

        // Compute first and second moment spatially. This code also applies cross-bilateral
        // filtering on the input illumination.
        for (int yy = -2; yy <= 2; yy++)
        {
            for (int xx = -2; xx <= 2; xx++)
            {
                int2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT + xx, SKIRT + yy);

                // Fetching sample data
                float3 sampleNormal;
                float sampleRoughnessDontCare;
                float sampleDepth;
                float sampleSpecular2ndMoment;
                float sampleDiffuse2ndMoment;
                unpackNormalRoughnessDepth2ndMoment(sharedPackedNormalRoughnessDepth2ndMoment[sharedMemoryIndex.y][sharedMemoryIndex.x],
                                                     sampleNormal,
                                                     sampleRoughnessDontCare,
                                                     sampleDepth,
                                                     sampleSpecular2ndMoment);

                float3 sampleSpecularIllumination;
                float sampleSpecular1stMoment;
                unpackIllumination1stMoment(sharedPackedIllumination1stMoment[sharedMemoryIndex.y][sharedMemoryIndex.x],
                                   sampleSpecularIllumination,
                                   sampleSpecular1stMoment);

                // Calculating weights
                float depthW = 1.0;// TODO: should we take in account depth here?
                float normalW = computeNormalWeight(centerNormal, sampleNormal, gPhiNormal);

                float specularW = normalW * depthW;

                // Accumulating
                sumWSpecularIllumination += specularW;
                sumSpecularIllumination += sampleSpecularIllumination.rgb * specularW;
                sumSpecular1stMoment += sampleSpecular1stMoment * specularW;
                sumSpecular2ndMoment += sampleSpecular2ndMoment * specularW;
            }
        }

        // Clamp sum to >0 to avoid NaNs.
        sumWSpecularIllumination = max(sumWSpecularIllumination, 1e-6f);
        sumSpecularIllumination /= sumWSpecularIllumination;
        sumSpecular1stMoment /= sumWSpecularIllumination;
        sumSpecular2ndMoment /= sumWSpecularIllumination;

        // compute variance using the first and second moments
        float specularVariance = abs(sumSpecular2ndMoment - sumSpecular1stMoment * sumSpecular1stMoment);

        // give the variance a boost for the first frames
        float boost = max(1.0, 4.0 / (historyLength + 1.0));
        specularVariance *= boost;

        gOutSpecularIlluminationAndVariance[ipos] = float4(sumSpecularIllumination, specularVariance);
    }
}
