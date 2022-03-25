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

float GetBlurRadius(float radius, float roughness, float hitDist, float viewZ, float nonLinearAccumSpeed, float boost, float error, float radiusBias, float radiusScale)
{
    // Modify by hit distance
    float hitDistFactor = hitDist / (hitDist + viewZ);
    float s = hitDistFactor;

    // A non zero addition is needed to avoid under-blurring:
    float addon = 9.0;
    addon = min(addon, radius * 0.333);
    addon *= hitDistFactor;
    addon *= roughness;

    // Avoid over-blurring on contact
    radiusBias *= lerp(roughness, 1.0, hitDistFactor);

    // Final blur radius
    float r = s * radius + addon;
    r = r * (radiusScale + radiusBias) + radiusBias;
    r *= GetSpecMagicCurve(roughness);

    return r;
}

float2x3 GetKernelBasis(float3 X, float3 N, float worldRadius, float roughness = 1.0, float anisoFade = 1.0)
{
    float3x3 basis = STL::Geometry::GetBasis(N);
    float3 T = basis[0];
    float3 B = basis[1];

    float3 V = -normalize(X);
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection(N, V, roughness, RELAX_SPEC_DOMINANT_DIRECTION);
    float NoD = abs(dot(N, D.xyz));

    if (NoD < 0.999 && roughness < RELAX_SPEC_BASIS_ROUGHNESS_THRESHOLD)
    {
        float3 R = reflect(-D.xyz, N);
        T = normalize(cross(N, R));
        B = cross(R, T);

#if( RELAX_USE_ANISOTROPIC_KERNEL == 1 )
        float NoV = abs(dot(N, V));
        float acos01sq = saturate(1.0 - NoV); // see AcosApprox()

        float skewFactor = lerp(1.0, roughness, D.w);
        skewFactor = lerp(1.0, skewFactor, STL::Math::Sqrt01(acos01sq));

        T *= lerp(skewFactor, 1.0, anisoFade);
#endif
    }

    T *= worldRadius;
    B *= worldRadius;

    return float2x3(T, B);
}
float getNormalWeightParams(float nonLinearAccumSpeed, float roughness = 1.0, float strictness = 1.0)
{
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle(roughness);
    float s = 0.15;
    s *= strictness;
    s = lerp(s, 1.0, nonLinearAccumSpeed);
    angle *= s;
    angle = 1.0 / max(angle, NRD_ENCODING_ERRORS.x);
    return angle;
}

#if( defined RELAX_DIFFUSE )
int2 DiffCheckerboard(int2 pos)
{
    int2 result = pos;
    if (gDiffuseCheckerboard != 2)
        result.x >>= 1;

    return result;
}
#endif

#if( defined RELAX_SPECULAR )
int2 SpecCheckerboard(int2 pos)
{
    int2 result = pos;
    if (gSpecularCheckerboard != 2)
        result.x >>= 1;

    return result;
}
#endif

float GetGaussianWeight(float r)
{
    // radius is normalized to 1
    return exp(-0.66 * r * r);
}

[numthreads(GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN(int2 ipos : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadId, uint3 groupId : SV_GroupId)
{
    // Calculating checkerboard fields
    uint checkerboard = STL::Sequence::CheckerBoard(ipos, gFrameIndex);

    // Reading center GBuffer data
    // Applying abs() to viewZ so it is positive down the denoising pipeline.
    // This ensures correct denoiser operation with different camera handedness.
    // Also ensuring viewZ is not zero
    float centerViewZ = max(1e-6, abs(gViewZ[ipos + gRectOrigin]));

    // Outputting ViewZ and scaled ViewZ to be used down the denoising pipeline
    gOutViewZ[ipos] = centerViewZ;
    gOutScaledViewZ[ipos] = min(centerViewZ * NRD_FP16_VIEWZ_SCALE, NRD_FP16_MAX);

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerViewZ > gDenoisingRange)
    {
        return;
    }

    float2 uv = ((float2)ipos + float2(0.5, 0.5)) * gInvRectSize;
    float3 centerWorldPos = GetCurrentWorldPosFromClipSpaceXY(uv * 2.0 - 1.0, centerViewZ);
    float4 rotator = GetBlurKernelRotation(NRD_FRAME, ipos, gRotator, gFrameIndex);

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
        float viewZ0 = abs(gViewZ[ipos + int2(-1, 0) + gRectOrigin]);
        float viewZ1 = abs(gViewZ[ipos + int2(1, 0) + gRectOrigin]);

        checkerboardResolveWeights = GetBilateralWeight(float2(viewZ0, viewZ1), centerViewZ);
        checkerboardResolveWeights *= STL::Math::PositiveRcp(checkerboardResolveWeights.x + checkerboardResolveWeights.y);
    }

    float centerMaterialID;
    float4 centerNormalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[ipos + gRectOrigin], centerMaterialID);
    float3 centerNormal = centerNormalRoughness.xyz;
    float centerRoughness = centerNormalRoughness.w;

#if( defined RELAX_DIFFUSE )
    // Reading diffuse & resolving diffuse checkerboard
    float4 diffuseIllumination = gDiffuseIllumination[DiffCheckerboard(ipos + gRectOrigin)];

    bool diffHasData = true;
    if (gDiffuseCheckerboard != 2)
    {
        diffHasData = (checkerboard == gDiffuseCheckerboard);
    }

    if (!diffHasData)
    {
        float4 d0 = gDiffuseIllumination[DiffCheckerboard(ipos + int2(-1, 0) + gRectOrigin)];
        float4 d1 = gDiffuseIllumination[DiffCheckerboard(ipos + int2(1, 0) + gRectOrigin)];
        float2 diffCheckerboardResolveWeights = checkerboardResolveWeights;
#if( NRD_USE_MATERIAL_ID == 1 )
        float materialID0;
        float materialID1;
        NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[ipos + int2(-1, 0) + gRectOrigin], materialID0);
        NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[ipos + int2(1, 0) + gRectOrigin], materialID1);
        diffCheckerboardResolveWeights.x *= CompareMaterials(centerMaterialID, materialID0, gDiffMaterialMask);
        diffCheckerboardResolveWeights.y *= CompareMaterials(centerMaterialID, materialID1, gDiffMaterialMask);
        diffCheckerboardResolveWeights *= STL::Math::PositiveRcp(diffCheckerboardResolveWeights.x + diffCheckerboardResolveWeights.y + 1.0e-4);
#endif
        diffuseIllumination *= saturate(1.0 - diffCheckerboardResolveWeights.x - diffCheckerboardResolveWeights.y);
        diffuseIllumination += d0 * diffCheckerboardResolveWeights.x + d1 * diffCheckerboardResolveWeights.y;
    }

    // Pre-blur for diffuse
    if (gDiffuseBlurRadius > 0)
    {
        // Diffuse blur radius
        float hitDist = GetNormHitDist(diffuseIllumination.w, centerViewZ);
        float blurRadius = GetBlurRadius(gDiffuseBlurRadius, 1.0, diffuseIllumination.w, centerViewZ, 1.0 / 9.0, 1.0, 1.0, 0, 1.0);

        float worldBlurRadius = PixelRadiusToWorld(gUnproject, gOrthoMode, blurRadius, centerViewZ) *
            min(gResolutionScale.x, gResolutionScale.y);

        float2x3 TvBv = GetKernelBasis(centerWorldPos, centerNormal, worldBlurRadius);
        float normalWeightParams = getNormalWeightParams(1.0 / 9.0, 1.0);
        float2 hitDistanceWeightParams = GetHitDistanceWeightParams(diffuseIllumination.w, 1.0 / 9.0);
        float weightSum = 1.0;

        // Spatial blur
        [unroll]
        for (uint i = 0; i < POISSON_SAMPLE_NUM; i++)
        {
            float3 offset = POISSON_SAMPLES[i];

            // Sample coordinates
            float2 uv = GetKernelSampleCoordinates(gWorldToClip, offset, centerWorldPos, TvBv[0], TvBv[1], rotator);

            // Handle half res input in the checkerboard mode
            float2 checkerboardUv = uv;
            if (gDiffuseCheckerboard != 2)
            {
                checkerboardUv = ApplyCheckerboard(uv, gDiffuseCheckerboard, i, gRectSize, gInvRectSize, gFrameIndex);
            }

            // Fetch data
            float2 uvScaled = uv * gResolutionScale;

            float sampleMaterialID;
            float3 sampleNormal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness.SampleLevel(gNearestMirror, uvScaled + gRectOffset, 0), sampleMaterialID).rgb;

            float2 checkerboardUvScaled = checkerboardUv * gResolutionScale + gRectOffset;
            float4 sampleDiffuseIllumination = gDiffuseIllumination.SampleLevel(gNearestMirror, checkerboardUvScaled, 0);
            float sampleViewZ = abs(gViewZ.SampleLevel(gNearestMirror, uvScaled + gRectOffset, 0));

            float3 sampleWorldPos = GetCurrentWorldPosFromClipSpaceXY(uv * 2.0 - 1.0, sampleViewZ);

            // Sample weight
            float sampleWeight = IsInScreen(uv);
            sampleWeight *= GetGaussianWeight(offset.z);
            sampleWeight *= CompareMaterials(centerMaterialID, sampleMaterialID, gDiffMaterialMask);

            float sampleNormalizedHitDist = GetNormHitDist(sampleDiffuseIllumination.w, sampleViewZ);

            float minHitDistanceWeight = 0.2;
            float hitDistanceWeight = GetHitDistanceWeight(hitDistanceWeightParams, sampleNormalizedHitDist);
            sampleWeight *= GetPlaneDistanceWeight(
                centerWorldPos,
                centerNormal,
                gOrthoMode == 0 ? centerViewZ : 1.0,
                sampleWorldPos,
                gDepthThreshold);
            sampleWeight *= GetNormalWeight(normalWeightParams, centerNormal, sampleNormal);
            sampleWeight *= lerp(minHitDistanceWeight, 1.0, hitDistanceWeight);

            diffuseIllumination += sampleDiffuseIllumination * sampleWeight;
            weightSum += sampleWeight;
        }
        diffuseIllumination /= weightSum;
    }
    gOutDiffuseIllumination[ipos] = clamp(diffuseIllumination, 0, NRD_FP16_MAX);
#endif

#if( defined RELAX_SPECULAR )
    // Reading specular & resolving specular checkerboard
    float4 specularIllumination = gSpecularIllumination[SpecCheckerboard(ipos + gRectOrigin)];

    bool specHasData = true;
    if (gSpecularCheckerboard != 2)
    {
        specHasData = (checkerboard == gSpecularCheckerboard);
    }

    if (!specHasData)
    {
        float4 s0 = gSpecularIllumination[SpecCheckerboard(ipos + int2(-1, 0) + gRectOrigin)];
        float4 s1 = gSpecularIllumination[SpecCheckerboard(ipos + int2(1, 0) + gRectOrigin)];
        float2 specCheckerboardResolveWeights = checkerboardResolveWeights;
#if( NRD_USE_MATERIAL_ID == 1 )
        float materialID0;
        float materialID1;
        NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[ipos + int2(-1, 0) + gRectOrigin], materialID0);
        NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[ipos + int2(1, 0) + gRectOrigin], materialID1);
        specCheckerboardResolveWeights.x *= CompareMaterials(centerMaterialID, materialID0, gSpecMaterialMask);
        specCheckerboardResolveWeights.y *= CompareMaterials(centerMaterialID, materialID1, gSpecMaterialMask);
        specCheckerboardResolveWeights *= STL::Math::PositiveRcp(specCheckerboardResolveWeights.x + specCheckerboardResolveWeights.y + 1.0e-4);
#endif
        specularIllumination *= saturate(1.0 - specCheckerboardResolveWeights.x - specCheckerboardResolveWeights.y);
        specularIllumination += s0 * specCheckerboardResolveWeights.x + s1 * specCheckerboardResolveWeights.y;
    }

    specularIllumination.a = max(0.001, min(gDenoisingRange, specularIllumination.a));
    float specularHitT = specularIllumination.a;

    // Pre-blur for specular
    if (gSpecularBlurRadius > 0)
    {

        float exposure = _NRD_GetColorCompressionExposureForSpatialPasses(centerRoughness);
        specularIllumination.rgb = STL::Color::Compress(specularIllumination.rgb, exposure);

        // Specular blur radius
        float hitDist = GetNormHitDist(specularIllumination.w, centerViewZ, float4(3.0, 0.1, 10.0, -25.0), centerRoughness);
        float blurRadius = GetBlurRadius(gSpecularBlurRadius, centerRoughness, specularIllumination.w, centerViewZ, 1.0 / 9.0, 1.0, 1.0, 0, 1.0);

        float worldBlurRadius = PixelRadiusToWorld(gUnproject, gOrthoMode, blurRadius, centerViewZ) *
            min(gResolutionScale.x, gResolutionScale.y);

        float anisoFade = 1.0 / 9.0;
        float2x3 TvBv = GetKernelBasis(centerWorldPos, centerNormal, worldBlurRadius, centerRoughness, anisoFade);
        float normalWeightParams = getNormalWeightParams(1.0 / 9.0, centerRoughness);
        float2 hitDistanceWeightParams = GetHitDistanceWeightParams(specularIllumination.w, 1.0 / 9.0, centerRoughness);
        float2 roughnessWeightParams = GetRoughnessWeightParams(centerRoughness, gRoughnessFraction);
        float weightSum = 1.0;

        // Spatial blur
        [unroll]
        for (uint i = 0; i < POISSON_SAMPLE_NUM; i++)
        {
            float3 offset = POISSON_SAMPLES[i];

            // Sample coordinates
            float2 uv = GetKernelSampleCoordinates(gWorldToClip, offset, centerWorldPos, TvBv[0], TvBv[1], rotator);

            // Handle half res input in the checkerboard mode
            float2 checkerboardUv = uv;
            if (gSpecularCheckerboard != 2)
            {
                checkerboardUv = ApplyCheckerboard(uv, gSpecularCheckerboard, i, gRectSize, gInvRectSize, gFrameIndex);
            }

            // Fetch data
            float2 uvScaled = uv * gResolutionScale;

            float sampleMaterialID;
            float4 sampleNormalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness.SampleLevel(gNearestMirror, uvScaled + gRectOffset, 0), sampleMaterialID);
            float3 sampleNormal = sampleNormalRoughness.rgb;
            float sampleRoughness = sampleNormalRoughness.a;

            float2 checkerboardUvScaled = checkerboardUv * gResolutionScale + gRectOffset;
            float4 sampleSpecularIllumination = gSpecularIllumination.SampleLevel(gNearestMirror, checkerboardUvScaled, 0);
            sampleSpecularIllumination.rgb = STL::Color::Compress(sampleSpecularIllumination.rgb, exposure);
            float sampleViewZ = abs(gViewZ.SampleLevel(gNearestMirror, uvScaled + gRectOffset, 0));

            float3 sampleWorldPos = GetCurrentWorldPosFromClipSpaceXY(uv * 2.0 - 1.0, sampleViewZ);

            // Sample weight
            float sampleWeight = IsInScreen(uv);
            sampleWeight *= GetGaussianWeight(offset.z);
            sampleWeight *= CompareMaterials(centerMaterialID, sampleMaterialID, gSpecMaterialMask);
            sampleWeight *= GetRoughnessWeight(roughnessWeightParams, sampleRoughness);

            float sampleNormalizedHitDist = GetNormHitDist(sampleSpecularIllumination.w, sampleViewZ);

            float hitDistanceWeight = GetHitDistanceWeight(hitDistanceWeightParams, sampleNormalizedHitDist);

            sampleWeight *= GetPlaneDistanceWeight(
                centerWorldPos,
                centerNormal,
                gOrthoMode == 0 ? centerViewZ : 1.0,
                sampleWorldPos,
                gDepthThreshold);
            sampleWeight *= GetNormalWeight(normalWeightParams, centerNormal, sampleNormal);
            float minHitDistanceWeight = 0.2;
            sampleWeight *= lerp(minHitDistanceWeight, 1.0, hitDistanceWeight);

            specularIllumination += sampleSpecularIllumination * sampleWeight;
            weightSum += sampleWeight;
        }
        specularIllumination /= weightSum;

        specularIllumination.rgb = STL::Color::Decompress(specularIllumination.rgb, exposure);
        specularIllumination.a = specularHitT; // No, we don't preblur specular HitT!
    }
    gOutSpecularIllumination[ipos] = clamp(specularIllumination, 0, NRD_FP16_MAX);
#endif

}
