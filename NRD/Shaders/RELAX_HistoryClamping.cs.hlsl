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
    int2 gResolution;
    float gColorBoxSigmaScale;
}

#include "RELAX_Common.hlsl"

// Inputs
NRI_RESOURCE(Texture2D<uint2>, gSpecularAndDiffuseIlluminationLogLuv, t, 0, 0);
NRI_RESOURCE(Texture2D<uint2>, gSpecularAndDiffuseIlluminationResponsiveLogLuv, t, 1, 0);

// Outputs
NRI_RESOURCE(RWTexture2D<uint2>, gOutSpecularAndDiffuseIlluminationLogLuv, u, 0, 0);

groupshared uint4 sharedPackedResponsiveIlluminationYCgCo[16 + 2][16 + 2];

// Helper functions
uint4 packIllumination(float3 specularIllum, float3 diffuseIllum)
{
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

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{

    // Populating shared memory
    //
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

    float3 specularResponsiveIllumination = 0;
    float3 diffuseResponsiveIllumination = 0;

    if (xx >= 0 && yy >= 0 && xx < gResolution.x && yy < gResolution.y)
    {
        UnpackSpecularAndDiffuseFromLogLuvUint2(specularResponsiveIllumination, diffuseResponsiveIllumination, gSpecularAndDiffuseIlluminationResponsiveLogLuv[int2(xx,yy)]);
    }
    sharedPackedResponsiveIlluminationYCgCo[oy][ox] = packIllumination(_NRD_LinearToYCoCg(specularResponsiveIllumination), _NRD_LinearToYCoCg(diffuseResponsiveIllumination));

    // Second stage
    linearThreadIndex += 16 * 16;
    newIdxX = linearThreadIndex % 18;
    newIdxY = linearThreadIndex / 18;

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - 1;
    yy = blockYStart + newIdxY - 1;

    specularResponsiveIllumination = 0;
    diffuseResponsiveIllumination = 0;

    if (linearThreadIndex < 18 * 18)
    {
        if (xx >= 0 && yy >= 0 && xx < (int)gResolution.x && yy < (int)gResolution.y)
        {
            UnpackSpecularAndDiffuseFromLogLuvUint2(specularResponsiveIllumination, diffuseResponsiveIllumination, gSpecularAndDiffuseIlluminationResponsiveLogLuv[int2(xx,yy)]);
        }
        sharedPackedResponsiveIlluminationYCgCo[oy][ox] = packIllumination(_NRD_LinearToYCoCg(specularResponsiveIllumination), _NRD_LinearToYCoCg(diffuseResponsiveIllumination));
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    //
    // Shared memory is populated now and can be used for filtering
    //

    if (any(int2(dispatchThreadId.xy) >= gResolution)) return;

    uint2 sharedMemoryIndex = groupThreadId.xy + int2(1,1);

    float3 specularIllumination;
    float3 diffuseIllumination;
    UnpackSpecularAndDiffuseFromLogLuvUint2(specularIllumination, diffuseIllumination, gSpecularAndDiffuseIlluminationLogLuv[dispatchThreadId.xy]);

    float3 specularFirstMoment = 0;
    float3 specularSecondMoment = 0;
    float3 diffuseFirstMoment = 0;
    float3 diffuseSecondMoment = 0;

    [unroll] 
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll] 
        for (int dx = -1; dx <= 1; dx++)
        {
            uint2 sharedMemoryIndexP = sharedMemoryIndex + int2(dx, dy);
            int2 p = dispatchThreadId.xy + int2(dx,dy);
            if (p.x <= 0 || p.y <= 0 || p.x >= gResolution.x || p.y >= gResolution.y) sharedMemoryIndexP = sharedMemoryIndex;

            float3 specularIlluminationP;
            float3 diffuseIlluminationP;
            unpackIllumination(sharedPackedResponsiveIlluminationYCgCo[sharedMemoryIndexP.y][sharedMemoryIndexP.x], specularIlluminationP, diffuseIlluminationP);

            specularFirstMoment += specularIlluminationP;
            specularSecondMoment += specularIlluminationP * specularIlluminationP;

            diffuseFirstMoment += diffuseIlluminationP;
            diffuseSecondMoment += diffuseIlluminationP * diffuseIlluminationP;
        }
    }

    specularFirstMoment /= 9.0;
    specularSecondMoment /= 9.0;

    diffuseFirstMoment /= 9.0;
    diffuseSecondMoment /= 9.0;

    float3 specularSigma = sqrt(max(0.0f, specularSecondMoment - specularFirstMoment * specularFirstMoment));
    float3 specularColorMin = specularFirstMoment - gColorBoxSigmaScale * specularSigma;
    float3 specularColorMax = specularFirstMoment + gColorBoxSigmaScale * specularSigma;

    float3 specularIlluminationYCgCo = _NRD_LinearToYCoCg(specularIllumination);
    specularIlluminationYCgCo = clamp(specularIlluminationYCgCo, specularColorMin, specularColorMax);
    specularIllumination = _NRD_YCoCgToLinear(specularIlluminationYCgCo);

    float3 diffuseSigma = sqrt(max(0.0f, diffuseSecondMoment - diffuseFirstMoment * diffuseFirstMoment));
    float3 diffuseColorMin = diffuseFirstMoment - gColorBoxSigmaScale * diffuseSigma;
    float3 diffuseColorMax = diffuseFirstMoment + gColorBoxSigmaScale * diffuseSigma;

    float3 diffuseIlluminationYCgCo = _NRD_LinearToYCoCg(diffuseIllumination);
    diffuseIlluminationYCgCo = clamp(diffuseIlluminationYCgCo, diffuseColorMin, diffuseColorMax);
    diffuseIllumination = _NRD_YCoCgToLinear(diffuseIlluminationYCgCo);

    gOutSpecularAndDiffuseIlluminationLogLuv[dispatchThreadId.xy] = PackSpecularAndDiffuseToLogLuvUint2(specularIllumination, diffuseIllumination);
}
