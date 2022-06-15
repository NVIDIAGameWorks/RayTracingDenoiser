/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// Helper functions
float getDiffuseNormalWeight(float3 centerNormal, float3 pointNormal)
{
    return pow(max(0.01, dot(centerNormal, pointNormal)), max(gDisocclusionFixEdgeStoppingNormalPower, 0.01));
}

float getRadius(float numFramesInHistory)
{
    return gMaxRadius / (numFramesInHistory + 1.0);
}

// Main
[numthreads(GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint2 pixelPos : SV_DispatchThreadId)
{

    float centerViewZ = gViewZFP16[pixelPos] / NRD_FP16_VIEWZ_SCALE;
    float historyLength = 255.0 * gHistoryLength[pixelPos];

    // Early out if linearZ is beyond denoising range
    // Early out if no disocclusion detected
    [branch]
    if ((centerViewZ > gDenoisingRange) || (historyLength > gFramesToFix))
        return;

#if( defined RELAX_DIFFUSE )
    float4 diffuseIlluminationAnd2ndMoment = gDiffuseIllumination[pixelPos];
#endif
#if( defined RELAX_SPECULAR )
    float4 specularIlluminationAnd2ndMoment = gSpecularIllumination[pixelPos];
#endif

    // Loading center data
    float centerMaterialID;
    float4 centerNormalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[pixelPos], centerMaterialID);
    float3 centerNormal = centerNormalRoughness.rgb;
    float centerRoughness = centerNormalRoughness.a;
    float3 centerWorldPos = GetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ);
    float normalWeightParams = GetNormalWeightParams(centerRoughness, 0.33);

    // Running sparse cross-bilateral filter
#if( defined RELAX_DIFFUSE )
    float4 diffuseIlluminationAnd2ndMomentSum = diffuseIlluminationAnd2ndMoment;
    float diffuseWSum = 1;
#endif
#if( defined RELAX_SPECULAR )
    float4 specularIlluminationAnd2ndMomentSum = specularIlluminationAnd2ndMoment;
    float specularWSum = 1;
#endif

    float r = getRadius(historyLength);

    [unroll]
    for (int j = -2; j <= 2; j++)
    {
        [unroll]
        for (int i = -2; i <= 2; i++)
        {
            int dx = (int)(i * r);
            int dy = (int)(j * r);

            int2 samplePosInt = (int2)pixelPos + int2(dx, dy);

            bool isInside = all(samplePosInt >= int2(0, 0)) && all(samplePosInt < (int2)gRectSize);
            if ((i == 0) && (j == 0))
                continue;

            float sampleMaterialID;
            float3 sampleNormal = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[samplePosInt], sampleMaterialID).rgb;

            float sampleViewZ = gViewZFP16[samplePosInt] / NRD_FP16_VIEWZ_SCALE;
            float3 sampleWorldPos = GetCurrentWorldPosFromPixelPos(samplePosInt, sampleViewZ);
            float geometryWeight = GetPlaneDistanceWeight(
                centerWorldPos,
                centerNormal,
                gOrthoMode == 0 ? centerViewZ : 1.0,
                sampleWorldPos,
                gDepthThreshold);

    #if( defined RELAX_DIFFUSE )
            // Summing up diffuse result
            float diffuseW = geometryWeight;
            diffuseW *= getDiffuseNormalWeight(centerNormal, sampleNormal);
            diffuseW = isInside ? diffuseW : 0;
            diffuseW *= CompareMaterials(sampleMaterialID, centerMaterialID, gDiffMaterialMask);

            if (diffuseW > 1e-4)
            {
                float4 sampleDiffuseIlluminationAnd2ndMoment = gDiffuseIllumination[samplePosInt];
                diffuseIlluminationAnd2ndMomentSum += sampleDiffuseIlluminationAnd2ndMoment * diffuseW;
                diffuseWSum += diffuseW;
            }
    #endif
    #if( defined RELAX_SPECULAR )
            // Summing up specular result
            float specularW = geometryWeight;
            specularW *= GetNormalWeight(normalWeightParams, centerNormal, sampleNormal);
            specularW = isInside ? specularW : 0;
            specularW *= CompareMaterials(sampleMaterialID, centerMaterialID, gSpecMaterialMask);

            if (specularW > 1e-4)
            {
                float4 sampleSpecularIlluminationAnd2ndMoment = gSpecularIllumination[samplePosInt];
                specularIlluminationAnd2ndMomentSum += sampleSpecularIlluminationAnd2ndMoment * specularW;
                specularWSum += specularW;
            }
    #endif
        }
    }

    // Output buffers will hold the pixels with disocclusion processed by history fix.
    // The next shader will have to copy these areas to normal and responsive history buffers.
#if( defined RELAX_DIFFUSE )
    float4 outDiffuseIlluminationAnd2ndMoment = diffuseIlluminationAnd2ndMomentSum / diffuseWSum;
    gOutDiffuseIllumination[pixelPos] = outDiffuseIlluminationAnd2ndMoment;
#endif

#if( defined RELAX_SPECULAR )
    float4 outSpecularIlluminationAnd2ndMoment = specularIlluminationAnd2ndMomentSum / specularWSum;
    gOutSpecularIllumination[pixelPos] = outSpecularIlluminationAnd2ndMoment;
#endif
}
