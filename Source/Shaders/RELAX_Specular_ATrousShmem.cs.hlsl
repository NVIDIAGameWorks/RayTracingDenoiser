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
#include "RELAX_Specular_ATrousShmem.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

#define THREAD_GROUP_SIZE 8
#define SKIRT 1

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 sharedSpecularIlluminationAndVariance[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
groupshared float4 sharedNormalRoughness[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
groupshared float4 sharedWorldPos[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
#if RELAX_USE_HAIR_AWARE_FILTERING
groupshared float sharedMaterialType[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
#endif

// Helper functions
float3 getCurrentWorldPos(int2 pixelPos, float depth)
{
    float2 uv = ((float2)pixelPos + float2(0.5, 0.5)) * gInvRectSize * 2.0 - 1.0;
    return depth * (gFrustumForward.xyz + gFrustumRight.xyz * uv.x - gFrustumUp.xyz * uv.y);
}

float2 getRoughnessWeightParams(float roughness0, float specularReprojectionConfidence)
{
    float a = 1.0 / (0.001 + 0.999 * roughness0 * (0.333 + gRoughnessEdgeStoppingRelaxation * (1.0 - specularReprojectionConfidence)));
    float b = roughness0 * a;
    return float2(a, b);
}

float getRoughnessWeight(float2 params0, float roughness)
{
    return saturate(1.0 - abs(params0.y - roughness * params0.x));
}

// computes a 3x3 gaussian blur of the variance, centered around
// the current pixel
void computeVariance(int2 groupThreadId, out float specularVariance)
{
    float specularSum = 0;
    float diffuseSum = 0;

    static const float kernel[2][2] =
    {
        { 1.0 / 4.0, 1.0 / 8.0  },
        { 1.0 / 8.0, 1.0 / 16.0 }
    };

    const int radius = 1;
    for (int yy = -radius; yy <= radius; yy++)
    {
        for (int xx = -radius; xx <= radius; xx++)
        {
            int2 sharedMemoryIndex = groupThreadId.xy + int2(1 + xx, 1 + yy);
            float4 specularIlluminationAndVariance = sharedSpecularIlluminationAndVariance[sharedMemoryIndex.y][sharedMemoryIndex.x];
            float k = kernel[abs(xx)][abs(yy)];
            specularSum += specularIlluminationAndVariance.a * k;
        }
    }
    specularVariance = specularSum;
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
NRD_EXPORT void NRD_CS_MAIN(int2 ipos : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadId, uint3 groupId : SV_GroupId)
{
    // Populating shared memory
    //
    // Renumerating threads to load 18x18 (16+2 x 16+2) block of data to shared memory
    //
    // The preloading will be done in two stages:
    // at the first stage the group will load 16x16 / 18 = 14.2 rows of the shared memory,
    // and all threads in the group will be following the same path.
    // At the second stage, the rest 18x18 - 16x16 = 68 threads = 2.125 warps will load the rest of data

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

    float4 specularIlluminationAndVariance = 0;
    float3 normal = 0;
    float roughness = 1.0;
    float4 worldPos = 0;
    float viewZ = 0.0;
    float materialType = 1.0;

    if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
    {
        specularIlluminationAndVariance = gSpecularIlluminationAndVariance[int2(xx, yy)];
        float4 normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx, yy)], materialType);
        normal = normalRoughness.rgb;
        roughness = normalRoughness.a;
        viewZ = gViewZFP16[int2(xx, yy)] / NRD_FP16_VIEWZ_SCALE;
        worldPos = float4(getCurrentWorldPos(int2(xx, yy), viewZ), viewZ);
    }
    sharedSpecularIlluminationAndVariance[oy][ox] = specularIlluminationAndVariance;
    sharedNormalRoughness[oy][ox] = float4(normal, roughness);
    sharedWorldPos[oy][ox] = worldPos;
#if (NRD_USE_MATERIAL_ID_AWARE_FILTERING == 1)
    sharedMaterialType[oy][ox] = materialType;
#endif

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    specularIlluminationAndVariance = 0;
    normal = 0;
    roughness = 1.0;
    worldPos = 0;
    viewZ = 0.0;
    materialType = 1.0;

    if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
        {
            specularIlluminationAndVariance = gSpecularIlluminationAndVariance[int2(xx, yy)];
            float4 normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx, yy)], materialType);
            normal = normalRoughness.rgb;
            roughness = normalRoughness.a;
            viewZ = gViewZFP16[int2(xx, yy)] / NRD_FP16_VIEWZ_SCALE;
            worldPos = float4(getCurrentWorldPos(int2(xx, yy), viewZ), viewZ);
        }
        sharedSpecularIlluminationAndVariance[oy][ox] = specularIlluminationAndVariance;
        sharedNormalRoughness[oy][ox] = float4(normal, roughness);
        sharedWorldPos[oy][ox] = worldPos;
#if (NRD_USE_MATERIAL_ID_AWARE_FILTERING == 1)
        sharedMaterialType[oy][ox] = materialType;
#endif
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    //
    // Shared memory is populated now and can be used for filtering
    //
    uint2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT, SKIRT);

    // Fetching center data
    float4 centerWorldPosAndViewZ = sharedWorldPos[sharedMemoryIndex.y][sharedMemoryIndex.x];
    float3 centerWorldPos = centerWorldPosAndViewZ.xyz;
    float centerViewZ = centerWorldPosAndViewZ.w;
    float3 centerV = normalize(centerWorldPos);

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerViewZ > gDenoisingRange)
    {
        return;
    }

    float3 centerNormal = sharedNormalRoughness[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb;
    float specularReprojectionConfidence = gSpecularReprojectionConfidence[ipos];

    float4 centerSpecularIlluminationAndVariance = sharedSpecularIlluminationAndVariance[sharedMemoryIndex.y][sharedMemoryIndex.x];


    // Calculating center luminance
    float centerSpecularLuminance = STL::Color::Luminance(centerSpecularIlluminationAndVariance.rgb);

    // Center roughness
    float centerRoughness = sharedNormalRoughness[sharedMemoryIndex.y][sharedMemoryIndex.x].a;
    float2 roughnessWeightParams = getRoughnessWeightParams(centerRoughness, specularReprojectionConfidence);

    float2 normalWeightParams = GetNormalWeightParams_ATrous(centerRoughness, 255.0 * gHistoryLength[ipos], specularReprojectionConfidence, gNormalEdgeStoppingRelaxation, gSpecularLobeAngleFraction);

#if RELAX_USE_HAIR_AWARE_FILTERING
    float centerMaterialType = sharedMaterialType[sharedMemoryIndex.y][sharedMemoryIndex.x];
#endif

    // Calculating variance, filtered using 3x3 gaussin blur
    float centerSpecularVar;
    computeVariance(groupThreadId.xy, centerSpecularVar);

    float specularPhiLIllumination = 1.0e-4 + gSpecularPhiLuminance * sqrt(max(0.0, centerSpecularVar));
    float phiDepth = gPhiDepth;

    float sumWSpecular = 0;
    float4 sumSpecularIlluminationAndVariance = 0;

    static const float kernelWeightGaussian3x3[2] = { 0.44198, 0.27901 };

    [unroll]
    for (int cy = -1; cy <= 1; cy++)
    {
        [unroll]
        for (int cx = -1; cx <= 1; cx++)
        {
            const float kernel = kernelWeightGaussian3x3[abs(cx)] * kernelWeightGaussian3x3[abs(cy)];
            const int2 p = ipos + int2(cx, cy);
            const bool isInside = all(p >= int2(0, 0)) && all(p < gResolution);
            const bool isCenter = ((cx == 0) && (cy == 0));

            int2 sampleSharedMemoryIndex = groupThreadId.xy + int2(SKIRT + cx, SKIRT + cy);

            float4 sampleNormalRoughness = sharedNormalRoughness[sampleSharedMemoryIndex.y][sampleSharedMemoryIndex.x];
            float3 sampleNormal = sampleNormalRoughness.rgb;
            float sampleRoughness = sampleNormalRoughness.a;
            float3 sampleWorldPos = sharedWorldPos[sampleSharedMemoryIndex.y][sampleSharedMemoryIndex.x].rgb;

#if RELAX_USE_HAIR_AWARE_FILTERING
            float sampleMaterialType = sharedMaterialType[sampleSharedMemoryIndex.y][sampleSharedMemoryIndex.x];
#endif

            float4 sampleSpecularIlluminationAndVariance = sharedSpecularIlluminationAndVariance[sampleSharedMemoryIndex.y][sampleSharedMemoryIndex.x];

            float sampleSpecularLuminance = STL::Color::Luminance(sampleSpecularIlluminationAndVariance.rgb);

            // Calculating geometry and normal weights
            float geometryW = exp_approx(-GetGeometryWeight(centerWorldPos, centerNormal, centerViewZ, sampleWorldPos, phiDepth));

#if RELAX_USE_HAIR_AWARE_FILTERING
            geometryW *= (sampleMaterialType == centerMaterialType) ? 1.0 : 0.0;
#endif

            float normalWSpecular = GetSpecularNormalWeight_ATrous(normalWeightParams, gSpecularLobeAngleSlack, centerNormal, sampleNormal);
            float normalWDiffuse = GetDiffuseNormalWeight_ATrous(centerNormal, sampleNormal, gPhiNormal);

            // Calculating luminande weigths
            float specularLuminanceW = abs(centerSpecularLuminance - sampleSpecularLuminance) / specularPhiLIllumination;
            float relaxation = lerp(1.0, specularReprojectionConfidence, gLuminanceEdgeStoppingRelaxation);
            specularLuminanceW *= relaxation;
            specularLuminanceW = min(gMaxLuminanceRelativeDifference, specularLuminanceW);

            // Roughness weight for specular
            float specularRoughnessW = getRoughnessWeight(roughnessWeightParams, sampleRoughness);

            float wSpecular = kernel;
            if (!isCenter)
            {
                // Calculating bilateral weight for specular
                wSpecular = exp_approx(-geometryW - specularLuminanceW);
                wSpecular *= gRoughnessEdgeStoppingEnabled ? (normalWSpecular * specularRoughnessW) : normalWDiffuse;
                wSpecular = kernel * max(1e-6, wSpecular);
            }

            // Discarding out of screen samples
            wSpecular *= isInside ? 1.0 : 0.0;

            // alpha channel contains the variance, therefore the weights need to be squared
            sumWSpecular += wSpecular;
            sumSpecularIlluminationAndVariance += float4(wSpecular.xxx, wSpecular * wSpecular) * sampleSpecularIlluminationAndVariance;
        }
    }

    float4 filteredSpecularIlluminationAndVariance = float4(sumSpecularIlluminationAndVariance / float4(sumWSpecular.xxx, sumWSpecular * sumWSpecular));

    gOutSpecularIlluminationAndVariance[ipos] = filteredSpecularIlluminationAndVariance;
}
