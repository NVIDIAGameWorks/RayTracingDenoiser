/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"
#include "RELAX_Diffuse_Reproject.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

#define THREAD_GROUP_SIZE 8
#define SKIRT 1

groupshared float4 sharedNormalRoughness[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

// Helper functions
float InterpolateAccumSpeeds(float a, float b, float f)
{
#if( RELAX_USE_ACCUM_SPEED_NONLINEAR_INTERPOLATION == 0 )
    return lerp(a, b, f);
#endif

    a = 1.0 / (1.0 + a);
    b = 1.0 / (1.0 + b);
    f = lerp(a, b, f);

    return 1.0 / f - 1.0;
}

float getJitterRadius(float jitterDelta, float linearZ)
{
    return jitterDelta * gUnproject * (gIsOrtho > 0 ? 1.0 : linearZ);
}

float3 getCurrentWorldPos(int2 pixelPos, float depth)
{
    float2 clipSpaceXY = ((float2)pixelPos + float2(0.5, 0.5)) * gInvRectSize * 2.0 - 1.0;
    return depth * (gFrustumForward.xyz + gFrustumRight.xyz * clipSpaceXY.x - gFrustumUp.xyz * clipSpaceXY.y);
}

float3 getPreviousWorldPos(int2 pixelPos, float depth)
{
    float2 clipSpaceXY = ((float2)pixelPos + float2(0.5, 0.5)) * (1.0 / gRectSizePrev) * 2.0 - 1.0;
    return depth * (gPrevFrustumForward.xyz + gPrevFrustumRight.xyz * clipSpaceXY.x - gPrevFrustumUp.xyz * clipSpaceXY.y);
}

float3 getPreviousWorldPos(float2 clipSpaceXY, float depth)
{
    return depth * (gPrevFrustumForward.xyz + gPrevFrustumRight.xyz * clipSpaceXY.x - gPrevFrustumUp.xyz * clipSpaceXY.y);
}

float isReprojectionTapValid(int2 pixelCoord, float currentLinearZ, float3 currentWorldPos, float3 previousWorldPos, float3 currentNormal, float3 previousNormal, float jitterRadius)
{
    // Check whether reprojected pixel is inside of the screen
    if (any(pixelCoord < int2(0, 0)) || any(pixelCoord >= int2(gResolution))) return 0;

    // Reject backfacing history: if angle between current normal and previous normal is larger than 90 deg
    if (dot(currentNormal, previousNormal) < 0.0) return 0;

    // Check if plane distance is acceptable
    float3 posDiff = currentWorldPos - previousWorldPos;
    float maxPlaneDistance = max(abs(dot(posDiff, previousNormal)), abs(dot(posDiff, currentNormal)));
    return GetPlaneDistanceWeight(maxPlaneDistance, currentLinearZ, gDisocclusionThreshold + jitterRadius) > 1.0 ? 0.0 : 1.0;
}

// Returns reprojection search result based on surface motion:
// 2 - reprojection found, bicubic footprint was used
// 1 - reprojection found, bilinear footprint was used 
// 0 - reprojection not found
//
// Also returns reprojected data from previous frame calculated using filtering based on filters above.
// For better performance, some data is filtered using weighed bilinear instead of bicubic even if all bicubic taps are valid.
float loadSurfaceMotionBasedPrevData(
    int2 pixelPosOnScreen,
    float3 currentWorldPos,
    float3 currentNormal,
    float currentLinearZ,
    float3 motionVector,
    out float4 prevDiffuseIllumAnd2ndMoment,
    out float3 prevDiffuseResponsiveIllum,
    out float3 prevNormal,
    out float historyLength,
    out float footprintQuality)
{
    // Calculating jitter margin radius in world space
    float jitterRadius = getJitterRadius(gJitterDelta, currentLinearZ);

    // Calculating previous pixel position and UV
    float2 pixelUV = (pixelPosOnScreen + 0.5) * gInvRectSize;
    float2 prevUV = STL::Geometry::GetPrevUvFromMotion(pixelUV, currentWorldPos, gPrevWorldToClip, motionVector, gWorldSpaceMotion);
    float2 prevPixelPosOnScreen = prevUV * gRectSizePrev;

    // Consider reprojection to the same pixel index a small motion.
    // It is useful for skipping reprojection test for static camera when the jitter is the only source of motion.
    int2 prevPixelPosInt = int2(prevPixelPosOnScreen);
    bool isSmallMotion = all(prevPixelPosInt == pixelPosOnScreen);
    bool skipReprojectionTest = gSkipReprojectionTestWithoutMotion && gIsCameraStatic && isSmallMotion;

    // Calculating footprint origin and weights
    int2 bilinearOrigin = int2(floor(prevPixelPosOnScreen - 0.5));
    float2 bilinearWeights = frac(prevPixelPosOnScreen - 0.5);

    // Checking bicubic footprint (with cut corners) first,
    // remembering bilinear taps validity and worldspace position along the way,
    // for faster weighted bilinear and for calculating previous worldspace position
    // bc - bicubic & bilinear tap,
    // bl - bilinear tap
    //
    // -- bc bc --
    // bc bl bl bc
    // bc bl bl bc
    // -- bc bc --

    float bicubicFootprintValid = 1.0;
    float4 bilinearTapsValid = 0;

    float3 prevNormalInTap;
    float prevViewZInTap;
    float3 prevWorldPosInTap;
    int2 tapPos;
    float reprojectionTapValid;
    float3 prevNormal00, prevNormal10, prevNormal01, prevNormal11;

    prevNormal00 = UnpackPrevNormalRoughness(gPrevNormalRoughness[bilinearOrigin + int2(0, 0)]).rgb;
    prevNormal10 = UnpackPrevNormalRoughness(gPrevNormalRoughness[bilinearOrigin + int2(1, 0)]).rgb;
    prevNormal01 = UnpackPrevNormalRoughness(gPrevNormalRoughness[bilinearOrigin + int2(0, 1)]).rgb;
    prevNormal11 = UnpackPrevNormalRoughness(gPrevNormalRoughness[bilinearOrigin + int2(1, 1)]).rgb;
    float3 prevNormalFlat = normalize(prevNormal00 + prevNormal10 + prevNormal01 + prevNormal11);

    // Adjusting worldspace position:
    // Applying worldspace motion first,
    motionVector *= gWorldSpaceMotion > 0 ? 1.0 : 0.0;

    // Then taking care of camera motion, because world space is always centered at camera position in NRD
    currentWorldPos += motionVector - gPrevCameraPosition;

    // Transforming bilinearOrigin to clip space coords to simplify previous world pos calculation
    float2 prevClipSpaceXY = ((float2)bilinearOrigin + float2(0.5, 0.5)) * (1.0 / gRectSizePrev) * 2.0 - 1.0;
    float2 dXY = (2.0 / gRectSizePrev);

    // 1st row
    tapPos = bilinearOrigin + int2(0, -1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(0.0, -1.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, -1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(1.0, -1.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // 2nd row
    tapPos = bilinearOrigin + int2(-1, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(-1.0, 0.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(0.0, 0.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.x = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(1.0, 0.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.y = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(2, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(2.0, 0.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // 3rd row
    tapPos = bilinearOrigin + int2(-1, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(-1.0, 1.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(0.0, 1.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.z = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(1.0, 1.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.w = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(2, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(2.0, 1.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // 4th row
    tapPos = bilinearOrigin + int2(0, 2);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(0.0, 2.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 2);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = getPreviousWorldPos(prevClipSpaceXY + dXY * float2(1.0, 2.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalFlat, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    bilinearTapsValid = skipReprojectionTest ? float4(1.0, 1.0, 1.0, 1.0) : bilinearTapsValid;

    // Calculating interpolated binary weight for bilinear taps in advance
    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;
    float interpolatedBinaryWeight = STL::Filtering::ApplyBilinearFilter(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w, bilinear);
    interpolatedBinaryWeight = max(1e-6, interpolatedBinaryWeight);

    // Applying reprojection filters
    float reprojectionFound = 0;
    prevDiffuseIllumAnd2ndMoment = 0;
    prevDiffuseResponsiveIllum = 0;
    prevNormal = currentNormal;
    historyLength = 0;
    footprintQuality = 0;

    if (any(bilinearTapsValid))
    {
        // Trying to apply bicubic filter first
        if (bicubicFootprintValid > 0)
        {
            // Bicubic for illumination and 2nd moments
            prevDiffuseIllumAnd2ndMoment =
                max(0, BicubicFloat4(gPrevDiffuseIllumination, gLinearClamp, prevPixelPosOnScreen, gInvViewSize));
#if( RELAX_USE_BICUBIC_FOR_FAST_HISTORY == 1 )
            prevDiffuseResponsiveIllum = max(0, BicubicFloat4(gPrevDiffuseIlluminationResponsive, gLinearClamp, prevPixelPosOnScreen, gInvViewSize)).rgb;
#else
            prevDiffuseResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevDiffuseIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
#endif
            footprintQuality = 1.0;
            reprojectionFound = 2.0;
        }
        else
        {
            // If no success with the bicubic, then do weighted bilinear
            prevDiffuseIllumAnd2ndMoment =
                BilinearWithBinaryWeightsFloat4(gPrevDiffuseIllumination, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

            prevDiffuseResponsiveIllum = 
                BilinearWithBinaryWeightsFloat4(gPrevDiffuseIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;

            reprojectionFound = 1.0;
        }

        prevNormal = BilinearWithBinaryWeightsImmediateFloat3(
            prevNormal00, prevNormal10, prevNormal01, prevNormal11,
            bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        historyLength = 255.0 * BilinearWithBinaryWeightsFloat(gPrevDiffuseHistoryLength, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        footprintQuality = interpolatedBinaryWeight;
    }
    return reprojectionFound;
}

//
// Main
//
[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
NRD_EXPORT void NRD_CS_MAIN(int2 ipos : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadId, uint3 groupId : SV_GroupId)
{
    // Calculating checkerboard fields
    bool diffHasData = true;
    uint2 checkerboardPixelPos = ipos.xx;
    uint checkerboard = STL::Sequence::CheckerBoard(ipos, gFrameIndex);

    if (gDiffCheckerboard != 2)
    {
        diffHasData = checkerboard == gDiffCheckerboard;
        checkerboardPixelPos.x >>= 1;
    }

    // Populating shared memory
    uint linearThreadIndex = groupThreadId.y * THREAD_GROUP_SIZE + groupThreadId.x;
    uint newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    uint newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    uint blockXStart = groupId.x * THREAD_GROUP_SIZE;
    uint blockYStart = groupId.y * THREAD_GROUP_SIZE;

    // First stage
    uint ox = newIdxX;
    uint oy = newIdxY;
    int xx = blockXStart + newIdxX - SKIRT;
    int yy = blockYStart + newIdxY - SKIRT;

    float4 normalRoughness = 0;

    if ((xx >= 0) && (yy >= 0) && (xx < (int)gResolution.x) && (yy < (int)gResolution.y))
    {
        normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx, yy) + gRectOrigin]);
    }
    sharedNormalRoughness[oy][ox] = normalRoughness;

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    normalRoughness = 0;

    if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if ((xx >= 0) && (yy >= 0) && (xx < (int)gResolution.x) && (yy < (int)gResolution.y))
        {
            normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx, yy) + gRectOrigin]);
        }
        sharedNormalRoughness[oy][ox] = normalRoughness;
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    uint2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT, SKIRT);

    // Center data
    float3 diffuseIllumination = gDiffuseIllumination[ipos.xy + gRectOrigin].rgb;

    // Reading current GBuffer data and center/left/right viewZ
    float3 currentNormal = sharedNormalRoughness[sharedMemoryIndex.y][sharedMemoryIndex.x].rgb;
    float currentLinearZ = gViewZ[ipos.xy + gRectOrigin];

    // Early out if linearZ is beyond denoising range
    [branch]
    if (currentLinearZ > gDenoisingRange)
    {
        return;
    }

    // Calculating average normal in 3x3 area around current pixel
    float3 currentNormalAveraged = currentNormal;

    [unroll]
    for (int i = -1; i <= 1; i++)
    {
        [unroll]
        for (int j = -1; j <= 1; j++)
        {
            // Skipping center pixel
            if ((i == 0) && (j == 0)) continue;

            int2 p = ipos + int2(i, j);
            float3 pNormal = sharedNormalRoughness[sharedMemoryIndex.y + j][sharedMemoryIndex.x + i].xyz;

            currentNormalAveraged += pNormal;
        }
    }
    currentNormalAveraged /= 9.0;

    // Computing 2nd moments of luminance
    float diffuse1stMoment = STL::Color::Luminance(diffuseIllumination.rgb);
    float diffuse2ndMoment = diffuse1stMoment * diffuse1stMoment;

    // Getting current frame worldspace position for current pixel
    float3 currentWorldPos = getCurrentWorldPos(ipos, currentLinearZ);

    // Reading motion vector
    float3 motionVector = gMotion[gRectOrigin + ipos].xyz * gMotionVectorScale.xyy;

    // Loading previous data based on surface motion vectors
    float4 prevDiffuseIlluminationAnd2ndMomentSMB;
    float3 prevDiffuseIlluminationAnd2ndMomentSMBResponsive;
    float3 prevNormalSMB;
    float historyLength;
    float footprintQuality;

    float surfaceMotionBasedReprojectionFound = loadSurfaceMotionBasedPrevData(
        ipos,
        currentWorldPos,
        normalize(currentNormalAveraged),
        currentLinearZ,
        motionVector,
        prevDiffuseIlluminationAnd2ndMomentSMB,
        prevDiffuseIlluminationAnd2ndMomentSMBResponsive,
        prevNormalSMB,
        historyLength, 
        footprintQuality
    );

    // History length is based on surface motion based disocclusion
    historyLength = historyLength + 1.0;
    historyLength = min(100.0, historyLength);

    // Minimize "getting stuck in history" effect when only fraction of bilinear footprint is valid 
    // by shortening the history length 
    if (footprintQuality < 1.0)
    {
        historyLength *= sqrt(footprintQuality);
        historyLength = max(historyLength, 1.0);
    }

    // Handling history reset if needed
    if (gResetHistory != 0) historyLength = 1.0;

    // DIFFUSE ACCUMULATION BELOW
    //
    // Temporal accumulation of diffuse illumination
    float diffMaxAccumulatedFrameNum = gDiffuseMaxAccumulatedFrameNum;
    float diffMaxFastAccumulatedFrameNum = gDiffuseMaxFastAccumulatedFrameNum;

    if (gRejectDiffuseHistoryNormalThreshold > 0)
    {
        float NDotPrevN = saturate(dot(currentNormal, prevNormalSMB));
        float diffuseNormalWeight = 0.5 + 0.5 * (NDotPrevN > (1.0 - gRejectDiffuseHistoryNormalThreshold) ? 1.0 : 0.0);
        diffMaxAccumulatedFrameNum *= diffuseNormalWeight;
        diffMaxFastAccumulatedFrameNum *= diffuseNormalWeight;
    }

    if (gUseConfidenceInputs)
    {
        float inDiffConfidence = gDiffConfidence[ipos];
        diffMaxAccumulatedFrameNum *= inDiffConfidence;
        diffMaxFastAccumulatedFrameNum *= inDiffConfidence;
    }

    float diffuseAlpha = (surfaceMotionBasedReprojectionFound > 0) ? max(1.0 / (diffMaxAccumulatedFrameNum + 1.0), 1.0 / historyLength) : 1.0;
    float diffuseAlphaResponsive = (surfaceMotionBasedReprojectionFound > 0) ? max(1.0 / (diffMaxFastAccumulatedFrameNum + 1.0), 1.0 / historyLength) : 1.0;

    if ((!diffHasData) && (historyLength > 1.0))
    {
        // Adjusting diffuse accumulation weights for checkerboard
        diffuseAlpha *= 1.0 - gCheckerboardResolveAccumSpeed;
        diffuseAlphaResponsive *= 1.0 - gCheckerboardResolveAccumSpeed;
    }

    float4 accumulatedDiffuseIlluminationAnd2ndMoment = lerp(prevDiffuseIlluminationAnd2ndMomentSMB, float4(diffuseIllumination.rgb, diffuse2ndMoment), diffuseAlpha);
    float3 accumulatedDiffuseIlluminationResponsive = lerp(prevDiffuseIlluminationAnd2ndMomentSMBResponsive.rgb, diffuseIllumination.rgb, diffuseAlphaResponsive);

    // Write out the diffuse results
    gOutDiffuseIllumination[ipos] = accumulatedDiffuseIlluminationAnd2ndMoment;
    gOutDiffuseIlluminationResponsive[ipos] = float4(accumulatedDiffuseIlluminationResponsive, 0);

    gOutDiffuseHistoryLength[ipos] = historyLength / 255.0;
}
