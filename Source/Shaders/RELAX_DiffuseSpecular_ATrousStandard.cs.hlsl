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
#include "RELAX_DiffuseSpecular_ATrousStandard.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

// Helper functions
float3 getCurrentWorldPos(int2 pixelPos, float viewZ)
{
    float2 uv = ((float2)pixelPos + float2(0.5, 0.5)) * gInvRectSize * 2.0 - 1.0;
    return viewZ * (gFrustumForward.xyz + gFrustumRight.xyz * uv.x - gFrustumUp.xyz * uv.y);
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

[numthreads(8, 8, 1)]
NRD_EXPORT void NRD_CS_MAIN(int2 ipos : SV_DispatchThreadId)
{
    float centerMaterialType;
    float4 centerNormalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[ipos], centerMaterialType);
    float3 centerNormal = centerNormalRoughness.rgb;
    float centerRoughness = centerNormalRoughness.a;

    float centerViewZ = gViewZFP16[ipos] / NRD_FP16_VIEWZ_SCALE;

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerViewZ > gDenoisingRange)
    {
#if( RELAX_BLACK_OUT_INF_PIXELS == 1 )
        gOutSpecularIlluminationAndVariance[ipos] = 0;
        gOutDiffuseIlluminationAndVariance[ipos] = 0;
#endif
        return;
    }

    float4 centerSpecularIlluminationAndVariance = gSpecularIlluminationAndVariance[ipos];
    float centerSpecularLuminance = STL::Color::Luminance(centerSpecularIlluminationAndVariance.rgb);

    float4 centerDiffuseIlluminationAndVariance = gDiffuseIlluminationAndVariance[ipos];
    float centerDiffuseLuminance = STL::Color::Luminance(centerDiffuseIlluminationAndVariance.rgb);
    float specularReprojectionConfidence = gSpecularReprojectionConfidence[ipos];

    float specularLuminanceWeightRelaxation = 1.0;
    if (gStepSize <= 4)
    {
        specularLuminanceWeightRelaxation = lerp(1.0, specularReprojectionConfidence, gLuminanceEdgeStoppingRelaxation);
    }

    // Variance, NOT filtered using 3x3 gaussin blur, as we don't need this in other than 1st Atrous pass
    float centerSpecularVar = centerSpecularIlluminationAndVariance.a;
    float centerDiffuseVar = centerDiffuseIlluminationAndVariance.a;

    float2 roughnessWeightParams = getRoughnessWeightParams(centerRoughness, specularReprojectionConfidence);
    float2 normalWeightParams = GetNormalWeightParams_ATrous(centerRoughness, 255.0 * gHistoryLength[ipos].y, specularReprojectionConfidence, gNormalEdgeStoppingRelaxation, gSpecularLobeAngleFraction);

    float3 centerWorldPos = getCurrentWorldPos(ipos, centerViewZ);
    float3 centerV = normalize(centerWorldPos);

    float specularPhiLIllumination = 1.0e-4 + gSpecularPhiLuminance * sqrt(max(0.0, centerSpecularVar));
    float diffusePhiLIllumination = 1.0e-4 + gDiffusePhiLuminance * sqrt(max(0.0, centerDiffuseVar));
    float phiDepth = gPhiDepth;

    static const float kernelWeightGaussian3x3[2] = { 0.44198, 0.27901 };

    float sumWSpecular = 0.44198 * 0.44198;
    float4 sumSpecularIlluminationAndVariance = centerSpecularIlluminationAndVariance * float4(sumWSpecular.xxx, sumWSpecular * sumWSpecular);

    float sumWDiffuse = 0.44198 * 0.44198;
    float4 sumDiffuseIlluminationAndVariance = centerDiffuseIlluminationAndVariance * float4(sumWDiffuse.xxx, sumWDiffuse * sumWDiffuse);

    // Adding random offsets to minimize "ringing" at large A-Trous steps
    uint2 offset = 0;
    if (gStepSize > 4)
    {
        STL::Rng::Initialize(ipos, gFrameIndex);
        offset = int2(gStepSize.xx * 0.5 * (STL::Rng::GetFloat2() - 0.5));
    }

    [unroll]
    for (int yy = -1; yy <= 1; yy++)
    {
        [unroll]
        for (int xx = -1; xx <= 1; xx++)
        {
            int2 p = ipos + offset + int2(xx, yy) * gStepSize;
            bool isInside = all(p >= int2(0, 0)) && all(p < gResolution);
            bool isCenter = ((xx == 0) && (yy == 0));
            if (isCenter) continue;

            float kernel = kernelWeightGaussian3x3[abs(xx)] * kernelWeightGaussian3x3[abs(yy)];

            // Discarding out of screen samples
            float wSpecular = isInside ? kernel : 0.0;
            float wDiffuse = isInside ? kernel : 0.0;

            // Fetching normal, roughness, linear Z
            float sampleMaterialType;
            float4 sampleNormalRoughnes = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[p], sampleMaterialType);
            float3 sampleNormal = sampleNormalRoughnes.rgb;
            float sampleRoughness = sampleNormalRoughnes.a;
            float sampleViewZ = gViewZFP16[p] / NRD_FP16_VIEWZ_SCALE;

            // Calculating sample world position
            float3 sampleWorldPos = getCurrentWorldPos(p, sampleViewZ);

            // Calculating geometry weight for diffuse and specular
            float geometryW = exp_approx(-GetGeometryWeight(centerWorldPos, centerNormal, centerViewZ, sampleWorldPos, phiDepth));

#if NRD_USE_MATERIAL_ID_AWARE_FILTERING
            geometryW *= (sampleMaterialType == centerMaterialType) ? 1.0 : 0.0;
#endif

            // Calculating normal weights for diffuse and specular
            float normalWSpecular = GetSpecularNormalWeight_ATrous(normalWeightParams, gSpecularLobeAngleSlack, centerNormal, sampleNormal);
            float normalWDiffuse = GetDiffuseNormalWeight_ATrous(centerNormal, sampleNormal, gPhiNormal);

            // Calculating roughness weight for specular
            float roughnessWSpecular = getRoughnessWeight(roughnessWeightParams, sampleRoughness);

            // Applying all the weights except luminance weights
            wDiffuse *= geometryW * normalWDiffuse;
            wSpecular *= geometryW * (gRoughnessEdgeStoppingEnabled ? (normalWSpecular * roughnessWSpecular) : normalWDiffuse);

            // Summing up specular
            if (wSpecular > 1e-4)
            {
                float4 sampleSpecularIlluminationAndVariance = gSpecularIlluminationAndVariance[p];
                float sampleSpecularLuminance = STL::Color::Luminance(sampleSpecularIlluminationAndVariance.rgb);

                float specularLuminanceW = abs(centerSpecularLuminance - sampleSpecularLuminance) / specularPhiLIllumination;
                // Adjusting specular weight to allow more blur for pixels with low reprojection confidence value
                specularLuminanceW *= specularLuminanceWeightRelaxation;
                specularLuminanceW = min(gMaxLuminanceRelativeDifference, specularLuminanceW);

                wSpecular *= exp_approx(-specularLuminanceW);
                sumSpecularIlluminationAndVariance += float4(wSpecular.xxx, wSpecular * wSpecular) * sampleSpecularIlluminationAndVariance;
                sumWSpecular += wSpecular;
            }

            // Summing up diffuse
            if (wDiffuse > 1e-4)
            {
                float4 sampleDiffuseIlluminationAndVariance = gDiffuseIlluminationAndVariance[p];
                float sampleDiffuseLuminance = STL::Color::Luminance(sampleDiffuseIlluminationAndVariance.rgb);

                float diffuseLuminanceW = abs(centerDiffuseLuminance - sampleDiffuseLuminance) / diffusePhiLIllumination;
                diffuseLuminanceW = min(gMaxLuminanceRelativeDifference, diffuseLuminanceW);

                wDiffuse *= exp_approx(-diffuseLuminanceW);
                sumDiffuseIlluminationAndVariance += float4(wDiffuse.xxx, wDiffuse * wDiffuse) * sampleDiffuseIlluminationAndVariance;
                sumWDiffuse += wDiffuse;
            }
        }
    }

    float4 filteredSpecularIlluminationAndVariance = float4(sumSpecularIlluminationAndVariance / float4(sumWSpecular.xxx, sumWSpecular * sumWSpecular));
    float4 filteredDiffuseIlluminationAndVariance = float4(sumDiffuseIlluminationAndVariance / float4(sumWDiffuse.xxx, sumWDiffuse * sumWDiffuse));

    gOutSpecularIlluminationAndVariance[ipos] = filteredSpecularIlluminationAndVariance;
    gOutDiffuseIlluminationAndVariance[ipos] = filteredDiffuseIlluminationAndVariance;
}
