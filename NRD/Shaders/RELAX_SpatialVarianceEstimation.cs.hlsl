/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE(cbuffer, globalConstants, b, 0, 0)
{
    int2       gResolution;

    float      gPhiNormal;
    uint       gHistoryThreshold;
};

#include "RELAX_Common.hlsl"

// Inputs
NRI_RESOURCE(Texture2D<uint2>, gSpecularAndDiffuseIlluminationLogLuv, t, 0, 0);
NRI_RESOURCE(Texture2D<float4>, gSpecularAndDiffuseMoments, t, 1, 0);
NRI_RESOURCE(Texture2D<float>, gHistoryLength, t, 2, 0);
NRI_RESOURCE(Texture2D<uint2>, gNormalRoughnessDepth, t, 3, 0);

// Outputs
NRI_RESOURCE(RWTexture2D<float4>, gOutSpecularIlluminationAndVariance, u, 0, 0);
NRI_RESOURCE(RWTexture2D<float4>, gOutDiffuseIlluminationAndVariance, u, 1, 0);

groupshared uint4       sharedPackedIllumination                [16 + 2 + 2][16 + 2 + 2];
groupshared uint4       sharedPackedMomentsNormalRoughnessDepth [16 + 2 + 2][16 + 2 + 2];

// Unpacking from LogLuv to RGB is expensive, so let's do it once,
// at the stage of populating the shared memory
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

void unpackIllumination(uint4 packed, out float3 specularIllum, out float3 diffuseIllum)
{
	specularIllum.r = f16tof32(packed.r);
	specularIllum.g = f16tof32(packed.r >> 16);
    specularIllum.b = f16tof32(packed.g);
	diffuseIllum.r = f16tof32(packed.b);
	diffuseIllum.g = f16tof32(packed.b >> 16);
    diffuseIllum.b = f16tof32(packed.a);
}

uint4 packMomentsNormalRoughnessDepth(float4 specularAndDiffuseMoments, uint2 packedNormalRoughnessDepth)
{
	uint4 result;
	result.r = f32tof16(specularAndDiffuseMoments.r) | f32tof16(specularAndDiffuseMoments.g) << 16;
	result.g = f32tof16(specularAndDiffuseMoments.b) | f32tof16(specularAndDiffuseMoments.a) << 16;
	result.b = packedNormalRoughnessDepth.r;
	result.a = packedNormalRoughnessDepth.g;
	return result;
}

void unpackMomentsNormalRoughnessDepth(uint4 packed, out float4 specularAndDiffuseMoments, out float3 normal, out float roughness, out float depth)
{
	specularAndDiffuseMoments.r = f16tof32(packed.r);
	specularAndDiffuseMoments.g = f16tof32(packed.r >> 16);
	specularAndDiffuseMoments.b = f16tof32(packed.g);
	specularAndDiffuseMoments.a = f16tof32(packed.g >> 16);
    UnpackNormalRoughnessDepth(normal, roughness, depth, packed.ba);
}

float computeDepthWeight(float depthCenter, float depthP, float phiDepth)
{
    return (phiDepth == 0) ? 0.0f : abs(depthCenter - depthP) / phiDepth;
}

float computeNormalWeight(float3 normalCenter, float3 normalP, float phiNormal)
{
    return pow(saturate(dot(normalCenter, normalP)), phiNormal);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    //if (any(dispatchThreadId.xy >= gResolution)) return;
    const int2 ipos = dispatchThreadId.xy;

    // Populating shared memory
    //
	// Renumerating threads to load 20x20 (16+2+2 x 16+2+2) block of data to shared memory
	//
	// The preloading will be done in two stages:
	// at the first stage the group will load 16x16 / 10 = 12.8 rows of the shared memory,
	// and all threads in the group will be following the same path.
	// At the second stage, the rest 20x20 - 16x16 = 144 threads = 4.5 warps will load the rest of data

	uint linearThreadIndex = groupThreadId.y * 16 + groupThreadId.x;
    uint newIdxX = linearThreadIndex % 20;
    uint newIdxY = linearThreadIndex / 20;

    uint blockXStart = groupId.x * 16;
    uint blockYStart = groupId.y * 16;

	// First stage
	int ox = newIdxX;
	int oy = newIdxY;
	int xx = blockXStart + newIdxX - 2;
	int yy = blockYStart + newIdxY - 2;

    uint4 packedIllumination = 0;
    float4 specularAndDiffuseMoments = 0;
    uint2 packedNormalRoughnessDepth = 0;

	if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
	{
        packedIllumination = repackIllumination(gSpecularAndDiffuseIlluminationLogLuv[int2(xx,yy)]);
        specularAndDiffuseMoments = gSpecularAndDiffuseMoments[int2(xx, yy)];
        packedNormalRoughnessDepth = gNormalRoughnessDepth[int2(xx, yy)];
	}
    sharedPackedIllumination[oy][ox] = packedIllumination;
    sharedPackedMomentsNormalRoughnessDepth[oy][ox] = packMomentsNormalRoughnessDepth(specularAndDiffuseMoments, packedNormalRoughnessDepth);

	// Second stage
	linearThreadIndex += 16 * 16;
	newIdxX = linearThreadIndex % 20;
	newIdxY = linearThreadIndex / 20;

	ox = newIdxX;
	oy = newIdxY;
	xx = blockXStart + newIdxX - 2;
	yy = blockYStart + newIdxY - 2;

    packedIllumination = 0;
    specularAndDiffuseMoments = 0;
    packedNormalRoughnessDepth = 0;

  	if (linearThreadIndex < 20 * 20)
	{
        if ((xx >= 0) && (yy >= 0) && (xx < gResolution.x) && (yy < gResolution.y))
	    {
            packedIllumination = repackIllumination(gSpecularAndDiffuseIlluminationLogLuv[int2(xx,yy)]);
            specularAndDiffuseMoments = gSpecularAndDiffuseMoments[int2(xx, yy)];
            packedNormalRoughnessDepth = gNormalRoughnessDepth[int2(xx, yy)];
	    }
        sharedPackedIllumination[oy][ox] = packedIllumination;
        sharedPackedMomentsNormalRoughnessDepth[oy][ox] = packMomentsNormalRoughnessDepth(specularAndDiffuseMoments, packedNormalRoughnessDepth);
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    //
    // Shared memory is populated now and can be used for filtering
    //

    int2 sharedMemoryCenterIndex = groupThreadId.xy + int2(2,2);

    float historyLength = gHistoryLength[ipos].x;

    uint2 centerSpecularAndDiffuseIlluminationLogLuv = gSpecularAndDiffuseIlluminationLogLuv[ipos];
    float3 centerSpecularIllumination;
    float3 centerDiffuseIllumination;
    UnpackSpecularAndDiffuseFromLogLuvUint2(centerSpecularIllumination, centerDiffuseIllumination, centerSpecularAndDiffuseIlluminationLogLuv);

    if (historyLength >= float(gHistoryThreshold)) 
    {
        // If we have enough temporal history available,
        // we pass illumination data unmodified
        // and calculate variance based on temporally accumulated moments
        float4 specularAndDiffuseMoments = gSpecularAndDiffuseMoments[ipos];
        float specularVariance = specularAndDiffuseMoments.g - specularAndDiffuseMoments.r * specularAndDiffuseMoments.r;
        float diffuseVariance = specularAndDiffuseMoments.a - specularAndDiffuseMoments.b * specularAndDiffuseMoments.b;

        gOutSpecularIlluminationAndVariance[ipos] = float4(centerSpecularIllumination, specularVariance);
        gOutDiffuseIlluminationAndVariance[ipos] = float4(centerDiffuseIllumination, diffuseVariance);
    }
    else
    {
        float4 centerSpecularAndDiffuseMoments;
        float3 centerNormal;
        float centerRoughnessDontCare;
        float centerDepth;
        uint4 centerPackedMomentsNormalRoughnessDepth = sharedPackedMomentsNormalRoughnessDepth[sharedMemoryCenterIndex.y][sharedMemoryCenterIndex.x];
        unpackMomentsNormalRoughnessDepth(centerPackedMomentsNormalRoughnessDepth, centerSpecularAndDiffuseMoments, centerNormal, centerRoughnessDontCare, centerDepth);

        float phiDepth = 1.0;// max(zSlope, 1e-8) * 3.0;

        if (centerDepth <= 0)
        {
            // current pixel does not have valid depth => must be envmap => do nothing
            gOutSpecularIlluminationAndVariance[ipos] = float4(centerSpecularIllumination, 0);
            gOutDiffuseIlluminationAndVariance[ipos] = float4(centerDiffuseIllumination, 0);
            return;
        }
        
        float sumWSpecularIllumination = 0;
        float3 sumSpecularIllumination = 0;

        float sumWDiffuseIllumination = 0;
        float3 sumDiffuseIllumination = 0;

        float4 sumSpecularAndDiffuseMoments = 0;

        // Compute first and second moment spatially. This code also applies cross-bilateral
        // filtering on the input illumination.
        for (int yy = -2; yy <= 2; yy++)
        {
            for (int xx = -2; xx <= 2; xx++)
            {
                int2 sharedMemoryIndex = groupThreadId.xy + int2(2 + xx, 2 + yy);

                // Fetching sample data
                float4 sampleSpecularAndDiffuseMoments;
                float3 sampleNormal;
                float sampleRoughnessDontCare;
                float sampleDepth;
                uint4 samplePackedMomentsNormalRoughnessDepth = sharedPackedMomentsNormalRoughnessDepth[sharedMemoryIndex.y][sharedMemoryIndex.x];
                unpackMomentsNormalRoughnessDepth(samplePackedMomentsNormalRoughnessDepth, sampleSpecularAndDiffuseMoments, sampleNormal, sampleRoughnessDontCare, sampleDepth);

                float3 sampleSpecularIllumination;
                float3 sampleDiffuseIllumination;
                unpackIllumination(sharedPackedIllumination[sharedMemoryIndex.y][sharedMemoryIndex.x], sampleSpecularIllumination, sampleDiffuseIllumination);

                // Calculating weights
                float depthW = 1.0;// TODO!! computeDepthWeight(zCenter.x, zP, phiDepth * length(float2(xx, yy)));
                float normalW = computeNormalWeight(centerNormal, sampleNormal, gPhiNormal);

                float specularW = normalW;
                float diffuseW = normalW;

                // Accumulating
                sumWSpecularIllumination += specularW;
                sumSpecularIllumination += sampleSpecularIllumination.rgb * specularW;
                sumSpecularAndDiffuseMoments.xy += sampleSpecularAndDiffuseMoments.xy * specularW;

                sumWDiffuseIllumination += diffuseW;
                sumDiffuseIllumination += sampleDiffuseIllumination.rgb * diffuseW;
                sumSpecularAndDiffuseMoments.zw += sampleSpecularAndDiffuseMoments.zw * diffuseW;
            }
        }

        // Clamp sum to >0 to avoid NaNs.
        sumWSpecularIllumination = max(sumWSpecularIllumination, 1e-6f);
        sumSpecularIllumination /= sumWSpecularIllumination;
        sumSpecularAndDiffuseMoments.xy /= sumWSpecularIllumination;

        sumWDiffuseIllumination = max(sumWDiffuseIllumination, 1e-6f);
        sumDiffuseIllumination /= sumWDiffuseIllumination;
        sumSpecularAndDiffuseMoments.zw /= sumWDiffuseIllumination;

        // compute variance using the first and second moments
        float specularVariance = abs(sumSpecularAndDiffuseMoments.g - sumSpecularAndDiffuseMoments.r * sumSpecularAndDiffuseMoments.r);
        float diffuseVariance = abs(sumSpecularAndDiffuseMoments.a - sumSpecularAndDiffuseMoments.b * sumSpecularAndDiffuseMoments.b);

        // give the variance a boost for the first frames
        float boost = max(1.0, 4.0 / (historyLength + 1.0));
        specularVariance *= boost;
        diffuseVariance *= boost;

        gOutSpecularIlluminationAndVariance[ipos] = float4(sumSpecularIllumination, specularVariance);
        gOutDiffuseIlluminationAndVariance[ipos] = float4(sumDiffuseIllumination, diffuseVariance);
    }
}
