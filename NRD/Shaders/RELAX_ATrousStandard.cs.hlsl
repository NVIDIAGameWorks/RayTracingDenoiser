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
    float4x4    gClipToWorld;
    float4x4    gViewToClip;

    int2        gResolution;
    float2      gInvViewSize;

    float       gSpecularPhiLuminance;
    float       gDiffusePhiLuminance;
    float       gPhiDepth;
    float       gPhiNormal;

    uint        gStepSize;
    float       gRoughnessEdgeStoppingRelaxation;
    float       gNormalEdgeStoppingRelaxation;
    float       gLuminanceEdgeStoppingRelaxation;
};

#include "RELAX_Common.hlsl"

// Inputs
NRI_RESOURCE(Texture2D<float4>, gSpecularIlluminationAndVariance, t, 0, 0);
NRI_RESOURCE(Texture2D<float4>, gDiffuseIlluminationAndVariance, t, 1, 0);
NRI_RESOURCE(Texture2D<float>, gHistoryLength, t, 2, 0);
NRI_RESOURCE(Texture2D<float>, gSpecularReprojectionConfidence, t, 3, 0);
NRI_RESOURCE(Texture2D<uint2>, gNormalRoughnessDepth, t, 4, 0);

// Outputs
NRI_RESOURCE(RWTexture2D<float4>, gOutSpecularIlluminationAndVariance, u, 0, 0);
NRI_RESOURCE(RWTexture2D<float4>, gOutDiffuseIlluminationAndVariance, u, 1, 0);

// Helper macros
#define linearStep(a, b, x) saturate((x - a)/(b - a))
#define PI 3.141593

#define smoothStep01(x) (x*x*(3.0 - 2.0*x))

float smoothStep(float a, float b, float x)
{
    x = linearStep(a, b, x); return smoothStep01(x);
}

// Helper functions
float deLinearizeDepth(float linearDepth)
{
    float4 viewPos = float4(0, 0, linearDepth, 1);
    float4 clipPos = mul(gViewToClip, viewPos);
    return clipPos.z / clipPos.w;
}

float3 getCurrentWorldPos(int2 pixelPos, float depth)
{
    float2 uv = ((float2)pixelPos + float2(0.5, 0.5)) * gInvViewSize;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1);
    float4 worldPos = mul(gClipToWorld, clipPos);
    return worldPos.xyz / worldPos.w;
}

float getGeometryWeight(float3 centerWorldPos, float3 centerNormal, float3 sampleWorldPos, float phiDepth)
{
	float distanceToCenterPointPlane = abs(dot(sampleWorldPos - centerWorldPos, centerNormal));
	return (isnan(distanceToCenterPointPlane) ? 1.0 : distanceToCenterPointPlane) / (phiDepth + 1e-6);
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

    // Approximation of https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf (page 72)
    // for [0..1] domain:

    // float k = 0.75; // % of NDF volume. Is it the trimming factor from VNDF sampling?
    // return atan(m * k / (1.0 - k));

    return PI*r2 / (1.0 + 0.5*r2 + r3);
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
    // Relaxing normal weights 
    // and if specular reprojection confidence is low
    float relaxation = lerp(1.0, specularReprojectionConfidence, gNormalEdgeStoppingRelaxation);
    float f = 0.9 + 0.1 * saturate(numFramesInHistory / 5.0) * relaxation;

    // This is the main parameter - cone angle
    float angle = getSpecularLobeHalfAngle(roughness);

    // Increasing angle ~10x to relax rejection of the neighbors if specular reprojection confidence is low
    angle *= 3.0 - 2.666 * relaxation * saturate(numFramesInHistory / 5.0);
    angle = min(0.5 * PI, angle);

    // Mitigate banding introduced by errors caused by normals being stored in octahedral 8+8 (Oct16) format
    // See http://jcgt.org/published/0003/02/01/ "A Survey of Efficient Representations for Independent Unit Vectors"
    angle += 0.94 * PI / 180.0;

    return float2(angle, f);
}

float getSpecularNormalWeight(float2 params0, float3 n0, float3 n)
{
    // Assuming that "n0" is normalized and "n" is not!
    float cosa = saturate(dot(n0, n));// *STL::Math::Rsqrt(STL::Math::LengthSquared(n)));
    float a = acos(cosa);
    a = 1.0 - smoothStep(0.0, params0.x, a);

    return saturate(1.0 + (a - 1.0) * params0.y);
}


[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 ipos = dispatchThreadId.xy;
    if (any(ipos >= gResolution)) return;

    const float epsVariance      = 1e-6;

    float4 centerSpecularIlluminationAndVariance = gSpecularIlluminationAndVariance[ipos];
    float centerSpecularLuminance = STL::Color::Luminance(centerSpecularIlluminationAndVariance.rgb);

    float4 centerDiffuseIlluminationAndVariance = gDiffuseIlluminationAndVariance[ipos];
    float centerDiffuseLuminance = STL::Color::Luminance(centerDiffuseIlluminationAndVariance.rgb);
    float specularReprojectionConfidence = gSpecularReprojectionConfidence[ipos];

    // Variance, NOT filtered using 3x3 gaussin blur, as we don't need this in other than 1st Atrous pass
    float centerSpecularVar = centerSpecularIlluminationAndVariance.a;
    float centerDiffuseVar = centerDiffuseIlluminationAndVariance.a;

    float3 centerNormal;
    float centerRoughness;
    float centerDepth;
    UnpackNormalRoughnessDepth(centerNormal, centerRoughness, centerDepth, gNormalRoughnessDepth[ipos]);

    float2 roughnessWeightParams = getRoughnessWeightParams(centerRoughness, specularReprojectionConfidence);
    float2 normalWeightParams = getNormalWeightParams(centerRoughness, gHistoryLength[ipos], specularReprojectionConfidence);

    float3 centerWorldPos = getCurrentWorldPos(ipos, centerDepth.x);

    float specularPhiLIllumination = gSpecularPhiLuminance * sqrt(max(0.0, epsVariance + centerSpecularVar));
    float diffusePhiLIllumination = gDiffusePhiLuminance * sqrt(max(0.0, epsVariance + centerDiffuseVar));
    float phiDepth = gPhiDepth;

    float sumWSpecular = 0;
    float4 sumSpecularIlluminationAndVariance = 0;

    float sumWDiffuse = 0;
    float4 sumDiffuseIlluminationAndVariance = 0;

    static const float kernelWeightGaussian3x3[2] = { 0.44198, 0.27901 };

    float3 sampleWorldPos = 0;

    [unroll]
    for (int yy = -1; yy <= 1; yy++)
    {
        [unroll]
        for (int xx = -1; xx <= 1; xx++)
        {
            int2 p = ipos + int2(xx, yy) * gStepSize;
            bool isInside = all(p >= int2(0, 0)) && all(p < gResolution);
            bool isCenter = ((xx == 0) && (yy == 0));
            
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
            sampleWorldPos = getCurrentWorldPos(p, sampleDepth);

            // Calculating geometry and normal weights
            float geometryW = getGeometryWeight(centerWorldPos, centerNormal, sampleWorldPos, phiDepth);

            float normalWSpecular = getSpecularNormalWeight(normalWeightParams, centerNormal, sampleNormal);
            float normalWDiffuse = getDiffuseNormalWeight(centerNormal, sampleNormal, gPhiNormal);

            // Calculating luminande weigths
            float specularLuminanceW = abs(centerSpecularLuminance - sampleSpecularLuminance) / specularPhiLIllumination;
            float diffuseLuminanceW = abs(centerDiffuseLuminance - sampleDiffuseLuminance) / diffusePhiLIllumination;

            // Roughness weight for specular
            float specularRoughnessW = getRoughnessWeight(roughnessWeightParams, sampleRoughness);

            // Adjusting specular weight to allow more blur for pixels with low reprojection confidence value  
            if(gStepSize <= 4)
            {
                float relaxation = lerp(1.0, specularReprojectionConfidence, gLuminanceEdgeStoppingRelaxation);
                specularLuminanceW *= relaxation;
            }

            // Calculating bilateral weight for specular
            float wSpecular = isCenter ? kernel : max(1e-6, normalWSpecular * exp(-geometryW - specularLuminanceW)) * specularRoughnessW * kernel;

            // Calculating bilateral weight for diffuse
            float wDiffuse = isCenter ? kernel : max(1e-6, normalWDiffuse * exp(-geometryW - diffuseLuminanceW)) * kernel;

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

    // renormalization is different for variance, check paper for the formula
    float4 filteredSpecularIlluminationAndVariance = float4(sumSpecularIlluminationAndVariance / float4(sumWSpecular.xxx, sumWSpecular * sumWSpecular));
    float4 filteredDiffuseIlluminationAndVariance = float4(sumDiffuseIlluminationAndVariance / float4(sumWDiffuse.xxx, sumWDiffuse * sumWDiffuse));

    //filteredSpecularIlluminationAndVariance = specularReprojectionConfidence;

    gOutSpecularIlluminationAndVariance[ipos] =  filteredSpecularIlluminationAndVariance;
    gOutDiffuseIlluminationAndVariance[ipos] =  filteredDiffuseIlluminationAndVariance;
}
