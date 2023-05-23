/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float4 sharedNormalSpecHitT[BUFFER_Y][BUFFER_X];

// Helper functions

float isReprojectionTapValid(float3 currentWorldPos, float3 previousWorldPos, float3 currentNormal, float disocclusionThreshold)
{
    // Check if plane distance is acceptable
    float3 posDiff = currentWorldPos - previousWorldPos;
    float maxPlaneDistance = abs(dot(posDiff, currentNormal));

    return maxPlaneDistance > disocclusionThreshold ? 0.0 : 1.0;
}

// Returns reprojected data from previous frame calculated using filtering based on filters above.
// Returns reprojection search result based on surface motion:
// 2 - reprojection found, bicubic footprint was used
// 1 - reprojection found, bilinear footprint was used
// 0 - reprojection not found

float loadSurfaceMotionBasedPrevData(
    int2 pixelPos,
    float3 prevWorldPos,
    float2 prevUVSMB,
    float currentLinearZ,
    float3 currentNormal,
#ifdef RELAX_SPECULAR
    float currentReflectionHitT,
#endif
    float NdotV,
    float parallaxInPixels,
    float currentMaterialID,
    uint materialIDMask,
    float mixedDisocclusionDepthThreshold,

    out float footprintQuality,
    out float historyLength
#ifdef RELAX_DIFFUSE
    , out float4 prevDiffuseIllumAnd2ndMoment
    , out float3 prevDiffuseResponsiveIllum
    #ifdef RELAX_SH
        , out float4 prevDiffuseSH1
        , out float4 prevDiffuseResponsiveSH1
    #endif
#endif
#ifdef RELAX_SPECULAR
    , out float4 prevSpecularIllumAnd2ndMoment
    , out float3 prevSpecularResponsiveIllum
    , out float  prevReflectionHitT
    #ifdef RELAX_SH
        , out float4 prevSpecularSH1
        , out float4 prevSpecularResponsiveSH1
    #endif
#endif
)
{
    // Calculating disocclusion threshold
    float pixelSize = PixelRadiusToWorld(gUnproject, gOrthoMode, 1.0, currentLinearZ);
    float frustumSize = pixelSize * min(gRectSize.x, gRectSize.y);
    float disocclusionThreshold = mixedDisocclusionDepthThreshold * frustumSize / lerp(NdotV, 1.0, saturate(parallaxInPixels / 30.0));

    // Calculating previous pixel position
    float2 prevPixelPosFloat = prevUVSMB * gRectSizePrev;

    // Calculating footprint origin and weights
    int2 bilinearOrigin = int2(floor(prevPixelPosFloat - 0.5));
    float2 bilinearWeights = frac(prevPixelPosFloat - 0.5);

    // Consider reprojection to the same pixel index a small motion.
    // It is useful for skipping reprojection test for static camera when the jitter is the only source of motion.
    int2 prevPixelPosInt = int2(prevPixelPosFloat);
    bool isSmallMotion = all(prevPixelPosInt == pixelPos);
    bool skipReprojectionTest = gSkipReprojectionTestWithoutMotion && isSmallMotion;

    // Checking bicubic footprint (with cut corners)
    // remembering bilinear taps validity and worldspace position along the way,
    // for faster weighted bilinear and for calculating previous worldspace position
    // bc - bicubic tap,
    // bl - bicubic & bilinear tap
    //
    // -- bc bc --
    // bc bl bl bc
    // bc bl bl bc
    // -- bc bc --

    /// Fetching previous viewZs and materialIDs
    float2 gatherOrigin00 = (float2(bilinearOrigin)+float2(0.0, 0.0)) * gInvResourceSize;
    float2 gatherOrigin10 = (float2(bilinearOrigin)+float2(2.0, 0.0)) * gInvResourceSize;
    float2 gatherOrigin01 = (float2(bilinearOrigin)+float2(0.0, 2.0)) * gInvResourceSize;
    float2 gatherOrigin11 = (float2(bilinearOrigin)+float2(2.0, 2.0)) * gInvResourceSize;
    float4 prevViewZs00 = gPrevViewZ.GatherRed(gNearestClamp, gatherOrigin00).wzxy;
    float4 prevViewZs10 = gPrevViewZ.GatherRed(gNearestClamp, gatherOrigin10).wzxy;
    float4 prevViewZs01 = gPrevViewZ.GatherRed(gNearestClamp, gatherOrigin01).wzxy;
    float4 prevViewZs11 = gPrevViewZ.GatherRed(gNearestClamp, gatherOrigin11).wzxy;
    float4 prevMaterialIDs00 = gPrevMaterialID.GatherRed(gNearestClamp, gatherOrigin00).wzxy;
    float4 prevMaterialIDs10 = gPrevMaterialID.GatherRed(gNearestClamp, gatherOrigin10).wzxy;
    float4 prevMaterialIDs01 = gPrevMaterialID.GatherRed(gNearestClamp, gatherOrigin01).wzxy;
    float4 prevMaterialIDs11 = gPrevMaterialID.GatherRed(gNearestClamp, gatherOrigin11).wzxy;

    // Calculating validity of 12 bicubic taps, 4 of those are bilinear taps
    float3 prevViewPos = STL::Geometry::AffineTransform(gPrevWorldToView, prevWorldPos);
    float3 planeDist0 = abs(prevViewZs00.yzw - prevViewPos.zzz);
    float3 planeDist1 = abs(prevViewZs10.xzw - prevViewPos.zzz);
    float3 planeDist2 = abs(prevViewZs01.xyw - prevViewPos.zzz);
    float3 planeDist3 = abs(prevViewZs11.xyz - prevViewPos.zzz);
    float3 tapsValid0 = step(planeDist0, disocclusionThreshold);
    float3 tapsValid1 = step(planeDist1, disocclusionThreshold);
    float3 tapsValid2 = step(planeDist2, disocclusionThreshold);
    float3 tapsValid3 = step(planeDist3, disocclusionThreshold);
    tapsValid0 *= CompareMaterials(currentMaterialID.xxx, prevMaterialIDs00.yzw, materialIDMask);
    tapsValid1 *= CompareMaterials(currentMaterialID.xxx, prevMaterialIDs10.xzw, materialIDMask);
    tapsValid2 *= CompareMaterials(currentMaterialID.xxx, prevMaterialIDs01.xyw, materialIDMask);
    tapsValid3 *= CompareMaterials(currentMaterialID.xxx, prevMaterialIDs11.xyz, materialIDMask);

    float bicubicFootprintValid = dot(tapsValid0 + tapsValid1 + tapsValid2 + tapsValid3, 1.0) > 11.5 ? 1.0 : 0.0;
    float4 bilinearTapsValid = float4(tapsValid0.z, tapsValid1.y, tapsValid2.y, tapsValid3.x);
    bilinearTapsValid = skipReprojectionTest ? float4(1.0, 1.0, 1.0, 1.0) : bilinearTapsValid;

    // Using bilinear to average 4 normal samples
    float2 uv = (float2(bilinearOrigin)+float2(1.0, 1.0)) * gInvResourceSize;
    float3 prevNormalFlat = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, uv, 0)).xyz;
    prevNormalFlat = gUseWorldPrevToWorld ? STL::Geometry::RotateVector(gWorldPrevToWorld, prevNormalFlat) : prevNormalFlat;

    // Reject backfacing history: if angle between current normal and previous normal is larger than 90 deg
    [flatten]
    if (dot(currentNormal, prevNormalFlat) < 0.0)
    {
        bilinearTapsValid = 0;
        bicubicFootprintValid = 0;
    }

    // Checking bicubic footprint validity for being in screen
    [flatten]
    if (any(bilinearOrigin < int2(1, 1)) || any(bilinearOrigin >= int2(gRectSizePrev)-int2(2, 2)))
    {
        bicubicFootprintValid = 0;
    }

    // Checking bilinear footprint validity for being in screen
    bilinearTapsValid *= IsInScreen(prevUVSMB);

    // Calculating bilinear weights in advance
    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;
    float4 bilinearCustomWeights = STL::Filtering::GetBilinearCustomWeights(bilinear, float4(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w));

    bool useBicubic = (bicubicFootprintValid > 0);

    // Fetching normal history
    BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
        prevPixelPosFloat, gInvResourceSize,
        bilinearCustomWeights, useBicubic
#ifdef RELAX_DIFFUSE
        , gPrevDiffuseIllumination, prevDiffuseIllumAnd2ndMoment
#endif
#ifdef RELAX_SPECULAR
        , gPrevSpecularIllumination, prevSpecularIllumAnd2ndMoment
#endif
    );

    // Fetching fast history
    float4 spec;
    float4 diff;
    BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
        prevPixelPosFloat, gInvResourceSize,
        bilinearCustomWeights, useBicubic
#ifdef RELAX_DIFFUSE
        , gPrevDiffuseIlluminationResponsive, diff
#endif
#ifdef RELAX_SPECULAR
        , gPrevSpecularIlluminationResponsive, spec
#endif
    );

#ifdef RELAX_DIFFUSE
    prevDiffuseIllumAnd2ndMoment = max(prevDiffuseIllumAnd2ndMoment, 0);
    prevDiffuseResponsiveIllum = max(diff.rgb, 0);
#endif
#ifdef RELAX_SPECULAR
    prevSpecularIllumAnd2ndMoment = max(prevSpecularIllumAnd2ndMoment, 0.0);
    prevSpecularResponsiveIllum = max(spec.rgb, 0);
#endif

    // Fitering previous SH data
    #ifdef RELAX_SH
        #ifdef RELAX_DIFFUSE
            prevDiffuseSH1 = BilinearWithCustomWeightsFloat4(gPrevDiffuseSH1, bilinearOrigin, bilinearCustomWeights);
            prevDiffuseResponsiveSH1 = BilinearWithCustomWeightsFloat4(gPrevDiffuseResponsiveSH1, bilinearOrigin, bilinearCustomWeights);
        #endif
        #ifdef RELAX_SPECULAR
            prevSpecularSH1 = BilinearWithCustomWeightsFloat4(gPrevSpecularSH1, bilinearOrigin, bilinearCustomWeights);
            prevSpecularResponsiveSH1 = BilinearWithCustomWeightsFloat4(gPrevSpecularResponsiveSH1, bilinearOrigin, bilinearCustomWeights);
        #endif
    #endif

    // Fitering more previous data that does not need bicubic
    float2 gatherOrigin = (float2(bilinearOrigin) + 1.0) * gInvResourceSize;
    float4 prevHistoryLengths = gPrevHistoryLength.GatherRed(gNearestClamp, gatherOrigin).wzxy;
    historyLength = 255.0 * BilinearWithCustomWeightsImmediateFloat(
        prevHistoryLengths.x,
        prevHistoryLengths.y,
        prevHistoryLengths.z,
        prevHistoryLengths.w,
        bilinearCustomWeights);

#ifdef RELAX_SPECULAR
    float4 prevReflectionHitTs = gPrevReflectionHitT.GatherRed(gNearestClamp, gatherOrigin).wzxy;
    prevReflectionHitT = BilinearWithCustomWeightsImmediateFloat(
        prevReflectionHitTs.x,
        prevReflectionHitTs.y,
        prevReflectionHitTs.z,
        prevReflectionHitTs.w,
        bilinearCustomWeights);
    prevReflectionHitT = max(0.001, prevReflectionHitT);
#endif

    float reprojectionFound = (bicubicFootprintValid > 0) ? 2.0 : 1.0;
    footprintQuality = (bicubicFootprintValid > 0) ? 1.0 : dot(bilinearCustomWeights, 1.0);

    [flatten]
    if (!any(bilinearTapsValid))
    {
        reprojectionFound = 0;
        footprintQuality = 0;
    }

    return reprojectionFound;
}

// Returns specular reprojection search result based on virtual motion
#ifdef RELAX_SPECULAR
float loadVirtualMotionBasedPrevData(
    int2 pixelPos,
    float3 currentWorldPos,
    float3 currentNormal,
    float currentLinearZ,
    float hitDistFocused,
    float hitDistOriginal,
    float3 currentViewVector,
    float3 prevWorldPos,
    bool surfaceBicubicValid,
    float currentMaterialID,
    uint materialIDMask,
    float2 prevSurfaceMotionBasedUV,
    float parallaxInPixels,
    float NdotV,
    float mixedDisocclusionDepthThreshold,
    out float4 prevSpecularIllumAnd2ndMoment,
    out float4 prevSpecularResponsiveIllum,
    out float3 prevNormal,
    out float prevRoughness,
    out float prevReflectionHitT,
    out float2 prevUVVMB
    #ifdef RELAX_SH
    , out float4 prevSpecularSH1
    , out float4 prevSpecularResponsiveSH1
    #endif
    )
{
    // Calculating previous worldspace virtual position based on reflection hitT
    float3 virtualViewVector = normalize(currentViewVector) * hitDistFocused;
    float3 prevVirtualWorldPos = prevWorldPos + virtualViewVector;

    float currentViewVectorLength = length(currentViewVector);
    float accumulatedSpecularVMBZ = currentViewVectorLength + hitDistFocused;

    float4 prevVirtualClipPos = mul(gPrevWorldToClip, float4(prevVirtualWorldPos, 1.0));
    prevVirtualClipPos.xy /= prevVirtualClipPos.w;
    prevUVVMB = prevVirtualClipPos.xy * float2(0.5, -0.5) + float2(0.5, 0.5);

    // If the focused HitT puts the UV for virtual motion based specular
    // too far from surface motion based UV,
    // then recalculate UV using the non-focused HitT
    [flatten]
    if (length((prevUVVMB - prevSurfaceMotionBasedUV) * gRectSize) > parallaxInPixels + 0.001)
    {
        virtualViewVector = normalize(currentViewVector) * hitDistOriginal;
        prevVirtualWorldPos = prevWorldPos + virtualViewVector;
        currentViewVectorLength = length(currentViewVector);
        accumulatedSpecularVMBZ = currentViewVectorLength + hitDistOriginal;
        prevVirtualClipPos = mul(gPrevWorldToClip, float4(prevVirtualWorldPos, 1.0));
        prevVirtualClipPos.xy /= prevVirtualClipPos.w;
        prevUVVMB = prevVirtualClipPos.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    }

    float2 prevVirtualPixelPosFloat = prevUVVMB * gRectSizePrev;
    float disocclusionThreshold = mixedDisocclusionDepthThreshold * (gOrthoMode == 0 ? currentLinearZ : 1.0);

    // Consider reprojection to the same pixel index a small motion.
    // It is useful for skipping reprojection test for static camera when the jitter is the only source of motion.
    int2 prevVirtualPixelPosInt = int2(prevVirtualPixelPosFloat);
    bool isSmallVirtualMotion = all(prevVirtualPixelPosInt == pixelPos);
    bool skipReprojectionTest = gSkipReprojectionTestWithoutMotion && isSmallVirtualMotion;

    // Calculating footprint origin and weights
    int2 bilinearOrigin = int2(floor(prevVirtualPixelPosFloat - 0.5));
    float2 bilinearWeights = frac(prevVirtualPixelPosFloat - 0.5);
    float2 gatherOrigin = (bilinearOrigin + 1.0) * gInvResourceSize;

    // Taking care of camera motion, because world-space is always centered at camera position in NRD
    currentWorldPos -= gPrevCameraPosition.xyz;

    // Checking bilinear footprint only for virtual motion based specular reprojection
    float4 prevViewZs = gPrevViewZ.GatherRed(gNearestClamp, gatherOrigin).wzxy;
    float4 prevMaterialIDs = gPrevMaterialID.GatherRed(gNearestClamp, gatherOrigin).wzxy;
    float3 prevWorldPosInTap;
    float4 bilinearTapsValid;

    prevWorldPosInTap = GetPreviousWorldPosFromPixelPos(bilinearOrigin + int2(0, 0), prevViewZs.x);
    bilinearTapsValid.x = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    prevWorldPosInTap = GetPreviousWorldPosFromPixelPos(bilinearOrigin + int2(1, 0), prevViewZs.y);
    bilinearTapsValid.y = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    prevWorldPosInTap = GetPreviousWorldPosFromPixelPos(bilinearOrigin + int2(0, 1), prevViewZs.z);
    bilinearTapsValid.z = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    prevWorldPosInTap = GetPreviousWorldPosFromPixelPos(bilinearOrigin + int2(1, 1), prevViewZs.w);
    bilinearTapsValid.w = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);

    bilinearTapsValid *= CompareMaterials(currentMaterialID.xxxx, prevMaterialIDs.xyzw, materialIDMask);
    bilinearTapsValid = skipReprojectionTest ? float4(1.0, 1.0, 1.0, 1.0) : bilinearTapsValid;

    // Checking bilinear footprint validity for being in screen
    bilinearTapsValid *= IsInScreen(prevUVVMB);

    // Applying reprojection
    prevSpecularIllumAnd2ndMoment = 0;
    prevSpecularResponsiveIllum = 0;
    prevNormal = currentNormal;
    prevRoughness = 0;
    prevReflectionHitT = gDenoisingRange;
    #ifdef RELAX_SH
        prevSpecularSH1 = 0;
        prevSpecularResponsiveSH1 = 0;
    #endif

    // Weighted bilinear (or bicubic optionally) for prev specular data based on virtual motion.
    if (any(bilinearTapsValid))
    {
        // Calculating bilinear weights in advance
        STL::Filtering::Bilinear bilinear;
        bilinear.weights = bilinearWeights;
        float4 bilinearCustomWeights = STL::Filtering::GetBilinearCustomWeights(bilinear, float4(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w));

        bool useBicubic = (surfaceBicubicValid > 0) & all(bilinearTapsValid);

        // Fetching normal virtual motion based specular history
        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            prevVirtualPixelPosFloat, gInvResourceSize,
            bilinearCustomWeights, useBicubic,
            gPrevSpecularIllumination, prevSpecularIllumAnd2ndMoment);

        prevSpecularIllumAnd2ndMoment = max(prevSpecularIllumAnd2ndMoment, 0.0);

        // Fetching fast virtual motion based specular history
        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            prevVirtualPixelPosFloat, gInvResourceSize,
            bilinearCustomWeights, useBicubic,
            gPrevSpecularIlluminationResponsive, prevSpecularResponsiveIllum);

        prevSpecularResponsiveIllum = max(prevSpecularResponsiveIllum, 0.0);

        // Fitering previous SH data
        #ifdef RELAX_SH
            prevSpecularSH1 = BilinearWithCustomWeightsFloat4(gPrevSpecularSH1, bilinearOrigin, bilinearCustomWeights);
            prevSpecularResponsiveSH1 = BilinearWithCustomWeightsFloat4(gPrevSpecularResponsiveSH1, bilinearOrigin, bilinearCustomWeights);
        #endif

        // Fitering previous data that does not need bicubic
        prevReflectionHitT = gPrevReflectionHitT.SampleLevel(gLinearClamp, prevUVVMB, 0).x;
        prevReflectionHitT = max(0.001, prevReflectionHitT);

        float2 prevUVVMBforGBuffer = prevUVVMB * gRectSizePrev * gInvResourceSize;
        float4 prevNormalRoughness = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, prevUVVMBforGBuffer, 0));
        prevNormal = prevNormalRoughness.xyz;
        prevNormal = gUseWorldPrevToWorld ? STL::Geometry::RotateVector(gWorldPrevToWorld, prevNormal) : prevNormal;
        prevRoughness = prevNormalRoughness.w;
    }
    // Using all() marks entire virtual motion based specular history footprint
    // invalid for specular reprojection logic down the shader code even if at least one bilinear tap is invalid.
    // This helps rejecting potentially incorrect data.
    return all(bilinearTapsValid) ? 1.0 : 0.0;
}
#endif

void Preload(uint2 sharedPos, int2 globalPos)
{
    globalPos = clamp(globalPos, 0, gRectSize - 1.0);

    float4 normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[globalPos + gRectOrigin]);
    float4 normalSpecHitT = normalRoughness;

#ifdef RELAX_SPECULAR
    float4 inSpecularIllumination = gSpecularIllumination[globalPos + gRectOrigin];
    normalSpecHitT.a = inSpecularIllumination.a;
#endif

    sharedNormalSpecHitT[sharedPos.y][sharedPos.x] = normalSpecHitT;
}

// Main
[numthreads(GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint2 pixelPos : SV_DispatchThreadId, uint2 threadPos : SV_GroupThreadId, uint threadIndex : SV_GroupIndex)
{
    float isSky = gTiles[pixelPos >> 4];
    PRELOAD_INTO_SMEM_WITH_TILE_CHECK;

    // Tile-based early out
    if (isSky != 0.0)
        return;

    // Early out if linearZ is beyond denoising range
    float currentLinearZ = abs(gViewZ[pixelPos.xy + gRectOrigin]);
    if (currentLinearZ > gDenoisingRange)
        return;

    int2 sharedMemoryIndex = threadPos.xy + int2(BORDER, BORDER);

    // Reading current GBuffer data
    float currentMaterialID;
    float4 currentNormalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[gRectOrigin + pixelPos], currentMaterialID);
    float3 currentNormal = currentNormalRoughness.xyz;
    float currentRoughness = currentNormalRoughness.w;

    // Getting current frame worldspace position and view vector for current pixel
    float3 mv = gMv[gRectOrigin + pixelPos] * gMvScale;
    float3 currentWorldPos = GetCurrentWorldPosFromPixelPos(pixelPos, currentLinearZ);
    float2 pixelUv = float2(pixelPos + 0.5) * gInvRectSize;
    float3 prevWorldPos = currentWorldPos;

    float2 prevUVSMB = pixelUv + mv.xy;
    if (gIsWorldSpaceMotionEnabled)
    {
        prevWorldPos += mv;
        prevUVSMB = STL::Geometry::GetScreenUv(gPrevWorldToClip, prevWorldPos);
    }
    else if (gMvScale.z != 0.0)
        prevWorldPos = GetPreviousWorldPosFromClipSpaceXY(prevUVSMB * 2.0 - 1.0, currentLinearZ + mv.z) + gPrevCameraPosition.xyz;

    float3 currentViewVector = (gOrthoMode == 0) ?
        currentWorldPos :
        currentLinearZ * normalize(gFrustumForward.xyz);
    float3 V = -normalize(currentViewVector);
    float NoV = abs(dot(currentNormal, V));

    // Input noisy data
#ifdef RELAX_DIFFUSE
    float3 diffuseIllumination = gDiffuseIllumination[pixelPos.xy + gRectOrigin].rgb;
    #ifdef RELAX_SH
        float4 diffuseSH1 = gDiffuseSH1[pixelPos.xy + gRectOrigin];
    #endif
#endif

#ifdef RELAX_SPECULAR
    float4 specularIllumination = gSpecularIllumination[pixelPos.xy + gRectOrigin];
    #ifdef RELAX_SH
        float4 specularSH1 = gSpecularSH1[pixelPos.xy + gRectOrigin];
    #endif
#endif

    // Calculating average normal, minHitDist and specular sigma
    float hitTM1 = sharedNormalSpecHitT[sharedMemoryIndex.y][sharedMemoryIndex.x].a;
    float hitTM2 = hitTM1 * hitTM1;
    float minHitDist3x3 = (hitTM1 != 0.0) ? hitTM1 : gDenoisingRange;
    float3 currentNormalAveraged = currentNormal;

    [unroll]
    for (i = -1; i <= 1; i++)
    {
        [unroll]
        for (j = -1; j <= 1; j++)
        {
            // Skipping center pixel
            if ((i == 0) && (j == 0))
                continue;

            float4 normalSpecHitT = sharedNormalSpecHitT[sharedMemoryIndex.y + j][sharedMemoryIndex.x + i];
            hitTM1 += normalSpecHitT.a;
            hitTM2 += normalSpecHitT.a * normalSpecHitT.a;

            minHitDist3x3 = (normalSpecHitT.a != 0) ? min(normalSpecHitT.a, minHitDist3x3) : minHitDist3x3;
            currentNormalAveraged += normalSpecHitT.rgb;
        }
    }
    hitTM1 /= 9.0;
    hitTM2 /= 9.0;
    float hitTSigma = GetStdDev(hitTM1, hitTM2);
    currentNormalAveraged /= 9.0;

#ifdef RELAX_SPECULAR
    float currentRoughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance(currentRoughness, currentNormalAveraged);
#endif

    // Computing 2nd moments of input noisy luminance
#ifdef RELAX_SPECULAR
    float specular1stMoment = STL::Color::Luminance(specularIllumination.rgb);
    float specular2ndMoment = specular1stMoment * specular1stMoment;
#endif

#ifdef RELAX_DIFFUSE
    float diffuse1stMoment = STL::Color::Luminance(diffuseIllumination.rgb);
    float diffuse2ndMoment = diffuse1stMoment * diffuse1stMoment;
#endif

    // Calculating surface parallax
    float parallaxInPixels = ComputeParallaxInPixels(prevWorldPos - gPrevCameraPosition.xyz, gOrthoMode == 0.0 ? pixelUv : prevUVSMB, gWorldToClip, gRectSize);

    // Calculating curvature along the direction of motion
#ifdef RELAX_SPECULAR
    float curvature = 0;
    float2 motionUv = STL::Geometry::GetScreenUv(gWorldToClip, prevWorldPos - gPrevCameraPosition.xyz, false);
    float2 cameraMotion2d = (pixelUv - motionUv) * gRectSize;
    cameraMotion2d /= max(length(cameraMotion2d), 1.0 / (1.0 + gSpecularMaxAccumulatedFrameNum));
    cameraMotion2d *= 0.5 * gInvRectSize;

    [unroll]
    for (int dir = -1; dir <= 1; dir += 2)
    {
        float2 uv = pixelUv + dir * cameraMotion2d;
        STL::Filtering::Bilinear f = STL::Filtering::GetBilinearFilter(uv, gRectSize);
        sharedMemoryIndex = threadPos + BORDER + int2(f.origin) - pixelPos;
        // WAR for out of shmem bounds fetch on some systems in VK
        if (any(sharedMemoryIndex.xy < 0) || sharedMemoryIndex.x >= BUFFER_X - 1 || sharedMemoryIndex.y >= BUFFER_Y - 1)
        {
            curvature = 0;
            break;
        }
        float3 n00 = sharedNormalSpecHitT[sharedMemoryIndex.y][sharedMemoryIndex.x].xyz;
        float3 n10 = sharedNormalSpecHitT[sharedMemoryIndex.y][sharedMemoryIndex.x + 1].xyz;
        float3 n01 = sharedNormalSpecHitT[sharedMemoryIndex.y + 1][sharedMemoryIndex.x].xyz;
        float3 n11 = sharedNormalSpecHitT[sharedMemoryIndex.y + 1][sharedMemoryIndex.x + 1].xyz;

        float3 pNormal = STL::Filtering::ApplyBilinearFilter(n00, n10, n01, n11, f);
        pNormal = normalize(pNormal);

        float3 x = GetCurrentWorldPosFromClipSpaceXY(uv * 2.0 - 1.0, 1.0);
        float3 v = normalize(gOrthoMode ? gFrustumForward.xyz : x);

        // Values below this threshold get turned into garbage due to numerical imprecision
        float d = STL::Math::ManhattanDistance(pNormal, currentNormal);
        float s = STL::Math::LinearStep(NRD_NORMAL_ENCODING_ERROR, 2.0 * NRD_NORMAL_ENCODING_ERROR, d);

        curvature += EstimateCurvature(normalize(currentNormalAveraged), pNormal, v, currentNormal, currentWorldPos) * s;
    }
    curvature *= 0.5;
#endif

    // Calculating disocclusion threshold
    float mixedDisocclusionDepthThreshold = gDisocclusionDepthThreshold;

    if (gUseDisocclusionThresholdMix > 0)
    {
        mixedDisocclusionDepthThreshold =
            lerp(gDisocclusionDepthThreshold,
                gDisocclusionDepthThresholdAlternate,
                gDisocclusionThresholdMix[pixelPos.xy + gRectOrigin]);
    }

    // Loading previous data based on surface motion vectors
    float footprintQuality;
#ifdef RELAX_DIFFUSE
    float4 prevDiffuseIlluminationAnd2ndMomentSMB;
    float3 prevDiffuseIlluminationAnd2ndMomentSMBResponsive;
    #ifdef RELAX_SH
        float4 prevDiffuseSH1;
        float4 prevDiffuseResponsiveSH1;
    #endif

#endif
#ifdef RELAX_SPECULAR
    float4 prevSpecularIlluminationAnd2ndMomentSMB;
    float3 prevSpecularIlluminationAnd2ndMomentSMBResponsive;
    float  prevReflectionHitTSMB;
    #ifdef RELAX_SH
        float4 prevSpecularSMBSH1;
        float4 prevSpecularSMBResponsiveSH1;
    #endif
#endif
    float historyLength;

    float SMBReprojectionFound = loadSurfaceMotionBasedPrevData(
        pixelPos,
        prevWorldPos,
        prevUVSMB,
        currentLinearZ,
        normalize(currentNormalAveraged),
        #ifdef RELAX_SPECULAR
            specularIllumination.a,
        #endif
        NoV,
        parallaxInPixels,
        currentMaterialID,
        gDiffMaterialMask | gSpecMaterialMask, // TODO: improve?
        mixedDisocclusionDepthThreshold,
        footprintQuality,
        historyLength
        #ifdef RELAX_DIFFUSE
            , prevDiffuseIlluminationAnd2ndMomentSMB
            , prevDiffuseIlluminationAnd2ndMomentSMBResponsive
            #ifdef RELAX_SH
                , prevDiffuseSH1
                , prevDiffuseResponsiveSH1
            #endif
        #endif
        #ifdef RELAX_SPECULAR
            , prevSpecularIlluminationAnd2ndMomentSMB
            , prevSpecularIlluminationAnd2ndMomentSMBResponsive
            , prevReflectionHitTSMB
            #ifdef RELAX_SH
                , prevSpecularSMBSH1
                , prevSpecularSMBResponsiveSH1
            #endif
        #endif
    );

    // History length is based on surface motion based disocclusion
    historyLength = historyLength + 1.0;
    historyLength = min(RELAX_MAX_ACCUM_FRAME_NUM, historyLength);

    // Avoid footprint momentary stretching due to changed viewing angle
    float3 Vprev = (gOrthoMode == 0) ? -normalize(prevWorldPos - gPrevCameraPosition.xyz) : -normalize(gPrevFrustumForward.xyz);
    float NoVprev = abs(dot(currentNormal, Vprev));
    float sizeQuality = (NoVprev + 1e-3) / (NoV + 1e-3); // this order because we need to fix stretching only, shrinking is OK
    sizeQuality *= sizeQuality;
    sizeQuality *= sizeQuality;
    footprintQuality *= lerp(0.1, 1.0, saturate(sizeQuality + abs(gOrthoMode)));

    // Minimize "getting stuck in history" effect when only fraction of bilinear footprint is valid
    // by shortening the history length
    [flatten]
    if (footprintQuality < 1.0)
    {
        historyLength *= sqrt(footprintQuality);
        historyLength = max(historyLength, 1.0);
    }

    // Handling history reset if needed
    historyLength = (gResetHistory != 0) ? 1.0 : historyLength;

    // Calculating checkerboard fields
    uint checkerboard = STL::Sequence::CheckerBoard(pixelPos, gFrameIndex);

#ifdef RELAX_DIFFUSE
    // DIFFUSE ACCUMULATION BELOW
    //
    // Temporal accumulation of diffuse illumination
    float diffMaxAccumulatedFrameNum = gDiffuseMaxAccumulatedFrameNum;
    float diffMaxFastAccumulatedFrameNum = gDiffuseMaxFastAccumulatedFrameNum;

    if (gUseConfidenceInputs)
    {
        float inDiffConfidence = gDiffConfidence[pixelPos];
        diffMaxAccumulatedFrameNum *= inDiffConfidence;
        diffMaxFastAccumulatedFrameNum *= inDiffConfidence;
    }

    float diffHistoryLength = historyLength;

    float diffuseAlpha = (SMBReprojectionFound > 0) ? max(1.0 / (diffMaxAccumulatedFrameNum + 1.0), 1.0 / diffHistoryLength) : 1.0;
    float diffuseAlphaResponsive = (SMBReprojectionFound > 0) ? max(1.0 / (diffMaxFastAccumulatedFrameNum + 1.0), 1.0 / diffHistoryLength) : 1.0;

    bool diffHasData = true;
    if (gDiffCheckerboard != 2)
    {
        diffHasData = checkerboard == gDiffCheckerboard;
    }

    if ((!diffHasData) && (diffHistoryLength > 1.0))
    {
        // Adjusting diffuse accumulation weights for checkerboard
        diffuseAlpha *= 1.0 - gCheckerboardResolveAccumSpeed;
        diffuseAlphaResponsive *= 1.0 - gCheckerboardResolveAccumSpeed;
    }

    float4 accumulatedDiffuseIlluminationAnd2ndMoment = lerp(prevDiffuseIlluminationAnd2ndMomentSMB, float4(diffuseIllumination.rgb, diffuse2ndMoment), diffuseAlpha);
    float3 accumulatedDiffuseIlluminationResponsive = lerp(prevDiffuseIlluminationAnd2ndMomentSMBResponsive.rgb, diffuseIllumination.rgb, diffuseAlphaResponsive);

    // Write out the diffuse results
    gOutDiffuseIllumination[pixelPos] = accumulatedDiffuseIlluminationAnd2ndMoment;
    gOutDiffuseIlluminationResponsive[pixelPos] = float4(accumulatedDiffuseIlluminationResponsive, 0);

    #ifdef RELAX_SH
        float4 accumulatedDiffuseSH1 = lerp(prevDiffuseSH1, diffuseSH1, diffuseAlpha);
        float4 accumulatedDiffuseResponsiveSH1 = lerp(prevDiffuseResponsiveSH1, diffuseSH1, diffuseAlphaResponsive);
        gOutDiffuseSH1[pixelPos] = accumulatedDiffuseSH1;
        gOutDiffuseResponsiveSH1[pixelPos] = float4(accumulatedDiffuseResponsiveSH1);
    #endif
#endif

    gOutHistoryLength[pixelPos] = historyLength / 255.0;

#ifdef RELAX_SPECULAR
    // SPECULAR ACCUMULATION BELOW
    //
    float specMaxAccumulatedFrameNum = gSpecularMaxAccumulatedFrameNum;
    float specMaxFastAccumulatedFrameNum = gSpecularMaxFastAccumulatedFrameNum;
    if (gUseConfidenceInputs)
    {
        float inSpecConfidence = gSpecConfidence[pixelPos];
        specMaxAccumulatedFrameNum *= inSpecConfidence;
        specMaxFastAccumulatedFrameNum *= inSpecConfidence;
    }

    float specHistoryLength = historyLength;
    float specHistoryFrames = min(specMaxAccumulatedFrameNum, specHistoryLength);
    float specHistoryResponsiveFrames = min(specMaxFastAccumulatedFrameNum, specHistoryLength);

    // Picking hitDist as minimal value in 3x3 area
    // and clamping to M1 with 3 sigma to avoid picking too small values
    float hitDist = minHitDist3x3;
    hitDist = clamp(hitDist, hitTM1 - hitTSigma * 3.0, hitTM1 + hitTSigma * 3.0);

    // Thin lens equation for adjusting reflection HitT
    float hitDistFocused = ApplyThinLensEquation(NoV, hitDist, curvature);

    [flatten]
    if (abs(hitDistFocused) < 0.001)
        hitDistFocused = 0.001;

    // Loading specular data based on virtual motion
    float4 prevSpecularIlluminationAnd2ndMomentVMB;
    float4 prevSpecularIlluminationAnd2ndMomentVMBResponsive;
    float3 prevNormalVMB;
    float  prevRoughnessVMB;
    float  prevReflectionHitTVMB;
    float2 prevUVVMB;
    #ifdef RELAX_SH
        float4 prevSpecularVMBSH1;
        float4 prevSpecularVMBResponsiveSH1;
    #endif

    float VMBReprojectionFound = loadVirtualMotionBasedPrevData(
        pixelPos,
        currentWorldPos,
        currentNormal,
        currentLinearZ,
        hitDistFocused,
        hitDist,
        currentViewVector,
        prevWorldPos,
        SMBReprojectionFound == 2.0 ? true : false,
        currentMaterialID,
        gSpecMaterialMask,
        prevUVSMB,
        parallaxInPixels,
        NoV,
        mixedDisocclusionDepthThreshold,
        prevSpecularIlluminationAnd2ndMomentVMB,
        prevSpecularIlluminationAnd2ndMomentVMBResponsive,
        prevNormalVMB,
        prevRoughnessVMB,
        prevReflectionHitTVMB,
        prevUVVMB
        #ifdef RELAX_SH
            , prevSpecularVMBSH1
            , prevSpecularVMBResponsiveSH1
        #endif
    );

    // Amount of virtual motion - dominant factor
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection(currentNormal, V, currentRoughnessModified, RELAX_SPEC_DOMINANT_DIRECTION);
    float virtualHistoryAmount = VMBReprojectionFound * D.w;

    // Decreasing virtual history amount for ortho case
    virtualHistoryAmount *= (gOrthoMode == 0) ? 1.0 : 0.75;

    // Virtual motion amount - back-facing
    virtualHistoryAmount *= float(dot(prevNormalVMB, currentNormalAveraged) > 0.0);

    // Curvature angle for virtual motion based reprojection
    float2 uvDiff = prevUVVMB - prevUVSMB;
    float uvDiffLengthInPixels = length(uvDiff * gRectSize);

    float pixelSize = PixelRadiusToWorld(gUnproject, gOrthoMode, 1.0, currentLinearZ);
    float tanCurvature = abs(curvature * pixelSize);
    tanCurvature *= 1.0 + uvDiffLengthInPixels / max(NoV, 0.01); // path length
    tanCurvature *= gFramerateScale;
    float curvatureAngle = atan(tanCurvature);

    // Normal weight for virtual motion based reprojection
    float lobeHalfAngle = STL::ImportanceSampling::GetSpecularLobeHalfAngle(currentRoughnessModified) + RELAX_NORMAL_ENCODING_ERROR;
    float angle = lobeHalfAngle + curvatureAngle;
    float normalWeight = GetEncodingAwareNormalWeight(currentNormal, prevNormalVMB, angle);
    virtualHistoryAmount *= lerp(1.0 - saturate(uvDiffLengthInPixels), 1.0, normalWeight); // jitter friendly

    // Roughness weight for virtual motion based reprojection
    float2 roughnessParams = GetRoughnessWeightParamsSq(currentRoughness, gRoughnessFraction);
    float virtualRoughnessWeight = GetRoughnessWeightSq(roughnessParams, prevRoughnessVMB);
    virtualRoughnessWeight = lerp(1.0 - saturate(uvDiffLengthInPixels), 1.0, virtualRoughnessWeight); // jitter friendly
    virtualHistoryAmount *= (gOrthoMode == 0) ? virtualRoughnessWeight : 1.0;
    float specVMBConfidence = virtualRoughnessWeight * 0.9 + 0.1;

    // "Looking back" 1 and 2 frames and applying normal weight to decrease lags
    uvDiff *= STL::Math::Rsqrt(STL::Math::LengthSquared(uvDiff));
    uvDiff /= gRectSizePrev;
    uvDiff *= saturate(uvDiffLengthInPixels / 0.1) + uvDiffLengthInPixels / 2.0;
    float2 backUV1 = prevUVVMB + 1.0 * uvDiff;
    float2 backUV2 = prevUVVMB + 2.0 * uvDiff;
    backUV1 *= (gInvResourceSize * gRectSizePrev);
    backUV2 *= (gInvResourceSize * gRectSizePrev);
    float4 backNormalRoughness1 = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, backUV1, 0));
    float4 backNormalRoughness2 = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, backUV2, 0));
    backNormalRoughness1.rgb = gUseWorldPrevToWorld ? STL::Geometry::RotateVector(gWorldPrevToWorld, backNormalRoughness1.rgb) : backNormalRoughness1.rgb;
    backNormalRoughness2.rgb = gUseWorldPrevToWorld ? STL::Geometry::RotateVector(gWorldPrevToWorld, backNormalRoughness2.rgb) : backNormalRoughness2.rgb;
    float maxAngle1 = angle + 1.0 * curvatureAngle + RELAX_NORMAL_ENCODING_ERROR;
    float maxAngle2 = angle + 2.0 * curvatureAngle + RELAX_NORMAL_ENCODING_ERROR;
    float prevPrevNormalWeight = IsInScreen(backUV1) ? GetEncodingAwareNormalWeight(prevNormalVMB, backNormalRoughness1.rgb, maxAngle1, curvatureAngle) : 1.0;
    prevPrevNormalWeight *= IsInScreen(backUV2) ? GetEncodingAwareNormalWeight(prevNormalVMB, backNormalRoughness2.rgb, maxAngle2, curvatureAngle) : 1.0;
    virtualHistoryAmount *= 0.33 + 0.67 * prevPrevNormalWeight;
    specVMBConfidence *= 0.33 + 0.67 * prevPrevNormalWeight;
    // Taking in account roughness 1 and 2 frames back helps cleaning up surfaces wigh varying roughness
    float rw = GetRoughnessWeightSq(roughnessParams, backNormalRoughness1.w);
    rw *= GetRoughnessWeightSq(roughnessParams, backNormalRoughness2.w);
    virtualHistoryAmount *= (gOrthoMode == 0) ? rw * 0.9 + 0.1 : 1.0;

    // Virtual history confidence - hit distance
    float SMC = GetSpecMagicCurve(currentRoughnessModified);
    float hitDistC = lerp(specularIllumination.a, prevReflectionHitTSMB, SMC);
    float hitDist1 = ApplyThinLensEquation(NoV, hitDistC, curvature);
    float hitDist2 = ApplyThinLensEquation(NoV, prevReflectionHitTVMB, curvature);
    float maxDist = max(hitDist1, hitDist2);
    float dHitT = abs(hitDist1 - hitDist2);
    float dHitTMultiplier = lerp(20.0, 0.0, SMC);
    float virtualHistoryHitDistConfidence = 1.0 - saturate(dHitTMultiplier * dHitT / (currentLinearZ + maxDist));
    virtualHistoryHitDistConfidence = lerp(virtualHistoryHitDistConfidence, 1.0, SMC);

    // Virtual history confidence - virtual UV discrepancy
    float3 virtualWorldPos = GetXvirtual(NoV, hitDist, curvature, currentWorldPos, prevWorldPos, V, D.w);
    float virtualWorldPosLength = length(virtualWorldPos);
    float hitDistForTrackingPrev = prevSpecularIlluminationAnd2ndMomentVMBResponsive.a;
    float3 prevVirtualWorldPos = GetXvirtual(NoV, hitDistForTrackingPrev, curvature, currentWorldPos, prevWorldPos, V, D.w);
    float virtualWorldPosLengthPrev = length(prevVirtualWorldPos);
    float2 prevUVVMBTest = STL::Geometry::GetScreenUv(gPrevWorldToClip, prevVirtualWorldPos, false);

    float percentOfVolume = 0.6;
    float lobeTanHalfAngle = STL::ImportanceSampling::GetSpecularLobeTanHalfAngle(currentRoughness, percentOfVolume);
    lobeTanHalfAngle = max(lobeTanHalfAngle, 0.5 * gInvRectSize.x);

    float unproj1 = min(hitDist, hitDistForTrackingPrev) / PixelRadiusToWorld(gUnproject, gOrthoMode, 1.0, max(virtualWorldPosLength, virtualWorldPosLengthPrev));
    float lobeRadiusInPixels = lobeTanHalfAngle * unproj1;

    float deltaParallaxInPixels = length((prevUVVMBTest - prevUVVMB) * gRectSize);
    virtualHistoryHitDistConfidence *= STL::Math::SmoothStep(lobeRadiusInPixels + 0.25, 0.0, deltaParallaxInPixels);

    // Current specular signal ( surface motion )
    float smcFactor = lerp(0.25, 0.001, SMC); // TODO: tune better?
    smcFactor *= lerp(1.0, lerp(1.0, 0.25, SMC), NoV);
    float specSMBConfidence = (SMBReprojectionFound > 0 ? 1.0 : 0.0) / (1.0 + smcFactor * parallaxInPixels);
    specSMBConfidence *= GetNormalWeight(V, Vprev, lobeHalfAngle * NoV / gFramerateScale);

    float specSMBAlpha = 1.0 - specSMBConfidence;
    float specSMBResponsiveAlpha = 1.0 - specSMBConfidence;
    specSMBAlpha = max(specSMBAlpha, 1.0 / (1.0 + specHistoryFrames));
    specSMBResponsiveAlpha = max(specSMBAlpha, 1.0 / (1.0 + specHistoryResponsiveFrames));

    bool specHasData = true;
    if (gSpecCheckerboard != 2)
    {
        specHasData = checkerboard == gSpecCheckerboard;
    }

    if (!specHasData && (parallaxInPixels < 0.5))
    {
        // Adjusting surface motion based specular accumulation weights for checkerboard
        specSMBAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * (SMBReprojectionFound > 0 ? 1.0 : 0.0);
        specSMBResponsiveAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * (SMBReprojectionFound > 0 ? 1.0 : 0.0);
    }

    float4 accumulatedSpecularSMB;
    accumulatedSpecularSMB.rgb = lerp(prevSpecularIlluminationAnd2ndMomentSMB.rgb, specularIllumination.rgb, specSMBAlpha);
    accumulatedSpecularSMB.w = lerp(prevReflectionHitTSMB, specularIllumination.w, max(specSMBAlpha, 0.1));
    float accumulatedSpecularM2SMB = lerp(prevSpecularIlluminationAnd2ndMomentSMB.a, specular2ndMoment, specSMBAlpha);
    float3 accumulatedSpecularSMBResponsive = lerp(prevSpecularIlluminationAnd2ndMomentSMBResponsive, specularIllumination.xyz, specSMBResponsiveAlpha);

    // Current specular signal ( virtual motion )
    float specVMBAlpha = 1.0 - specVMBConfidence;
    float specVMBResponsiveAlpha = 1.0 - specVMBConfidence * virtualHistoryHitDistConfidence;
    float specVMBHitTAlpha = specVMBResponsiveAlpha;

    specVMBAlpha = max(specVMBAlpha, 1.0 / (1.0 + specHistoryFrames));
    specVMBResponsiveAlpha = max(specVMBResponsiveAlpha, 1.0 / (1.0 + specHistoryResponsiveFrames));
    specVMBHitTAlpha = max(specVMBHitTAlpha, 1.0 / (1.0 + specHistoryFrames));

    [flatten]
    if (!specHasData && (parallaxInPixels < 0.5))
    {
        // Adjusting virtual motion based specular accumulation weights for checkerboard
        specVMBAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * (VMBReprojectionFound > 0 ? 1.0 : 0.0);
        specVMBResponsiveAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * (VMBReprojectionFound > 0 ? 1.0 : 0.0);
        specVMBHitTAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * (VMBReprojectionFound > 0 ? 1.0 : 0.0);
    }

    float4 accumulatedSpecularVMB;
    accumulatedSpecularVMB.rgb = lerp(prevSpecularIlluminationAnd2ndMomentVMB.rgb, specularIllumination.rgb, specVMBAlpha);
    accumulatedSpecularVMB.a = lerp(prevReflectionHitTVMB, specularIllumination.a, max(specVMBHitTAlpha, 0.1));
    float accumulatedSpecularM2VMB = lerp(prevSpecularIlluminationAnd2ndMomentVMB.a, specular2ndMoment, specVMBAlpha);
    float3 accumulatedSpecularVMBResponsive = lerp(prevSpecularIlluminationAnd2ndMomentVMBResponsive.rgb, specularIllumination.xyz, specVMBResponsiveAlpha);

    // Fallback to surface motion if virtual motion doesn't go well
    virtualHistoryAmount *= saturate(specVMBConfidence / (specSMBConfidence + NRD_EPS));

    // Temporal accumulation of reflection HitT
    float accumulatedReflectionHitT = lerp(accumulatedSpecularSMB.a, accumulatedSpecularVMB.a, virtualHistoryAmount);

    // Temporal accumulation of specular illumination
    float3 accumulatedSpecularIllumination = lerp(accumulatedSpecularSMB.xyz, accumulatedSpecularVMB.xyz, virtualHistoryAmount);
    float3 accumulatedSpecularIlluminationResponsive = lerp(accumulatedSpecularSMBResponsive.xyz, accumulatedSpecularVMBResponsive.xyz, virtualHistoryAmount);
    float accumulatedSpecular2ndMoment = lerp(accumulatedSpecularM2SMB, accumulatedSpecularM2VMB, virtualHistoryAmount);

    #ifdef RELAX_SH
        float4 accumulatedSpecularSMBSH1 = lerp(prevSpecularSMBSH1, specularSH1, specSMBAlpha);
        float4 accumulatedSpecularSMBResponsiveSH1 = lerp(prevSpecularSMBResponsiveSH1, specularSH1, specSMBResponsiveAlpha);

        float4 accumulatedSpecularVMBSH1 = lerp(prevSpecularVMBSH1, specularSH1, specVMBAlpha);
        float4 accumulatedSpecularVMBResponsiveSH1 = lerp(prevSpecularVMBResponsiveSH1, specularSH1, specVMBResponsiveAlpha);

        float4 accumulatedSpecularSH1 = lerp(accumulatedSpecularSMBSH1, accumulatedSpecularVMBSH1, virtualHistoryAmount);
        float4 accumulatedSpecularResponsiveSH1 = lerp(accumulatedSpecularSMBResponsiveSH1, accumulatedSpecularVMBResponsiveSH1, virtualHistoryAmount);
        gOutSpecularSH1[pixelPos] = float4(accumulatedSpecularSH1.rgb, currentRoughnessModified);
        gOutSpecularResponsiveSH1[pixelPos] = accumulatedSpecularResponsiveSH1;
    #endif

    // If zero specular sample (color = 0), artificially adding variance for pixels with low reprojection confidence
    float specularHistoryConfidence = lerp(specSMBConfidence, specVMBConfidence, virtualHistoryAmount);
    if (accumulatedSpecular2ndMoment == 0) accumulatedSpecular2ndMoment = gSpecularVarianceBoost * (1.0 - specularHistoryConfidence);

    // Write out the results
    gOutSpecularIllumination[pixelPos] = float4(accumulatedSpecularIllumination, accumulatedSpecular2ndMoment);
    gOutSpecularIlluminationResponsive[pixelPos] = float4(accumulatedSpecularIlluminationResponsive, hitDist);

    gOutReflectionHitT[pixelPos] = accumulatedReflectionHitT;
    gOutSpecularReprojectionConfidence[pixelPos] = specularHistoryConfidence;
#endif
}
