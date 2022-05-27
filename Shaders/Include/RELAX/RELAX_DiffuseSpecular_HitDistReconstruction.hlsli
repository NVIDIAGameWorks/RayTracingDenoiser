/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float4 sharedNormalRoughness[BUFFER_Y][BUFFER_X];
groupshared float2 sharedHitdistViewZ[BUFFER_Y][BUFFER_X];

void Preload(uint2 sharedPos, int2 globalPos)
{
    globalPos = clamp(globalPos, 0, gRectSize - 1.0);
    uint2 globalIdUser = gRectOrigin + globalPos;

    // It's ok that we don't use materialID in Hitdist reconstruction
    float4 normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[globalIdUser]);
    float viewZ = abs(gViewZ[globalIdUser]);
    float hitdist = gSpecularIllumination[globalPos].w;

    sharedNormalRoughness[sharedPos.y][sharedPos.x] = normalRoughness;
    sharedHitdistViewZ[sharedPos.y][sharedPos.x] = float2(hitdist, viewZ);
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN(int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex)
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2(pixelPos + 0.5) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    int2 smemPos = threadPos + BORDER;
    float2 centerHitdistViewZ = sharedHitdistViewZ[smemPos.y][smemPos.x];

    // Early out
    [branch]
    if (centerHitdistViewZ.y > gDenoisingRange)
    {
        return;
    }

    // Center data
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[pixelPosUser]);
    float3 centerNormal = normalAndRoughness.xyz;
    float centerRoughness = normalAndRoughness.w;
    float3 centerSpecular = gSpecularIllumination[pixelPos].xyz;

    // Hit distance reconstruction,
    // We only care about specular signal in ReLAX as diffuse does not use Hitdist
    float sumWeight = 100.0 * gDenoisingRange * float(centerHitdistViewZ.x != 0.0);
    float sumHitdist = centerHitdistViewZ.x * sumWeight;

    [unroll]
    for (int dy = 0; dy <= BORDER * 2; dy++)
    {
        [unroll]
        for (int dx = 0; dx <= BORDER * 2; dx++)
        {
            int2 o = int2(dx, dy) - BORDER;

            if (o.x == 0 && o.y == 0)
            {
                continue;
            }

            int2 pos = threadPos + int2(dx, dy);
            float4 sampleNormalRoughness = sharedNormalRoughness[pos.y][pos.x];
            float2 sampleHitdistViewZ = sharedHitdistViewZ[pos.y][pos.x];

            float w = IsInScreen(pixelUv + o * gInvRectSize);
            w *= GetGaussianWeight(length(o) * 0.5);
            w *= GetBilateralWeight(sampleHitdistViewZ.y, centerHitdistViewZ.y);
            w *= GetEncodingAwareNormalWeight(sampleNormalRoughness.xyz, centerNormal, STL::Math::Pi(0.5)); // TODO: use diffuse and specular lobe angle? roughness weight for specular?
            w *= float(sampleHitdistViewZ.x != 0.0);

            sumHitdist += sampleHitdistViewZ.x * w;
            sumWeight += w;
        }
    }

    // Normalize weighted sum
    sumHitdist /= max(sumWeight, 1e-6);

    // Output
    gOutSpecularIllumination[pixelPos] = float4(centerSpecular, sumHitdist);
}
