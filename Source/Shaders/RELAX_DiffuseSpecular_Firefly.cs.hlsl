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
#include "RELAX_DiffuseSpecular_Firefly.resources.hlsl"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsl"

#define THREAD_GROUP_SIZE 16
#define SKIRT 1

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared uint4 sharedPackedIllumination[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
groupshared float4 sharedPackedZAndNormal[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

// Helper functions
uint4 repackIllumination(uint2 specularAndDiffuseIlluminationLogLuv)
{
    float3 specularIllum;
    float3 diffuseIllum;
    UnpackSpecularAndDiffuseFromLogLuvUint2(specularIllum, diffuseIllum, specularAndDiffuseIlluminationLogLuv);
    uint4 result;
    result.r = f32tof16(specularIllum.r) | f32tof16(specularIllum.g) << 16;
    result.g = f32tof16(specularIllum.b);
    result.b = f32tof16(diffuseIllum.r) | f32tof16(diffuseIllum.g) << 16;
    result.a = f32tof16(diffuseIllum.b);
    return result;
}

void unpackIllumination(uint4 packedData, out float3 specularIllum, out float3 diffuseIllum)
{
    specularIllum.r = f16tof32(packedData.r);
    specularIllum.g = f16tof32(packedData.r >> 16);
    specularIllum.b = f16tof32(packedData.g);
    diffuseIllum.r = f16tof32(packedData.b);
    diffuseIllum.g = f16tof32(packedData.b >> 16);
    diffuseIllum.b = f16tof32(packedData.a);
}

float4 repackNormalAndDepth(uint2 packedNormalRoughnessDepth)
{
    float3 normal;
    float roughnessDontCare;
    float depth;
    UnpackNormalRoughnessDepth(normal, roughnessDontCare, depth, packedNormalRoughnessDepth);
    return float4(normal, depth);
}

void unpackNormalAndDepth(float4 packedData, out float3 normal, out float depth)
{
    normal = packedData.rgb;
    depth = packedData.a;
}

float edgeStoppingDepth(float centerDepth, float sampleDepth)
{
    return (abs(centerDepth - sampleDepth) / (centerDepth + 1e-6)) < 0.1 ? 1.0 : 0.0;
}

void PopulateSharedMemoryForFirefly(uint2 dispatchThreadId, uint2 groupThreadId, uint2 groupId)
{
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

    uint4 packedIllumination = 0;
    float4 packedNormalAndDepth = 0;

    if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
    {
        packedIllumination = repackIllumination(gSpecularAndDiffuseIlluminationLogLuv[int2(xx,yy)]);
        packedNormalAndDepth = repackNormalAndDepth(gNormalRoughnessDepth[int2(xx,yy)]);
    }
    sharedPackedIllumination[oy][ox] = packedIllumination;
    sharedPackedZAndNormal[oy][ox] = packedNormalAndDepth;

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    packedIllumination = 0;
    packedNormalAndDepth = 0;

      if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
        {
            packedIllumination = repackIllumination(gSpecularAndDiffuseIlluminationLogLuv[int2(xx,yy)]);
            packedNormalAndDepth = repackNormalAndDepth(gNormalRoughnessDepth[int2(xx,yy)]);
        }
        sharedPackedIllumination[oy][ox] = packedIllumination;
        sharedPackedZAndNormal[oy][ox] = packedNormalAndDepth;
    }
}

// Cross bilateral Rank-Conditioned Rank-Selection (RCRS) filter
void runRCRS(int2 dispatchThreadId, int2 groupThreadId, float3 centerNormal, float centerDepth, out float3 outSpecular, out float3 outDiffuse)
{
    // Fetching center data
    uint2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT, SKIRT);

    float3 specularIlluminationCenter;
    float3 diffuseIlluminationCenter;
    unpackIllumination(sharedPackedIllumination[sharedMemoryIndex.y][sharedMemoryIndex.x], specularIlluminationCenter, diffuseIlluminationCenter);

    float specularLuminanceCenter = STL::Color::Luminance(specularIlluminationCenter);
    float diffuseLuminanceCenter = STL::Color::Luminance(diffuseIlluminationCenter);

    // Finding max and min luminance in neighborhood, rejecting samples that don't belong to center pixel's surface
    float maxSpecularLuminance = -1.0;
    float minSpecularLuminance = 1.0e6;
    int2 maxSpecularLuminanceCoords = sharedMemoryIndex;
    int2 minSpecularLuminanceCoords = sharedMemoryIndex;

    float maxDiffuseLuminance = -1.0;
    float minDiffuseLuminance = 1.0e6;
    int2 maxDiffuseLuminanceCoords = sharedMemoryIndex;
    int2 minDiffuseLuminanceCoords = sharedMemoryIndex;

    for (int yy = -1; yy <= 1; yy++)
    {
        for (int xx = -1; xx <= 1; xx++)
        {
            int2 p = dispatchThreadId.xy + int2(xx, yy);
            int2 sharedMemoryIndexSample = groupThreadId.xy + int2(SKIRT, SKIRT) + int2(xx,yy);

            if ((xx == 0) && (yy == 0)) continue;
            if ((p.x < 0) || (p.x >= gResolution.x)) continue;
            if ((p.y < 0) || (p.y >= gResolution.y)) continue;

            // Fetching sample data
            float3 sampleNormal;
            float sampleDepth;
            unpackNormalAndDepth(sharedPackedZAndNormal[sharedMemoryIndexSample.y][sharedMemoryIndexSample.x], sampleNormal, sampleDepth);

            float3 specularIlluminationSample;
            float3 diffuseIlluminationSample;
            unpackIllumination(sharedPackedIllumination[sharedMemoryIndexSample.y][sharedMemoryIndexSample.x], specularIlluminationSample, diffuseIlluminationSample);

            float specularLuminanceSample = STL::Color::Luminance(specularIlluminationSample);
            float diffuseLuminanceSample = STL::Color::Luminance(diffuseIlluminationSample);

            // Applying weights
            // ..normal weight
            float weight = dot(centerNormal, sampleNormal) > 0.99 ? 1.0 : 0.0;

            // ..depth weight
            weight *= edgeStoppingDepth(centerDepth, sampleDepth);

            if(weight > 0)
            {
                if(specularLuminanceSample > maxSpecularLuminance)
                {
                    maxSpecularLuminance = specularLuminanceSample;
                    maxSpecularLuminanceCoords = sharedMemoryIndexSample;
                }
                if(specularLuminanceSample < minSpecularLuminance)
                {
                    minSpecularLuminance = specularLuminanceSample;
                    minSpecularLuminanceCoords = sharedMemoryIndexSample;
                }

                if(diffuseLuminanceSample > maxDiffuseLuminance)
                {
                    maxDiffuseLuminance = diffuseLuminanceSample;
                    maxDiffuseLuminanceCoords = sharedMemoryIndexSample;
                }
                if(diffuseLuminanceSample < minDiffuseLuminance)
                {
                    minDiffuseLuminance = diffuseLuminanceSample;
                    minDiffuseLuminanceCoords = sharedMemoryIndexSample;
                }

            }
        }
    }

    // Replacing current value with min or max in the neighborhood if outside min..max range,
    // or leaving sample as it is if it's within the range
    int2 specularCoords = sharedMemoryIndex;
    int2 diffuseCoords = sharedMemoryIndex;

    if(specularLuminanceCenter > maxSpecularLuminance)
    {
        specularCoords = maxSpecularLuminanceCoords;
    }
    if(specularLuminanceCenter < minSpecularLuminance)
    {
        specularCoords = minSpecularLuminanceCoords;
    }

    if(diffuseLuminanceCenter > maxDiffuseLuminance)
    {
        diffuseCoords = maxDiffuseLuminanceCoords;
    }
    if(diffuseLuminanceCenter < minDiffuseLuminance)
    {
        diffuseCoords = minDiffuseLuminanceCoords;
    }

    float3 dontcare;
    unpackIllumination(sharedPackedIllumination[specularCoords.y][specularCoords.x], outSpecular, dontcare);
    unpackIllumination(sharedPackedIllumination[diffuseCoords.y][diffuseCoords.x], dontcare, outDiffuse);
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    // Populating shared memory for firefly filter
    PopulateSharedMemoryForFirefly(dispatchThreadId.xy, groupThreadId.xy, groupId.xy);

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    // Shared memory is populated now and can be used for filtering
    float3 centerNormal;
    float centerDepth;
    unpackNormalAndDepth(sharedPackedZAndNormal[groupThreadId.y + SKIRT][groupThreadId.x + SKIRT], centerNormal, centerDepth);

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerDepth > gDenoisingRange)
    {
        return;
    }

    // Running firefly filter
    float3 outSpecularIllumination;
    float3 outDiffuseIllumination;

    if (gFireflyEnabled > 0)
    {
        runRCRS(dispatchThreadId.xy, groupThreadId.xy, centerNormal, centerDepth, outSpecularIllumination, outDiffuseIllumination);
    }
    else
    {
        // No firefly filter, passing data from shared memory without modification
        unpackIllumination(sharedPackedIllumination[groupThreadId.y + SKIRT][groupThreadId.x + SKIRT], outSpecularIllumination, outDiffuseIllumination);
    }

    gOutSpecularAndDiffuseIlluminationLogLuv[dispatchThreadId.xy] = PackSpecularAndDiffuseToLogLuvUint2(outSpecularIllumination, outDiffuseIllumination);
    gOutSpecularIllumination[dispatchThreadId.xy] = float4(outSpecularIllumination, 0);
    gOutDiffuseIllumination[dispatchThreadId.xy] = float4(outDiffuseIllumination, 0);
}

