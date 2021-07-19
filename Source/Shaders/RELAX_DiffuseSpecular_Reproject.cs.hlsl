/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsl"
#include "STL.hlsl"
#include "RELAX_DiffuseSpecular_Reproject.resources.hlsl"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS
#include "RELAX_Common.hlsl"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

// Helper functions

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
    float powerScale = 1.0 + parallax * parallaxSensitivity;
    float accumSpeed = GetSpecAccumulatedFrameNum(roughness, powerScale);
    accumSpeed = min(accumSpeed, maxAccumSpeed);
    return accumSpeed;
}

float GetNormalWeightParams(float viewZ, float roughness, float edge, float nonLinearAccumSpeed)
{
    float r = PixelRadiusToWorld(gUnproject, gIsOrtho, 1.0 / gInvViewSize.y, viewZ);
    float a = 1.0 / (r + 1.0); // estimate normalized angular size
    float b = lerp(0.15, 0.02, a); // % of lobe angle
    float f = max(nonLinearAccumSpeed, 0.5 * edge); // less strict on edges
    float angle = STL::ImportanceSampling::GetSpecularLobeHalfAngle(roughness);
    angle *= lerp(b, 1.0, f);
    angle += RELAX_NORMAL_BANDING_FIX;
    return rcp(angle);
}

float GetNormalWeight(float params0, float3 n0, float3 n)
{
    float cosa = saturate(dot(n0, n));
    float angle = STL::Math::AcosApprox(cosa);
    return _ComputeWeight(float2(params0, -0.001), angle);
}

float2 GetRoughnessWeightParams(float roughness0)
{
    float a = rcp(roughness0 * 0.2 * 0.99 + 0.01);
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

    // TODO: add ortho projection support (see ComputeParallax)
    float parallaxInPixels = parallax / (RELAX_PARALLAX_NORMALIZATION * gUnproject);

    return parallaxInPixels;
}

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
    float2 uv = ((float2)pixelPos + float2(0.5, 0.5)) * gInvRectSize * 2.0 - 1.0;
    return depth * (gFrustumForward.xyz + gFrustumRight.xyz * uv.x - gFrustumUp.xyz * uv.y);
}

float3 getPreviousWorldPos(int2 pixelPos, float depth)
{
    float2 uv = ((float2)pixelPos + float2(0.5, 0.5)) * (1.0 / gRectSizePrev) * 2.0 - 1.0;
    return depth * (gPrevFrustumForward.xyz + gPrevFrustumRight.xyz * uv.x - gPrevFrustumUp.xyz * uv.y);
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
    return GetPlaneDistanceWeight(maxPlaneDistance, currentLinearZ, gDisocclusionThreshold + jitterRadius * 2.0) > 1.0 ? 0.0 : 1.0;
}


// Returns reprojection search result based on surface motion:
// 1 - reprojection found
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
    out float3 prevSpecularIllum,
    out float3 prevDiffuseIllum,
    out float3 prevSpecularResponsiveIllum,
    out float3 prevDiffuseResponsiveIllum,
    out float2 prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments,
    out float3 prevWorldPos,
    out float  prevReflectionHitT,
    out float2 historyLength)
{
    // Setting default values for output
    prevSpecularIllum = 0;
    prevDiffuseIllum = 0;
    prevSpecularResponsiveIllum = 0;
    prevDiffuseResponsiveIllum = 0;
    prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments = 0;
    prevWorldPos = currentWorldPos;
    prevReflectionHitT = currentReflectionHitT;
    historyLength = 0;

    // Calculating jitter margin radius in world space
    float jitterRadius = getJitterRadius(gJitterDelta, currentLinearZ);

    // Calculating previous pixel position and UV
    float2 pixelUV = (pixelPosOnScreen + 0.5) * gInvRectSize;
    float2 prevPixelUV = STL::Geometry::GetPrevUvFromMotion(pixelUV, currentWorldPos, gPrevWorldToClip, motionVector, gWorldSpaceMotion);
    float2 prevPixelPosOnScreen = prevPixelUV * gRectSizePrev;

    // Calculating footprint origin and weights
    int2 bilinearOrigin = int2(floor(prevPixelPosOnScreen - 0.5) + 0.5);
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
    float prevRoughnessDontCare;
    float prevDepthInTap;
    float3 prevWorldPosInTap;
    int2 tapPos;
    float reprojectionTapValid;
    float3 prevWorldPos00, prevWorldPos10, prevWorldPos01, prevWorldPos11;

    // Adjusting worldspace position for disocclusion fix:
    // Applying worldspace motion first,
    motionVector *= gWorldSpaceMotion > 0 ? 1.0 : 0.0;

    // Then taking care of camera motion, because world space is always centered at camera position in NRD
    currentWorldPos += motionVector - gPrevCameraPosition;

    // 1st row
    tapPos = bilinearOrigin + int2(0, -1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, -1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // 2nd row
    tapPos = bilinearOrigin + int2(-1, 0);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 0);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.x = reprojectionTapValid ? 1.0 : 0.0;
    prevWorldPos00 = prevWorldPosInTap;

    tapPos = bilinearOrigin + int2(1, 0);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.y = reprojectionTapValid ? 1.0 : 0.0;
    prevWorldPos10 = prevWorldPosInTap;

    tapPos = bilinearOrigin + int2(2, 0);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // 3rd row
    tapPos = bilinearOrigin + int2(-1, 1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.z = reprojectionTapValid ? 1.0 : 0.0;
    prevWorldPos01 = prevWorldPosInTap;

    tapPos = bilinearOrigin + int2(1, 1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.w = reprojectionTapValid ? 1.0 : 0.0;
    prevWorldPos11 = prevWorldPosInTap;

    tapPos = bilinearOrigin + int2(2, 1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // 4th row
    tapPos = bilinearOrigin + int2(0, 2);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 2);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // Calculating interpolated binary weight for bilinear taps in advance
    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;
    float interpolatedBinaryWeight = STL::Filtering::ApplyBilinearFilter(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w, bilinear);
    interpolatedBinaryWeight = max(1e-6, interpolatedBinaryWeight);

    // Applying reprojection filters
    float reprojectionFound = 0;

    if (any(bilinearTapsValid))
    {
        // Trying to apply bicubic filter first
        if (gUseBicubic && (bicubicFootprintValid > 0))
        {
            // Bicubic for illumination and 2nd moments
            prevSpecularIllum =
                max(0, BicubicFloat4(gPrevSpecularIlluminationUnpacked, gLinearClamp, prevPixelPosOnScreen, gInvViewSize).rgb);
            prevDiffuseIllum =
                max(0, BicubicFloat4(gPrevDiffuseIlluminationUnpacked, gLinearClamp, prevPixelPosOnScreen, gInvViewSize).rgb);
            prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments =
                max(0, BicubicFloat2(gPrevSpecularAndDiffuse2ndMoments, gLinearClamp, prevPixelPosOnScreen, gInvViewSize));
        }
        else
        {
            // If no success with the bicubic, then do weighted bilinear
            prevSpecularIllum =
                BilinearWithBinaryWeightsFloat4(gPrevSpecularIlluminationUnpacked, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
            prevDiffuseIllum =
                BilinearWithBinaryWeightsFloat4(gPrevDiffuseIlluminationUnpacked, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
            prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments =
                max(0, BilinearWithBinaryWeightsFloat2(gPrevSpecularAndDiffuse2ndMoments, gLinearClamp, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight));
        }

        // Always weighted Bilinear for responsive illumination
        BilinearWithBinaryWeightsLogLuvX2(prevSpecularResponsiveIllum, prevDiffuseResponsiveIllum, gPrevSpecularAndDiffuseIlluminationResponsiveLogLuv, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        // Calculating previousd worldspace position
        // by applying weighted bilinear to worldspace positions in taps.
        // Also calculating history length by using weighted bilinear
        prevWorldPos =
            BilinearWithBinaryWeightsImmediateFloat3(
                prevWorldPos00, prevWorldPos10, prevWorldPos01, prevWorldPos11,
                bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        historyLength = 255.0 * BilinearWithBinaryWeightsFloat2(gPrevSpecularAndDiffuseHistoryLength, gLinearClamp, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        prevReflectionHitT = BilinearWithBinaryWeightsFloat(gPrevReflectionHitT, gLinearClamp, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        prevReflectionHitT = max(0.001, prevReflectionHitT);
        reprojectionFound = 1.0;
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
    out float3 prevSpecularIllum,
    out float3 prevSpecularResponsiveIllum,
    out float  prevSpecular2ndMoment,
    out float3 prevNormal,
    out float prevRoughness,
    out float prevReflectionHitT)
{
    // Setting default values for output
    prevSpecularIllum = 0;
    prevSpecularResponsiveIllum = 0;
    prevSpecular2ndMoment = 0;
    prevNormal = currentNormal;
    prevRoughness = 0;
    prevReflectionHitT = currentReflectionHitT;

    // Taking care of camera motion, because world space is always centered at camera position in NRD
    prevWorldPos += gPrevCameraPosition;
    currentWorldPos -= gPrevCameraPosition;


    // Calculating previous worldspace virtual position based on reflection hitT
    float3 virtualViewVector = normalize(currentViewVector) * currentReflectionHitT;
    float3 prevVirtualWorldPos = prevWorldPos + virtualViewVector;

    float currentViewVectorLength = length(currentViewVector);
    float currentVirtualZ = currentViewVectorLength + currentReflectionHitT;

    float4 prevVirtualClipPos = mul(gPrevWorldToClip, float4(prevVirtualWorldPos, 1.0));
    prevVirtualClipPos.xy /= prevVirtualClipPos.w;
    float2 prevVirtualUV = prevVirtualClipPos.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    float2 prevVirtualPixelPosOnScreen = prevVirtualUV * gRectSizePrev;

    float jitterRadius = getJitterRadius(gJitterDelta, currentVirtualZ);

    // Calculating footprint origin and weights
    int2 bilinearOrigin = int2(floor(prevVirtualPixelPosOnScreen - 0.5) + 0.5);
    float2 bilinearWeights = frac(prevVirtualPixelPosOnScreen - 0.5);

    // Checking bilinear footprint
    float3 prevNormal00, prevNormal10, prevNormal01, prevNormal11;
    float prevRoughness00, prevRoughness10, prevRoughness01, prevRoughness11;
    uint2 tapPos;
    float prevLinearZInTap;
    float3 prevWorldPosInTap;
    float4 bilinearTapsValid;

    tapPos = bilinearOrigin + int2(0, 0);
    UnpackNormalRoughnessDepth(prevNormal00, prevRoughness00, prevLinearZInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevLinearZInTap);
    bilinearTapsValid.x = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormal00, jitterRadius);

    tapPos = bilinearOrigin + int2(1, 0);
    UnpackNormalRoughnessDepth(prevNormal10, prevRoughness10, prevLinearZInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevLinearZInTap);
    bilinearTapsValid.y = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormal10, jitterRadius);

    tapPos = bilinearOrigin + int2(0, 1);
    UnpackNormalRoughnessDepth(prevNormal01, prevRoughness01, prevLinearZInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevLinearZInTap);
    bilinearTapsValid.z = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormal01, jitterRadius);

    tapPos = bilinearOrigin + int2(1, 1);
    UnpackNormalRoughnessDepth(prevNormal11, prevRoughness11, prevLinearZInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevLinearZInTap);
    bilinearTapsValid.w = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormal11, jitterRadius);

    // Calculating interpolated binary weight for bilinear taps in advance
    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;
    float interpolatedBinaryWeight = STL::Filtering::ApplyBilinearFilter(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w, bilinear);
    interpolatedBinaryWeight = max(1e-6, interpolatedBinaryWeight);

    //Applying reprojection
    float reprojectionFound = 0;

    // Weighted bilinear for prev specular data based on virtual motion
    if (any(bilinearTapsValid))
    {
        prevSpecularIllum = BilinearWithBinaryWeightsFloat4(gPrevSpecularIlluminationUnpacked, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
        float3 diffuseIllumDontCare = 0;
        BilinearWithBinaryWeightsLogLuvX2(prevSpecularResponsiveIllum, diffuseIllumDontCare, gPrevSpecularAndDiffuseIlluminationResponsiveLogLuv, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        prevSpecular2ndMoment = max(0, BilinearWithBinaryWeightsFloat2(gPrevSpecularAndDiffuse2ndMoments, gLinearClamp, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight)).r;
        prevNormal = BilinearWithBinaryWeightsImmediateFloat3(
            prevNormal00, prevNormal10, prevNormal01, prevNormal11,
            bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        prevRoughness = BilinearWithBinaryWeightsImmediateFloat(
            prevRoughness00, prevRoughness10, prevRoughness01, prevRoughness11,
            bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        prevReflectionHitT = BilinearWithBinaryWeightsFloat(gPrevReflectionHitT, gLinearClamp, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        prevReflectionHitT = max(0.001, prevReflectionHitT);
        reprojectionFound = 1.0;
    }
    return reprojectionFound;
}

//
// Main
//

[numthreads(8, 8, 1)]
NRD_EXPORT void NRD_CS_MAIN(int2 ipos : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    // Checkerboard
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

    // Center data
    float3 diffuseIllumination = gDiffuseIllumination[gRectOrigin + uint2(checkerboardPixelPos.x, ipos.y)].rgb;
    float4 specularIllumination = gSpecularIllumination[gRectOrigin + uint2(checkerboardPixelPos.y, ipos.y)];


    // Reading current GBuffer data and center/left/right viewZ
    float3 currentNormal;
    float currentRoughness;
    float currentLinearZ;
    UnpackNormalRoughnessDepth(currentNormal, currentRoughness, currentLinearZ, gNormalRoughnessDepth[gRectOrigin + ipos]);

    specularIllumination.a = max(0.001, min(gDenoisingRange, specularIllumination.a));

    // Early out if linearZ is beyond denoising range
    [branch]
    if (currentLinearZ > gDenoisingRange)
    {
        return;
    }

    // Reading left/right viewZ
    if ((gSpecCheckerboard) != 2 || (gDiffCheckerboard != 2))
    {
        float3 normalDontCare;
        float roughnessDontCare;
        float linearZ0;
        float linearZ1;
        UnpackNormalRoughnessDepth(normalDontCare, roughnessDontCare, linearZ0, gNormalRoughnessDepth[gRectOrigin + ipos + int2(-1, 0)]);
        UnpackNormalRoughnessDepth(normalDontCare, roughnessDontCare, linearZ1, gNormalRoughnessDepth[gRectOrigin + ipos + int2(1, 0)]);

        float2 w = GetBilateralWeight(float2(linearZ0, linearZ1), currentLinearZ);
        w *= STL::Math::PositiveRcp(w.x + w.y);

        int3 checkerboardPos = ipos.xyx + int3(-1, 0, 1);
        checkerboardPos.xz >>= 1;
        checkerboardPos += gRectOrigin.xyx;

        float3 d0 = gDiffuseIllumination[checkerboardPos.xy].rgb;
        float3 d1 = gDiffuseIllumination[checkerboardPos.zy].rgb;
        if (!diffHasData)
        {
            diffuseIllumination *= saturate(1.0 - w.x - w.y);
            diffuseIllumination += d0 * w.x + d1 * w.y;
        }

        float4 s0 = gSpecularIllumination[checkerboardPos.xy];
        float4 s1 = gSpecularIllumination[checkerboardPos.zy];
        if (!specHasData)
        {
            specularIllumination *= saturate(1.0 - w.x - w.y);
            specularIllumination += s0 * w.x + s1 * w.y;
        }
    }

    // Reading motion vector
    float3 motionVector = gMotion[gRectOrigin + ipos].xyz * gMotionVectorScale.xyy;

    // Calculating average normal and specular moments in 3x3 area around current pixel
    float3 averageNormal = currentNormal;
    float sumW = 1.0;
    float4 specM1 = specularIllumination;
    float4 specM2 = specM1 * specM1;
    for (int i = -1; i <= 1; i++)
    {
        for (int j = -1; j <= 1; j++)
        {
            // Skipping center pixel
            if ((i == 0) && (j == 0)) continue;

            int2 p = ipos + int2(i, j);
            float3 pNormal;
            float pRoughness;
            float pZ;
            UnpackNormalRoughnessDepth(pNormal, pRoughness, pZ, gNormalRoughnessDepth[gRectOrigin + p]);

            float w = GetBilateralWeight(abs(currentLinearZ), abs(pZ));

            int2 pSpec = int2(checkerboardPixelPos.y, ipos.y) + int2(i, j);

            float4 spec = gSpecularIllumination[gRectOrigin + pSpec];
            specM1 += spec * w;
            specM2 += spec * spec * w;
            averageNormal += pNormal * w;
            sumW += w;
        }
    }
    float invSumW = 1.0 / sumW;
    averageNormal *= invSumW;
    specM1 *= invSumW;
    specM2 *= invSumW;
    float4 specSigma = sqrt(max(0, specM2 - specM1 * specM1));

    // Calculating modified roughness that takes normal variation in account
    float currentRoughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance(currentRoughness, averageNormal);

    // Computing 2nd moments of luminance
    float specular1stMoment = STL::Color::Luminance(specularIllumination.rgb);
    float specular2ndMoment = specular1stMoment * specular1stMoment;

    float diffuse1stMoment = STL::Color::Luminance(diffuseIllumination.rgb);
    float diffuse2ndMoment = diffuse1stMoment * diffuse1stMoment;

    // Getting current frame worldspace position and view vector for current pixel
    float3 currentWorldPos = getCurrentWorldPos(ipos, currentLinearZ);
    float3 currentViewVector = currentWorldPos;

    // Loading previous data based on surface motion vectors
    float3 prevSurfaceMotionBasedSpecularIllumination;
    float3 prevDiffuseIllumination;
    float3 prevSurfaceMotionBasedSpecularResponsiveIllumination;
    float3 prevDiffuseResponsiveIllumination;
    float2 prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments;
    float  prevSurfaceMotionBasedReflectionHitT;
    float3 prevSurfaceMotionBasedWorldPos;
    float2 historyLength;
    float3 debugOut1;

    float surfaceMotionBasedReprojectionFound = loadSurfaceMotionBasedPrevData(ipos,
        currentWorldPos,
        currentNormal,
        currentLinearZ,
        specularIllumination.a,
        motionVector,
        prevSurfaceMotionBasedSpecularIllumination,
        prevDiffuseIllumination,
        prevSurfaceMotionBasedSpecularResponsiveIllumination,
        prevDiffuseResponsiveIllumination,
        prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments,
        prevSurfaceMotionBasedWorldPos,
        prevSurfaceMotionBasedReflectionHitT,
        historyLength
    );

    // History length is based on surface motion based disocclusion
    historyLength = ceil(historyLength) + 1.0;
    historyLength = min(100.0, historyLength);

    // Handling history reset if needed
    if (gFrameIndex == 0) historyLength = 1.0;

    // Temporal accumulation of reflection HitT will happen later in the shader
    float accumulatedReflectionHitT = prevSurfaceMotionBasedReflectionHitT;

    // Loading specular data based on virtual motion
    float3 prevVirtualMotionBasedSpecularIllumination;
    float3 prevVirtualMotionBasedSpecularResponsiveIllumination;
    float  prevVirtualMotionBasedSpecular2ndMoment;
    float3 prevVirtualMotionBasedNormal;
    float  prevVirtualMotionBasedRoughness;
    float  prevVirtualMotionBasedReflectionHitT;

    float virtualHistoryConfidence = loadVirtualMotionBasedPrevData(ipos,
        currentWorldPos,
        currentNormal,
        currentLinearZ,
        accumulatedReflectionHitT,
        currentViewVector,
        prevSurfaceMotionBasedWorldPos,
        prevVirtualMotionBasedSpecularIllumination,
        prevVirtualMotionBasedSpecularResponsiveIllumination,
        prevVirtualMotionBasedSpecular2ndMoment,
        prevVirtualMotionBasedNormal,
        prevVirtualMotionBasedRoughness,
        prevVirtualMotionBasedReflectionHitT);

    float specHistoryFrames = min(gSpecularMaxAccumulatedFrameNum, historyLength.x);
    float specHistoryResponsiveFrames = min(gSpecularMaxFastAccumulatedFrameNum, historyLength.x);
    float2 roughnessParams = GetRoughnessWeightParams(currentRoughness);

    float4 specHistoryVirtual = float4(prevVirtualMotionBasedSpecularIllumination, prevVirtualMotionBasedReflectionHitT);
    float4 specHistorySurface = float4(prevSurfaceMotionBasedSpecularIllumination, prevSurfaceMotionBasedReflectionHitT);

    float3 V = normalize(-currentViewVector);
    float NoV = saturate(dot(currentNormal, V));
    float specNormalParams = GetNormalWeightParams(currentLinearZ, currentRoughnessModified, 0.0, 1.0);
    float virtualNormalWeight = GetNormalWeight(specNormalParams, currentNormal, prevVirtualMotionBasedNormal);

    float parallax = ComputeParallax(currentWorldPos, currentWorldPos + motionVector * (gWorldSpaceMotion != 0 ? 1.0 : 0.0), gPrevCameraPosition.xyz);

    // Current specular signal ( surface motion )
    float specSurfaceFrames = gRoughnessBasedSpecularAccumulation ? GetSpecAccumSpeed(specHistoryFrames, currentRoughnessModified, NoV, parallax) : specHistoryFrames;
    float specSurfaceAlpha = 1.0 / (specSurfaceFrames + 1.0);

    float specSurfaceResponsiveFrames = gRoughnessBasedSpecularAccumulation ? GetSpecAccumSpeed(specHistoryResponsiveFrames, currentRoughnessModified, NoV, parallax) : specHistoryResponsiveFrames;
    float specSurfaceResponsiveAlpha = 1.0 / (specSurfaceResponsiveFrames + 1.0);

    float surfaceHistoryConfidence = specSurfaceFrames / (specHistoryFrames + 1.0);

    float hitDist = specHistorySurface.w;
    float parallaxOrig = parallax;
    float hitDistToSurfaceRatio = saturate(hitDist / currentLinearZ);
    parallax *= hitDistToSurfaceRatio;

    float4 currentSurface;
    if (!specHasData)
    {
        // Adjusting surface motion based specular accumulation weights for checkerboard
        specSurfaceAlpha *= 1.0 - gCheckerboardResolveAccumSpeed;
        specSurfaceResponsiveAlpha *= 1.0 - gCheckerboardResolveAccumSpeed;
    }
    currentSurface.rgb = lerp(specHistorySurface.rgb, specularIllumination.rgb, specSurfaceAlpha);
    currentSurface.w = lerp(specHistorySurface.w, specularIllumination.w, max(specSurfaceAlpha, RELAX_HIT_DIST_MIN_ACCUM_SPEED(currentRoughnessModified)));

    float3 currentSurfaceResponsive = lerp(prevSurfaceMotionBasedSpecularResponsiveIllumination, specularIllumination.xyz, specSurfaceResponsiveAlpha);
    float currentSpecM2 = lerp(prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments.x, specular2ndMoment, specSurfaceAlpha);

    float fresnelFactor = STL::BRDF::Pow5(NoV);
    virtualHistoryConfidence *= lerp(virtualNormalWeight, 1.0, saturate(fresnelFactor * parallax));

    // Amount of virtual motion - dominant factor
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection(currentNormal, V, currentRoughness, RELAX_SPEC_DOMINANT_DIRECTION);
    float virtualHistoryAmount = virtualHistoryConfidence * D.w;

    // Amount of virtual motion - virtual motion correctness
    float3 R = reflect(-D.xyz, currentNormal);
    float3 Xvirtual = currentWorldPos - R * hitDist * D.w;
    float2 uvVirtualExpected = STL::Geometry::GetScreenUv(gWorldToClip, Xvirtual);

    float4 Dvirtual = STL::ImportanceSampling::GetSpecularDominantDirection(prevVirtualMotionBasedNormal, V, prevVirtualMotionBasedRoughness, RELAX_SPEC_DOMINANT_DIRECTION);
    float3 Rvirtual = reflect(-Dvirtual.xyz, prevVirtualMotionBasedNormal);
    float hitDistVirtual = specHistoryVirtual.w;
    Xvirtual = currentWorldPos - Rvirtual * hitDistVirtual * Dvirtual.w;
    float2 uvVirtualAtSample = STL::Geometry::GetScreenUv(gWorldToClip, Xvirtual);

    float thresholdMax = GetParallaxInPixels(max(0.01, parallaxOrig));
    float thresholdMin = thresholdMax * 0.05;
    float parallaxVirtual = length((uvVirtualAtSample - uvVirtualExpected) * gResolution);
    virtualHistoryAmount *= STL::Math::LinearStep(thresholdMax + 0.00001, thresholdMin, parallaxVirtual);

    // Virtual history confidence - roughness
    float virtualRoughnessWeight = GetRoughnessWeight(roughnessParams, prevVirtualMotionBasedRoughness);
    virtualHistoryConfidence *= virtualRoughnessWeight;

    // Virtual history confidence - hit distance
    float maxDist = max(prevVirtualMotionBasedReflectionHitT, accumulatedReflectionHitT);
    float hitTDisocclusionAdjustment = saturate(2.0*abs(prevVirtualMotionBasedReflectionHitT - accumulatedReflectionHitT) / (maxDist + currentLinearZ));
    virtualHistoryConfidence *= 1.0 - (0.75 + 0.25*saturate(parallax)) * hitTDisocclusionAdjustment;

    // Clamp virtual history
    if (gVirtualHistoryClampingEnabled != 0)
    {
        float sigmaScale = 3.0;
        float4 specHistoryVirtualClamped = STL::Color::Clamp(specM1, specSigma * sigmaScale, specHistoryVirtual);
        float3 specHistoryVirtualResponsiveClamped = STL::Color::Clamp(specM1, specSigma * sigmaScale, prevVirtualMotionBasedSpecularResponsiveIllumination.rgbb).rgb;

        float virtualForcedConfidence = lerp(0.75, 0.95, STL::Math::LinearStep(0.04, 0.25, currentRoughness));
        float virtualUnclampedAmount = lerp(virtualHistoryConfidence * virtualForcedConfidence, 1.0, currentRoughness * currentRoughness);
        specHistoryVirtual = lerp(specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount);
        prevVirtualMotionBasedSpecularResponsiveIllumination = lerp(specHistoryVirtualResponsiveClamped, prevVirtualMotionBasedSpecularResponsiveIllumination, virtualUnclampedAmount);
    }

    // Current specular signal ( virtual motion )
    float specVirtualFrames = gRoughnessBasedSpecularAccumulation ? GetSpecAccumSpeed(specHistoryFrames, currentRoughnessModified, NoV, 0.0) : specHistoryFrames; // parallax = 0 cancels NoV too
    float minSpecVirtualFrames = min(specVirtualFrames, specVirtualFrames * (0.1 + 0.1 * STL::Math::Sqrt01(currentRoughnessModified)));
    specVirtualFrames = InterpolateAccumSpeeds(minSpecVirtualFrames, specVirtualFrames, STL::Math::Sqrt01(virtualHistoryConfidence));

    float specVirtualResponsiveFrames = gRoughnessBasedSpecularAccumulation ? GetSpecAccumSpeed(specHistoryResponsiveFrames, currentRoughnessModified, NoV, 0.0) : specHistoryResponsiveFrames; // parallax = 0 cancels NoV too
    float minSpecVirtualResponsiveFrames = min(specVirtualResponsiveFrames, specVirtualResponsiveFrames * (0.1 + 0.1 * STL::Math::Sqrt01(currentRoughnessModified)));
    specVirtualResponsiveFrames = InterpolateAccumSpeeds(minSpecVirtualResponsiveFrames, specVirtualResponsiveFrames, STL::Math::Sqrt01(virtualHistoryConfidence));

    float specVirtualAlpha = 1.0 / (specVirtualFrames + 1.0);
    float specVirtualResponsiveAlpha = 1.0 / (specVirtualResponsiveFrames + 1.0);

    float4 currentVirtual;
    if (!specHasData)
    {
        // Adjusting virtual motion based specular accumulation weights for checkerboard
        specVirtualAlpha *= 1.0 - gCheckerboardResolveAccumSpeed;
        specVirtualResponsiveAlpha *= 1.0 - gCheckerboardResolveAccumSpeed;
    }
    currentVirtual.xyz = lerp(specHistoryVirtual.xyz, specularIllumination.xyz, specVirtualAlpha);
    currentVirtual.w = lerp(specHistoryVirtual.w, specularIllumination.w, max(specVirtualAlpha, RELAX_HIT_DIST_MIN_ACCUM_SPEED(currentRoughnessModified)));
    float3 currentVirtualResponsive = lerp(prevVirtualMotionBasedSpecularResponsiveIllumination, specularIllumination.xyz, specVirtualResponsiveAlpha);
    float virtualSpecM2 = lerp(prevVirtualMotionBasedSpecular2ndMoment, specular2ndMoment, specVirtualAlpha);

    // Temporal accumulation of reflection HitT
    accumulatedReflectionHitT = lerp(currentSurface.w, currentVirtual.w, virtualHistoryAmount * hitDistToSurfaceRatio);

    // Temporal accumulation of specular illumination
    float3 accumulatedSpecularIllumination = lerp(currentSurface.xyz, currentVirtual.xyz, virtualHistoryAmount);
    float3 accumulatedSpecularIlluminationResponsive = lerp(currentSurfaceResponsive.xyz, currentVirtualResponsive.xyz, virtualHistoryAmount);
    float accumulatedSpecular2ndMoment = lerp(currentSpecM2, virtualSpecM2, virtualHistoryAmount);

    // If zero specular sample (color = 0), artificially adding variance for pixels with low reprojection confidence
    float specularHistoryConfidence = saturate(virtualHistoryConfidence + surfaceHistoryConfidence);
    if (accumulatedSpecular2ndMoment == 0) accumulatedSpecular2ndMoment = gSpecularVarianceBoost * (1.0 - specularHistoryConfidence);

    // Temporal accumulation of diffuse illumination
    float diffuseAlpha = surfaceMotionBasedReprojectionFound ? max(1.0 / (gDiffuseMaxAccumulatedFrameNum + 1.0), 1.0 / historyLength.y) : 1.0;
    float diffuseAlphaResponsive = surfaceMotionBasedReprojectionFound ? max(1.0 / (gDiffuseMaxFastAccumulatedFrameNum + 1.0), 1.0 / historyLength.y) : 1.0;
    if (!diffHasData)
    {
        // Adjusting diffuse accumulation weights for checkerboard
        diffuseAlpha *= 1.0 - gCheckerboardResolveAccumSpeed;
        diffuseAlphaResponsive *= 1.0 - gCheckerboardResolveAccumSpeed;
    }
    float3 accumulatedDiffuseIllumination = lerp(prevDiffuseIllumination.rgb, diffuseIllumination.rgb, diffuseAlpha);
    float3 accumulatedDiffuseIlluminationResponsive = lerp(prevDiffuseResponsiveIllumination.rgb, diffuseIllumination.rgb, diffuseAlphaResponsive);
    float accumulatedDiffuse2ndMoment = lerp(prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments.g, diffuse2ndMoment, diffuseAlpha);

    // Write out the results
    gOutSpecularAndDiffuseIlluminationLogLuv[ipos] = PackSpecularAndDiffuseToLogLuvUint2(accumulatedSpecularIllumination, accumulatedDiffuseIllumination);
    gOutSpecularAndDiffuseIlluminationResponsiveLogLuv[ipos] = PackSpecularAndDiffuseToLogLuvUint2(accumulatedSpecularIlluminationResponsive, accumulatedDiffuseIlluminationResponsive);

    gOutSpecularAndDiffuse2ndMoments[ipos] = float2(accumulatedSpecular2ndMoment, accumulatedDiffuse2ndMoment);

    gOutReflectionHitT[ipos] = accumulatedReflectionHitT;

    gOutSpecularAndDiffuseHistoryLength[ipos] = historyLength / 255.0;
    gOutSpecularReprojectionConfidence[ipos] = specularHistoryConfidence;
}
