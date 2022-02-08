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
#include "RELAX_Specular_SpatialVarianceEstimation.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

#define THREAD_GROUP_SIZE 16
#define SKIRT 2

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 sharedSpecularAnd2ndMoment[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
groupshared float4 sharedNormalViewZ[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

float computeDepthWeight(float depthCenter, float depthP, float depthThreshold)
{
    return 1;
}

float computeNormalWeight(float3 normalCenter, float3 normalP, float phiNormal)
{
    return phiNormal == 0.0f ? 1.0f : pow(saturate(dot(normalCenter, normalP)), phiNormal);
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadId, uint3 groupId : SV_GroupId)
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

    float4 specular = 0;
    float3 normal = 0;
    float viewZ = 0;

    if ((xx >= 0) && (yy >= 0) && (xx < (int)gRectSize.x) && (yy < (int)gRectSize.y))
    {
        specular = gSpecularIllumination[int2(xx, yy)];
        normal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx, yy)]).rgb;
        viewZ = gViewZ[int2(xx, yy)] / NRD_FP16_VIEWZ_SCALE;
    }
    sharedSpecularAnd2ndMoment[oy][ox] = specular;
    sharedNormalViewZ[oy][ox] = float4(normal, viewZ);

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    specular = 0;
    normal = 0;
    viewZ = 0;

    if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if ((xx >= 0) && (yy >= 0) && (xx < (int)gRectSize.x) && (yy < (int)gRectSize.y))
        {
            specular = gSpecularIllumination[int2(xx, yy)];
            normal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx, yy)]).rgb;
            viewZ = gViewZ[int2(xx, yy)] / NRD_FP16_VIEWZ_SCALE;
        }
        sharedSpecularAnd2ndMoment[oy][ox] = specular;
        sharedNormalViewZ[oy][ox] = float4(normal, viewZ);
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    //
    // Shared memory is populated now and can be used for filtering
    //

    int2 sharedMemoryCenterIndex = groupThreadId.xy + int2(SKIRT, SKIRT);

    // Repacking normal and roughness to prev normal roughness to be used in the next frame
    float4 normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[ipos]);
    gOutNormalRoughness[ipos] = PackPrevNormalRoughness(normalRoughness);

    // Using diffuse history length for spatial variance estimation
    float historyLength = 255.0 * gHistoryLength[ipos];

    float4 centerSpecularAnd2ndMoment = sharedSpecularAnd2ndMoment[sharedMemoryCenterIndex.y][sharedMemoryCenterIndex.x];
    float3 centerSpecularIllumination = centerSpecularAnd2ndMoment.rgb;
    float centerSpecular1stMoment = STL::Color::Luminance(centerSpecularIllumination);
    float centerSpecular2ndMoment = centerSpecularAnd2ndMoment.a;

    [branch]
    if (historyLength >= float(gHistoryThreshold))
    {
        // If we have enough temporal history available,
        // we pass illumination data unmodified
        // and calculate variance based on temporally accumulated moments
        float specularVariance = centerSpecular2ndMoment - centerSpecular1stMoment * centerSpecular1stMoment;

        gOutSpecularIlluminationAndVariance[ipos] = float4(centerSpecularIllumination, specularVariance);
        return;
    }

    float4 centerNormalViewZ = sharedNormalViewZ[sharedMemoryCenterIndex.y][sharedMemoryCenterIndex.x];
    float3 centerNormal = centerNormalViewZ.xyz;
    float centerViewZ = centerNormalViewZ.a;

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerViewZ > gDenoisingRange)
    {
        return;
    }

    float sumWSpecularIllumination = 0;
    float3 sumSpecularIllumination = 0;

    float sumSpecular1stMoment = 0;
    float sumSpecular2ndMoment = 0;

    // Compute first and second moment spatially. This code also applies cross-bilateral
    // filtering on the input illumination.
    [unroll]
    for (int cy = -2; cy <= 2; cy++)
    {
        [unroll]
        for (int cx = -2; cx <= 2; cx++)
        {
            int2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT + cx, SKIRT + cy);

            // Fetching sample data
            float3 sampleNormal = sharedNormalViewZ[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb;

            float4 sampleSpecular = sharedSpecularAnd2ndMoment[sharedMemoryIndex.y][sharedMemoryIndex.x];
            float3 sampleSpecularIllumination = sampleSpecular.rgb;
            float sampleSpecular1stMoment = STL::Color::Luminance(sampleSpecularIllumination);
            float sampleSpecular2ndMoment = sampleSpecular.a;


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
