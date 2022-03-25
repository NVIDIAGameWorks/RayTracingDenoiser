/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#if( defined RELAX_DIFFUSE)
    groupshared float4 sharedDiffuse[BUFFER_X][BUFFER_Y];
#endif

#if( defined RELAX_SPECULAR)
    groupshared float4 sharedSpecular[BUFFER_X][BUFFER_Y];
#endif

groupshared float4 sharedNormalRoughness[BUFFER_X][BUFFER_Y];
groupshared float4 sharedWorldPos[BUFFER_X][BUFFER_Y];
groupshared float sharedMaterialID[BUFFER_X][BUFFER_Y];

// Helper functions

// computes a 3x3 gaussian blur of the variance, centered around
// the current pixel
void computeVariance(
    int2 threadPos
#if( defined RELAX_SPECULAR )
    ,out float specularVariance
#endif
#if( defined RELAX_DIFFUSE )
    ,out float diffuseVariance
#endif
)
{
#if( defined RELAX_SPECULAR )
    float4 specularSum = 0;
#endif
#if( defined RELAX_DIFFUSE )
    float4 diffuseSum = 0;
#endif

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
            int2 sharedMemoryIndex = threadPos.xy + int2(xx + BORDER,yy + BORDER);
            float k = kernel[abs(xx)][abs(yy)];
#if( defined RELAX_SPECULAR )
            float4 specular = sharedSpecular[sharedMemoryIndex.y][sharedMemoryIndex.x];
            specularSum += specular * k;
#endif
#if( defined RELAX_DIFFUSE )
            float4 diffuse = sharedDiffuse[sharedMemoryIndex.y][sharedMemoryIndex.x];
            diffuseSum += diffuse * k;
#endif
        }
    }
#if( defined RELAX_SPECULAR )
    float specular1stMoment = STL::Color::Luminance(specularSum.rgb);
    float specular2ndMoment = specularSum.a;
    specularVariance = max(0, specular2ndMoment - specular1stMoment * specular1stMoment);
#endif
#if( defined RELAX_DIFFUSE )
    float diffuse1stMoment = STL::Color::Luminance(diffuseSum.rgb);
    float diffuse2ndMoment = diffuseSum.a;
    diffuseVariance = max(0, diffuse2ndMoment - diffuse1stMoment * diffuse1stMoment);
#endif
}

void Preload(uint2 sharedPos, int2 globalPos)
{
    globalPos = clamp(globalPos, 0, gRectSize - 1.0);

#if( defined RELAX_SPECULAR )
    sharedSpecular[sharedPos.y][sharedPos.x] = gSpecularIlluminationAnd2ndMoment[globalPos];
#endif

#if( defined RELAX_DIFFUSE )
    sharedDiffuse[sharedPos.y][sharedPos.x] = gDiffuseIlluminationAnd2ndMoment[globalPos];
#endif
    float materialID;
    sharedNormalRoughness[sharedPos.y][sharedPos.x] = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[globalPos], materialID);

    float viewZ = gViewZFP16[globalPos] / NRD_FP16_VIEWZ_SCALE;
    sharedWorldPos[sharedPos.y][sharedPos.x] = float4(GetCurrentWorldPosFromPixelPos(globalPos, viewZ), viewZ);

    sharedMaterialID[sharedPos.y][sharedPos.x] = materialID;
}

[numthreads(GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN(int2 pixelPos : SV_DispatchThreadId, uint2 threadPos : SV_GroupThreadId, uint threadIndex : SV_GroupIndex)
{
    PRELOAD_INTO_SMEM;
    // Shared memory is populated now and can be used for filtering

    uint2 sharedMemoryIndex = threadPos.xy + int2(BORDER, BORDER);

    // Repacking normal and roughness to prev normal roughness to be used in the next frame
    float materialID;
    float4 normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[pixelPos], materialID);
    gOutNormalRoughness[pixelPos] = PackPrevNormalRoughness(normalRoughness);
#if( NRD_USE_MATERIAL_ID == 1 )
    gOutMaterialID[pixelPos] = floor( materialID * 3.0 + 0.5 ) / 255.0; // IMPORTANT: properly repack 2-bits to 8-bits
#endif

    float4 centerWorldPosAndViewZ = sharedWorldPos[sharedMemoryIndex.y][sharedMemoryIndex.x];
    float3 centerWorldPos = centerWorldPosAndViewZ.xyz;
    float centerViewZ = centerWorldPosAndViewZ.w;

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerViewZ > gDenoisingRange)
    {
        return;
    }

    float3 centerNormal = normalRoughness.rgb;
    float centerRoughness = normalRoughness.a;

#if( defined RELAX_SPECULAR )
    float2 roughnessWeightParams = GetRoughnessWeightParams(centerRoughness, gRoughnessFraction);
#endif

#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )
    float historyLength = 255.0 * gHistoryLength[pixelPos].y;
#else
    float historyLength = 255.0 * gHistoryLength[pixelPos];
#endif

    float centerMaterialID = sharedMaterialID[sharedMemoryIndex.y][sharedMemoryIndex.x];

    [branch]
    if (historyLength >= float(gHistoryThreshold)) // Running Atrous 3x3
    {
        // Calculating variance, filtered using 3x3 gaussin blur
#if( defined RELAX_SPECULAR )
        float centerSpecularVar;
#endif
#if( defined RELAX_DIFFUSE )
        float centerDiffuseVar;
#endif
        computeVariance(
            threadPos.xy
#if( defined RELAX_SPECULAR )
            , centerSpecularVar
#endif
#if( defined RELAX_DIFFUSE )
            , centerDiffuseVar
#endif
        );

#if( defined RELAX_SPECULAR )
        float specularReprojectionConfidence = gSpecularReprojectionConfidence[pixelPos];
        float2 normalWeightParams = GetNormalWeightParams_ATrous(centerRoughness, historyLength, specularReprojectionConfidence, gNormalEdgeStoppingRelaxation, gSpecularLobeAngleFraction);

        float centerSpecularLuminance = STL::Color::Luminance(sharedSpecular[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb);
        float specularPhiLIllumination = gSpecularPhiLuminance * max(1.0e-4, sqrt(centerSpecularVar));
        float sumWSpecular = 0;
        float4 sumSpecularIlluminationAnd2ndMoment = 0;
#endif
#if( defined RELAX_DIFFUSE )
        float centerDiffuseLuminance = STL::Color::Luminance(sharedDiffuse[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb);
        float diffusePhiLIllumination = gDiffusePhiLuminance * max(1.0e-4, sqrt(centerDiffuseVar));
        float sumWDiffuse = 0;
        float4 sumDiffuseIlluminationAnd2ndMoment = 0;
#endif

        static const float kernelWeightGaussian3x3[2] = { 0.44198, 0.27901 };

        [unroll]
        for (int cy = -1; cy <= 1; cy++)
        {
            [unroll]
            for (int cx = -1; cx <= 1; cx++)
            {
                const float kernel = kernelWeightGaussian3x3[abs(cx)] * kernelWeightGaussian3x3[abs(cy)];
                const int2 p = pixelPos + int2(cx, cy);
                const bool isInside = all(p >= int2(0, 0)) && all(p < int2(gResourceSize));
                const bool isCenter = ((cx == 0) && (cy == 0));

                int2 sampleSharedMemoryIndex = threadPos.xy + int2(BORDER + cx, BORDER + cy);

                float4 sampleNormalRoughness = sharedNormalRoughness[sampleSharedMemoryIndex.y][sampleSharedMemoryIndex.x];
                float3 sampleNormal = sampleNormalRoughness.rgb;
                float sampleRoughness = sampleNormalRoughness.a;
                float3 sampleWorldPos = sharedWorldPos[sampleSharedMemoryIndex.y][sampleSharedMemoryIndex.x].rgb;

                float sampleMaterialID = sharedMaterialID[sampleSharedMemoryIndex.y][sampleSharedMemoryIndex.x];
#if( defined RELAX_SPECULAR )
                float4 sampleSpecularIlluminationAnd2ndMoment = sharedSpecular[sampleSharedMemoryIndex.y][sampleSharedMemoryIndex.x];
                float sampleSpecularLuminance = STL::Color::Luminance(sampleSpecularIlluminationAnd2ndMoment.rgb);
#endif
#if( defined RELAX_DIFFUSE )
                float4 sampleDiffuseIlluminationAnd2ndMoment = sharedDiffuse[sampleSharedMemoryIndex.y][sampleSharedMemoryIndex.x];
                float sampleDiffuseLuminance = STL::Color::Luminance(sampleDiffuseIlluminationAnd2ndMoment.rgb);
#endif

                // Calculating geometry and normal weights
                float geometryW = GetPlaneDistanceWeight(
                    centerWorldPos,
                    centerNormal,
                    gOrthoMode == 0 ? centerViewZ : 1.0,
                    sampleWorldPos,
                    gDepthThreshold);

                float diffuseNormalWeightParams = GetNormalWeightParams(1.0, gDiffuseLobeAngleFraction);
                float normalWDiffuse = GetNormalWeight(diffuseNormalWeightParams, centerNormal, sampleNormal);

#if( defined RELAX_SPECULAR )
                float normalWSpecular = GetSpecularNormalWeight_ATrous(normalWeightParams, gSpecularLobeAngleSlack, centerNormal, sampleNormal);
                float specularRoughnessW = GetRoughnessWeight(roughnessWeightParams, sampleRoughness);
                float specularLuminanceW = abs(centerSpecularLuminance - sampleSpecularLuminance) / specularPhiLIllumination;
                float relaxation = lerp(1.0, specularReprojectionConfidence, gLuminanceEdgeStoppingRelaxation);
                specularLuminanceW *= relaxation;
                specularLuminanceW = min(gMaxLuminanceRelativeDifference, specularLuminanceW);
                float wSpecular = geometryW * exp_approx(-specularLuminanceW);
                wSpecular *= gRoughnessEdgeStoppingEnabled ? (normalWSpecular * specularRoughnessW) : normalWDiffuse;
                wSpecular = kernel * wSpecular;
                wSpecular = isCenter ? kernel : wSpecular;
                wSpecular *= isInside ? 1.0 : 0.0;
                wSpecular *= CompareMaterials(sampleMaterialID, centerMaterialID, gSpecMaterialMask);

                sumWSpecular += wSpecular;
                sumSpecularIlluminationAnd2ndMoment += wSpecular * sampleSpecularIlluminationAnd2ndMoment;
#endif
#if( defined RELAX_DIFFUSE )
                float diffuseLuminanceW = abs(centerDiffuseLuminance - sampleDiffuseLuminance) / diffusePhiLIllumination;
                diffuseLuminanceW = min(gMaxLuminanceRelativeDifference, diffuseLuminanceW);
                float wDiffuse = geometryW * kernel * normalWDiffuse * exp_approx(-diffuseLuminanceW);
                wDiffuse = isCenter ? kernel : wDiffuse;
                wDiffuse *= isInside ? 1.0 : 0.0;
                wDiffuse *= CompareMaterials(sampleMaterialID, centerMaterialID, gDiffMaterialMask);

                sumWDiffuse += wDiffuse;
                sumDiffuseIlluminationAnd2ndMoment += wDiffuse * sampleDiffuseIlluminationAnd2ndMoment;
#endif
            }
        }
#if( defined RELAX_SPECULAR )
        sumWSpecular = max(sumWSpecular, 1e-6f);
        sumSpecularIlluminationAnd2ndMoment /= sumWSpecular;
        float specular1stMoment = STL::Color::Luminance(sumSpecularIlluminationAnd2ndMoment.rgb);
        float specular2ndMoment = sumSpecularIlluminationAnd2ndMoment.a;
        float specularVariance = max(0, specular2ndMoment - specular1stMoment * specular1stMoment);
        float4 filteredSpecularIlluminationAndVariance = float4(sumSpecularIlluminationAnd2ndMoment.rgb, specularVariance);
        gOutSpecularIlluminationAndVariance[pixelPos] = filteredSpecularIlluminationAndVariance;
#endif
#if( defined RELAX_DIFFUSE )
        sumWDiffuse = max(sumWDiffuse, 1e-6f);
        sumDiffuseIlluminationAnd2ndMoment /= sumWDiffuse;
        float diffuse1stMoment = STL::Color::Luminance(sumDiffuseIlluminationAnd2ndMoment.rgb);
        float diffuse2ndMoment = sumDiffuseIlluminationAnd2ndMoment.a;
        float diffuseVariance = max(0, diffuse2ndMoment - diffuse1stMoment * diffuse1stMoment);
        float4 filteredDiffuseIlluminationAndVariance = float4(sumDiffuseIlluminationAnd2ndMoment.rgb, diffuseVariance);
        gOutDiffuseIlluminationAndVariance[pixelPos] = filteredDiffuseIlluminationAndVariance;
#endif
    }
    else
    // Running spatial variance estimation
    {
#if( defined RELAX_SPECULAR )
        float sumWSpecularIllumination = 0;
        float3 sumSpecularIllumination = 0;
        float sumSpecular1stMoment = 0;
        float sumSpecular2ndMoment = 0;
#endif

#if( defined RELAX_DIFFUSE )
        float sumWDiffuseIllumination = 0;
        float3 sumDiffuseIllumination = 0;
        float sumDiffuse1stMoment = 0;
        float sumDiffuse2ndMoment = 0;
#endif

        // Compute first and second moment spatially. This code also applies cross-bilateral
        // filtering on the input illumination.
        [unroll]
        for (int cy = -2; cy <= 2; cy++)
        {
            [unroll]
            for (int cx = -2; cx <= 2; cx++)
            {
                int2 sharedMemoryIndex = threadPos.xy + int2(BORDER + cx, BORDER + cy);

                float3 sampleNormal = sharedNormalRoughness[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb;
                float sampleMaterialID = sharedMaterialID[sharedMemoryIndex.y][sharedMemoryIndex.x];

                // Calculating weights
                float depthW = 1.0;// TODO: should we take in account depth here?
                float diffuseNormalWeightParams = GetNormalWeightParams(1.0, gDiffuseLobeAngleFraction);
                float normalW = GetNormalWeight(diffuseNormalWeightParams, centerNormal, sampleNormal);

#if( defined RELAX_SPECULAR )
                float4 sampleSpecular = sharedSpecular[sharedMemoryIndex.y][sharedMemoryIndex.x];
                float3 sampleSpecularIllumination = sampleSpecular.rgb;
                float sampleSpecular1stMoment = STL::Color::Luminance(sampleSpecularIllumination);
                float sampleSpecular2ndMoment = sampleSpecular.a;
                float specularW = normalW * depthW;
                specularW *= CompareMaterials(sampleMaterialID, centerMaterialID, gSpecMaterialMask);

                sumWSpecularIllumination += specularW;
                sumSpecularIllumination += sampleSpecularIllumination.rgb * specularW;
                sumSpecular1stMoment += sampleSpecular1stMoment * specularW;
                sumSpecular2ndMoment += sampleSpecular2ndMoment * specularW;
#endif

#if( defined RELAX_DIFFUSE )
                float4 sampleDiffuse = sharedDiffuse[sharedMemoryIndex.y][sharedMemoryIndex.x];
                float3 sampleDiffuseIllumination = sampleDiffuse.rgb;
                float sampleDiffuse1stMoment = STL::Color::Luminance(sampleDiffuseIllumination);
                float sampleDiffuse2ndMoment = sampleDiffuse.a;
                float diffuseW = normalW * depthW;
                diffuseW *= CompareMaterials(sampleMaterialID, centerMaterialID, gDiffMaterialMask);

                sumWDiffuseIllumination += diffuseW;
                sumDiffuseIllumination += sampleDiffuseIllumination.rgb * diffuseW;
                sumDiffuse1stMoment += sampleDiffuse1stMoment * diffuseW;
                sumDiffuse2ndMoment += sampleDiffuse2ndMoment * diffuseW;
#endif
            }
        }

        float boost = max(1.0, 4.0 / (historyLength + 1.0));

#if( defined RELAX_SPECULAR )
        sumWSpecularIllumination = max(sumWSpecularIllumination, 1e-6f);
        sumSpecularIllumination /= sumWSpecularIllumination;
        sumSpecular1stMoment /= sumWSpecularIllumination;
        sumSpecular2ndMoment /= sumWSpecularIllumination;
        float specularVariance = max(0, sumSpecular2ndMoment - sumSpecular1stMoment * sumSpecular1stMoment);
        specularVariance *= boost;
        gOutSpecularIlluminationAndVariance[pixelPos] = float4(sumSpecularIllumination, specularVariance);
#endif

#if( defined RELAX_DIFFUSE )
        sumWDiffuseIllumination = max(sumWDiffuseIllumination, 1e-6f);
        sumDiffuseIllumination /= sumWDiffuseIllumination;
        sumDiffuse1stMoment /= sumWDiffuseIllumination;
        sumDiffuse2ndMoment /= sumWDiffuseIllumination;
        float diffuseVariance = max(0, sumDiffuse2ndMoment - sumDiffuse1stMoment * sumDiffuse1stMoment);
        diffuseVariance *= boost;
        gOutDiffuseIlluminationAndVariance[pixelPos] = float4(sumDiffuseIllumination, diffuseVariance);
#endif
    }

}
