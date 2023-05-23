/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float4 sharedNormalRoughness[BUFFER_Y][BUFFER_X];
groupshared float3 sharedHitdistViewZ[BUFFER_Y][BUFFER_X];

float GetNormalWeightParams(float nonLinearAccumSpeed, float fraction, float roughness = 1.0)
{
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle(roughness);
    angle *= lerp(saturate(fraction), 1.0, nonLinearAccumSpeed); // TODO: use as "percentOfVolume" instead?

    return 1.0 / max(angle, RELAX_NORMAL_ENCODING_ERROR);
}

void Preload(uint2 sharedPos, int2 globalPos)
{
    globalPos = clamp(globalPos, 0, gRectSize - 1.0);
    uint2 globalIdUser = gRectOrigin + globalPos;

    // It's ok that we don't use materialID in Hitdist reconstruction
    float4 normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[globalIdUser]);
    float viewZ = abs(gViewZ[globalIdUser]);
    float2 hitDist = gDenoisingRange;

    #ifdef RELAX_SPECULAR
        hitDist.x = gSpecularIllumination[globalPos].w;
    #endif

    #ifdef RELAX_DIFFUSE
        hitDist.y = gDiffuseIllumination[globalPos].w;
    #endif

    sharedNormalRoughness[sharedPos.y][sharedPos.x] = normalRoughness;
    sharedHitdistViewZ[sharedPos.y][sharedPos.x] = float3(hitDist, viewZ);
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN(int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex)
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2(pixelPos + 0.5) * gInvRectSize;

    // Preload
    float isSky = gTiles[pixelPos >> 4];
    PRELOAD_INTO_SMEM_WITH_TILE_CHECK;

    // Tile-based early out
    if (isSky != 0.0)
        return;

    int2 smemPos = threadPos + BORDER;
    float3 centerHitdistViewZ = sharedHitdistViewZ[smemPos.y][smemPos.x];
    float centerViewZ = centerHitdistViewZ.z;

    // Early out
    if (centerViewZ > gDenoisingRange)
        return;

    // Center data
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[pixelPosUser]);
    float3 centerNormal = normalAndRoughness.xyz;
    float centerRoughness = normalAndRoughness.w;

    // Hit distance reconstruction
#ifdef RELAX_SPECULAR
    float3 centerSpecularIllumination = gSpecularIllumination[pixelPos].xyz;
    float centerSpecularHitDist = centerHitdistViewZ.x;
    float2 roughnessWeightParams = GetCoarseRoughnessWeightParams(centerRoughness);
    float specularNormalWeightParam = GetNormalWeightParams(1.0, 1.0, centerRoughness);

    float sumSpecularWeight = 1000.0 * float(centerSpecularHitDist != 0.0);
    float sumSpecularHitDist = centerSpecularHitDist * sumSpecularWeight;
#endif

#ifdef RELAX_DIFFUSE
    float3 centerDiffuseIllumination = gDiffuseIllumination[pixelPos].xyz;
    float centerDiffuseHitDist = centerHitdistViewZ.y;
    float diffuseNormalWeightParam = GetNormalWeightParams(1.0, 1.0, 1.0);

    float sumDiffuseWeight = 1000.0 * float(centerDiffuseHitDist != 0.0);
    float sumDiffuseHitDist = centerDiffuseHitDist * sumDiffuseWeight;
#endif

    [unroll]
    for (int dy = 0; dy <= BORDER * 2; dy++)
    {
        [unroll]
        for (int dx = 0; dx <= BORDER * 2; dx++)
        {
            int2 o = int2(dx, dy) - BORDER;

            if (o.x == 0 && o.y == 0)
                continue;

            int2 pos = threadPos + int2(dx, dy);
            float4 sampleNormalRoughness = sharedNormalRoughness[pos.y][pos.x];
            float3 sampleNormal = sampleNormalRoughness.xyz;
            float3 sampleRoughness = sampleNormalRoughness.w;
            float3 sampleHitdistViewZ = sharedHitdistViewZ[pos.y][pos.x];
            float sampleViewZ = sampleHitdistViewZ.z;
            float cosa = saturate(dot(centerNormal, sampleNormal));
            float angle = STL::Math::AcosApprox(cosa);

            float w = IsInScreen(pixelUv + o * gInvRectSize);
            w *= GetGaussianWeight(length(o) * 0.5);
            w *= GetBilateralWeight(sampleViewZ, centerViewZ);

#ifdef RELAX_SPECULAR
            float sampleSpecularHitDist = sampleHitdistViewZ.x;
            float specularWeight = w;
            specularWeight *= _ComputeExponentialWeight(angle, specularNormalWeightParam, 0.0);
            specularWeight *= _ComputeExponentialWeight(normalAndRoughness.w, roughnessWeightParams.x, roughnessWeightParams.y);
            specularWeight *= float(sampleSpecularHitDist != 0.0);
            sumSpecularHitDist += sampleSpecularHitDist * specularWeight;
            sumSpecularWeight += specularWeight;
#endif

#ifdef RELAX_DIFFUSE
            float sampleDiffuseHitDist = sampleHitdistViewZ.y;
            float diffuseWeight = w;
            diffuseWeight *= _ComputeExponentialWeight(angle, diffuseNormalWeightParam, 0.0);
            diffuseWeight *= float(sampleDiffuseHitDist != 0.0);

            sumDiffuseHitDist += sampleDiffuseHitDist * diffuseWeight;
            sumDiffuseWeight += diffuseWeight;
#endif
        }
    }

    // Output
#ifdef RELAX_SPECULAR
    sumSpecularHitDist /= max(sumSpecularWeight, 1e-6);
    gOutSpecularIllumination[pixelPos] = float4(centerSpecularIllumination, sumSpecularHitDist);
#endif

#ifdef RELAX_DIFFUSE
    sumDiffuseHitDist /= max(sumDiffuseWeight, 1e-6);
    gOutDiffuseIllumination[pixelPos] = float4(centerDiffuseIllumination, sumDiffuseHitDist);
#endif

}
