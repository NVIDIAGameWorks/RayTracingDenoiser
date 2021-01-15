/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE(cbuffer, globalConstants, b, 0, 0)
{
    int2 gResolution;
    uint gFireflyEnabled;
}

#include "RELAX_Common.hlsl"

// Inputs
NRI_RESOURCE(Texture2D<uint2>, gSpecularAndDiffuseIlluminationLogLuv, t, 0, 0);
NRI_RESOURCE(Texture2D<uint2>, gNormalRoughnessDepth, t, 1, 0);

// Outputs
NRI_RESOURCE(RWTexture2D<uint2>, gOutSpecularAndDiffuseIlluminationLogLuv, u, 0, 0);
NRI_RESOURCE(RWTexture2D<float4>, gOutSpecularIllumination, u, 1, 0);
NRI_RESOURCE(RWTexture2D<float4>, gOutDiffuseIllumination, u, 2, 0);

groupshared uint4 sharedPackedIllumination[16 + 2][16 + 2];
groupshared float4 sharedPackedZAndNormal[16 + 2][16 + 2];

//---------------------
// Helper functions
//---------------------

static const float c_floatMax = 3.402823466e+38f;

// Unpacking from LogLuv to RGB is expensive, so let's do it
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

float4 repackNormalAndDepth(uint2 packedNormalRoughnessDepth)
{
    float3 normal;
    float roughnessDontCare;
    float depth;
    UnpackNormalRoughnessDepth(normal, roughnessDontCare, depth, packedNormalRoughnessDepth);
    return float4(normal, depth);
}

void unpackNormalAndDepth(float4 packed, out float3 normal, out float depth)
{
    normal = packed.rgb;
    depth = packed.a;
}

float edgeStoppingDepth(float centerDepth, float sampleDepth)
{
    return (abs(centerDepth - sampleDepth) / (centerDepth + 1e-6)) < 0.1 ? 1.0 : 0.0;
}

//---------------------
// Firefly filters
//---------------------

void PopulateSharedMemoryForFirefly(uint2 dispatchThreadId, uint2 groupThreadId, uint2 groupId)
{
    // Renumerating threads to load 18x18 (16+2 x 16+2) block of data to shared memory
	//
	// The preloading will be done in two stages:
	// at the first stage the group will load 16x16 / 18 = 14.2 rows of the shared memory,
	// and all threads in the group will be following the same path.
	// At the second stage, the rest 18x18 - 16x16 = 68 threads = 2.125 warps will load the rest of data

	uint linearThreadIndex = groupThreadId.y * 16 + groupThreadId.x;
	uint newIdxX = linearThreadIndex % 18;
	uint newIdxY = linearThreadIndex / 18;

    uint blockXStart = groupId.x * 16;
    uint blockYStart = groupId.y * 16;

	// First stage
	uint ox = newIdxX;
	uint oy = newIdxY;
	int xx = blockXStart + newIdxX - 1;
	int yy = blockYStart + newIdxY - 1;

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
	linearThreadIndex += 16 * 16;
	newIdxX = linearThreadIndex % 18;
	newIdxY = linearThreadIndex / 18;

	ox = newIdxX;
	oy = newIdxY;
	xx = blockXStart + newIdxX - 1;
	yy = blockYStart + newIdxY - 1;

    packedIllumination = 0;
    packedNormalAndDepth = 0;

  	if (linearThreadIndex < 18 * 18)
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
void runRCRS(int2 dispatchThreadId, int2 groupThreadId, out float3 outSpecular, out float3 outDiffuse)
{
    // Fetching center data
    uint2 sharedMemoryIndex = groupThreadId.xy + int2(1,1);

    float3 normalCenter;
    float depthCenter;
    unpackNormalAndDepth(sharedPackedZAndNormal[sharedMemoryIndex.y][sharedMemoryIndex.x], normalCenter, depthCenter);

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
            int2 sharedMemoryIndexSample = groupThreadId.xy + int2(1,1) + int2(xx,yy);

            if ((xx == 0) && (yy == 0)) continue;
            if ((p.x < 0) || (p.x >= gResolution.x)) continue;
            if ((p.y < 0) || (p.y >= gResolution.y)) continue;

            // Fetching sample data
            float3 normalSample;
            float depthSample;
            unpackNormalAndDepth(sharedPackedZAndNormal[sharedMemoryIndexSample.y][sharedMemoryIndexSample.x], normalSample, depthSample);

            float3 specularIlluminationSample;
            float3 diffuseIlluminationSample;
            unpackIllumination(sharedPackedIllumination[sharedMemoryIndexSample.y][sharedMemoryIndexSample.x], specularIlluminationSample, diffuseIlluminationSample);

            float specularLuminanceSample = STL::Color::Luminance(specularIlluminationSample);
            float diffuseLuminanceSample = STL::Color::Luminance(diffuseIlluminationSample);

            // Applying weights
            // ..normal weight
            float weight = dot(normalCenter, normalSample) > 0.99 ? 1.0 : 0.0;

            // ..depth weight
            weight *= edgeStoppingDepth(depthCenter, depthSample);

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

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    // Populating shared memory for firefly filter
    PopulateSharedMemoryForFirefly(dispatchThreadId.xy, groupThreadId.xy, groupId.xy);

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    // Shared memory is populated now and can be used for filtering
    if (any(int2(dispatchThreadId.xy) >= gResolution)) return;

    // Running firefly filter
    float3 outSpecularIllumination;
    float3 outDiffuseIllumination;

    if (gFireflyEnabled > 0)
    {
        runRCRS(dispatchThreadId.xy, groupThreadId.xy, outSpecularIllumination, outDiffuseIllumination);
    }
    else
    {
        // No firefly filter, passing data from shared memory without modification
        uint2 sharedMemoryIndex = groupThreadId.xy + int2(1,1);
        unpackIllumination(sharedPackedIllumination[sharedMemoryIndex.y][sharedMemoryIndex.x], outSpecularIllumination, outDiffuseIllumination);
    }

    gOutSpecularAndDiffuseIlluminationLogLuv[dispatchThreadId.xy] = PackSpecularAndDiffuseToLogLuvUint2(outSpecularIllumination, outDiffuseIllumination);
    gOutSpecularIllumination[dispatchThreadId.xy] = float4(outSpecularIllumination, 0);
    gOutDiffuseIllumination[dispatchThreadId.xy] = float4(outDiffuseIllumination, 0);
}

