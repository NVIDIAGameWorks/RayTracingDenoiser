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
#include "RELAX_DiffuseSpecular_Firefly.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

#define THREAD_GROUP_SIZE 16
#define SKIRT 1

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 sharedDiffuseIllumination[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
groupshared float4 sharedSpecularIllumination[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
groupshared float4 sharedNormalAndViewZ[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

// Helper functions

float edgeStoppingDepth(float centerViewZ, float sampleViewZ)
{
    return (abs(centerViewZ - sampleViewZ) / (centerViewZ + 1e-6)) < 0.1 ? 1.0 : 0.0;
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

    float4 specularIllumination = 0;
    float4 diffuseIllumination = 0;
    float3 normal = 0;
    float viewZ = 0;

    if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
    {
        specularIllumination = gSpecularIllumination[int2(xx, yy)];
        diffuseIllumination = gDiffuseIllumination[int2(xx, yy)];
        normal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx,yy)]).rgb;
        viewZ = gViewZFP16[int2(xx, yy)] / NRD_FP16_VIEWZ_SCALE;
    }
    sharedSpecularIllumination[oy][ox] = specularIllumination;
    sharedDiffuseIllumination[oy][ox] = diffuseIllumination;
    sharedNormalAndViewZ[oy][ox] = float4(normal, viewZ);

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    specularIllumination = 0;
    diffuseIllumination = 0;
    normal = 0;
    viewZ = 0;

    if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
        {
            specularIllumination = gSpecularIllumination[int2(xx, yy)];
            diffuseIllumination = gDiffuseIllumination[int2(xx, yy)];
            normal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx, yy)]).rgb;
            viewZ = gViewZFP16[int2(xx, yy)] / NRD_FP16_VIEWZ_SCALE;
        }
        sharedSpecularIllumination[oy][ox] = specularIllumination;
        sharedDiffuseIllumination[oy][ox] = diffuseIllumination;
        sharedNormalAndViewZ[oy][ox] = float4(normal, viewZ);
    }
}

// Cross bilateral Rank-Conditioned Rank-Selection (RCRS) filter
void runRCRS(int2 dispatchThreadId, int2 groupThreadId, float3 centerNormal, float centerViewZ, out float4 outSpecular, out float4 outDiffuse)
{
    // Fetching center data
    uint2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT, SKIRT);

    float4 s = sharedSpecularIllumination[sharedMemoryIndex.y][sharedMemoryIndex.x];
    float4 d = sharedDiffuseIllumination[sharedMemoryIndex.y][sharedMemoryIndex.x];

    float3 specularIlluminationCenter = s.rgb;
    float3 diffuseIlluminationCenter = d.rgb;
    float specular2ndMomentCenter = s.a;
    float diffuse2ndMomentCenter = d.a;

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

    [unroll]
    for (int yy = -1; yy <= 1; yy++)
    {
        [unroll]
        for (int xx = -1; xx <= 1; xx++)
        {
            int2 p = dispatchThreadId.xy + int2(xx, yy);
            int2 sharedMemoryIndexSample = groupThreadId.xy + int2(SKIRT, SKIRT) + int2(xx,yy);

            if ((xx == 0) && (yy == 0)) continue;
            if ((p.x < 0) || (p.x >= gResolution.x)) continue;
            if ((p.y < 0) || (p.y >= gResolution.y)) continue;

            // Fetching sample data
            float4 v = sharedNormalAndViewZ[sharedMemoryIndexSample.y][sharedMemoryIndexSample.x];
            float3 sampleNormal = v.xyz;
            float sampleViewZ = v.w;

            float3 specularIlluminationSample = sharedSpecularIllumination[sharedMemoryIndexSample.y][sharedMemoryIndexSample.x].rgb;
            float3 diffuseIlluminationSample = sharedDiffuseIllumination[sharedMemoryIndexSample.y][sharedMemoryIndexSample.x].rgb;

            float specularLuminanceSample = STL::Color::Luminance(specularIlluminationSample);
            float diffuseLuminanceSample = STL::Color::Luminance(diffuseIlluminationSample);

            // Applying weights
            // ..normal weight
            float weight = dot(centerNormal, sampleNormal) > 0.99 ? 1.0 : 0.0;

            // ..depth weight
            weight *= edgeStoppingDepth(centerViewZ, sampleViewZ);

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
    outSpecular = float4(sharedSpecularIllumination[specularCoords.y][specularCoords.x].rgb, specular2ndMomentCenter);
    outDiffuse = float4(sharedDiffuseIllumination[diffuseCoords.y][diffuseCoords.x].rgb, diffuse2ndMomentCenter);
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadId, uint3 groupId : SV_GroupId)
{
    // Populating shared memory for firefly filter
    PopulateSharedMemoryForFirefly(dispatchThreadId.xy, groupThreadId.xy, groupId.xy);

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    // Shared memory is populated now and can be used for filtering
    float4 v = sharedNormalAndViewZ[groupThreadId.y + SKIRT][groupThreadId.x + SKIRT];
    float3 centerNormal = v.xyz;
    float centerViewZ = v.w;

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerViewZ > gDenoisingRange)
    {
        return;
    }

    // Running firefly filter
    float4 outSpecularIlluminationAnd2ndMoment;
    float4 outDiffuseIlluminationAnd2ndMoment;

    runRCRS(dispatchThreadId.xy, groupThreadId.xy, centerNormal, centerViewZ, outSpecularIlluminationAnd2ndMoment, outDiffuseIlluminationAnd2ndMoment);

    gOutSpecularIllumination[dispatchThreadId.xy] = outSpecularIlluminationAnd2ndMoment;
    gOutDiffuseIllumination[dispatchThreadId.xy] = outDiffuseIlluminationAnd2ndMoment;
}

