/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "NRD.hlsl"
#include "STL.hlsl"
#include "RELAX_Config.hlsl"

NRI_RESOURCE(cbuffer, globalConstants, b, 0, 0)
{
    float4 gFrustumRight;
    float4 gFrustumUp;
    float4 gFrustumForward;
    int2   gResolution;
    float2 gInvRectSize;
    float  gSpecularPhiLuminance;
    float  gDiffusePhiLuminance;
    float  gPhiDepth;
    float  gPhiNormal;
    uint   gStepSize;
    uint   gIsLastPass;
    float  gRoughnessEdgeStoppingRelaxation;
    float  gNormalEdgeStoppingRelaxation;
    float  gLuminanceEdgeStoppingRelaxation;
    float  gDenoisingRange;
};

#include "NRD_Common.hlsl"
#include "RELAX_Common.hlsl"

// Inputs
NRI_RESOURCE(Texture2D<float4>, gSpecularIlluminationAndVariance, t, 0, 0);
NRI_RESOURCE(Texture2D<float4>, gDiffuseIlluminationAndVariance, t, 1, 0);
NRI_RESOURCE(Texture2D<float2>, gHistoryLength, t, 2, 0);
NRI_RESOURCE(Texture2D<float>, gSpecularReprojectionConfidence, t, 3, 0);
NRI_RESOURCE(Texture2D<uint2>, gNormalRoughnessDepth, t, 4, 0);

// Outputs
NRI_RESOURCE(RWTexture2D<float4>, gOutSpecularIlluminationAndVariance, u, 0, 0);
NRI_RESOURCE(RWTexture2D<float4>, gOutDiffuseIlluminationAndVariance, u, 1, 0);

// Helper functions
float3 getCurrentWorldPos(int2 pixelPos, float depth)
{
    float2 uv = ((float2)pixelPos + float2(0.5, 0.5)) * gInvRectSize * 2.0 - 1.0;
    return depth * (gFrustumForward.xyz + gFrustumRight.xyz * uv.x - gFrustumUp.xyz * uv.y);
}

float getGeometryWeight(float3 centerWorldPos, float3 centerNormal, float3 sampleWorldPos, float phiDepth)
{
	float distanceToCenterPointPlane = abs(dot(sampleWorldPos - centerWorldPos, centerNormal));
	return distanceToCenterPointPlane / (phiDepth + 1e-6);
}

float getDiffuseNormalWeight(float3 centerNormal, float3 sampleNormal, float phiNormal)
{
    return pow(saturate(dot(centerNormal, sampleNormal)), phiNormal);
}

float getSpecularLobeHalfAngle(float roughness)
{
    // Defines a cone angle, where micro-normals are distributed
    float r2 = roughness * roughness;
    float r3 = roughness * r2;
    return 3.141592 * r2 / (1.0 + 0.5*r2 + r3);
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

float2 getNormalWeightParams(float roughness, float numFramesInHistory, float specularReprojectionConfidence)
{
    // Relaxing normal weights if not enough frames in history
    // and if specular reprojection confidence is low
    float relaxation = lerp(1.0, specularReprojectionConfidence, gNormalEdgeStoppingRelaxation);
    float f = 0.9 + 0.1 * saturate(numFramesInHistory / 5.0) * relaxation;

    // This is the main parameter - cone angle
    float angle = getSpecularLobeHalfAngle(roughness);

    // Increasing angle ~10x to relax rejection of the neighbors if specular reprojection confidence is low
    angle *= 3.0 - 2.666 * relaxation * saturate(numFramesInHistory / 5.0);
    angle = min(0.5 * 3.141592, angle);

    return float2(angle, f);
}

float getSpecularVWeight(float2 params0, float3 v0, float3 v)
{
    float cosa = saturate(dot(v0, v));
    float a = STL::Math::AcosApprox(cosa) * 0.5;
    a = 1.0 - STL::Math::SmoothStep(0.0, params0.x, a);
    return saturate(1.0 + (a - 1.0) * params0.y);
}

float getSpecularNormalWeight(float2 params0, float3 n0, float3 n)
{
    float cosa = saturate(dot(n0, n));
    float a = STL::Math::AcosApprox(cosa);
    params0.x += 1.0 * 3.141592 / 180.0; // adding 1 deg of slack to avoid "faceted" look caused by low precision of normals
    a = 1.0 - STL::Math::SmoothStep(0.0, params0.x, a);
    return saturate(1.0 + (a - 1.0) * params0.y);
}

[numthreads(8, 8, 1)]
void main(int2 ipos : SV_DispatchThreadID)
{
    float3 centerNormal;
    float centerRoughness;
    float centerDepth;
    UnpackNormalRoughnessDepth(centerNormal, centerRoughness, centerDepth, gNormalRoughnessDepth[ipos]);

    // Early out if linearZ is beyond denoising range
    [branch]
    if (centerDepth > gDenoisingRange)
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
    float2 normalWeightParams = getNormalWeightParams(centerRoughness, 255.0*gHistoryLength[ipos].y, specularReprojectionConfidence);

    float3 centerWorldPos = getCurrentWorldPos(ipos, centerDepth);
    float3 centerV = normalize(centerWorldPos);

    float specularPhiLIllumination = 1.0e-4 + gSpecularPhiLuminance * sqrt(max(0.0, centerSpecularVar));
    float diffusePhiLIllumination = 1.0e-4 + gDiffusePhiLuminance * sqrt(max(0.0, centerDiffuseVar));
    float phiDepth = gPhiDepth;

    static const float kernelWeightGaussian3x3[2] = { 0.44198, 0.27901 };

    float sumWSpecular = 0.44198 * 0.44198;
    float4 sumSpecularIlluminationAndVariance = centerSpecularIlluminationAndVariance * float4(sumWSpecular.xxx, sumWSpecular * sumWSpecular);

    float sumWDiffuse = 0.44198 * 0.44198;
    float4 sumDiffuseIlluminationAndVariance = centerDiffuseIlluminationAndVariance * float4(sumWDiffuse.xxx, sumWDiffuse * sumWDiffuse);

    [unroll]
    for (int yy = -1; yy <= 1; yy++)
    {
        [unroll]
        for (int xx = -1; xx <= 1; xx++)
        {
            int2 p = ipos + int2(xx, yy) * gStepSize;
            bool isInside = all(p >= int2(0, 0)) && all(p < gResolution);
            bool isCenter = ((xx == 0) && (yy == 0));
            if(isCenter) continue;

            float kernel = kernelWeightGaussian3x3[abs(xx)] * kernelWeightGaussian3x3[abs(yy)];

            float3 sampleNormal;
            float sampleRoughness;
            float sampleDepth;
            UnpackNormalRoughnessDepth(sampleNormal, sampleRoughness, sampleDepth, gNormalRoughnessDepth[p]);

            float4 sampleSpecularIlluminationAndVariance = gSpecularIlluminationAndVariance[p];
            float sampleSpecularLuminance = STL::Color::Luminance(sampleSpecularIlluminationAndVariance.rgb);

            float4 sampleDiffuseIlluminationAndVariance = gDiffuseIlluminationAndVariance[p];
            float sampleDiffuseLuminance = STL::Color::Luminance(sampleDiffuseIlluminationAndVariance.rgb);

            // Calculating sample world position
            float3 sampleWorldPos = getCurrentWorldPos(p, sampleDepth);
            float3 sampleV = normalize(sampleWorldPos);

            // Calculating geometry and normal weights
            float geometryW = getGeometryWeight(centerWorldPos, centerNormal, sampleWorldPos, phiDepth);

            float normalWSpecular = getSpecularNormalWeight(normalWeightParams, centerNormal, sampleNormal);
            normalWSpecular *= getSpecularVWeight(normalWeightParams, centerV, sampleV);
            float normalWDiffuse = getDiffuseNormalWeight(centerNormal, sampleNormal, gPhiNormal);

            // Calculating luminande weigths
            float specularLuminanceW = abs(centerSpecularLuminance - sampleSpecularLuminance) / specularPhiLIllumination;
            float diffuseLuminanceW = abs(centerDiffuseLuminance - sampleDiffuseLuminance) / diffusePhiLIllumination;

            // Roughness weight for specular
            float specularRoughnessW = getRoughnessWeight(roughnessWeightParams, sampleRoughness);

            // Adjusting specular weight to allow more blur for pixels with low reprojection confidence value
            specularLuminanceW *= specularLuminanceWeightRelaxation;

            // Calculating bilateral weight for specular
            float wSpecular = max(1e-6, normalWSpecular * exp(-geometryW - specularLuminanceW)) * specularRoughnessW * kernel;

            // Calculating bilateral weight for diffuse
            float wDiffuse = max(1e-6, normalWDiffuse * exp(-geometryW - diffuseLuminanceW)) * kernel;

            // Discarding out of screen samples
            wSpecular *= isInside ? 1.0 : 0.0;
            wDiffuse *= isInside ? 1.0 : 0.0;

            // Alpha channel contains the variance, therefore the weights need to be squared, see paper for the formula
            sumWSpecular += wSpecular;
            sumSpecularIlluminationAndVariance += float4(wSpecular.xxx, wSpecular * wSpecular) * sampleSpecularIlluminationAndVariance;

            sumWDiffuse += wDiffuse;
            sumDiffuseIlluminationAndVariance += float4(wDiffuse.xxx, wDiffuse * wDiffuse) * sampleDiffuseIlluminationAndVariance;
        }
    }

    float4 filteredSpecularIlluminationAndVariance = float4(sumSpecularIlluminationAndVariance / float4(sumWSpecular.xxx, sumWSpecular * sumWSpecular));
    float4 filteredDiffuseIlluminationAndVariance = float4(sumDiffuseIlluminationAndVariance / float4(sumWDiffuse.xxx, sumWDiffuse * sumWDiffuse));

    if( gIsLastPass != 0 )
    {
        filteredSpecularIlluminationAndVariance.xyz = RELAX_BackEnd_UnpackRadiance( filteredSpecularIlluminationAndVariance.xyz, centerRoughness );
        filteredDiffuseIlluminationAndVariance.xyz = RELAX_BackEnd_UnpackRadiance( filteredDiffuseIlluminationAndVariance.xyz );
    }

    gOutSpecularIlluminationAndVariance[ipos] = filteredSpecularIlluminationAndVariance;
    gOutDiffuseIlluminationAndVariance[ipos] = filteredDiffuseIlluminationAndVariance;
}
