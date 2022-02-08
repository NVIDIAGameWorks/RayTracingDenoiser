/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"
#include "RELAX_Diffuse_ATrousStandard.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

[numthreads(16, 16, 1)]
NRD_EXPORT void NRD_CS_MAIN(int2 ipos : SV_DispatchThreadId)
{
    uint centerMaterialType;
    float3 centerNormal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[ipos], centerMaterialType).rgb;

    float centerViewZ = gViewZFP16[ipos] / NRD_FP16_VIEWZ_SCALE;

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerViewZ > gDenoisingRange)
    {
#if( RELAX_BLACK_OUT_INF_PIXELS == 1 )
        gOutDiffuseIlluminationAndVariance[ipos] = 0;
#endif
        return;
    }

    float4 centerDiffuseIlluminationAndVariance = gDiffuseIlluminationAndVariance[ipos];
    float centerDiffuseLuminance = STL::Color::Luminance(centerDiffuseIlluminationAndVariance.rgb);

    // Variance, NOT filtered using 3x3 gaussin blur, as we don't need this in other than 1st Atrous pass
    float centerDiffuseVar = centerDiffuseIlluminationAndVariance.a;

    float3 centerWorldPos = GetCurrentWorldPos(ipos, centerViewZ);

    float diffusePhiLIllumination = 1.0e-4 + gDiffusePhiLuminance * sqrt(max(0.0, centerDiffuseVar));
    float depthThreshold = gDepthThreshold;

    static const float kernelWeightGaussian3x3[2] = { 0.44198, 0.27901 };

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
            bool isInside = all(p >= int2(0, 0)) && all(p < (int2)gRectSize);
            bool isCenter = ((xx == 0) && (yy == 0));
            if (isCenter) continue;

            float kernel = kernelWeightGaussian3x3[abs(xx)] * kernelWeightGaussian3x3[abs(yy)];

            // Discarding out of screen samples
            float wDiffuse = isInside ? kernel : 0.0;

            // Fetching normal, roughness, linear Z
            uint sampleMaterialType;
            float3 sampleNormal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[p], sampleMaterialType).rgb;
            float sampleViewZ = gViewZFP16[p] / NRD_FP16_VIEWZ_SCALE;

            // Calculating sample world position
            float3 sampleWorldPos = GetCurrentWorldPos(p, sampleViewZ);

            // Calculating geometry weight for diffuse and specular
            float geometryW = GetPlaneDistanceWeight(
                                centerWorldPos,
                                centerNormal,
                                gIsOrtho == 0 ? centerViewZ : 1.0,
                                sampleWorldPos,
                                depthThreshold);

#if NRD_USE_MATERIAL_ID_AWARE_FILTERING
            geometryW *= (sampleMaterialType == centerMaterialType) ? 1.0 : 0.0;
#endif

            // Calculating normal weight for diffuse
            float normalWDiffuse = GetDiffuseNormalWeight_ATrous(centerNormal, sampleNormal, gPhiNormal);

            // Applying all the weights except luminance weights
            wDiffuse *= geometryW * normalWDiffuse;

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

    float4 filteredDiffuseIlluminationAndVariance = float4(sumDiffuseIlluminationAndVariance / float4(sumWDiffuse.xxx, sumWDiffuse * sumWDiffuse));

    gOutDiffuseIlluminationAndVariance[ipos] = filteredDiffuseIlluminationAndVariance;
}
