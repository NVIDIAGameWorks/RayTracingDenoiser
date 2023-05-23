/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define POISSON_SAMPLE_NUM      8
#define POISSON_SAMPLES         g_Poisson8

#ifdef RELAX_DIFFUSE
int2 DiffCheckerboard(int2 pos)
{
    int2 result = pos;
    if (gDiffuseCheckerboard != 2)
        result.x >>= 1;

    return result;
}
#endif

#ifdef RELAX_SPECULAR
int2 SpecCheckerboard(int2 pos)
{
    int2 result = pos;
    if (gSpecularCheckerboard != 2)
        result.x >>= 1;

    return result;
}
#endif

[numthreads(GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN(int2 pixelPos : SV_DispatchThreadId, uint2 threadPos : SV_GroupThreadId, uint threadIndex : SV_GroupIndex)
{
    // Calculating checkerboard fields
    uint checkerboard = STL::Sequence::CheckerBoard(pixelPos, gFrameIndex);

    // Tile-based early out
    float isSky = gTiles[pixelPos >> 4];
    if (isSky != 0.0)
        return;

    // Early out if linearZ is beyond denoising range
    float centerViewZ = abs(gViewZ[pixelPos + gRectOrigin]);
    if (centerViewZ > gDenoisingRange)
        return;

    // Checkerboard resolve weights
    float2 checkerboardResolveWeights = 1.0;
#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )
    if ((gSpecularCheckerboard != 2) || (gDiffuseCheckerboard != 2))
#elif( defined RELAX_DIFFUSE )
    if (gDiffuseCheckerboard != 2)
#elif( defined RELAX_SPECULAR )
    if (gSpecularCheckerboard != 2)
#endif
    {
        float viewZ0 = abs(gViewZ[pixelPos + int2(-1, 0) + gRectOrigin]);
        float viewZ1 = abs(gViewZ[pixelPos + int2(1, 0) + gRectOrigin]);

        checkerboardResolveWeights = GetBilateralWeight(float2(viewZ0, viewZ1), centerViewZ);
        checkerboardResolveWeights *= STL::Math::PositiveRcp(checkerboardResolveWeights.x + checkerboardResolveWeights.y);
    }

    float centerMaterialID;
    float4 centerNormalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[pixelPos + gRectOrigin], centerMaterialID);
    float3 centerNormal = centerNormalRoughness.xyz;
    float centerRoughness = centerNormalRoughness.w;

    float3 centerWorldPos = GetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ);
    float4 rotator = GetBlurKernelRotation(NRD_FRAME, pixelPos, gRotator, gFrameIndex);

    float2 pixelUv = float2(pixelPos + 0.5) * gInvRectSize;

#ifdef RELAX_DIFFUSE
    // Reading diffuse & resolving diffuse checkerboard
    float4 diffuseIllumination = gDiffuseIllumination[DiffCheckerboard(pixelPos + gRectOrigin)];
    #ifdef RELAX_SH
        float4 diffuseSH1 = gDiffuseSH1[DiffCheckerboard(pixelPos + gRectOrigin)];
    #endif

    bool diffHasData = true;
    if (gDiffuseCheckerboard != 2)
        diffHasData = (checkerboard == gDiffuseCheckerboard);

    if (!diffHasData)
    {
        float4 d0 = gDiffuseIllumination[DiffCheckerboard(pixelPos + int2(-1, 0) + gRectOrigin)];
        float4 d1 = gDiffuseIllumination[DiffCheckerboard(pixelPos + int2(1, 0) + gRectOrigin)];
        float2 diffCheckerboardResolveWeights = checkerboardResolveWeights;
        #if( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_R10G10B10A2_UNORM )
            float materialID0;
            float materialID1;
            NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[pixelPos + int2(-1, 0) + gRectOrigin], materialID0);
            NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[pixelPos + int2(1, 0) + gRectOrigin], materialID1);
            diffCheckerboardResolveWeights.x *= CompareMaterials(centerMaterialID, materialID0, gDiffMaterialMask);
            diffCheckerboardResolveWeights.y *= CompareMaterials(centerMaterialID, materialID1, gDiffMaterialMask);
            diffCheckerboardResolveWeights *= STL::Math::PositiveRcp(diffCheckerboardResolveWeights.x + diffCheckerboardResolveWeights.y + 1.0e-4);
        #endif
        diffuseIllumination *= saturate(1.0 - diffCheckerboardResolveWeights.x - diffCheckerboardResolveWeights.y);
        diffuseIllumination += d0 * diffCheckerboardResolveWeights.x + d1 * diffCheckerboardResolveWeights.y;
        #ifdef RELAX_SH
            float4 d0SH1 = gDiffuseSH1[DiffCheckerboard(pixelPos + int2(-1, 0) + gRectOrigin)];
            float4 d1SH1 = gDiffuseSH1[DiffCheckerboard(pixelPos + int2(1, 0) + gRectOrigin)];
            diffuseSH1 *= saturate(1.0 - diffCheckerboardResolveWeights.x - diffCheckerboardResolveWeights.y);
            diffuseSH1 += d0SH1 * diffCheckerboardResolveWeights.x + d1SH1 * diffCheckerboardResolveWeights.y;
        #endif
    }

    // Pre-blur for diffuse
    if (gDiffuseBlurRadius > 0)
    {
        // Diffuse blur radius
        float frustumSize = PixelRadiusToWorld(gUnproject, gOrthoMode, min(gRectSize.x, gRectSize.y), centerViewZ);
        float hitDist = (diffuseIllumination.w == 0.0 ? 1.0 : diffuseIllumination.w);
        float hitDistFactor = GetHitDistFactor(hitDist, frustumSize); // NoD = 1
        float blurRadius = gDiffuseBlurRadius * hitDistFactor;

        if (diffuseIllumination.w == 0.0)
            blurRadius = max(blurRadius, 1.0);

        float worldBlurRadius = PixelRadiusToWorld(gUnproject, gOrthoMode, blurRadius, centerViewZ) * min(gResolutionScale.x, gResolutionScale.y);
        float normalWeightParams = GetNormalWeightParams(1.0, 0.25 * gDiffuseLobeAngleFraction);
        float2 hitDistanceWeightParams = GetHitDistanceWeightParams(diffuseIllumination.w, 1.0 / 9.0);

        float weightSum = 1.0;

        float diffMinHitDistanceWeight = 0.2;

        // Spatial blur
        [unroll]
        for (uint i = 0; i < POISSON_SAMPLE_NUM; i++)
        {
            float3 offset = POISSON_SAMPLES[i];

            // Sample coordinates
            float2 uv = pixelUv + STL::Geometry::RotateVector(rotator, offset.xy) * gInvResourceSize * blurRadius;

            if (gDiffuseCheckerboard != 2)
                uv = ApplyCheckerboardShift(uv, gDiffuseCheckerboard, i, gRectSize, gInvRectSize, gFrameIndex);

            // Fetch data
            float2 uvScaled = uv * gResolutionScale;
            float2 checkerboardUvScaled = float2( uvScaled.x * ( gDiffuseCheckerboard != 2 ? 0.5 : 1.0 ), uvScaled.y ) + gRectOffset;

            float sampleMaterialID;
            float3 sampleNormal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness.SampleLevel(gNearestClamp, uvScaled, 0), sampleMaterialID).rgb;
            float sampleViewZ = abs(gViewZ.SampleLevel(gNearestClamp, uvScaled, 0));
            float3 sampleWorldPos = GetCurrentWorldPosFromClipSpaceXY(uv * 2.0 - 1.0, sampleViewZ);
            float4 sampleDiffuseIllumination = gDiffuseIllumination.SampleLevel(gNearestClamp, checkerboardUvScaled, 0);

            // Sample weight
            float sampleWeight = IsInScreen(uv);
            sampleWeight *= GetGaussianWeight(offset.z);
            sampleWeight *= CompareMaterials(centerMaterialID, sampleMaterialID, gDiffMaterialMask);

            sampleWeight *= GetPlaneDistanceWeight(
                centerWorldPos,
                centerNormal,
                gOrthoMode == 0 ? centerViewZ : 1.0,
                sampleWorldPos,
                gDepthThreshold);
            sampleWeight *= GetNormalWeight(normalWeightParams, centerNormal, sampleNormal);

            sampleWeight *= lerp(diffMinHitDistanceWeight, 1.0, GetHitDistanceWeight(hitDistanceWeightParams, sampleDiffuseIllumination.a));

            diffuseIllumination += (sampleWeight > 0) ? sampleDiffuseIllumination * sampleWeight : 0;
            
            #ifdef RELAX_SH
                float4 sampleDiffuseSH1 = gDiffuseSH1.SampleLevel(gNearestClamp, checkerboardUvScaled, 0);
                diffuseSH1 += (sampleWeight > 0) ? sampleDiffuseSH1 * sampleWeight : 0;
            #endif

            weightSum += sampleWeight;
        }

        diffuseIllumination /= weightSum;
        #ifdef RELAX_SH
            diffuseSH1 /= weightSum;
        #endif
    }

    gOutDiffuseIllumination[pixelPos] = clamp(diffuseIllumination, 0, NRD_FP16_MAX);
    #ifdef RELAX_SH
        gOutDiffuseSH1[pixelPos] = clamp(diffuseSH1, -NRD_FP16_MAX, NRD_FP16_MAX);
    #endif
#endif

#ifdef RELAX_SPECULAR
    // Reading specular & resolving specular checkerboard
    float4 specularIllumination = gSpecularIllumination[SpecCheckerboard(pixelPos + gRectOrigin)];
    #ifdef RELAX_SH
        float4 specularSH1 = gSpecularSH1[SpecCheckerboard(pixelPos + gRectOrigin)];
    #endif

    bool specHasData = true;
    if (gSpecularCheckerboard != 2)
        specHasData = (checkerboard == gSpecularCheckerboard);

    if (!specHasData)
    {
        float4 s0 = gSpecularIllumination[SpecCheckerboard(pixelPos + int2(-1, 0) + gRectOrigin)];
        float4 s1 = gSpecularIllumination[SpecCheckerboard(pixelPos + int2(1, 0) + gRectOrigin)];
        float2 specCheckerboardResolveWeights = checkerboardResolveWeights;
#if( NRD_NORMAL_ENCODING == NRD_NORMAL_ENCODING_R10G10B10A2_UNORM )
        float materialID0;
        float materialID1;
        NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[pixelPos + int2(-1, 0) + gRectOrigin], materialID0);
        NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[pixelPos + int2(1, 0) + gRectOrigin], materialID1);
        specCheckerboardResolveWeights.x *= CompareMaterials(centerMaterialID, materialID0, gSpecMaterialMask);
        specCheckerboardResolveWeights.y *= CompareMaterials(centerMaterialID, materialID1, gSpecMaterialMask);
        specCheckerboardResolveWeights *= STL::Math::PositiveRcp(specCheckerboardResolveWeights.x + specCheckerboardResolveWeights.y + 1.0e-4);
#endif
        specularIllumination *= saturate(1.0 - specCheckerboardResolveWeights.x - specCheckerboardResolveWeights.y);
        specularIllumination += s0 * specCheckerboardResolveWeights.x + s1 * specCheckerboardResolveWeights.y;
        #ifdef RELAX_SH
            float4 s0SH1 = gSpecularSH1[SpecCheckerboard(pixelPos + int2(-1, 0) + gRectOrigin)];
            float4 s1SH1 = gSpecularSH1[SpecCheckerboard(pixelPos + int2(1, 0) + gRectOrigin)];
            specularSH1 *= saturate(1.0 - specCheckerboardResolveWeights.x - specCheckerboardResolveWeights.y);
            specularSH1 += s0SH1 * specCheckerboardResolveWeights.x + s1SH1 * specCheckerboardResolveWeights.y;
        #endif
    }

    specularIllumination.a = max(0, min(gDenoisingRange, specularIllumination.a));

    // Pre-blur for specular
    if (gSpecularBlurRadius > 0)
    {
        // Specular blur radius
        float3 viewVector = (gOrthoMode == 0) ? normalize(-centerWorldPos) : gFrustumForward.xyz;
        float4 D = STL::ImportanceSampling::GetSpecularDominantDirection(centerNormal, viewVector, centerRoughness, STL_SPECULAR_DOMINANT_DIRECTION_G2);
        float NoD = abs(dot(centerNormal, D.xyz));

        float frustumSize = PixelRadiusToWorld(gUnproject, gOrthoMode, min(gRectSize.x, gRectSize.y), centerViewZ);
        float hitDist = (specularIllumination.w == 0.0 ? 1.0 : specularIllumination.w);

        float hitDistFactor = GetHitDistFactor(hitDist * NoD, frustumSize);

        float blurRadius = gSpecularBlurRadius * hitDistFactor * GetSpecMagicCurve(centerRoughness);
        float lobeTanHalfAngle = STL::ImportanceSampling::GetSpecularLobeTanHalfAngle(centerRoughness);
        float lobeRadius = hitDist * NoD * lobeTanHalfAngle;
        float minBlurRadius = lobeRadius / PixelRadiusToWorld(gUnproject, gOrthoMode, 1.0, centerViewZ + hitDist * D.w);

        blurRadius = min(blurRadius, minBlurRadius);

        if (specularIllumination.w == 0.0)
            blurRadius = max(blurRadius, 1.0);

        float normalWeightParams = GetNormalWeightParams(centerRoughness, 0.5 * gSpecularLobeAngleFraction);
        float2 hitDistanceWeightParams = GetHitDistanceWeightParams(specularIllumination.w, 1.0 / 9.0, centerRoughness);
        float2 roughnessWeightParams = GetRoughnessWeightParams(centerRoughness, gRoughnessFraction);

        float specMinHitDistanceWeight = (specularIllumination.a == 0) ? 1.0 : 0.2;
        float specularHitT = (specularIllumination.a == 0) ? gDenoisingRange : specularIllumination.a;

        float NoV = abs(dot(centerNormal, viewVector));

        float minHitT = specularHitT;
        float weightSum = 1.0;

        // Spatial blur
        [unroll]
        for (uint i = 0; i < POISSON_SAMPLE_NUM; i++)
        {
            float3 offset = POISSON_SAMPLES[i];

            // Sample coordinates
            float2 uv = pixelUv + STL::Geometry::RotateVector(rotator, offset.xy) * gInvResourceSize * blurRadius;

            if (gSpecularCheckerboard != 2)
                uv = ApplyCheckerboardShift(uv, gSpecularCheckerboard, i, gRectSize, gInvRectSize, gFrameIndex);

            float2 uvScaled = uv * gResolutionScale;
            float2 checkerboardUvScaled = float2( uvScaled.x * ( gSpecularCheckerboard != 2 ? 0.5 : 1.0 ), uvScaled.y ) + gRectOffset;

            // Fetch data
            float4 sampleSpecularIllumination = gSpecularIllumination.SampleLevel(gNearestClamp, checkerboardUvScaled, 0);

            float sampleMaterialID;
            float4 sampleNormalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness.SampleLevel(gNearestClamp, uvScaled, 0), sampleMaterialID);
            float3 sampleNormal = sampleNormalRoughness.rgb;
            float sampleRoughness = sampleNormalRoughness.a;
            float sampleViewZ = abs(gViewZ.SampleLevel(gNearestClamp, uvScaled, 0));

            // Sample weight
            float sampleWeight = IsInScreen(uv);
            sampleWeight *= GetGaussianWeight(offset.z);
            sampleWeight *= CompareMaterials(centerMaterialID, sampleMaterialID, gSpecMaterialMask);
            sampleWeight *= GetRoughnessWeight(roughnessWeightParams, sampleRoughness);
            sampleWeight *= GetNormalWeight(normalWeightParams, centerNormal, sampleNormal);

            float hitDistanceWeight = GetHitDistanceWeight(hitDistanceWeightParams, sampleSpecularIllumination.a);

            float3 sampleWorldPos = GetCurrentWorldPosFromClipSpaceXY(uv * 2.0 - 1.0, sampleViewZ);
            sampleWeight *= GetPlaneDistanceWeight(
                centerWorldPos,
                centerNormal,
                gOrthoMode == 0 ? centerViewZ : 1.0,
                sampleWorldPos,
                gDepthThreshold);

            // Decreasing weight for samples that most likely are very close to reflection contact
            // which should not be pre-blurred
            float d = length(sampleWorldPos - centerWorldPos);
            float h = sampleSpecularIllumination.a; 
            float t = h / (specularIllumination.a + d);
            sampleWeight *= lerp(saturate(t), 1.0, STL::Math::LinearStep(0.5, 1.0, centerRoughness));

            specularIllumination.rgb += (sampleWeight > 0) ? sampleSpecularIllumination.rgb * sampleWeight : 0;
            #ifdef RELAX_SH
                float4 sampleSpecularSH1 = gSpecularSH1.SampleLevel(gNearestClamp, checkerboardUvScaled, 0);
                specularSH1 += (sampleWeight > 0) ? sampleSpecularSH1 * sampleWeight : 0;
            #endif
            weightSum += sampleWeight;

            if (sampleWeight > 0)
            {
                if ((sampleSpecularIllumination.a != 0) && (minHitT > sampleSpecularIllumination.a)) minHitT = sampleSpecularIllumination.a;
            }

        }
        specularIllumination.rgb /= weightSum;
        specularIllumination.a = minHitT;
        #ifdef RELAX_SH
            specularSH1 /= weightSum;
        #endif
    }

    gOutSpecularIllumination[pixelPos] = clamp(specularIllumination, 0, NRD_FP16_MAX);
    #ifdef RELAX_SH
        gOutSpecularSH1[pixelPos] = clamp(specularSH1, -NRD_FP16_MAX, NRD_FP16_MAX);
    #endif
#endif
}