/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#if (defined RELAX_SPECULAR)
groupshared float4 sharedInSpecular[BUFFER_Y][BUFFER_X];
#endif

groupshared float4 sharedNormalRoughness[BUFFER_Y][BUFFER_X];

// Helper functions
#if (defined RELAX_SPECULAR)
float GetSpecAccumulatedFrameNum(float roughness, float powerScale)
{
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIzMSooeF4wLjY2KSIsImNvbG9yIjoiIzAwMDAwMCJ9LHsidHlwZSI6MCwiZXEiOiIzMSooMS0yXigtMjAwKngqeCkpKih4XjAuNSkiLCJjb2xvciI6IiNGQTBEMEQifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIzMSJdfV0-
    float f = 1.0 - exp2(-200.0 * roughness * roughness);
    f *= STL::Math::Pow01(roughness, RELAX_SPEC_ACCUM_BASE_POWER * powerScale);

    return RELAX_MAX_ACCUM_FRAME_NUM * f;
}

float GetSpecAccumSpeed(float maxAccumSpeed, float roughness, float NoV, float parallax)
{
    float acos01sq = saturate(1.0 - NoV); // see AcosApprox()

    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIoMS4wNSsoeCp4KV4xLjApLygxLjA1LSh4KngpXjEuMCkiLCJjb2xvciI6IiM1MkExMDgifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC42NikvKDEuMDUtKHgqeCleMC42NikiLCJjb2xvciI6IiNFM0Q4MDkifSx7InR5cGUiOjAsImVxIjoiKDEuMDUrKHgqeCleMC41KS8oMS4wNS0oeCp4KV4wLjUpIiwiY29sb3IiOiIjRjUwQTMxIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMCIsIjEiLCIwIiwiNDIiXSwic2l6ZSI6WzE5MDAsNzAwXX1d
    float a = STL::Math::Pow01(acos01sq, RELAX_SPEC_ACCUM_CURVE);
    float b = 1.1 + roughness * roughness;
    float parallaxSensitivity = (b + a) / (b - a);
    float powerScale = 1.0 + 3.0 * parallax * parallaxSensitivity;
    float accumSpeed = GetSpecAccumulatedFrameNum(roughness, powerScale);
    accumSpeed = min(accumSpeed, maxAccumSpeed);

    return accumSpeed;
}

#endif // (defined RELAX_SPECULAR)

float getJitterRadius(float jitterDelta, float linearZ)
{
    return jitterDelta * gUnproject * (gOrthoMode == 0 ? linearZ : 1.0);
}

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
    float currentMaterialID,
    uint materialIDMask,

    out float footprintQuality,
    out float historyLength
#ifdef RELAX_DIFFUSE
    , out float4 prevDiffuseIllumAnd2ndMoment
    , out float3 prevDiffuseResponsiveIllum
#endif
#ifdef RELAX_SPECULAR
    , out float4 prevSpecularIllumAnd2ndMoment
    , out float3 prevSpecularResponsiveIllum
    , out float  prevReflectionHitT
#endif
)
{
    // Calculating jitter margin radius in world space
    float jitterRadius = getJitterRadius(gJitterDelta, currentLinearZ);
    float disocclusionThreshold = (gDisocclusionDepthThreshold + jitterRadius) * (gOrthoMode == 0 ? currentLinearZ : 1.0);

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
    float2 gatherOrigin00 = (float2(bilinearOrigin) + float2(0.0, 0.0)) * gInvResourceSize;
    float2 gatherOrigin10 = (float2(bilinearOrigin) + float2(2.0, 0.0)) * gInvResourceSize;
    float2 gatherOrigin01 = (float2(bilinearOrigin) + float2(0.0, 2.0)) * gInvResourceSize;
    float2 gatherOrigin11 = (float2(bilinearOrigin) + float2(2.0, 2.0)) * gInvResourceSize;
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
    float3 planeDist0 = abs(prevViewZs00.yzw - prevViewPos.zzz) * NdotV; 
    float3 planeDist1 = abs(prevViewZs10.xzw - prevViewPos.zzz) * NdotV; 
    float3 planeDist2 = abs(prevViewZs01.xyw - prevViewPos.zzz) * NdotV; 
    float3 planeDist3 = abs(prevViewZs11.xyz - prevViewPos.zzz) * NdotV; 
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
    float2 uv = (float2(bilinearOrigin) + float2(1.0, 1.0)) * gInvResourceSize;
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
    float4 bilinearWeightsWithValidity = STL::Filtering::GetBilinearCustomWeights(bilinear, float4(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w));

    bool useBicubic = (bicubicFootprintValid > 0);

    // Fetching normal history
    BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
        prevPixelPosFloat, gInvResourceSize,
        bilinearWeightsWithValidity, useBicubic
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
        bilinearWeightsWithValidity, useBicubic
#ifdef RELAX_DIFFUSE
        , gPrevDiffuseIlluminationResponsive, diff
#endif
#ifdef RELAX_SPECULAR
        , gPrevSpecularIlluminationResponsive, spec
#endif
    );

#ifdef RELAX_DIFFUSE
    prevDiffuseIllumAnd2ndMoment = max(prevDiffuseIllumAnd2ndMoment, 0.0);
    prevDiffuseResponsiveIllum = max(diff.rgb, 0);
#endif
#ifdef RELAX_SPECULAR
    prevSpecularIllumAnd2ndMoment = max(prevSpecularIllumAnd2ndMoment, 0.0);
    prevSpecularResponsiveIllum = max(spec.rgb, 0);
#endif

    // Fitering previous data that does not need bicubic
    float interpolatedBinaryWeight = STL::Filtering::ApplyBilinearFilter(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w, bilinear);
    interpolatedBinaryWeight = max(1e-6, interpolatedBinaryWeight);

    float2 gatherOrigin = (float2(bilinearOrigin) + 1.0) * gInvResourceSize;

    float4 prevHistoryLengths = gPrevHistoryLength.GatherRed(gNearestClamp, gatherOrigin).wzxy;
    historyLength = 255.0 * BilinearWithBinaryWeightsImmediateFloat(
        prevHistoryLengths.x,
        prevHistoryLengths.y,
        prevHistoryLengths.z,
        prevHistoryLengths.w,
        bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

#ifdef RELAX_SPECULAR
    float4 prevReflectionHitTs = gPrevReflectionHitT.GatherRed(gNearestClamp, gatherOrigin).wzxy;
    prevReflectionHitT = BilinearWithBinaryWeightsImmediateFloat(
        prevReflectionHitTs.x,
        prevReflectionHitTs.y,
        prevReflectionHitTs.z,
        prevReflectionHitTs.w,
        bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
    prevReflectionHitT = max(0.001, prevReflectionHitT);
#endif

    float reprojectionFound = (bicubicFootprintValid > 0) ? 2.0 : 1.0;
    footprintQuality = (bicubicFootprintValid > 0) ? 1.0 : interpolatedBinaryWeight;

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
    float parallax,
    float NdotV,
    out float4 prevSpecularIllumAnd2ndMoment,
    out float3 prevSpecularResponsiveIllum,
    out float3 prevNormal,
    out float prevRoughness,
    out float prevReflectionHitT,
    out float2 prevUVVMB)
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
    if (length(prevUVVMB - prevSurfaceMotionBasedUV) > (parallax / NRD_PARALLAX_NORMALIZATION) + 0.001)
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
    float jitterRadius = getJitterRadius(gJitterDelta, accumulatedSpecularVMBZ);
    float disocclusionThreshold = (gDisocclusionDepthThreshold + jitterRadius) * (gOrthoMode == 0 ? currentLinearZ : 1.0);

    // Consider reprojection to the same pixel index a small motion.
    // It is useful for skipping reprojection test for static camera when the jitter is the only source of motion.
    int2 prevVirtualPixelPosInt = int2(prevVirtualPixelPosFloat);
    bool isSmallVirtualMotion = all(prevVirtualPixelPosInt == pixelPos);
    bool skipReprojectionTest = gSkipReprojectionTestWithoutMotion && isSmallVirtualMotion;

    // Calculating footprint origin and weights
    int2 bilinearOrigin = int2(floor(prevVirtualPixelPosFloat - 0.5));
    float2 bilinearWeights = frac(prevVirtualPixelPosFloat - 0.5);
    float2 gatherOrigin = (bilinearOrigin + 1.0) * gInvResourceSize;

    // Taking care of camera motion, because world space is always centered at camera position in NRD
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
    float reprojectionFound = 0;
    prevSpecularIllumAnd2ndMoment = 0;
    prevSpecularResponsiveIllum = 0;
    prevNormal = currentNormal;
    prevRoughness = 0;
    prevReflectionHitT = gDenoisingRange;

    // Weighted bilinear (or bicubic optionally) for prev specular data based on virtual motion.
    if (any(bilinearTapsValid))
    {
        // Calculating bilinear weights in advance 
        STL::Filtering::Bilinear bilinear;
        bilinear.weights = bilinearWeights;
        float4 bilinearWeightsWithValidity = STL::Filtering::GetBilinearCustomWeights(bilinear, float4(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w));

        bool useBicubic = (surfaceBicubicValid > 0) & all(bilinearTapsValid);

        // Fetching normal virtual motion based specular history
        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            prevVirtualPixelPosFloat, gInvResourceSize,
            bilinearWeightsWithValidity, useBicubic,
            gPrevSpecularIllumination, prevSpecularIllumAnd2ndMoment);

        prevSpecularIllumAnd2ndMoment = max(prevSpecularIllumAnd2ndMoment, 0.0);

        // Fetching fast virtual motion based specular history
        float4 spec;
        BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            prevVirtualPixelPosFloat, gInvResourceSize,
            bilinearWeightsWithValidity, useBicubic,
            gPrevSpecularIlluminationResponsive, spec);

        spec = max(spec, 0.0);
        prevSpecularResponsiveIllum = spec.rgb;

        // Fitering previous data that does not need bicubic
        float4 prevNormalRoughness = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, prevUVVMB, 0));
        prevNormal = prevNormalRoughness.xyz;
        prevNormal = gUseWorldPrevToWorld ? STL::Geometry::RotateVector(gWorldPrevToWorld, prevNormal) : prevNormal;
        prevRoughness = prevNormalRoughness.w;

        prevReflectionHitT = gPrevReflectionHitT.SampleLevel(gLinearClamp, prevUVVMB, 0).x;
        prevReflectionHitT = max(0.001, prevReflectionHitT);
        reprojectionFound = 1.0;
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

#ifdef RELAX_SPECULAR
    float4 inSpecularIllumination = gSpecularIllumination[globalPos + gRectOrigin];
    sharedInSpecular[sharedPos.y][sharedPos.x] = inSpecularIllumination;
#endif

    float4 normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[globalPos + gRectOrigin]);
    sharedNormalRoughness[sharedPos.y][sharedPos.x] = normalRoughness;
}

// Main
[numthreads(GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN(uint2 pixelPos : SV_DispatchThreadId, uint2 threadPos : SV_GroupThreadId, uint threadIndex : SV_GroupIndex)
{
    PRELOAD_INTO_SMEM;

    // Calculating checkerboard fields
    uint2 checkerboardPixelPos = pixelPos.xx;
    uint checkerboard = STL::Sequence::CheckerBoard(pixelPos, gFrameIndex);

#ifdef RELAX_DIFFUSE
    bool diffHasData = true;
    if (gDiffCheckerboard != 2)
    {
        diffHasData = checkerboard == gDiffCheckerboard;
        checkerboardPixelPos.x >>= 1;
    }
#endif

#ifdef RELAX_SPECULAR
    bool specHasData = true;
    if (gSpecCheckerboard != 2)
    {
        specHasData = checkerboard == gSpecCheckerboard;
        checkerboardPixelPos.y >>= 1;
    }
#endif

    // Early out if linearZ is beyond denoising range
    float currentLinearZ = gViewZ[pixelPos.xy + gRectOrigin];

    [branch]
    if (currentLinearZ > gDenoisingRange)
        return;

    uint2 sharedMemoryIndex = threadPos.xy + int2(BORDER, BORDER);

    // Reading current GBuffer data
    float4 currentNormalRoughness = sharedNormalRoughness[sharedMemoryIndex.y][sharedMemoryIndex.x];
    float3 currentNormal = currentNormalRoughness.xyz;
    float currentRoughness = currentNormalRoughness.w;

    // Handling materialID
    // Combining logic is valid even if non-combined denoisers are used
    // since undefined masks are zeroes in those cases
    float currentMaterialID;
    NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[gRectOrigin + pixelPos], currentMaterialID);
    currentMaterialID = floor(currentMaterialID * 3.0 + 0.5) / 255.0; // IMPORTANT: properly repack 2 bits to 8 bits for checking against prevMaterialID

    // Getting current frame worldspace position and view vector for current pixel
    float3 motionVector = gMotion[gRectOrigin + pixelPos].xyz * gMotionVectorScale.xyy;
    float3 currentWorldPos = GetCurrentWorldPosFromPixelPos(pixelPos, currentLinearZ);
    float3 prevWorldPos = currentWorldPos + motionVector * float(gIsWorldSpaceMotionEnabled != 0);
    float2 pixelUv = float2(pixelPos + 0.5) * gInvRectSize;
    float2 prevUVSMB = STL::Geometry::GetPrevUvFromMotion(pixelUv, currentWorldPos, gPrevWorldToClip, motionVector, gIsWorldSpaceMotionEnabled);

    float3 currentViewVector = (gOrthoMode == 0) ?
        currentWorldPos :
        currentLinearZ * normalize(gFrustumForward.xyz);
    float3 V = normalize(-currentViewVector);
    float NoV = saturate(dot(currentNormal, V));

    // Input noisy data
#ifdef RELAX_DIFFUSE
    float3 diffuseIllumination = gDiffuseIllumination[pixelPos.xy + gRectOrigin].rgb;
#endif

#ifdef RELAX_SPECULAR
    float4 specularIllumination = sharedInSpecular[sharedMemoryIndex.y][sharedMemoryIndex.x];
#endif

#ifdef RELAX_SPECULAR
    // Calculating minHitDist and specular sigma
    float4 specM1 = specularIllumination;
    float4 specM2 = specM1 * specM1;
    float minHitDist3x3 = (specularIllumination.a != 0.0) ? specularIllumination.a : gDenoisingRange;
    float minHitDist5x5 = minHitDist3x3;

    [unroll]
    for (int i = -2; i <= 2; i++)
    {
        [unroll]
        for (int j = -2; j <= 2; j++)
        {
            // Skipping center pixel
            if ((i == 0) && (j == 0))
                continue;

            float4 spec = sharedInSpecular[sharedMemoryIndex.y + j][sharedMemoryIndex.x + i];
            specM1 += spec;
            specM2 += spec * spec;
            minHitDist5x5 = (spec.w != 0) ? min(spec.w, minHitDist5x5) : minHitDist5x5;

            if ((abs(i) <= 1) && (abs(j) <= 1))
                minHitDist3x3 = (spec.w != 0) ? min(spec.w, minHitDist3x3) : minHitDist3x3;
        }
    }
    specM1 /= 25.0;
    specM2 /= 25.0;
    float4 specSigma = GetStdDev(specM1, specM2);
#endif

    // Averaging current normal
    float3 currentNormalAveraged = currentNormal;
    [unroll]
    for (int k = -1; k <= 1; k++)
    {
        [unroll]
        for (int l = -1; l <= 1; l++)
        {
            // Skipping center pixel
            if ((k == 0) && (l == 0))
                continue;

            float3 pNormal = sharedNormalRoughness[sharedMemoryIndex.y + l][sharedMemoryIndex.x + k].xyz;
            currentNormalAveraged += pNormal;
        }
    }
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

    // Calculating curvature along the direction of motion
#ifdef RELAX_SPECULAR
    float curvature = 0;
    float3 cameraMotion3d = prevWorldPos - currentWorldPos - gPrevCameraPosition.xyz;
    float2 mv = STL::Geometry::RotateVectorInverse(gViewToWorld, cameraMotion3d).xy;
    float mvLen = length(mv);
    mv = mvLen > 1e-7 ? mv / mvLen : float2(1, 0);
    mv *= 0.5 * gInvRectSize;

    [unroll]
    for (int dir = -1; dir <= 1; dir += 2)
    {
        float2 uv = pixelUv + dir * mv;
        STL::Filtering::Bilinear f = STL::Filtering::GetBilinearFilter(uv, gRectSize);

        sharedMemoryIndex = threadPos + BORDER + uint2(f.origin) - pixelPos;
        float3 n00 = sharedNormalRoughness[sharedMemoryIndex.y][sharedMemoryIndex.x].xyz;
        float3 n10 = sharedNormalRoughness[sharedMemoryIndex.y][sharedMemoryIndex.x + 1].xyz;
        float3 n01 = sharedNormalRoughness[sharedMemoryIndex.y + 1][sharedMemoryIndex.x].xyz;
        float3 n11 = sharedNormalRoughness[sharedMemoryIndex.y + 1][sharedMemoryIndex.x + 1].xyz;

        float3 pNormal = STL::Filtering::ApplyBilinearFilter(n00, n10, n01, n11, f);
        pNormal = normalize(pNormal);

        float3 x = GetCurrentWorldPosFromClipSpaceXY(uv * 2.0 - 1.0, 1.0);
        float3 v = normalize(gOrthoMode ? gFrustumForward.xyz : x);
        curvature += EstimateCurvature(pNormal, v, currentNormal, currentWorldPos);
    }
    curvature *= 0.5;

    float pixelSize = PixelRadiusToWorld(gUnproject, gOrthoMode, 1.0, currentLinearZ);
    float pixelSizeOverCurvatureRadius = curvature * pixelSize;
    float curvatureAnglePerPixel = STL::Math::AtanApprox(abs(pixelSizeOverCurvatureRadius));
#endif

    // Loading previous data based on surface motion vectors
    float footprintQuality;
#ifdef RELAX_DIFFUSE
    float4 prevDiffuseIlluminationAnd2ndMomentSMB;
    float3 prevDiffuseIlluminationAnd2ndMomentSMBResponsive;
#endif
#ifdef RELAX_SPECULAR
    float4 prevSpecularIlluminationAnd2ndMomentSMB;
    float3 prevSpecularIlluminationAnd2ndMomentSMBResponsive;
    float  prevReflectionHitTSMB;
#endif
    float historyLength;

    float surfaceMotionBasedReprojectionFound = loadSurfaceMotionBasedPrevData(
        pixelPos,
        prevWorldPos,
        prevUVSMB,
        currentLinearZ,
        normalize(currentNormalAveraged),
#ifdef RELAX_SPECULAR
        specularIllumination.a,
#endif
        NoV,
        currentMaterialID,
        gDiffMaterialMask | gSpecMaterialMask, // TODO: improve?
        footprintQuality,
        historyLength
#ifdef RELAX_DIFFUSE
        , prevDiffuseIlluminationAnd2ndMomentSMB
        , prevDiffuseIlluminationAnd2ndMomentSMBResponsive
#endif
#ifdef RELAX_SPECULAR
        , prevSpecularIlluminationAnd2ndMomentSMB
        , prevSpecularIlluminationAnd2ndMomentSMBResponsive
        , prevReflectionHitTSMB
#endif
    );

    // History length is based on surface motion based disocclusion
    historyLength = historyLength + 1.0;
    historyLength = min(RELAX_MAX_ACCUM_FRAME_NUM, historyLength);

    // Avoid footprint momentary stretching due to changed viewing angle
    float3 Vprev = normalize(prevWorldPos - gPrevCameraPosition.xyz);
    float VoNflat = abs(dot(currentNormalAveraged, normalize(currentViewVector))) + 1e-3;
    float VoNflatprev = abs(dot(currentNormalAveraged, Vprev)) + 1e-3;
    float sizeQuality = VoNflatprev / VoNflat; // this order because we need to fix stretching only, shrinking is OK
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

    float diffuseAlpha = (surfaceMotionBasedReprojectionFound > 0) ? max(1.0 / (diffMaxAccumulatedFrameNum + 1.0), 1.0 / diffHistoryLength) : 1.0;
    float diffuseAlphaResponsive = (surfaceMotionBasedReprojectionFound > 0) ? max(1.0 / (diffMaxFastAccumulatedFrameNum + 1.0), 1.0 / diffHistoryLength) : 1.0;

    [flatten]
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

    // Calculating surface parallax
    float parallax = ComputeParallax(prevWorldPos - gPrevCameraPosition.xyz, gOrthoMode == 0.0 ? pixelUv : prevUVSMB, gWorldToClip, gRectSize, gUnproject, gOrthoMode);
    float parallaxOrig = parallax;
    float hitDistToSurfaceRatio = saturate(prevReflectionHitTSMB / currentLinearZ);
    parallax *= hitDistToSurfaceRatio;
    float parallaxInPixels = GetParallaxInPixels(max(0.01, parallaxOrig), gUnproject);

    // Current specular signal ( surface motion )
    float specSurfaceFrames = GetSpecAccumSpeed(specHistoryFrames, max(0.05, currentRoughnessModified), NoV, parallax);
    float specSurfaceResponsiveFrames = GetSpecAccumSpeed(specHistoryResponsiveFrames, max(0.05, currentRoughnessModified), NoV, parallax);

    float specSurfaceAlpha = saturate(1.0 / specSurfaceFrames);
    float specSurfaceResponsiveAlpha = saturate(1.0 / specSurfaceResponsiveFrames);

    float surfaceHistoryConfidence = saturate(specSurfaceFrames / (specHistoryFrames + 1.0));

    [flatten]
    if (!specHasData && (parallaxInPixels < 2.0))
    {
        // Adjusting surface motion based specular accumulation weights for checkerboard
        specSurfaceAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * (surfaceMotionBasedReprojectionFound > 0 ? 1.0 : 0.0);
        specSurfaceResponsiveAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * (surfaceMotionBasedReprojectionFound > 0 ? 1.0 : 0.0);
    }

    float4 accumulatedSpecularSMB;
    accumulatedSpecularSMB.rgb = lerp(prevSpecularIlluminationAnd2ndMomentSMB.rgb, specularIllumination.rgb, specSurfaceAlpha);
    accumulatedSpecularSMB.w = lerp(prevReflectionHitTSMB, specularIllumination.w, max(specSurfaceAlpha, RELAX_HIT_DIST_MIN_ACCUM_SPEED(currentRoughnessModified)));

    float3 accumulatedSpecularSMBResponsive = lerp(prevSpecularIlluminationAnd2ndMomentSMBResponsive, specularIllumination.xyz, specSurfaceResponsiveAlpha);
    float accumulatedSpecularM2SMB = lerp(prevSpecularIlluminationAnd2ndMomentSMB.a, specular2ndMoment, specSurfaceAlpha);

    // Thin lens equation for adjusting reflection HitT
    float hitDist = specularIllumination.a;
    hitDist = lerp(minHitDist3x3, minHitDist5x5, STL::Math::SmoothStep(0.04, 0.08, currentRoughnessModified));
    hitDist = clamp(hitDist, specM1.a - specSigma.a * 3.0, specM1.a + specSigma.a * 3.0);
    float divider = 0.5 + curvature * hitDist * NoV;
    float hitDistFocused = 0.5 * hitDist / (divider == 0 ? 0.5 : divider);

    // Adjusting for the highly sloped angles with curvature
    hitDistFocused *= lerp(1.0, NoV, saturate(curvature * hitDist));

    // Limiting hitDist in ortho case to avoid extreme amounts of motion in reflection
    if (gOrthoMode != 0)
        hitDistFocused = min(currentLinearZ, hitDistFocused);

    [flatten]
    if (abs(hitDistFocused) < 0.001)
        hitDistFocused = 0.001;

    // Loading specular data based on virtual motion
    float4 prevSpecularIlluminationAnd2ndMomentVMB;
    float3 prevSpecularIlluminationAnd2ndMomentVMBResponsive;
    float3 prevNormalVMB;
    float  prevRoughnessVMB;
    float  prevReflectionHitTVMB;
    float2 prevUVVMB;

    float virtualHistoryConfidence = loadVirtualMotionBasedPrevData(
        pixelPos,
        currentWorldPos,
        currentNormal,
        currentLinearZ,
        hitDistFocused,
        hitDist,
        currentViewVector,
        prevWorldPos, 
        surfaceMotionBasedReprojectionFound == 2.0 ? true : false,
        currentMaterialID,
        gSpecMaterialMask,
        prevUVSMB,
        parallaxOrig,
        NoV,
        prevSpecularIlluminationAnd2ndMomentVMB,
        prevSpecularIlluminationAnd2ndMomentVMBResponsive,
        prevNormalVMB,
        prevRoughnessVMB,
        prevReflectionHitTVMB,
        prevUVVMB
    );

    // Curvature angle for virtual motion based reprojection
    float2 uvDiff = prevUVVMB - prevUVSMB;
    float curvatureAngle = curvatureAnglePerPixel * length(uvDiff * gRectSize);

    // Normal weight for virtual history
    float fresnelFactor = STL::BRDF::Pow5(NoV);
    float virtualLobeScale = lerp(0.5, 1.0, fresnelFactor);
    float lobeHalfAngle = STL::ImportanceSampling::GetSpecularLobeHalfAngle(currentRoughnessModified);
    lobeHalfAngle *= virtualLobeScale;
    lobeHalfAngle += curvatureAngle;
    lobeHalfAngle += NRD_NORMAL_ENCODING_ERROR + STL::Math::DegToRad(1.5); // TODO: tune better?
    float virtualNormalWeight = GetEncodingAwareNormalWeight(currentNormal, prevNormalVMB, lobeHalfAngle);
    virtualHistoryConfidence *= lerp(virtualNormalWeight, 1.0, saturate(fresnelFactor * parallax));

    // Amount of virtual motion - dominant factor
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection(currentNormal, V, currentRoughness, RELAX_SPEC_DOMINANT_DIRECTION);
    float virtualHistoryAmount = virtualHistoryConfidence * D.w;

    // Virtual history confidence & amount - roughness
    float2 roughnessParams = GetRoughnessWeightParams(currentRoughness, gRoughnessFraction);
    float rw = GetRoughnessWeight(roughnessParams, prevRoughnessVMB);
    float virtualRoughnessWeight = 0.75 + 0.25 * rw;
    virtualHistoryConfidence *= virtualRoughnessWeight;
    virtualHistoryAmount *= (gOrthoMode == 0) ? rw * 0.9 + 0.1 : 1.0;

    // Decreasing virtual history amount for ortho case
    virtualHistoryAmount *= (gOrthoMode == 0) ? 1.0 : 0.75;

    // Virtual history confidence - hit distance
    float maxDist = max(prevReflectionHitTVMB, prevReflectionHitTSMB);
    float dHitT = max(0, sqrt(abs(prevReflectionHitTVMB - prevReflectionHitTSMB)) - 0.0 * specSigma.a);
    float dHitTMultiplier = lerp(20.0, 0.0, GetSpecMagicCurve(currentRoughnessModified));
    float virtualHistoryHitDistConfidence = 1.0 - saturate(dHitTMultiplier * dHitT / (currentLinearZ + maxDist));
    virtualHistoryConfidence *= virtualHistoryHitDistConfidence;

    // "Looking back" 1 and 2 frames and applying normal weight to decrease lags
    float uvDiffScale = lerp(10.0, 1.0, saturate(parallaxInPixels / 5.0)) / 2.0;
    float2 backUV1 = prevUVVMB + 1.0 * uvDiff * uvDiffScale;
    float2 backUV2 = prevUVVMB + 2.0 * uvDiff * uvDiffScale;
    backUV1 *= (gInvResourceSize * gRectSizePrev); // Taking in account resolution scale
    backUV2 *= (gInvResourceSize * gRectSizePrev);
    float4 backNormalRoughness1 = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, backUV1, 0));
    float4 backNormalRoughness2 = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, backUV2, 0));
    backNormalRoughness1.rgb = gUseWorldPrevToWorld ? STL::Geometry::RotateVector(gWorldPrevToWorld, backNormalRoughness1.rgb) : backNormalRoughness1.rgb;
    backNormalRoughness2.rgb = gUseWorldPrevToWorld ? STL::Geometry::RotateVector(gWorldPrevToWorld, backNormalRoughness2.rgb) : backNormalRoughness2.rgb;
    float maxAngle1 = lobeHalfAngle + uvDiffScale * curvatureAngle * 1.0;
    float maxAngle2 = lobeHalfAngle + uvDiffScale * curvatureAngle * 2.0;
    float backNormalWeight1 = IsInScreen(backUV1) ? GetEncodingAwareNormalWeight(currentNormal, backNormalRoughness1.rgb, maxAngle1) : 1.0;
    float backNormalWeight2 = IsInScreen(backUV2) ? GetEncodingAwareNormalWeight(currentNormal, backNormalRoughness2.rgb, maxAngle2) : 1.0;
    float backNormalWeight = backNormalWeight1 * backNormalWeight2;
    virtualHistoryConfidence *= backNormalWeight;
    virtualHistoryAmount *= lerp(0.333, 1.0, backNormalWeight);
    // Applying similar, but simpler (just 1 tap) logic for roughness
    rw = GetRoughnessWeight(roughnessParams, backNormalRoughness2.w);
    virtualRoughnessWeight *= 0.75 + 0.25 * rw;
    virtualHistoryAmount *= (gOrthoMode == 0) ? rw * 0.9 + 0.1 : 1.0;

    // Clamping specular virtual history to current specular signal
    float4 specHistoryVirtual = float4(prevSpecularIlluminationAnd2ndMomentVMB.rgb, prevReflectionHitTVMB);

    if (gVirtualHistoryClampingEnabled != 0)
    {
        float sigmaScale = 6.0;
        sigmaScale *= 1.0 + 7.0 * max(gFramerateScale, 1.0); // Looks aggressive, but it will be balanced by virtualUnclampedAmount
        float4 specHistoryVirtualClamped = STL::Color::Clamp(specM1, specSigma * sigmaScale, specHistoryVirtual);
        float3 specHistoryVirtualResponsiveClamped = STL::Color::Clamp(specM1, specSigma * sigmaScale, prevSpecularIlluminationAnd2ndMomentVMBResponsive.rgbb).rgb;

        float virtualUnclampedAmount = 0.5 * virtualHistoryConfidence + 0.5 * STL::Math::SmoothStep(0.1, 0.25, currentRoughnessModified);
        virtualUnclampedAmount += gFramerateScale * (0.1 + 0.33 * (1.0 - virtualHistoryHitDistConfidence));
        virtualUnclampedAmount = saturate(virtualUnclampedAmount);

        specHistoryVirtual = lerp(specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount);
        specHistoryVirtual.a = specHistoryVirtualClamped.a;
        prevSpecularIlluminationAnd2ndMomentVMBResponsive = lerp(specHistoryVirtualResponsiveClamped, prevSpecularIlluminationAnd2ndMomentVMBResponsive, virtualUnclampedAmount);

        // Clamping 2nd moment too
        float specM2VirtualClamped = STL::Color::Clamp(specular2ndMoment, max(max(specSigma.r, specSigma.g), specSigma.b) * sigmaScale * 2.0, prevSpecularIlluminationAnd2ndMomentVMB.a);
        prevSpecularIlluminationAnd2ndMomentVMB.a = lerp(specM2VirtualClamped, prevSpecularIlluminationAnd2ndMomentVMB.a, virtualUnclampedAmount);
    }

    // Current specular signal ( virtual motion )
    float specVirtualFrames = specHistoryFrames * virtualRoughnessWeight * (0.5 + 0.5 * backNormalWeight);
    float specVirtualResponsiveFrames = specHistoryResponsiveFrames * virtualRoughnessWeight * backNormalWeight;

    // Relying on fast history to be up to speed with reflection HitT inconsistency
    specVirtualResponsiveFrames *= lerp(virtualHistoryHitDistConfidence, 0.5 + 0.5 * virtualHistoryHitDistConfidence, GetSpecMagicCurve(currentRoughnessModified));

    // Artificially decreasing virtual history frames if FPS is lower than 60 and virtual confidence is low, to decrease lags
    float fpsScaler = lerp(saturate(gFramerateScale * gFramerateScale), 1.0, virtualHistoryConfidence);
    specVirtualResponsiveFrames *= fpsScaler;
    specVirtualFrames *= fpsScaler;

    float specVirtualAlpha = 1.0 / (specVirtualFrames + 1.0);
    float specVirtualResponsiveAlpha = 1.0 / (specVirtualResponsiveFrames + 1.0);

    if (!specHasData)
    {
        // Adjusting virtual motion based specular accumulation weights for checkerboard
        specVirtualAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * virtualHistoryConfidence;
        specVirtualResponsiveAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * virtualHistoryConfidence;
    }

    float4 accumulatedSpecularVMB;
    accumulatedSpecularVMB.xyz = lerp(specHistoryVirtual.xyz, specularIllumination.xyz, specVirtualAlpha);
    accumulatedSpecularVMB.w = lerp(specHistoryVirtual.w, specularIllumination.w, max(specVirtualAlpha, RELAX_HIT_DIST_MIN_ACCUM_SPEED(currentRoughnessModified)));
    float3 accumulatedSpecularVMBResponsive = lerp(prevSpecularIlluminationAnd2ndMomentVMBResponsive, specularIllumination.xyz, specVirtualResponsiveAlpha);
    float accumulatedSpecularM2VMB = lerp(prevSpecularIlluminationAnd2ndMomentVMB.a, specular2ndMoment, specVirtualAlpha);

    // Temporal accumulation of reflection HitT
    float accumulatedReflectionHitT = lerp(accumulatedSpecularSMB.w, accumulatedSpecularVMB.w, virtualHistoryAmount * hitDistToSurfaceRatio);

    // Temporal accumulation of specular illumination
    float3 accumulatedSpecularIllumination = lerp(accumulatedSpecularSMB.xyz, accumulatedSpecularVMB.xyz, virtualHistoryAmount);
    float3 accumulatedSpecularIlluminationResponsive = lerp(accumulatedSpecularSMBResponsive.xyz, accumulatedSpecularVMBResponsive.xyz, virtualHistoryAmount);
    float accumulatedSpecular2ndMoment = lerp(accumulatedSpecularM2SMB, accumulatedSpecularM2VMB, virtualHistoryAmount);

    // If zero specular sample (color = 0), artificially adding variance for pixels with low reprojection confidence
    float specularHistoryConfidence = saturate(virtualHistoryConfidence + surfaceHistoryConfidence);
    if (accumulatedSpecular2ndMoment == 0) accumulatedSpecular2ndMoment = gSpecularVarianceBoost * (1.0 - specularHistoryConfidence);

    // Write out the results
    gOutSpecularIllumination[pixelPos] = float4(accumulatedSpecularIllumination, accumulatedSpecular2ndMoment);
    gOutSpecularIlluminationResponsive[pixelPos] = float4(accumulatedSpecularIlluminationResponsive, 0);

    gOutReflectionHitT[pixelPos] = accumulatedReflectionHitT;
    gOutSpecularReprojectionConfidence[pixelPos] = specularHistoryConfidence;
#endif
}
