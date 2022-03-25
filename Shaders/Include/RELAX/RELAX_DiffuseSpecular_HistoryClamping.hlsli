/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#if( defined RELAX_SPECULAR )
    groupshared float4 sharedSpecular[GROUP_Y + BORDER * 2][GROUP_X + BORDER * 2];
#endif

#if( defined RELAX_DIFFUSE )
    groupshared float4 sharedDiffuse[GROUP_Y + BORDER * 2][GROUP_X + BORDER * 2];
#endif

void Preload(uint2 sharedPos, int2 globalPos)
{
    globalPos = clamp(globalPos, 0, gRectSize - 1.0);

    #if( defined RELAX_SPECULAR )
        float3 specularResponsive = gSpecularIlluminationResponsive[globalPos].rgb;
        sharedSpecular[sharedPos.y][sharedPos.x] = float4(STL::Color::LinearToYCoCg(specularResponsive), 0);
    #endif

    #if( defined RELAX_DIFFUSE )
        float3 diffuseResponsive = gDiffuseIlluminationResponsive[globalPos].rgb;
        sharedDiffuse[sharedPos.y][sharedPos.x] = float4(STL::Color::LinearToYCoCg(diffuseResponsive), 0);
    #endif
}

[numthreads(GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint2 pixelPos : SV_DispatchThreadId, uint2 threadPos : SV_GroupThreadId, uint threadIndex : SV_GroupIndex)
{
    PRELOAD_INTO_SMEM;
    // Shared memory is populated with responsive history now and can be used for history clamping

    // Reading history length
#if( defined RELAX_SPECULAR && defined RELAX_DIFFUSE)
    float2 specularAndDiffuseHistoryLength = gSpecularAndDiffuseHistoryLength[pixelPos];
#elif( defined RELAX_SPECULAR )
    float specularHistoryLength = gSpecularHistoryLength[pixelPos];
#else
    float diffuseHistoryLength = gDiffuseHistoryLength[pixelPos];
#endif

    // Reading normal history
#if( defined RELAX_SPECULAR )
    float4 specularIlluminationAnd2ndMoment = gSpecularIllumination[pixelPos];
    float3 specularYCoCg = STL::Color::LinearToYCoCg(specularIlluminationAnd2ndMoment.rgb);
    float3 specularFirstMoment = 0;
    float3 specularSecondMoment = 0;
#endif

#if( defined RELAX_DIFFUSE )
    float4 diffuseIlluminationAnd2ndMoment = gDiffuseIllumination[pixelPos];
    float3 diffuseYCoCg = STL::Color::LinearToYCoCg(diffuseIlluminationAnd2ndMoment.rgb);
    float3 diffuseFirstMoment = 0;
    float3 diffuseSecondMoment = 0;
#endif

    // Running history clamping
    uint2 sharedMemoryIndex = threadPos.xy + int2(BORDER, BORDER);
    [unroll]
    for (int dy = -2; dy <= 2; dy++)
    {
        [unroll]
        for (int dx = -2; dx <= 2; dx++)
        {
            uint2 sharedMemoryIndexP = sharedMemoryIndex + int2(dx, dy);
            int2 p = pixelPos.xy + int2(dx, dy);
            if (any(p < int2(0, 0)) || any(p >= (int2)gRectSize)) sharedMemoryIndexP = sharedMemoryIndex;

#if( defined RELAX_SPECULAR )
            float3 specularP = sharedSpecular[sharedMemoryIndexP.y][sharedMemoryIndexP.x].rgb;
            specularFirstMoment += specularP;
            specularSecondMoment += specularP * specularP;
#endif

#if( defined RELAX_DIFFUSE )
            float3 diffuseP = sharedDiffuse[sharedMemoryIndexP.y][sharedMemoryIndexP.x].rgb;
            diffuseFirstMoment += diffuseP;
            diffuseSecondMoment += diffuseP * diffuseP;
#endif
        }
    }

#if( defined RELAX_SPECULAR )
    // Calculating color box
    specularFirstMoment /= 25.0;
    specularSecondMoment /= 25.0;
    float3 specularSigma = sqrt(max(0.0f, specularSecondMoment - specularFirstMoment * specularFirstMoment));
    float3 specularColorMin = specularFirstMoment - gColorBoxSigmaScale * specularSigma;
    float3 specularColorMax = specularFirstMoment + gColorBoxSigmaScale * specularSigma;

    // Expanding color box with color of the center pixel to minimize introduced bias
    float3 specularCenter = sharedSpecular[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb;
    specularColorMin = min(specularColorMin, specularCenter);
    specularColorMax = max(specularColorMax, specularCenter);

    // Color clamping
    specularYCoCg = clamp(specularYCoCg, specularColorMin, specularColorMax);
    float3 clampedSpecular = STL::Color::YCoCgToLinear(specularYCoCg);

    // Writing out the results
    gOutSpecularIllumination[pixelPos.xy] = float4(clampedSpecular, specularIlluminationAnd2ndMoment.a);
#endif

#if( defined RELAX_DIFFUSE )
    // Calculating color box
    diffuseFirstMoment /= 25.0;
    diffuseSecondMoment /= 25.0;
    float3 diffuseSigma = sqrt(max(0.0f, diffuseSecondMoment - diffuseFirstMoment * diffuseFirstMoment));
    float3 diffuseColorMin = diffuseFirstMoment - gColorBoxSigmaScale * diffuseSigma;
    float3 diffuseColorMax = diffuseFirstMoment + gColorBoxSigmaScale * diffuseSigma;

    // Expanding color box with color of the center pixel to minimize introduced bias
    float3 diffuseCenter = sharedDiffuse[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb;
    diffuseColorMin = min(diffuseColorMin, diffuseCenter);
    diffuseColorMax = max(diffuseColorMax, diffuseCenter);

    // Color clamping
    diffuseYCoCg = clamp(diffuseYCoCg, diffuseColorMin, diffuseColorMax);
    float3 clampedDiffuse = STL::Color::YCoCgToLinear(diffuseYCoCg);

    // Writing out the results
    gOutDiffuseIllumination[pixelPos.xy] = float4(clampedDiffuse, diffuseIlluminationAnd2ndMoment.a);
#endif

    // Writing out history length
#if( defined RELAX_SPECULAR && defined RELAX_DIFFUSE)
    gOutSpecularAndDiffuseHistoryLength[pixelPos] = specularAndDiffuseHistoryLength;
#elif( defined RELAX_SPECULAR )
    gOutSpecularHistoryLength[pixelPos] = specularHistoryLength;
#elif( defined RELAX_DIFFUSE )
    gOutDiffuseHistoryLength[pixelPos] = diffuseHistoryLength;
#endif

}
