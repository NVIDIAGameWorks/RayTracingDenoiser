/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifdef RELAX_SPECULAR
    groupshared float4 sharedSpecularYCoCg[GROUP_Y + BORDER * 2][GROUP_X + BORDER * 2];
#endif

#ifdef RELAX_DIFFUSE
    groupshared float4 sharedDiffuseYCoCg[GROUP_Y + BORDER * 2][GROUP_X + BORDER * 2];
#endif

void Preload(uint2 sharedPos, int2 globalPos)
{
    globalPos = clamp(globalPos, 0, gRectSize - 1.0);

    #ifdef RELAX_SPECULAR
        float4 specularResponsive = gSpecularIlluminationResponsive[globalPos];
        sharedSpecularYCoCg[sharedPos.y][sharedPos.x] = float4(STL::Color::LinearToYCoCg(specularResponsive.rgb), specularResponsive.a);
    #endif

    #ifdef RELAX_DIFFUSE
        float4 diffuseResponsive = gDiffuseIlluminationResponsive[globalPos];
        sharedDiffuseYCoCg[sharedPos.y][sharedPos.x] = float4(STL::Color::LinearToYCoCg(diffuseResponsive.rgb), diffuseResponsive.a);
    #endif
}

[numthreads(GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint2 pixelPos : SV_DispatchThreadId, uint2 threadPos : SV_GroupThreadId, uint threadIndex : SV_GroupIndex)
{
    PRELOAD_INTO_SMEM;
    // Shared memory is populated with responsive history now and can be used for history clamping

    // Reading history length
    float historyLength = 255.0 * gHistoryLength[pixelPos];

    // Reading normal history
#ifdef RELAX_SPECULAR
    float4 specularIlluminationAnd2ndMoment = gSpecularIllumination[pixelPos];
    float3 specularYCoCg = STL::Color::LinearToYCoCg(specularIlluminationAnd2ndMoment.rgb);
    float3 specularFirstMomentYCoCg = 0;
    float3 specularSecondMomentYCoCg = 0;
#endif

#ifdef RELAX_DIFFUSE
    float4 diffuseIlluminationAnd2ndMoment = gDiffuseIllumination[pixelPos];
    float3 diffuseYCoCg = STL::Color::LinearToYCoCg(diffuseIlluminationAnd2ndMoment.rgb);
    float3 diffuseFirstMomentYCoCg = 0;
    float3 diffuseSecondMomentYCoCg = 0;
#endif

    // Running history clamping
    uint2 sharedMemoryIndex = threadPos.xy + int2(BORDER, BORDER);
    [unroll]
    for (int dx = -2; dx <= 2; dx++)
    {
        [unroll]
        for (int dy = -2; dy <= 2; dy++)
        {
            uint2 sharedMemoryIndexP = sharedMemoryIndex + int2(dx, dy);

#ifdef RELAX_SPECULAR
            float3 specularSampleYCoCg = sharedSpecularYCoCg[sharedMemoryIndexP.y][sharedMemoryIndexP.x].rgb;
            specularFirstMomentYCoCg += specularSampleYCoCg;
            specularSecondMomentYCoCg += specularSampleYCoCg * specularSampleYCoCg;
#endif

#ifdef RELAX_DIFFUSE
            float3 diffuseSampleYCoCg = sharedDiffuseYCoCg[sharedMemoryIndexP.y][sharedMemoryIndexP.x].rgb;
            diffuseFirstMomentYCoCg += diffuseSampleYCoCg;
            diffuseSecondMomentYCoCg += diffuseSampleYCoCg * diffuseSampleYCoCg;
#endif
        }
    }

#ifdef RELAX_SPECULAR
    // Calculating color box
    specularFirstMomentYCoCg /= 25.0;
    specularSecondMomentYCoCg /= 25.0;
    float3 specularSigmaYCoCg = sqrt(max(0.0f, specularSecondMomentYCoCg - specularFirstMomentYCoCg * specularFirstMomentYCoCg));
    float3 specularColorMinYCoCg = specularFirstMomentYCoCg - gColorBoxSigmaScale * specularSigmaYCoCg;
    float3 specularColorMaxYCoCg = specularFirstMomentYCoCg + gColorBoxSigmaScale * specularSigmaYCoCg;

    // Expanding color box with color of the center pixel to minimize introduced bias
    float4 specularCenterYCoCg = sharedSpecularYCoCg[sharedMemoryIndex.y][sharedMemoryIndex.x];
    specularColorMinYCoCg = min(specularColorMinYCoCg, specularCenterYCoCg.rgb);
    specularColorMaxYCoCg = max(specularColorMaxYCoCg, specularCenterYCoCg.rgb);

    // Color clamping
    if (gSpecFastHistory)
        specularYCoCg = clamp(specularYCoCg, specularColorMinYCoCg, specularColorMaxYCoCg);
    float3 clampedSpecular = STL::Color::YCoCgToLinear(specularYCoCg);

    // If history length is less than gFramesToFix,
    // then it is the pixel with history fix applied in the previous (history fix) shader,
    // so data from responsive history needs to be copied to normal history,
    // and no history clamping is needed.
    float4 outSpecular = float4(clampedSpecular, specularIlluminationAnd2ndMoment.a);
    float4 outSpecularResponsive = float4(STL::Color::YCoCgToLinear(specularCenterYCoCg.rgb), specularCenterYCoCg.a);
    if (historyLength <= gFramesToFix)
        outSpecular = outSpecularResponsive;

    // Writing out the results
    gOutSpecularIllumination[pixelPos.xy] = outSpecular;
    gOutSpecularIlluminationResponsive[pixelPos.xy] = outSpecularResponsive;
#endif

#ifdef RELAX_DIFFUSE
    // Calculating color box
    diffuseFirstMomentYCoCg /= 25.0;
    diffuseSecondMomentYCoCg /= 25.0;
    float3 diffuseSigmaYCoCg = sqrt(max(0.0f, diffuseSecondMomentYCoCg - diffuseFirstMomentYCoCg * diffuseFirstMomentYCoCg));
    float3 diffuseColorMinYCoCg = diffuseFirstMomentYCoCg - gColorBoxSigmaScale * diffuseSigmaYCoCg;
    float3 diffuseColorMaxYCoCg = diffuseFirstMomentYCoCg + gColorBoxSigmaScale * diffuseSigmaYCoCg;

    // Expanding color box with color of the center pixel to minimize introduced bias
    float4 diffuseCenterYCoCg = sharedDiffuseYCoCg[sharedMemoryIndex.y][sharedMemoryIndex.x];
    diffuseColorMinYCoCg = min(diffuseColorMinYCoCg, diffuseCenterYCoCg.rgb);
    diffuseColorMaxYCoCg = max(diffuseColorMaxYCoCg, diffuseCenterYCoCg.rgb);

    // Color clamping
    if (gDiffFastHistory)
        diffuseYCoCg = clamp(diffuseYCoCg, diffuseColorMinYCoCg, diffuseColorMaxYCoCg);
    float3 clampedDiffuse = STL::Color::YCoCgToLinear(diffuseYCoCg);

    // If history length is less than gFramesToFix,
    // then it is the pixel with history fix applied in the previous (history fix) shader,
    // so data from responsive history needs to be copied to normal history,
    // and no history clamping is needed.
    float4 outDiffuse = float4(clampedDiffuse, diffuseIlluminationAnd2ndMoment.a);
    float4 outDiffuseResponsive = float4(STL::Color::YCoCgToLinear(diffuseCenterYCoCg.rgb), diffuseCenterYCoCg.a);
    if (historyLength <= gFramesToFix)
        outDiffuse = outDiffuseResponsive;

    // Writing out the results
    gOutDiffuseIllumination[pixelPos.xy] = outDiffuse;
    gOutDiffuseIlluminationResponsive[pixelPos.xy] = outDiffuseResponsive;
#endif

    // Writing out history length for use in the next frame
    gOutHistoryLength[pixelPos] = historyLength / 255.0;
}
