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
    // Preload
    float isSky = gTiles[pixelPos >> 4];
    PRELOAD_INTO_SMEM_WITH_TILE_CHECK;

    // Tile-based early out
    if (isSky != 0.0)
        return;

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
    float3 clampedSpecularYCoCg = specularYCoCg;
    if (gSpecFastHistory) clampedSpecularYCoCg = clamp(specularYCoCg, specularColorMinYCoCg, specularColorMaxYCoCg);
    float3 clampedSpecular = STL::Color::YCoCgToLinear(clampedSpecularYCoCg);

    // If history length is less than gHistoryFixFrameNum,
    // then it is the pixel with history fix applied in the previous (history fix) shader,
    // so data from responsive history needs to be copied to normal history,
    // and no history clamping is needed.
    float4 outSpecular = float4(clampedSpecular, specularIlluminationAnd2ndMoment.a);
    float4 outSpecularResponsive = float4(STL::Color::YCoCgToLinear(specularCenterYCoCg.rgb), specularCenterYCoCg.a);
    if (historyLength <= gHistoryFixFrameNum)
        outSpecular = outSpecularResponsive;

    // Writing out the results
    gOutSpecularIllumination[pixelPos.xy] = outSpecular;
    gOutSpecularIlluminationResponsive[pixelPos.xy] = outSpecularResponsive;

    #ifdef RELAX_SH
        float4 specularSH1 = gSpecularSH1[pixelPos.xy];
        float roughnessModified = specularSH1.w;
        float4 specularResponsiveSH1 = gSpecularResponsiveSH1[pixelPos.xy];

        // Clamping factor: (clamped - slow) / (fast - slow)
        // The closest clamped is to fast, the closer clamping factor is to 1.
        float specClampingFactor = (specularCenterYCoCg.x - specularYCoCg.x) == 0 ?
            1.0 : saturate( (clampedSpecularYCoCg.x - specularYCoCg.x) / (specularCenterYCoCg.x - specularYCoCg.x));

        if (historyLength <= gHistoryFixFrameNum)
            specClampingFactor = 1.0;

        gOutSpecularSH1[pixelPos.xy] = float4(lerp(specularSH1.rgb, specularResponsiveSH1.rgb, specClampingFactor), roughnessModified);
        gOutSpecularResponsiveSH1[pixelPos.xy] = specularResponsiveSH1;
    #endif

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
    float3 clampedDiffuseYCoCg = diffuseYCoCg;
    if (gDiffFastHistory) clampedDiffuseYCoCg = clamp(diffuseYCoCg, diffuseColorMinYCoCg, diffuseColorMaxYCoCg);
    float3 clampedDiffuse = STL::Color::YCoCgToLinear(clampedDiffuseYCoCg);

    // If history length is less than gHistoryFixFrameNum,
    // then it is the pixel with history fix applied in the previous (history fix) shader,
    // so data from responsive history needs to be copied to normal history,
    // and no history clamping is needed.
    float4 outDiffuse = float4(clampedDiffuse, diffuseIlluminationAnd2ndMoment.a);
    float4 outDiffuseResponsive = float4(STL::Color::YCoCgToLinear(diffuseCenterYCoCg.rgb), diffuseCenterYCoCg.a);
    if (historyLength <= gHistoryFixFrameNum)
        outDiffuse = outDiffuseResponsive;

    // Writing out the results
    gOutDiffuseIllumination[pixelPos.xy] = outDiffuse;
    gOutDiffuseIlluminationResponsive[pixelPos.xy] = outDiffuseResponsive;

    #ifdef RELAX_SH
        float4 diffuseSH1 = gDiffuseSH1[pixelPos.xy];
        float4 diffuseResponsiveSH1 = gDiffuseResponsiveSH1[pixelPos.xy];

        // Clamping factor: (clamped - slow) / (fast - slow)
        // The closest clamped is to fast, the closer clamping factor is to 1.
        float diffClampingFactor = (diffuseCenterYCoCg.x - diffuseYCoCg.x) == 0 ?
            1.0 : saturate( (clampedDiffuseYCoCg.x - diffuseYCoCg.x) / (diffuseCenterYCoCg.x - diffuseYCoCg.x));

        if (historyLength <= gHistoryFixFrameNum)
            diffClampingFactor = 1.0;

        gOutDiffuseSH1[pixelPos.xy] = lerp(diffuseSH1, diffuseResponsiveSH1, diffClampingFactor);
        gOutDiffuseResponsiveSH1[pixelPos.xy] = diffuseResponsiveSH1;
    #endif

#endif

    // Writing out history length for use in the next frame
    gOutHistoryLength[pixelPos] = historyLength / 255.0;
}
