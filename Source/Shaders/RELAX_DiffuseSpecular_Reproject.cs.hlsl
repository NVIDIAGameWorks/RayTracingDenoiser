/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"
#include "RELAX_DiffuseSpecular_Reproject.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

#define THREAD_GROUP_SIZE 8
#define SKIRT 2

groupshared float4 sharedInSpecular[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];
groupshared float4 sharedNormalRoughness[THREAD_GROUP_SIZE + SKIRT * 2][THREAD_GROUP_SIZE + SKIRT * 2];

// Helper functions

float GetSpecMagicCurve(float roughness, float power = 0.25)
{
    // http://fooplot.com/#W3sidHlwZSI6MCwiZXEiOiIxLjAtMl4oLTE1LjAqeCkiLCJjb2xvciI6IiNGMjE4MTgifSx7InR5cGUiOjAsImVxIjoiKDEtMl4oLTIwMCp4KngpKSooeF4wLjI1KSIsImNvbG9yIjoiIzIyRUQxNyJ9LHsidHlwZSI6MCwiZXEiOiIoMS0yXigtMjAwKngqeCkpKih4XjAuNSkiLCJjb2xvciI6IiMxNzE2MTYifSx7InR5cGUiOjEwMDAsIndpbmRvdyI6WyIwIiwiMSIsIjAiLCIxLjEiXSwic2l6ZSI6WzEwMDAsNTAwXX1d

    float f = 1.0 - exp2(-200.0 * roughness * roughness);
    f *= STL::Math::Pow01(roughness, power);

    return f;
}

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

float GetNormalWeight(float params0, float3 n0, float3 n)
{
    float cosa = saturate(dot(n0, n));
    float angle = STL::Math::AcosApprox(cosa);
    return _ComputeWeight(float2(params0, -0.001), angle);
}

float2 GetRoughnessWeightParams(float roughness0)
{
    float a = rcp(roughness0 * 0.05 * 0.99 + 0.01);
    float b = roughness0 * a;
    return float2(a, -b);
}

#define GetRoughnessWeight _ComputeWeight

float ComputeParallax(float3 X, float3 Xprev, float3 cameraDelta)
{
    float3 Xt = Xprev - cameraDelta;
    float cosa = dot(X, Xt);
    cosa *= STL::Math::Rsqrt(STL::Math::LengthSquared(Xt) * STL::Math::LengthSquared(X));
    cosa = saturate(cosa);
    float parallax = STL::Math::Sqrt01(1.0 - cosa * cosa) * STL::Math::PositiveRcp(cosa);
    parallax *= RELAX_PARALLAX_NORMALIZATION;
    parallax /= 1.0 + RELAX_PARALLAX_COMPRESSION_STRENGTH * parallax;
    return parallax;
}

float GetParallaxInPixels(float parallax)
{
    parallax /= 1.0 - RELAX_PARALLAX_COMPRESSION_STRENGTH * parallax;

    float parallaxInPixels = parallax / (RELAX_PARALLAX_NORMALIZATION * gUnproject);

    return parallaxInPixels;
}

float getJitterRadius(float jitterDelta, float linearZ)
{
    return jitterDelta * gUnproject * (gIsOrtho == 0 ? linearZ : 1.0);
}

float isReprojectionTapValid(float3 currentWorldPos, float3 previousWorldPos, float3 currentNormal, float disocclusionThreshold)
{
    // Check if plane distance is acceptable
    float3 posDiff = currentWorldPos - previousWorldPos;
    float maxPlaneDistance = abs(dot(posDiff, currentNormal));
    return maxPlaneDistance > disocclusionThreshold ? 0.0 : 1.0;
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
    float currentReflectionHitT,
    float3 motionVector,
    out float4 prevSpecularIllumAnd2ndMoment,
    out float4 prevDiffuseIllumAnd2ndMoment,
    out float3 prevSpecularResponsiveIllum,
    out float3 prevDiffuseResponsiveIllum,
    out float3 prevWorldPos,
    out float3 prevNormal,
    out float  prevReflectionHitT,
    out float2 historyLength,
    out float2 prevUV,
    out float footprintQuality)
{
    // Calculating jitter margin radius in world space
    float jitterRadius = getJitterRadius(gPrevCameraPositionAndJitterDelta.w, currentLinearZ);
    float disocclusionThreshold = (gDisocclusionDepthThreshold + jitterRadius) * (gIsOrtho == 0 ? currentLinearZ : 1.0);

    // Calculating previous pixel position and UV
    float2 pixelUV = (pixelPosOnScreen + 0.5) * gInvRectSize;
    prevUV = STL::Geometry::GetPrevUvFromMotion(pixelUV, currentWorldPos, gPrevWorldToClip, motionVector, gIsWorldSpaceMotion);
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
    float3 prevWorldPos00, prevWorldPos10, prevWorldPos01, prevWorldPos11;
    float3 prevNormal00, prevNormal10, prevNormal01, prevNormal11;

    prevNormal00 = UnpackPrevNormalRoughness(gPrevNormalRoughness[bilinearOrigin + int2(0, 0)]).rgb;
    prevNormal10 = UnpackPrevNormalRoughness(gPrevNormalRoughness[bilinearOrigin + int2(1, 0)]).rgb;
    prevNormal01 = UnpackPrevNormalRoughness(gPrevNormalRoughness[bilinearOrigin + int2(0, 1)]).rgb;
    prevNormal11 = UnpackPrevNormalRoughness(gPrevNormalRoughness[bilinearOrigin + int2(1, 1)]).rgb;
    float3 prevNormalFlat = normalize(prevNormal00 + prevNormal10 + prevNormal01 + prevNormal11);

    // Adjusting worldspace position:
    // Applying worldspace motion first,
    motionVector *= gIsWorldSpaceMotion > 0 ? 1.0 : 0.0;

    // Then taking care of camera motion, because world space is always centered at camera position in NRD
    currentWorldPos += motionVector - gPrevCameraPositionAndJitterDelta.xyz;

    // Transforming bilinearOrigin to clip space coords to simplify previous world pos calculation
    float2 prevClipSpaceXY = ((float2)bilinearOrigin + float2(0.5, 0.5)) * (1.0 / gRectSizePrev) * 2.0 - 1.0;
    float2 dXY = (2.0 / gRectSizePrev);

    // 1st row
    tapPos = bilinearOrigin + int2(0, -1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(0.0, -1.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, -1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(1.0, -1.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;

    // 2nd row
    tapPos = bilinearOrigin + int2(-1, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(-1.0, 0.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(0.0, 0.0), prevViewZInTap);
    prevWorldPos00 = prevWorldPosInTap;
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.x = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(1.0, 0.0), prevViewZInTap);
    prevWorldPos10 = prevWorldPosInTap;
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.y = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(2, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(2.0, 0.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;

    // 3rd row
    tapPos = bilinearOrigin + int2(-1, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(-1.0, 1.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(0.0, 1.0), prevViewZInTap);
    prevWorldPos01 = prevWorldPosInTap;
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.z = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(1.0, 1.0), prevViewZInTap);
    prevWorldPos11 = prevWorldPosInTap;
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.w = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(2, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(2.0, 1.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;

    // 4th row
    tapPos = bilinearOrigin + int2(0, 2);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(0.0, 2.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 2);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPos(prevClipSpaceXY + dXY * float2(1.0, 2.0), prevViewZInTap);
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);
    bicubicFootprintValid *= reprojectionTapValid;

    bilinearTapsValid = skipReprojectionTest ? float4(1.0, 1.0, 1.0, 1.0) : bilinearTapsValid;

    // Reject backfacing history: if angle between current normal and previous normal is larger than 90 deg
    if (dot(currentNormal, prevNormalFlat) < 0.0)
    {
        bilinearTapsValid = 0;
        bicubicFootprintValid = 0;
    }

    // Checking bicubic footprint validity for being in rect
    if (any(bilinearOrigin < int2(1, 1)) || any(bilinearOrigin >= int2(gRectSizePrev) - int2(2, 2)))
    {
        bicubicFootprintValid = 0;
    }

    // Checking bilinear footprint validity for being in rect
    // Bilinear footprint:
    // x y
    // z w
    if (bilinearOrigin.x < 0) bilinearTapsValid.xz = 0;
    if (bilinearOrigin.x >= gRectSizePrev.x) bilinearTapsValid.yw = 0;
    if (bilinearOrigin.y < 0) bilinearTapsValid.xy = 0;
    if (bilinearOrigin.y >= gRectSizePrev.y) bilinearTapsValid.zw = 0;

    // Calculating interpolated binary weight for bilinear taps in advance
    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;
    float interpolatedBinaryWeight = STL::Filtering::ApplyBilinearFilter(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w, bilinear);
    interpolatedBinaryWeight = max(1e-6, interpolatedBinaryWeight);

    // Applying reprojection filters
    float reprojectionFound = 0;
    prevSpecularIllumAnd2ndMoment = 0;
    prevDiffuseIllumAnd2ndMoment = 0;
    prevSpecularResponsiveIllum = 0;
    prevDiffuseResponsiveIllum = 0;
    prevWorldPos = currentWorldPos;
    prevNormal = currentNormal;
    prevReflectionHitT = currentReflectionHitT;
    historyLength = 0;
    footprintQuality = 0;

    if (any(bilinearTapsValid))
    {
        // Trying to apply bicubic filter first
        if (bicubicFootprintValid > 0)
        {
            // Bicubic for illumination and 2nd moments
            BicubicFloat4x2(
                prevSpecularIllumAnd2ndMoment,
                prevDiffuseIllumAnd2ndMoment,
                gPrevSpecularIllumination,
                gPrevDiffuseIllumination,
                gLinearClamp,
                prevPixelPosOnScreen,
                gInvResourceSize);
            prevSpecularIllumAnd2ndMoment = max(0, prevSpecularIllumAnd2ndMoment);
            prevDiffuseIllumAnd2ndMoment = max(0, prevDiffuseIllumAnd2ndMoment);

#if( RELAX_USE_BICUBIC_FOR_FAST_HISTORY == 1 )

            float4 spec, diff;
            BicubicFloat4x2(
                spec,
                diff,
                gPrevSpecularIlluminationResponsive,
                gPrevDiffuseIlluminationResponsive,
                gLinearClamp,
                prevPixelPosOnScreen,
                gInvResourceSize);
            prevSpecularResponsiveIllum = max(0, spec.rgb);
            prevDiffuseResponsiveIllum = max(0, diff.rgb);
#else
            prevSpecularResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevSpecularIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
            prevDiffuseResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevDiffuseIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
#endif
            footprintQuality = 1.0;

            reprojectionFound = 2.0;
        }
        else
        {
            // If no success with the bicubic, then do weighted bilinear
            prevSpecularIllumAnd2ndMoment =
                BilinearWithBinaryWeightsFloat4(gPrevSpecularIllumination, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
            prevDiffuseIllumAnd2ndMoment =
                BilinearWithBinaryWeightsFloat4(gPrevDiffuseIllumination, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

            prevSpecularResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevSpecularIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
            prevDiffuseResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevDiffuseIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;

            reprojectionFound = 1.0;
        }

        // Calculating previous worldspace position
        // by applying weighted bilinear to worldspace positions in taps.
        // Also calculating history length by using weighted bilinear
        prevWorldPos = BilinearWithBinaryWeightsImmediateFloat3(
                prevWorldPos00, prevWorldPos10, prevWorldPos01, prevWorldPos11,
                bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        prevNormal = BilinearWithBinaryWeightsImmediateFloat3(
            prevNormal00, prevNormal10, prevNormal01, prevNormal11,
            bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        historyLength = 255.0 * BilinearWithBinaryWeightsFloat2(gPrevSpecularAndDiffuseHistoryLength, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        float2 gatherOrigin = (bilinearOrigin + 1.0) * gInvResourceSize;
        float4 prevReflectionHitTs = gPrevReflectionHitT.GatherRed(gNearestClamp, gatherOrigin).wzxy;

        prevReflectionHitT = BilinearWithBinaryWeightsImmediateFloat(
            prevReflectionHitTs.x, prevReflectionHitTs.y, prevReflectionHitTs.z, prevReflectionHitTs.w,
            bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        prevReflectionHitT = max(0.001, prevReflectionHitT);

        footprintQuality = interpolatedBinaryWeight;
    }
    return reprojectionFound;
}

// Returns specular reprojection search result based on virtual motion
float loadVirtualMotionBasedPrevData(
    int2 pixelPosOnScreen,
    float3 currentWorldPos,
    float3 currentNormal,
    float currentLinearZ,
    float currentReflectionHitT,
    float3 currentViewVector,
    float3 prevWorldPos,
    bool surfaceBicubicValid,
    out float4 prevSpecularIllumAnd2ndMoment,
    out float3 prevSpecularResponsiveIllum,
    out float3 prevNormal,
    out float prevRoughness,
    out float prevReflectionHitT,
    out float2 prevUVVMB)
{
    // Taking care of camera motion, because world space is always centered at camera position in NRD
    prevWorldPos += gPrevCameraPositionAndJitterDelta.xyz;
    currentWorldPos -= gPrevCameraPositionAndJitterDelta.xyz;

    // Calculating previous worldspace virtual position based on reflection hitT
    float3 virtualViewVector = normalize(currentViewVector) * currentReflectionHitT;
    float3 prevVirtualWorldPos = prevWorldPos + virtualViewVector;

    float currentViewVectorLength = length(currentViewVector);
    float accumulatedSpecularVMBZ = currentViewVectorLength + currentReflectionHitT;

    float4 prevVirtualClipPos = mul(gPrevWorldToClip, float4(prevVirtualWorldPos, 1.0));
    prevVirtualClipPos.xy /= prevVirtualClipPos.w;
    prevUVVMB = prevVirtualClipPos.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    float2 prevVirtualPixelPosOnScreen = prevUVVMB * gRectSizePrev;

    float jitterRadius = getJitterRadius(gPrevCameraPositionAndJitterDelta.w, accumulatedSpecularVMBZ);
    float disocclusionThreshold = (gDisocclusionDepthThreshold + jitterRadius) * (gIsOrtho == 0 ? currentLinearZ : 1.0);

    // Consider reprojection to the same pixel index a small motion.
    // It is useful for skipping reprojection test for static camera when the jitter is the only source of motion.
    int2 prevVirtualPixelPosInt = int2(prevVirtualPixelPosOnScreen);
    bool isSmallVirtualMotion = all(prevVirtualPixelPosInt == pixelPosOnScreen);
    bool skipReprojectionTest = gSkipReprojectionTestWithoutMotion && gIsCameraStatic && isSmallVirtualMotion;

    // Calculating footprint origin and weights
    int2 bilinearOrigin = int2(floor(prevVirtualPixelPosOnScreen - 0.5));
    float2 bilinearWeights = frac(prevVirtualPixelPosOnScreen - 0.5);

    // Checking bilinear footprint
    float3 prevNormal00, prevNormal10, prevNormal01, prevNormal11;
    float prevRoughness00, prevRoughness10, prevRoughness01, prevRoughness11;
    int2 tapPos;
    float prevLinearZInTap;
    float3 prevWorldPosInTap;
    float4 bilinearTapsValid;
    float4 prevNormalRoughness;

    float2 gatherOrigin = (bilinearOrigin + 1.0) * gInvResourceSize;
    float4 prevViewZs = gPrevViewZ.GatherRed(gNearestClamp, gatherOrigin).wzxy;

    tapPos = bilinearOrigin + int2(0, 0);
    prevNormalRoughness = UnpackPrevNormalRoughness(gPrevNormalRoughness[tapPos]);
    prevNormal00 = prevNormalRoughness.rgb;
    prevRoughness00 = prevNormalRoughness.a;
    prevLinearZInTap = prevViewZs.x;
    prevWorldPosInTap = GetPreviousWorldPos(tapPos, prevLinearZInTap);
    bilinearTapsValid.x = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);

    tapPos = bilinearOrigin + int2(1, 0);
    prevNormalRoughness = UnpackPrevNormalRoughness(gPrevNormalRoughness[tapPos]);
    prevNormal10 = prevNormalRoughness.rgb;
    prevRoughness10 = prevNormalRoughness.a;
    prevLinearZInTap = prevViewZs.y;
    prevWorldPosInTap = GetPreviousWorldPos(tapPos, prevLinearZInTap);
    bilinearTapsValid.y = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);

    tapPos = bilinearOrigin + int2(0, 1);
    prevNormalRoughness = UnpackPrevNormalRoughness(gPrevNormalRoughness[tapPos]);
    prevNormal01 = prevNormalRoughness.rgb;
    prevRoughness01 = prevNormalRoughness.a;
    prevLinearZInTap = prevViewZs.z;
    prevWorldPosInTap = GetPreviousWorldPos(tapPos, prevLinearZInTap);
    bilinearTapsValid.z = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);

    tapPos = bilinearOrigin + int2(1, 1);
    prevNormalRoughness = UnpackPrevNormalRoughness(gPrevNormalRoughness[tapPos]);
    prevNormal11 = prevNormalRoughness.rgb;
    prevRoughness11 = prevNormalRoughness.a;
    prevLinearZInTap = prevViewZs.w;
    prevWorldPosInTap = GetPreviousWorldPos(tapPos, prevLinearZInTap);
    bilinearTapsValid.w = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold);

    bilinearTapsValid = skipReprojectionTest ? float4(1.0, 1.0, 1.0, 1.0) : bilinearTapsValid;

    // Checking bilinear footprint validity for being in rect
    // Bilinear footprint:
    // x y
    // z w
    if (bilinearOrigin.x < 0) bilinearTapsValid.xz = 0;
    if (bilinearOrigin.x >= gRectSizePrev.x) bilinearTapsValid.yw = 0;
    if (bilinearOrigin.y < 0) bilinearTapsValid.xy = 0;
    if (bilinearOrigin.y >= gRectSizePrev.y) bilinearTapsValid.zw = 0;

    // Calculating interpolated binary weight for bilinear taps in advance
    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;
    float interpolatedBinaryWeight = STL::Filtering::ApplyBilinearFilter(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w, bilinear);
    interpolatedBinaryWeight = max(1e-6, interpolatedBinaryWeight);

    // Applying reprojection
    float reprojectionFound = 0;
    prevSpecularIllumAnd2ndMoment = 0;
    prevSpecularResponsiveIllum = 0;
    prevNormal = currentNormal;
    prevRoughness = 0;
    prevReflectionHitT = currentReflectionHitT;

    // Weighted bilinear (or bicubic optionally) for prev specular data based on virtual motion
    if (any(bilinearTapsValid))
    {
#if( RELAX_USE_BICUBIC_FOR_VIRTUAL_MOTION_SPECULAR == 1 )
        if (surfaceBicubicValid)
        {
            prevSpecularIllumAnd2ndMoment = max(0, BicubicFloat4(gPrevSpecularIllumination, gLinearClamp, prevVirtualPixelPosOnScreen, gInvResourceSize));

#if( RELAX_USE_BICUBIC_FOR_FAST_HISTORY == 1 )
            prevSpecularResponsiveIllum = max(0, BicubicFloat4(gPrevSpecularIlluminationResponsive, gLinearClamp, prevVirtualPixelPosOnScreen, gInvResourceSize).rgb);
#else
            prevSpecularResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevSpecularIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
#endif
        }
        else
        {
            prevSpecularIllumAnd2ndMoment = BilinearWithBinaryWeightsFloat4(gPrevSpecularIllumination, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
            prevSpecularResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevSpecularIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
        }
#else
        prevSpecularIllumAnd2ndMoment = BilinearWithBinaryWeightsFloat4(gPrevSpecularIllumination, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        prevSpecularResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevSpecularIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
#endif

        float4 prevNormalRoughness = BilinearWithBinaryWeightsImmediateFloat4(
            float4(prevNormal00, prevRoughness00),
            float4(prevNormal10, prevRoughness10),
            float4(prevNormal01, prevRoughness01),
            float4(prevNormal11, prevRoughness11),
            bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        prevNormal = prevNormalRoughness.xyz;
        prevRoughness = prevNormalRoughness.w;

        float4 prevReflectionHitTs = gPrevReflectionHitT.GatherRed(gNearestClamp, gatherOrigin).wzxy;

        prevReflectionHitT = BilinearWithBinaryWeightsImmediateFloat(
            prevReflectionHitTs.x, prevReflectionHitTs.y, prevReflectionHitTs.z, prevReflectionHitTs.w,
            bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        prevReflectionHitT = max(0.001, prevReflectionHitT);
        reprojectionFound = 1.0;
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
    bool specHasData = true;
    uint2 checkerboardPixelPos = ipos.xx;
    uint checkerboard = STL::Sequence::CheckerBoard(ipos, gFrameIndex);

    if (gDiffCheckerboard != 2)
    {
        diffHasData = checkerboard == gDiffCheckerboard;
        checkerboardPixelPos.x >>= 1;
    }

    if (gSpecCheckerboard != 2)
    {
        specHasData = checkerboard == gSpecCheckerboard;
        checkerboardPixelPos.y >>= 1;
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

    float4 inSpecularIllumination = 0;
    float4 normalRoughness = 0;

    if ((xx >= 0) && (yy >= 0) && (xx < (int)gRectSize.x) && (yy < (int)gRectSize.y))
    {
        inSpecularIllumination = gSpecularIllumination[int2(xx, yy) + gRectOrigin];
        normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx, yy) + gRectOrigin]);
    }
    sharedInSpecular[oy][ox] = inSpecularIllumination;
    sharedNormalRoughness[oy][ox] = normalRoughness;

    // Second stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    inSpecularIllumination = 0;
    normalRoughness = 0;

    if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if ((xx >= 0) && (yy >= 0) && (xx < (int)gRectSize.x) && (yy < (int)gRectSize.y))
        {
            inSpecularIllumination = gSpecularIllumination[int2(xx, yy) + gRectOrigin];
            normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx, yy) + gRectOrigin]);
        }
        sharedInSpecular[oy][ox] = inSpecularIllumination;
        sharedNormalRoughness[oy][ox] = normalRoughness;
    }

    // Third stage
    linearThreadIndex += THREAD_GROUP_SIZE * THREAD_GROUP_SIZE;
    newIdxX = linearThreadIndex % (THREAD_GROUP_SIZE + SKIRT * 2);
    newIdxY = linearThreadIndex / (THREAD_GROUP_SIZE + SKIRT * 2);

    ox = newIdxX;
    oy = newIdxY;
    xx = blockXStart + newIdxX - SKIRT;
    yy = blockYStart + newIdxY - SKIRT;

    inSpecularIllumination = 0;
    normalRoughness = 0;

    if (linearThreadIndex < (THREAD_GROUP_SIZE + SKIRT * 2) * (THREAD_GROUP_SIZE + SKIRT * 2))
    {
        if ((xx >= 0) && (yy >= 0) && (xx < (int)gRectSize.x) && (yy < (int)gRectSize.y))
        {
            inSpecularIllumination = gSpecularIllumination[int2(xx, yy) + gRectOrigin];
            normalRoughness = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[int2(xx, yy) + gRectOrigin]);
        }
        sharedInSpecular[oy][ox] = inSpecularIllumination;
        sharedNormalRoughness[oy][ox] = normalRoughness;
    }

    // Ensuring all the writes to shared memory are done by now
    GroupMemoryBarrierWithGroupSync();

    uint2 sharedMemoryIndex = groupThreadId.xy + int2(SKIRT, SKIRT);

    // Center data
    float3 diffuseIllumination = gDiffuseIllumination[ipos.xy + gRectOrigin].rgb;
    float4 specularIllumination = sharedInSpecular[sharedMemoryIndex.y][sharedMemoryIndex.x];

    // Reading current GBuffer data and center/left/right viewZ
    float4 currentNormalRoughness = sharedNormalRoughness[sharedMemoryIndex.y][sharedMemoryIndex.x];
    float3 currentNormal = currentNormalRoughness.xyz;
    float currentRoughness = currentNormalRoughness.w;
    float currentLinearZ = gViewZ[ipos.xy + gRectOrigin];

    specularIllumination.a = max(0.001, min(gDenoisingRange, specularIllumination.a));

    // Early out if linearZ is beyond denoising range
    [branch]
    if (currentLinearZ > gDenoisingRange)
    {
        return;
    }

    // Calculating average normal, curvature, min hit dist and specular moments in 3x3 and 5x5 areas around current pixel
    float3 currentNormalAveraged = currentNormal;
    float4 specM1 = specularIllumination;
    float4 specM2 = specM1 * specM1;
    float curvature = 0;
    float minHitDist3x3 = gDenoisingRange;
    float minHitDist5x5 = gDenoisingRange;

    [unroll]
    for (int i = -2; i <= 2; i++)
    {
        [unroll]
        for (int j = -2; j <= 2; j++)
        {
            // Skipping center pixel
            if ((i == 0) && (j == 0)) continue;

            int2 p = ipos + int2(i, j);

            float4 spec = sharedInSpecular[sharedMemoryIndex.y + j][sharedMemoryIndex.x + i];
            specM1 += spec;
            specM2 += spec * spec;
            minHitDist5x5 = min(spec.w, minHitDist5x5);
            if ((abs(i) <= 1) && (abs(j) <= 1))
            {
                minHitDist3x3 = min(spec.w, minHitDist3x3);
            }
        }
    }
    specM1 /= 25.0;
    specM2 /= 25.0;
    float4 specSigma = GetStdDev(specM1, specM2);

    [unroll]
    for (int k = -1; k <= 1; k++)
    {
        [unroll]
        for (int l = -1; l <= 1; l++)
        {
            // Skipping center pixel
            if ((k == 0) && (l == 0)) continue;

            int2 p = ipos + int2(k, l);
            float3 pNormal = sharedNormalRoughness[sharedMemoryIndex.y + k][sharedMemoryIndex.x + l].xyz;
            currentNormalAveraged += pNormal;
            curvature += length(pNormal - currentNormal) * rsqrt(STL::Math::LengthSquared(float2(k, l)));
        }
    }
    currentNormalAveraged /= 9.0;
    curvature /= 8.0;

    // Only needed to mitigate banding
    curvature = STL::Math::LinearStep(NRD_ENCODING_ERRORS.y, 1.0, curvature);

    // Calculating modified roughness that takes normal variation in account
    float currentRoughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance(currentRoughness, currentNormalAveraged);

    // Computing 2nd moments of luminance
    float specular1stMoment = STL::Color::Luminance(specularIllumination.rgb);
    float specular2ndMoment = specular1stMoment * specular1stMoment;

    float diffuse1stMoment = STL::Color::Luminance(diffuseIllumination.rgb);
    float diffuse2ndMoment = diffuse1stMoment * diffuse1stMoment;

    // Getting current frame worldspace position and view vector for current pixel
    float3 currentWorldPos = GetCurrentWorldPos(ipos, currentLinearZ);
    float3 currentViewVector = (gIsOrtho == 0) ?
        currentWorldPos :
        currentLinearZ * normalize(gFrustumForward.xyz);

    // Reading motion vector
    float3 motionVector = gMotion[gRectOrigin + ipos].xyz * gMotionVectorScale.xyy;

    // Loading previous data based on surface motion vectors
    float4 prevSpecularIlluminationAnd2ndMomentSMB;
    float4 prevDiffuseIlluminationAnd2ndMomentSMB;
    float3 prevSpecularIlluminationAnd2ndMomentSMBResponsive;
    float3 prevDiffuseIlluminationAnd2ndMomentSMBResponsive;
    float  prevReflectionHitTSMB;
    float3 prevWorldPosSMB;
    float3 prevNormalSMB;
    float2 historyLength;
    float2 prevUVSMB;
    float footprintQuality;

    float surfaceMotionBasedReprojectionFound = loadSurfaceMotionBasedPrevData(
        ipos,
        currentWorldPos,
        normalize(currentNormalAveraged),
        currentLinearZ,
        specularIllumination.a,
        motionVector,
        prevSpecularIlluminationAnd2ndMomentSMB,
        prevDiffuseIlluminationAnd2ndMomentSMB,
        prevSpecularIlluminationAnd2ndMomentSMBResponsive,
        prevDiffuseIlluminationAnd2ndMomentSMBResponsive,
        prevWorldPosSMB,
        prevNormalSMB,
        prevReflectionHitTSMB,
        historyLength,
        prevUVSMB,
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

    float diffuseAlpha = (surfaceMotionBasedReprojectionFound > 0) ? max(1.0 / (diffMaxAccumulatedFrameNum + 1.0), 1.0 / historyLength.y) : 1.0;
    float diffuseAlphaResponsive = (surfaceMotionBasedReprojectionFound > 0) ? max(1.0 / (diffMaxFastAccumulatedFrameNum + 1.0), 1.0 / historyLength.y) : 1.0;

    if ((!diffHasData) && (historyLength.y > 1.0))
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

    gOutSpecularAndDiffuseHistoryLength[ipos] = historyLength / 255.0;

    // SPECULAR ACCUMULATION BELOW
    //
    float specMaxAccumulatedFrameNum = gSpecularMaxAccumulatedFrameNum;
    float specMaxFastAccumulatedFrameNum = gSpecularMaxFastAccumulatedFrameNum;
    if (gUseConfidenceInputs)
    {
        float inSpecConfidence = gSpecConfidence[ipos];
        specMaxAccumulatedFrameNum *= inSpecConfidence;
        specMaxFastAccumulatedFrameNum *= inSpecConfidence;
    }

    float specHistoryFrames = min(specMaxAccumulatedFrameNum, historyLength.x);
    float specHistoryResponsiveFrames = min(specMaxFastAccumulatedFrameNum, historyLength.x);

    // Calculating surface parallax
    float parallax = ComputeParallax(currentWorldPos, currentWorldPos + motionVector * (gIsWorldSpaceMotion != 0 ? 1.0 : 0.0), gPrevCameraPositionAndJitterDelta.xyz);
    float parallaxOrig = parallax;
    float hitDistToSurfaceRatio = saturate(prevReflectionHitTSMB / currentLinearZ);
    parallax *= hitDistToSurfaceRatio;
    float parallaxInPixels = GetParallaxInPixels(max(0.01, parallaxOrig));

    // Params required for surface motion based (SMB) specular reprojection
    float3 V = normalize(-currentViewVector);
    float NoV = saturate(dot(currentNormal, V));

    // Current specular signal ( surface motion )
    float specSurfaceFrames = GetSpecAccumSpeed(specHistoryFrames, max(0.05, currentRoughnessModified), NoV, parallax);
    float specSurfaceResponsiveFrames = GetSpecAccumSpeed(specHistoryResponsiveFrames, max(0.05, currentRoughnessModified), NoV, parallax);

    float specSurfaceAlpha = saturate(1.0 / specSurfaceFrames);
    float specSurfaceResponsiveAlpha = saturate(1.0 / specSurfaceResponsiveFrames);

    float surfaceHistoryConfidence = saturate(specSurfaceFrames / (specHistoryFrames + 1.0));

    if (!specHasData)
    {
        // Adjusting surface motion based specular accumulation weights for checkerboard
        if ((gIsOrtho == 0) || ((gIsOrtho != 0) && (gIsCameraStatic != 0)))
        {
            specSurfaceAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * (surfaceMotionBasedReprojectionFound > 0 ? 1.0 : 0.0);
            specSurfaceResponsiveAlpha *= 1.0 - gCheckerboardResolveAccumSpeed * (surfaceMotionBasedReprojectionFound > 0 ? 1.0 : 0.0);
        }
    }

    float4 accumulatedSpecularSMB;
    accumulatedSpecularSMB.rgb = lerp(prevSpecularIlluminationAnd2ndMomentSMB.rgb, specularIllumination.rgb, specSurfaceAlpha);
    accumulatedSpecularSMB.w = lerp(prevReflectionHitTSMB, specularIllumination.w, max(specSurfaceAlpha, RELAX_HIT_DIST_MIN_ACCUM_SPEED(currentRoughnessModified)));

    float3 accumulatedSpecularSMBResponsive = lerp(prevSpecularIlluminationAnd2ndMomentSMBResponsive, specularIllumination.xyz, specSurfaceResponsiveAlpha);
    float accumulatedSpecularM2SMB = lerp(prevSpecularIlluminationAnd2ndMomentSMB.a, specular2ndMoment, specSurfaceAlpha);

    // Thin lens equation for adjusting reflection HitT
    float pixelSize = PixelRadiusToWorld(gUnproject, gIsOrtho, 1.0, currentLinearZ);
    curvature *= NoV / pixelSize;
    float hitDist = specularIllumination.a;
    hitDist = lerp(minHitDist3x3, minHitDist5x5, STL::Math::SmoothStep(0.04, 0.08, currentRoughnessModified));
    hitDist = clamp(hitDist, specM1.a - specSigma.a * 3.0, specM1.a + specSigma.a * 3.0);
    float divider = 0.5 + curvature * hitDist;
    float hitDistFocused = max(0.001, 0.5 * hitDist / (divider == 0 ? 1.0 : divider));

    // Limiting hitDist in ortho case to avoid extreme amounts of motion in reflection
    if(gIsOrtho != 0) hitDistFocused = min(currentLinearZ, hitDistFocused);

    // Loading specular data based on virtual motion
    float4 prevSpecularIlluminationAnd2ndMomentVMB;
    float3 prevSpecularIlluminationAnd2ndMomentVMBResponsive;
    float3 prevNormalVMB;
    float  prevRoughnessVMB;
    float  prevReflectionHitTVMB;
    float2 prevUVVMB;

    float virtualHistoryConfidence = loadVirtualMotionBasedPrevData(
        ipos,
        currentWorldPos,
        currentNormal,
        currentLinearZ,
        hitDistFocused,
        currentViewVector,
        prevWorldPosSMB,
        surfaceMotionBasedReprojectionFound == 2.0 ? true : false,
        prevSpecularIlluminationAnd2ndMomentVMB,
        prevSpecularIlluminationAnd2ndMomentVMBResponsive,
        prevNormalVMB,
        prevRoughnessVMB,
        prevReflectionHitTVMB,
        prevUVVMB
    );

    // Normal weight for virtual history
    float fresnelFactor = STL::BRDF::Pow5(NoV);
    float normalWeightRenorm = lerp(0.9, 1.0, STL::Math::LinearStep(0.0, 0.15, currentRoughnessModified)); // mitigate imprecision problems introduced by normals encoded with different precision (test #6 and #12)
    float virtualLobeScale = lerp(0.5, 1.0, fresnelFactor);
    float specNormalParams = STL::ImportanceSampling::GetSpecularLobeHalfAngle(currentRoughnessModified);
    specNormalParams *= virtualLobeScale;
    specNormalParams = 1.0 / max(specNormalParams, STL::Math::DegToRad(1.5));
    float virtualNormalWeight = GetNormalWeight(specNormalParams, currentNormal, prevNormalVMB);

    virtualHistoryConfidence *= lerp(virtualNormalWeight, 1.0, saturate(fresnelFactor * parallax));

#if( RELAX_USE_BICUBIC_FOR_VIRTUAL_MOTION_SPECULAR == 0 )
    // Artificially decreasing virtual motion confidence by 50% if there is no parallax to avoid overblurring
    // due to bilinear filtering used for virtual motion based reprojection,
    // this will put more weight to surface based reprojection which uses higher order filter
    float noParallaxSharpener = 0.5 + 0.5 * STL::Math::LinearStep(0.5, 1.0, parallaxInPixels);
    virtualHistoryConfidence *= noParallaxSharpener;
#endif

    // Amount of virtual motion - dominant factor
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection(currentNormal, V, currentRoughness, RELAX_SPEC_DOMINANT_DIRECTION);
    float virtualHistoryAmount = virtualHistoryConfidence * D.w;

    // Virtual history confidence & amount - roughness
    float2 roughnessParams = GetRoughnessWeightParams(currentRoughness);
    float rw = GetRoughnessWeight(roughnessParams, prevRoughnessVMB);
    float virtualRoughnessWeight = 0.75 + 0.25 * rw;
    virtualHistoryConfidence *= virtualRoughnessWeight;
    virtualHistoryAmount *= (gIsOrtho == 0) ? rw * 0.9 + 0.1 : 1.0;

    // Decreasing virtual history amount for ortho case
    virtualHistoryAmount *= (gIsOrtho == 0) ? 1.0 : 0.5;

    // Virtual history confidence - hit distance
    float maxDist = max(prevReflectionHitTVMB, prevReflectionHitTSMB);
    float dHitT = max(0, sqrt(abs(prevReflectionHitTVMB - prevReflectionHitTSMB)) - 0.0 * specSigma.a);
    float virtualHistoryHitDistConfidence = 1.0 - saturate(10.0 * dHitT / (currentLinearZ + maxDist));
    virtualHistoryConfidence *= virtualHistoryHitDistConfidence;

    // "Looking back" 1 and 2 frames and applying normal weight to decrease lags
    float2 uvDiff = prevUVVMB - prevUVSMB;
    float2 backUV1 = prevUVVMB + 1.0 * uvDiff;
    float2 backUV2 = prevUVVMB + 2.0 * uvDiff;
    backUV1 *= (gInvResourceSize * gRectSizePrev); // Taking in account resolution scale
    backUV2 *= (gInvResourceSize * gRectSizePrev);
    float3 backNormal1 = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, backUV1, 0)).rgb;
    float3 backNormal2 = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, backUV2, 0)).rgb;
    float backNormalWeight1 = GetNormalWeight(specNormalParams, currentNormal, backNormal1);
    float backNormalWeight2 = GetNormalWeight(specNormalParams, currentNormal, backNormal2);
    float backNormalWeight = backNormalWeight1 * backNormalWeight2;
    virtualHistoryConfidence *= backNormalWeight;

    // Clamping specular virtual history to current specular signal
    float4 specHistoryVirtual = float4(prevSpecularIlluminationAnd2ndMomentVMB.rgb, prevReflectionHitTVMB);
    
    if(gVirtualHistoryClampingEnabled != 0)
    {
        float SMC = STL::Math::SmoothStep(0.15, 0.25, currentRoughnessModified);
        float sigmaScale = lerp(2.0, 6.0, SMC);
        sigmaScale *= 1.0 + 7.0 * SMC * max(gFramerateScale, 1.0); // Looks aggressive, but it will be balanced by virtualUnclampedAmount
        float4 specHistoryVirtualClamped = STL::Color::Clamp(specM1, specSigma * sigmaScale, specHistoryVirtual);
        float3 specHistoryVirtualResponsiveClamped = STL::Color::Clamp(specM1, specSigma * sigmaScale, prevSpecularIlluminationAnd2ndMomentVMBResponsive.rgbb).rgb;

        float virtualUnclampedAmount = GetSpecMagicCurve(currentRoughnessModified) * virtualHistoryConfidence;
        virtualUnclampedAmount += gFramerateScale * (0.2 + 0.33 * GetSpecMagicCurve(currentRoughnessModified) * (1.0 - virtualHistoryHitDistConfidence));
        virtualUnclampedAmount = saturate(virtualUnclampedAmount);

        specHistoryVirtual = lerp(specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount);
        specHistoryVirtual.a = specHistoryVirtualClamped.a;
        prevSpecularIlluminationAnd2ndMomentVMBResponsive = lerp(specHistoryVirtualResponsiveClamped, prevSpecularIlluminationAnd2ndMomentVMBResponsive, virtualUnclampedAmount);

        // Clamping 2nd moment too
        float specM2VirtualClamped = STL::Color::Clamp(specular2ndMoment, max(max(specSigma.r, specSigma.g), specSigma.b) * sigmaScale * 2.0, prevSpecularIlluminationAnd2ndMomentVMB.a);
        prevSpecularIlluminationAnd2ndMomentVMB.a = lerp(specM2VirtualClamped, prevSpecularIlluminationAnd2ndMomentVMB.a, virtualUnclampedAmount);
    }
    
    // Current specular signal ( virtual motion )
    float specVirtualFrames = specHistoryFrames * virtualRoughnessWeight * (0.1 + 0.9 * backNormalWeight);
    float specVirtualResponsiveFrames = specHistoryResponsiveFrames * virtualRoughnessWeight * (0.0 + 1.0 * backNormalWeight);

    // Artificially decreasing virtual history frames if FPS is lower than 60 and virtual confidence is low, to decrease lags
    float fpsScaler = lerp(max(0.25, min(gFramerateScale * gFramerateScale, 1.0)), 1.0, virtualHistoryConfidence);
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
    gOutSpecularIllumination[ipos] = float4(accumulatedSpecularIllumination, accumulatedSpecular2ndMoment);
    gOutSpecularIlluminationResponsive[ipos] = float4(accumulatedSpecularIlluminationResponsive, 0);

    gOutReflectionHitT[ipos] = accumulatedReflectionHitT;
    gOutSpecularReprojectionConfidence[ipos] = specularHistoryConfidence;
}
