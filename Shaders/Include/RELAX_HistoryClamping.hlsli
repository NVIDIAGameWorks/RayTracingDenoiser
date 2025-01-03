/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifdef RELAX_SPECULAR
    groupshared float4 s_SpecResponsiveYCoCg[BUFFER_Y][BUFFER_X];
    groupshared float4 s_SpecNoisy_M2[BUFFER_Y][BUFFER_X];
#endif

#ifdef RELAX_DIFFUSE
    groupshared float4 s_DiffNoisy_M2[BUFFER_Y][BUFFER_X];
    groupshared float4 s_DiffResponsiveYCoCg[BUFFER_Y][BUFFER_X];
#endif

void Preload(uint2 sharedPos, int2 globalPos)
{
    globalPos = clamp(globalPos, 0, gRectSize - 1.0);

    #ifdef RELAX_SPECULAR
        float4 specularResponsive = gIn_SpecFast[globalPos];
        s_SpecResponsiveYCoCg[sharedPos.y][sharedPos.x] = float4(Color::RgbToYCoCg(specularResponsive.rgb), specularResponsive.a);

        float4 specularNoisy = gIn_SpecNoisy[globalPos];
        float specularNoisyLuminance = Color::Luminance(specularNoisy.rgb);
        s_SpecNoisy_M2[sharedPos.y][sharedPos.x] = float4(specularNoisy.rgb, specularNoisyLuminance * specularNoisyLuminance);

    #endif

    #ifdef RELAX_DIFFUSE
        float4 diffuseResponsive = gIn_DiffFast[globalPos];
        s_DiffResponsiveYCoCg[sharedPos.y][sharedPos.x] = float4(Color::RgbToYCoCg(diffuseResponsive.rgb), diffuseResponsive.a);

        float4 diffuseNoisy = gIn_DiffNoisy[globalPos];
        float diffuseNoisyLuminance = Color::Luminance(diffuseNoisy.rgb);
        s_DiffNoisy_M2[sharedPos.y][sharedPos.x] = float4(diffuseNoisy.rgb, diffuseNoisyLuminance * diffuseNoisyLuminance);
    #endif
}

/*
// TODO: antilag code can be simplified to this:
    float f = saturate( ( lumaSlowClamped - lumaSlow ) / ( lumaFast - lumaSlow ) * gAccelerationAmount );
    slow = lerp( slow, fast, f );
    fast = lerp( fast, noisyFirstMoment, f );
*/

[numthreads(GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN( NRD_CS_MAIN_ARGS )
{
    NRD_CTA_ORDER_DEFAULT;

    // Preload
    float isSky = gIn_Tiles[pixelPos >> 4];
    PRELOAD_INTO_SMEM_WITH_TILE_CHECK;

    // Tile-based early out
    if (isSky != 0.0 || pixelPos.x >= gRectSize.x || pixelPos.y >= gRectSize.y)
        return;

    // Reading history length
    float historyLength = 255.0 * gIn_HistoryLength[pixelPos];

    // Reading normal history
#ifdef RELAX_SPECULAR
    float3 specularResponsiveFirstMomentYCoCg = 0;
    float3 specularResponsiveSecondMomentYCoCg = 0;
    float3 specularNoisyFirstMoment = 0;
    float specularNoisySecondMoment = 0;
#endif

#ifdef RELAX_DIFFUSE
    float3 diffuseResponsiveFirstMomentYCoCg = 0;
    float3 diffuseResponsiveSecondMomentYCoCg = 0;
    float3 diffuseNoisyFirstMoment = 0;
    float diffuseNoisySecondMoment = 0;
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
            float3 specularSampleYCoCg = s_SpecResponsiveYCoCg[sharedMemoryIndexP.y][sharedMemoryIndexP.x].rgb;
            specularResponsiveFirstMomentYCoCg += specularSampleYCoCg;
            specularResponsiveSecondMomentYCoCg += specularSampleYCoCg * specularSampleYCoCg;

            float4 specularNoisySample = s_SpecNoisy_M2[sharedMemoryIndexP.y][sharedMemoryIndexP.x];
            specularNoisyFirstMoment += specularNoisySample.rgb;
            specularNoisySecondMoment += specularNoisySample.a;
#endif

#ifdef RELAX_DIFFUSE
            float3 diffuseSampleYCoCg = s_DiffResponsiveYCoCg[sharedMemoryIndexP.y][sharedMemoryIndexP.x].rgb;
            diffuseResponsiveFirstMomentYCoCg += diffuseSampleYCoCg;
            diffuseResponsiveSecondMomentYCoCg += diffuseSampleYCoCg * diffuseSampleYCoCg;

            float4 diffuseNoisySample = s_DiffNoisy_M2[sharedMemoryIndexP.y][sharedMemoryIndexP.x];
            diffuseNoisyFirstMoment += diffuseNoisySample.rgb;
            diffuseNoisySecondMoment += diffuseNoisySample.a;
#endif
        }
    }

#ifdef RELAX_SPECULAR
    // Calculating color box
    specularResponsiveFirstMomentYCoCg /= 25.0;
    specularResponsiveSecondMomentYCoCg /= 25.0;
    specularNoisyFirstMoment /= 25.0;
    specularNoisySecondMoment /= 25.0;
    float3 specularResponsiveSigmaYCoCg = sqrt(max(0.0f, specularResponsiveSecondMomentYCoCg - specularResponsiveFirstMomentYCoCg * specularResponsiveFirstMomentYCoCg));
    float3 specularResponsiveColorMinYCoCg = specularResponsiveFirstMomentYCoCg - gColorBoxSigmaScale * specularResponsiveSigmaYCoCg;
    float3 specularResponsiveColorMaxYCoCg = specularResponsiveFirstMomentYCoCg + gColorBoxSigmaScale * specularResponsiveSigmaYCoCg;

    // Expanding color box with color of the center pixel to minimize introduced bias
    float4 specularResponsiveCenterYCoCg = s_SpecResponsiveYCoCg[sharedMemoryIndex.y][sharedMemoryIndex.x];
    specularResponsiveColorMinYCoCg = min(specularResponsiveColorMinYCoCg, specularResponsiveCenterYCoCg.rgb);
    specularResponsiveColorMaxYCoCg = max(specularResponsiveColorMaxYCoCg, specularResponsiveCenterYCoCg.rgb);

    // Clamping color with color box expansion
    float4 specularIlluminationAnd2ndMoment = gIn_Spec[pixelPos];
    float3 specularYCoCg = Color::RgbToYCoCg(specularIlluminationAnd2ndMoment.rgb);
    float3 clampedSpecularYCoCg = specularYCoCg;
    if (gSpecMaxFastAccumulatedFrameNum < gSpecMaxAccumulatedFrameNum)
        clampedSpecularYCoCg = clamp(specularYCoCg, specularResponsiveColorMinYCoCg, specularResponsiveColorMaxYCoCg);
    float3 clampedSpecular = Color::YCoCgToRgb(clampedSpecularYCoCg);

    // If history length is less than gHistoryFixFrameNum,
    // then it is the pixel with history fix applied in the previous (history fix) shader,
    // so data from responsive history needs to be copied to normal history,
    // and no history clamping is needed.
    float4 outSpecular = float4(clampedSpecular, specularIlluminationAnd2ndMoment.a);
    float3 specularResponsiveCenter = Color::YCoCgToRgb(specularResponsiveCenterYCoCg.rgb);
    float4 outSpecularResponsive = float4(specularResponsiveCenter, specularResponsiveCenterYCoCg.a);
    if (historyLength <= gHistoryFixFrameNum)
        outSpecular = outSpecularResponsive;

    // Clamping factor: (clamped - slow) / (fast - slow)
     // The closer clamped is to fast, the closer clamping factor is to 1.
    float specClampingFactor = (clampedSpecularYCoCg.x - specularYCoCg.x) == 0 ?
        0.0 : saturate((clampedSpecularYCoCg.x - specularYCoCg.x) / (specularResponsiveCenterYCoCg.x - specularYCoCg.x));

    if (historyLength <= gHistoryFixFrameNum)
        specClampingFactor = 1.0;

    // History acceleration based on (responsive - normal)
    // Decreased 3x for specular since specular reprojection logic already has a set of various history rejection heuristics that diffuse does not have
    float specularHistoryDifferenceL = 0.33 * RELAX_ANTILAG_ACCELERATION_AMOUNT_SCALE * gHistoryAccelerationAmount * Color::Luminance(abs(specularResponsiveCenter.rgb - specularIlluminationAnd2ndMoment.rgb));

    // History acceleration amount should be proportional to history clamping amount
    specularHistoryDifferenceL *= specClampingFactor;

    // No history acceleration if there is no difference between normal and responsive history
    if (historyLength <= gHistoryFixFrameNum)
        specularHistoryDifferenceL = 0;

    // Using color space distance from responsive history to averaged noisy input to accelerate history
    float3 specularColorDistanceToNoisyInput = specularNoisyFirstMoment.rgb - specularResponsiveCenter;

    float specularColorDistanceToNoisyInputL = Color::Luminance(abs(specularColorDistanceToNoisyInput));

    float3 specularColorAcceleration = (specularColorDistanceToNoisyInputL == 0) ?
        0.0 : specularColorDistanceToNoisyInput * specularHistoryDifferenceL / specularColorDistanceToNoisyInputL;

    // Preventing overshooting and noise amplification by making sure luminance of accelerated responsive history
    // does not move beyond luminance of noisy input,
    // or does not move back from noisy input
    float specularColorAccelerationL = Color::Luminance(abs(specularColorAcceleration.rgb));

    float specularColorAccelerationRatio = (specularColorAccelerationL == 0) ?
        0 : specularColorDistanceToNoisyInputL / specularColorAccelerationL;

    if (specularColorAccelerationRatio < 1.0)
        specularColorAcceleration *= specularColorAccelerationRatio;

    if (specularColorAccelerationRatio <= 0.0)
        specularColorAcceleration = 0;

    // Accelerating history
    outSpecular.rgb += specularColorAcceleration;
    outSpecularResponsive.rgb += specularColorAcceleration;

    // Calculating possibility for history reset
    float specularL = Color::Luminance(specularIlluminationAnd2ndMoment.rgb);
    float specularNoisyInputL = Color::Luminance(specularNoisyFirstMoment.rgb);
    float noisyspecularTemporalSigma = gHistoryResetTemporalSigmaScale * sqrt(max(0.0f, specularNoisySecondMoment - specularNoisyInputL * specularNoisyInputL));
    float noisyspecularSpatialSigma = gHistoryResetSpatialSigmaScale * specularResponsiveSigmaYCoCg.x;
    float specularHistoryResetAmount = 0.5 * gHistoryResetAmount * max(0, abs(specularL - specularNoisyInputL) - noisyspecularSpatialSigma - noisyspecularTemporalSigma) / (1.0e-6 + max(specularL, specularNoisyInputL) + noisyspecularSpatialSigma + noisyspecularTemporalSigma);
    specularHistoryResetAmount = saturate(specularHistoryResetAmount);

    // Resetting history
    outSpecular.rgb = lerp(outSpecular.rgb, s_SpecNoisy_M2[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb, specularHistoryResetAmount);
    outSpecularResponsive.rgb = lerp(outSpecularResponsive.rgb, s_SpecNoisy_M2[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb, specularHistoryResetAmount);

    // 2nd moment correction
    float outSpecularL = Color::Luminance(outSpecular.rgb);
    float specularMomentCorrection = (outSpecularL * outSpecularL - specularL * specularL);

    outSpecular.a += specularMomentCorrection;
    outSpecular.a = max(0, outSpecular.a);

    // Writing outputs
    gOut_Spec[pixelPos.xy] = outSpecular;
    gOut_SpecFast[pixelPos.xy] = outSpecularResponsive;

#ifdef RELAX_SH
    float4 specularSH = gIn_SpecSh[pixelPos.xy];
    float4 specularResponsiveSH = gIn_SpecShFast[pixelPos.xy];

    gOut_SpecSh[pixelPos.xy] = lerp(specularSH, specularResponsiveSH, specClampingFactor);
    gOut_SpecShFast[pixelPos.xy] = specularResponsiveSH;
#endif

#endif

#ifdef RELAX_DIFFUSE
    // Calculating color box
    diffuseResponsiveFirstMomentYCoCg /= 25.0;
    diffuseResponsiveSecondMomentYCoCg /= 25.0;
    diffuseNoisyFirstMoment /= 25.0;
    diffuseNoisySecondMoment /= 25.0;
    float3 diffuseResponsiveSigmaYCoCg = sqrt(max(0.0f, diffuseResponsiveSecondMomentYCoCg - diffuseResponsiveFirstMomentYCoCg * diffuseResponsiveFirstMomentYCoCg));
    float3 diffuseResponsiveColorMinYCoCg = diffuseResponsiveFirstMomentYCoCg - gColorBoxSigmaScale * diffuseResponsiveSigmaYCoCg;
    float3 diffuseResponsiveColorMaxYCoCg = diffuseResponsiveFirstMomentYCoCg + gColorBoxSigmaScale * diffuseResponsiveSigmaYCoCg;

    // Expanding color box with color of the center pixel to minimize introduced bias
    float4 diffuseResponsiveCenterYCoCg = s_DiffResponsiveYCoCg[sharedMemoryIndex.y][sharedMemoryIndex.x];
    diffuseResponsiveColorMinYCoCg = min(diffuseResponsiveColorMinYCoCg, diffuseResponsiveCenterYCoCg.rgb);
    diffuseResponsiveColorMaxYCoCg = max(diffuseResponsiveColorMaxYCoCg, diffuseResponsiveCenterYCoCg.rgb);


    // Clamping color with color box expansion
    float4 diffuseIlluminationAnd2ndMoment = gIn_Diff[pixelPos];
    float3 diffuseYCoCg = Color::RgbToYCoCg(diffuseIlluminationAnd2ndMoment.rgb);
    float3 clampedDiffuseYCoCg = diffuseYCoCg;
    if (gDiffMaxFastAccumulatedFrameNum < gDiffMaxAccumulatedFrameNum)
        clampedDiffuseYCoCg = clamp(diffuseYCoCg, diffuseResponsiveColorMinYCoCg, diffuseResponsiveColorMaxYCoCg);
    float3 clampedDiffuse = Color::YCoCgToRgb(clampedDiffuseYCoCg);

    // If history length is less than gHistoryFixFrameNum,
    // then it is the pixel with history fix applied in the previous (history fix) shader,
    // so data from responsive history needs to be copied to normal history,
    // and no history clamping is needed.
    float4 outDiffuse = float4(clampedDiffuse, diffuseIlluminationAnd2ndMoment.a);
    float3 diffuseResponsiveCenter = Color::YCoCgToRgb(diffuseResponsiveCenterYCoCg.rgb);
    float4 outDiffuseResponsive = float4(diffuseResponsiveCenter, 0);
    if (historyLength <= gHistoryFixFrameNum)
        outDiffuse.rgb = outDiffuseResponsive.rgb;

    // Clamping factor: (clamped - slow) / (fast - slow)
    // The closer clamped is to fast, the closer clamping factor is to 1.
    float diffClampingFactor = (clampedDiffuseYCoCg.x - diffuseYCoCg.x) == 0 ?
        0.0 : saturate((clampedDiffuseYCoCg.x - diffuseYCoCg.x) / (diffuseResponsiveCenterYCoCg.x - diffuseYCoCg.x));

    if (historyLength <= gHistoryFixFrameNum)
        diffClampingFactor = 1.0;

    // History acceleration based on (responsive - normal)
    float diffuseHistoryDifferenceL = RELAX_ANTILAG_ACCELERATION_AMOUNT_SCALE * gHistoryAccelerationAmount * Color::Luminance(abs(diffuseResponsiveCenter - diffuseIlluminationAnd2ndMoment.rgb));

    // History acceleration amount should be proportional to history clamping amount
    diffuseHistoryDifferenceL *= diffClampingFactor;

    // No history acceleration if there is no difference between normal and responsive history
    if (historyLength <= gHistoryFixFrameNum)
        diffuseHistoryDifferenceL = 0;

    // Using color space distance from responsive history to averaged noisy input to accelerate history
    float3 diffuseColorDistanceToNoisyInput = diffuseNoisyFirstMoment.rgb - diffuseResponsiveCenter;

    float diffuseColorDistanceToNoisyInputL = Color::Luminance(abs(diffuseColorDistanceToNoisyInput));

    float3 diffuseColorAcceleration = (diffuseColorDistanceToNoisyInputL == 0) ?
        0.0 : diffuseColorDistanceToNoisyInput * diffuseHistoryDifferenceL / diffuseColorDistanceToNoisyInputL;

    // Preventing overshooting and noise amplification by making sure luminance of accelerated responsive history
    // does not move beyond luminance of noisy input,
    // or does not move back from noisy input
    float diffuseColorAccelerationL = Color::Luminance(abs(diffuseColorAcceleration.rgb));

    float diffuseColorAccelerationRatio = (diffuseColorAccelerationL == 0) ?
        0 : diffuseColorDistanceToNoisyInputL / diffuseColorAccelerationL;

    if (diffuseColorAccelerationRatio < 1.0)
        diffuseColorAcceleration *= diffuseColorAccelerationRatio;

    if (diffuseColorAccelerationRatio <= 0.0)
        diffuseColorAcceleration = 0;

    // Accelerating history
    outDiffuse.rgb += diffuseColorAcceleration;
    outDiffuseResponsive.rgb += diffuseColorAcceleration;

    // Calculating possibility for history reset
    float diffuseL = Color::Luminance(diffuseIlluminationAnd2ndMoment.rgb);
    float diffuseNoisyInputL = Color::Luminance(diffuseNoisyFirstMoment.rgb);
    float noisydiffuseTemporalSigma = gHistoryResetTemporalSigmaScale * sqrt(max(0.0f, diffuseNoisySecondMoment - diffuseNoisyInputL * diffuseNoisyInputL));
    float noisydiffuseSpatialSigma = gHistoryResetSpatialSigmaScale * diffuseResponsiveSigmaYCoCg.x;
    float diffuseHistoryResetAmount = gHistoryResetAmount * max(0, abs(diffuseL - diffuseNoisyInputL) - noisydiffuseSpatialSigma - noisydiffuseTemporalSigma) / (1.0e-6 + max(diffuseL, diffuseNoisyInputL) + noisydiffuseSpatialSigma + noisydiffuseTemporalSigma);
    diffuseHistoryResetAmount = saturate(diffuseHistoryResetAmount);
    // Resetting history
    outDiffuse.rgb = lerp(outDiffuse.rgb, s_DiffNoisy_M2[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb, diffuseHistoryResetAmount);
    outDiffuseResponsive.rgb = lerp(outDiffuseResponsive.rgb, s_DiffNoisy_M2[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb, diffuseHistoryResetAmount);

    // 2nd moment correction
    float outDiffuseL = Color::Luminance(outDiffuse.rgb);
    float diffuseMomentCorrection = (outDiffuseL * outDiffuseL - diffuseL * diffuseL);

    outDiffuse.a += diffuseMomentCorrection;
    outDiffuse.a = max(0, outDiffuse.a);

    // Writing outputs
    gOut_Diff[pixelPos.xy] = outDiffuse;
    gOut_DiffFast[pixelPos.xy] = outDiffuseResponsive;

    #ifdef RELAX_SH
        float4 diffuseSH = gIn_DiffSh[pixelPos.xy];
        float4 diffuseResponsiveSH = gIn_DiffShFast[pixelPos.xy];

        gOut_DiffSh[pixelPos.xy] = lerp(diffuseSH, diffuseResponsiveSH, diffClampingFactor);
        gOut_DiffShFast[pixelPos.xy] = diffuseResponsiveSH;
    #endif

#endif

    // Writing out history length for use in the next frame
    gOut_HistoryLength[pixelPos] = historyLength / 255.0;
}
