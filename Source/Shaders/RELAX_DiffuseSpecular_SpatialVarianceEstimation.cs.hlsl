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
#include "RELAX_DiffuseSpecular_SpatialVarianceEstimation.resources.hlsl"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsl"

#define THREAD_GROUP_SIZE 16
#define SKIRT 2

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared uint4 sharedPackedIllumination1stMoments        [THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
groupshared uint4 sharedPackedNormalRoughnessDepth2ndMoments[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

// Unpacking from LogLuv to RGB is expensive, so let's do it once,
// at the stage of populating the shared memory
uint4 packIllumination1stMoments(uint2 specularAndDiffuseIlluminationLogLuv)
{
    float3 specularIllum;
    float3 diffuseIllum;
    UnpackSpecularAndDiffuseFromLogLuvUint2(specularIllum, diffuseIllum, specularAndDiffuseIlluminationLogLuv);
    uint4 result;
    result.r = f32tof16(specularIllum.r) | f32tof16(specularIllum.g) << 16;
    result.g = f32tof16(specularIllum.b) | f32tof16(STL::Color::Luminance(specularIllum.rgb)) << 16;
    result.b = f32tof16(diffuseIllum.r) | f32tof16(diffuseIllum.g) << 16;
    result.a = f32tof16(diffuseIllum.b) | f32tof16(STL::Color::Luminance(diffuseIllum.rgb)) << 16;
    return result;
}

void unpackIllumination1stMoments(uint4 packedData, out float3 specularIllum, out float3 diffuseIllum, out float specular1stMoment, out float diffuse1stMoment)
{
    specularIllum.r = f16tof32(packedData.r);
    specularIllum.g = f16tof32(packedData.r >> 16);
    specularIllum.b = f16tof32(packedData.g);
    specular1stMoment = f16tof32(packedData.g >> 16);
    diffuseIllum.r = f16tof32(packedData.b);
    diffuseIllum.g = f16tof32(packedData.b >> 16);
    diffuseIllum.b = f16tof32(packedData.a);
    diffuse1stMoment = f16tof32(packedData.a >> 16);
}

uint4 packNormalRoughnessDepth2ndMoments(uint2 packedNormalRoughnessDepth, float2 specularAndDiffuse2ndMoments)
{
    uint4 result;
    result.rg = packedNormalRoughnessDepth.rg;
    result.b = f32tof16(specularAndDiffuse2ndMoments.r) | f32tof16(specularAndDiffuse2ndMoments.g) << 16;
    result.a = 0;
    return result;
}

void unpackNormalRoughnessDepth2ndMoments(uint4 packedData, out float3 normal, out float roughness, out float depth, out float specular2ndMoment, out float diffuse2ndMoment)
{
    UnpackNormalRoughnessDepth(normal, roughness, depth, packedData.rg);
    specular2ndMoment = f16tof32(packedData.b);
    diffuse2ndMoment = f16tof32(packedData.b >> 16);
}

void unpack2ndMoments(uint4 packed, out float specular2ndMoment, out float diffuse2ndMoment)
{
    specular2ndMoment = f16tof32(packed.b);
    diffuse2ndMoment = f16tof32(packed.b >> 16);
}

float computeDepthWeight(float depthCenter, float depthP, float phiDepth)
{
    return 1;
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

    uint4 packedIllumination1stMoments = 0;
    float2 specularAndDiffuse2ndMoments = 0;
    uint2 packedNormalRoughnessDepth = 0;

    if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
    {
        packedIllumination1stMoments = packIllumination1stMoments(gSpecularAndDiffuseIlluminationLogLuv[int2(xx,yy)]);
        specularAndDiffuse2ndMoments = gSpecularAndDiffuse2ndMoments[int2(xx, yy)];
        packedNormalRoughnessDepth = gNormalRoughnessDepth[int2(xx, yy)];
    }
    sharedPackedIllumination1stMoments[oy][ox] = packedIllumination1stMoments;
    sharedPackedNormalRoughnessDepth2ndMoments[oy][ox] = packNormalRoughnessDepth2ndMoments(packedNormalRoughnessDepth, specularAndDiffuse2ndMoments);

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    packedIllumination1stMoments = 0;
    specularAndDiffuse2ndMoments = 0;
    packedNormalRoughnessDepth = 0;

      if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
        {
            packedIllumination1stMoments = packIllumination1stMoments(gSpecularAndDiffuseIlluminationLogLuv[int2(xx, yy)]);
            specularAndDiffuse2ndMoments = gSpecularAndDiffuse2ndMoments[int2(xx, yy)];
            packedNormalRoughnessDepth = gNormalRoughnessDepth[int2(xx, yy)];
        }
        sharedPackedIllumination1stMoments[oy][ox] = packedIllumination1stMoments;
        sharedPackedNormalRoughnessDepth2ndMoments[oy][ox] = packNormalRoughnessDepth2ndMoments(packedNormalRoughnessDepth, specularAndDiffuse2ndMoments);
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    //
    // Shared memory is populated now and can be used for filtering
    //

    int2 sharedMemoryCenterIndex = groupThreadId.xy + int2(SKIRT, SKIRT);

    // Using diffuse history length for spatial variance estimation
    float historyLength = 255.0*gHistoryLength[ipos].y;

    float3 centerSpecularIllumination;
    float3 centerDiffuseIllumination;
    float centerSpecular1stMoment;
    float centerDiffuse1stMoment;
    unpackIllumination1stMoments(sharedPackedIllumination1stMoments[sharedMemoryCenterIndex.y][sharedMemoryCenterIndex.x],
                                centerSpecularIllumination,
                                centerDiffuseIllumination,
                                centerSpecular1stMoment,
                                centerDiffuse1stMoment);

    if (historyLength >= float(gHistoryThreshold))
    {
        // If we have enough temporal history available,
        // we pass illumination data unmodified
        // and calculate variance based on temporally accumulated moments
        float specular2ndMoment;
        float diffuse2ndMoment;
        unpack2ndMoments(sharedPackedNormalRoughnessDepth2ndMoments[sharedMemoryCenterIndex.y][sharedMemoryCenterIndex.x], specular2ndMoment, diffuse2ndMoment);

        float specularVariance = specular2ndMoment - centerSpecular1stMoment * centerSpecular1stMoment;
        float diffuseVariance = diffuse2ndMoment - centerDiffuse1stMoment * centerDiffuse1stMoment;

        gOutSpecularIlluminationAndVariance[ipos] = float4(centerSpecularIllumination, specularVariance);
        gOutDiffuseIlluminationAndVariance[ipos] = float4(centerDiffuseIllumination, diffuseVariance);
    }
    else
    {
        float3 centerNormal;
        float centerRoughnessDontCare;
        float centerDepth;
        float centerSpecular2ndMoment;
        float centerDiffuse2ndMoment;
        uint4 centerPackedNormalRoughnessDepth2ndMoments = sharedPackedNormalRoughnessDepth2ndMoments[sharedMemoryCenterIndex.y][sharedMemoryCenterIndex.x];
        unpackNormalRoughnessDepth2ndMoments(centerPackedNormalRoughnessDepth2ndMoments,
                                            centerNormal,
                                            centerRoughnessDontCare,
                                            centerDepth,
                                            centerSpecular2ndMoment,
                                            centerDiffuse2ndMoment);

        // Early out if linearZ is beyond denoising range
        [branch]
        if (centerDepth > gDenoisingRange)
        {
            return;
        }

        float sumWSpecularIllumination = 0;
        float3 sumSpecularIllumination = 0;

        float sumWDiffuseIllumination = 0;
        float3 sumDiffuseIllumination = 0;

        float sumSpecular1stMoment = 0;
        float sumDiffuse1stMoment = 0;
        float sumSpecular2ndMoment = 0;
        float sumDiffuse2ndMoment = 0;

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
                unpackNormalRoughnessDepth2ndMoments(sharedPackedNormalRoughnessDepth2ndMoments[sharedMemoryIndex.y][sharedMemoryIndex.x],
                                                     sampleNormal,
                                                     sampleRoughnessDontCare,
                                                     sampleDepth,
                                                     sampleSpecular2ndMoment,
                                                     sampleDiffuse2ndMoment );

                float3 sampleSpecularIllumination;
                float3 sampleDiffuseIllumination;
                float sampleSpecular1stMoment;
                float sampleDiffuse1stMoment;
                unpackIllumination1stMoments(sharedPackedIllumination1stMoments[sharedMemoryIndex.y][sharedMemoryIndex.x],
                                   sampleSpecularIllumination,
                                   sampleDiffuseIllumination,
                                   sampleSpecular1stMoment,
                                   sampleDiffuse1stMoment);

                // Calculating weights
                float depthW = 1.0;// TODO: should we take in account depth here?
                float normalW = computeNormalWeight(centerNormal, sampleNormal, gPhiNormal);

                float specularW = normalW * depthW;
                float diffuseW = normalW * depthW;

                // Accumulating
                sumWSpecularIllumination += specularW;
                sumSpecularIllumination += sampleSpecularIllumination.rgb * specularW;
                sumSpecular1stMoment += sampleSpecular1stMoment * specularW;
                sumSpecular2ndMoment += sampleSpecular2ndMoment * specularW;

                sumWDiffuseIllumination += diffuseW;
                sumDiffuseIllumination += sampleDiffuseIllumination.rgb * diffuseW;
                sumDiffuse1stMoment += sampleDiffuse1stMoment * diffuseW;
                sumDiffuse2ndMoment += sampleDiffuse2ndMoment * diffuseW;
            }
        }

        // Clamp sum to >0 to avoid NaNs.
        sumWSpecularIllumination = max(sumWSpecularIllumination, 1e-6f);
        sumWDiffuseIllumination = max(sumWDiffuseIllumination, 1e-6f);
        sumSpecularIllumination /= sumWSpecularIllumination;
        sumSpecular1stMoment /= sumWSpecularIllumination;
        sumSpecular2ndMoment /= sumWSpecularIllumination;

        sumWDiffuseIllumination = sumWDiffuseIllumination;
        sumDiffuseIllumination /= sumWDiffuseIllumination;
        sumDiffuse1stMoment /= sumWDiffuseIllumination;
        sumDiffuse2ndMoment /= sumWDiffuseIllumination;

        // compute variance using the first and second moments
        float specularVariance = abs(sumSpecular2ndMoment - sumSpecular1stMoment * sumSpecular1stMoment);
        float diffuseVariance = abs(sumDiffuse2ndMoment - sumDiffuse1stMoment * sumDiffuse1stMoment);

        // give the variance a boost for the first frames
        float boost = max(1.0, 4.0 / (historyLength + 1.0));
        specularVariance *= boost;
        diffuseVariance *= boost;

        gOutSpecularIlluminationAndVariance[ipos] = float4(sumSpecularIllumination, specularVariance);
        gOutDiffuseIlluminationAndVariance[ipos] = float4(sumDiffuseIllumination, diffuseVariance);
    }
}
