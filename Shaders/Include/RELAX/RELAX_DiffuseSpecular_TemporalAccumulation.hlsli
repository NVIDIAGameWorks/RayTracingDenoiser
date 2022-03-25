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

#define CHECK_GEOMETRY \
    reprojectionTapValid = isReprojectionTapValid(currentWorldPos, prevWorldPosInTap, currentNormal, disocclusionThreshold)

#define CHECK_MATERIAL_ID \
    reprojectionTapValid *= CompareMaterials(currentMaterialID, gPrevMaterialID[tapPos], materialIDMask)

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
#if( defined RELAX_SPECULAR)
    float currentReflectionHitT,
#endif
    float3 motionVector,
    float currentMaterialID,
    uint materialIDMask,
    out float footprintQuality,
#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )
    out float2 historyLength,
#else
    out float historyLength,
#endif
#if( defined RELAX_DIFFUSE)
    out float4 prevDiffuseIllumAnd2ndMoment,
    out float3 prevDiffuseResponsiveIllum,
#endif
#if( defined RELAX_SPECULAR)
    out float4 prevSpecularIllumAnd2ndMoment,
    out float3 prevSpecularResponsiveIllum,
    out float3 prevWorldPos,
    out float  prevReflectionHitT,
    out float2 prevUV,
#endif
    out float3 prevNormal
)
{
    // Calculating jitter margin radius in world space
    float jitterRadius = getJitterRadius(gJitterDelta, currentLinearZ);
    float disocclusionThreshold = (gDisocclusionDepthThreshold + jitterRadius) * (gOrthoMode == 0 ? currentLinearZ : 1.0);

    // Calculating previous pixel position and UV
    float2 uv = (pixelPosOnScreen + 0.5) * gInvRectSize;
#ifndef RELAX_SPECULAR
    float2 prevUV;
#endif
    prevUV = STL::Geometry::GetPrevUvFromMotion(uv, currentWorldPos, gPrevWorldToClip, motionVector, gIsWorldSpaceMotionEnabled);
    float2 prevPixelPosOnScreen = prevUV * gRectSizePrev;

    // Consider reprojection to the same pixel index a small motion.
    // It is useful for skipping reprojection test for static camera when the jitter is the only source of motion.
    int2 prevPixelPosInt = int2(prevPixelPosOnScreen);
    bool isSmallMotion = all(prevPixelPosInt == pixelPosOnScreen);
    bool skipReprojectionTest = gSkipReprojectionTestWithoutMotion && isSmallMotion;

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
    motionVector *= gIsWorldSpaceMotionEnabled > 0 ? 1.0 : 0.0;

    // Then taking care of camera motion, because world space is always centered at camera position in NRD
    currentWorldPos += motionVector - gPrevCameraPosition.xyz;

    // Transforming bilinearOrigin to clip space coords to simplify previous world pos calculation
    float2 prevClipSpaceXY = ((float2)bilinearOrigin + float2(0.5, 0.5)) * (1.0 / gRectSizePrev) * 2.0 - 1.0;
    float2 dXY = (2.0 / gRectSizePrev);

    // 1st row
    tapPos = bilinearOrigin + int2(0, -1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(0.0, -1.0), prevViewZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, -1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(1.0, -1.0), prevViewZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;

    // 2nd row
    tapPos = bilinearOrigin + int2(-1, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(-1.0, 0.0), prevViewZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(0.0, 0.0), prevViewZInTap);
    prevWorldPos00 = prevWorldPosInTap;
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.x = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(1.0, 0.0), prevViewZInTap);
    prevWorldPos10 = prevWorldPosInTap;
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.y = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(2, 0);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(2.0, 0.0), prevViewZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;

    // 3rd row
    tapPos = bilinearOrigin + int2(-1, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(-1.0, 1.0), prevViewZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(0.0, 1.0), prevViewZInTap);
    prevWorldPos01 = prevWorldPosInTap;
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.z = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(1.0, 1.0), prevViewZInTap);
    prevWorldPos11 = prevWorldPosInTap;
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.w = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(2, 1);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(2.0, 1.0), prevViewZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;

    // 4th row
    tapPos = bilinearOrigin + int2(0, 2);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(0.0, 2.0), prevViewZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 2);
    prevViewZInTap = gPrevViewZ[tapPos];
    prevWorldPosInTap = GetPreviousWorldPosFromClipSpaceXY(prevClipSpaceXY + dXY * float2(1.0, 2.0), prevViewZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
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
    historyLength = 0;
    footprintQuality = 0;
    prevNormal = currentNormal;
#if( defined RELAX_DIFFUSE)
    prevDiffuseIllumAnd2ndMoment = 0;
    prevDiffuseResponsiveIllum = 0;
#endif
#if( defined RELAX_SPECULAR)
    prevSpecularIllumAnd2ndMoment = 0;
    prevSpecularResponsiveIllum = 0;
    prevWorldPos = currentWorldPos;
    prevReflectionHitT = currentReflectionHitT;
#endif

    if (any(bilinearTapsValid))
    {
        // Trying to apply bicubic filter first
        if (bicubicFootprintValid > 0)
        {
            // Bicubic for illumination and 2nd moments
#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )
            BicubicFloat4x2(
                prevSpecularIllumAnd2ndMoment,
                prevDiffuseIllumAnd2ndMoment,
                gPrevSpecularIllumination,
                gPrevDiffuseIllumination,
                gLinearClamp,
                prevPixelPosOnScreen,
                gInvResourceSize);
#elif( defined RELAX_DIFFUSE)
            prevDiffuseIllumAnd2ndMoment = BicubicFloat4(
                gPrevDiffuseIllumination,
                gLinearClamp,
                prevPixelPosOnScreen,
                gInvResourceSize);
#elif( defined RELAX_SPECULAR)
            prevSpecularIllumAnd2ndMoment = BicubicFloat4(
                gPrevSpecularIllumination,
                gLinearClamp,
                prevPixelPosOnScreen,
                gInvResourceSize);
#endif

#if(defined RELAX_SPECULAR )
            prevSpecularIllumAnd2ndMoment = max(0, prevSpecularIllumAnd2ndMoment);
#endif
#if(defined RELAX_DIFFUSE )
            prevDiffuseIllumAnd2ndMoment = max(0, prevDiffuseIllumAnd2ndMoment);
#endif

#if( RELAX_USE_BICUBIC_FOR_FAST_HISTORY == 1 )

#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )
            float4 spec;
            float4 diff;
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
#elif( defined RELAX_DIFFUSE)
            float4 diff = BicubicFloat4(
                gPrevDiffuseIlluminationResponsive,
                gLinearClamp,
                prevPixelPosOnScreen,
                gInvResourceSize);
            prevDiffuseResponsiveIllum = max(0, diff.rgb);
#elif( defined RELAX_SPECULAR)
            float4 spec = BicubicFloat4(
                gPrevSpecularIlluminationResponsive,
                gLinearClamp,
                prevPixelPosOnScreen,
                gInvResourceSize);
            prevSpecularResponsiveIllum = max(0, spec.rgb);
#endif

#else // #if( RELAX_USE_BICUBIC_FOR_FAST_HISTORY == 1 )
#if( defined RELAX_DIFFUSE)
            prevDiffuseResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevDiffuseIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
#endif
#if( defined RELAX_SPECULAR)
            prevSpecularResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevSpecularIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
#endif

#endif // #if( RELAX_USE_BICUBIC_FOR_FAST_HISTORY == 1 )
            footprintQuality = 1.0;

            reprojectionFound = 2.0;
        }
        else
        {
            // If no success with the bicubic, then do weighted bilinear
#if( defined RELAX_DIFFUSE)
            prevDiffuseIllumAnd2ndMoment =
                BilinearWithBinaryWeightsFloat4(gPrevDiffuseIllumination, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

            prevDiffuseResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevDiffuseIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
#endif
#if( defined RELAX_SPECULAR)
            prevSpecularIllumAnd2ndMoment =
                BilinearWithBinaryWeightsFloat4(gPrevSpecularIllumination, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

            prevSpecularResponsiveIllum = BilinearWithBinaryWeightsFloat4(gPrevSpecularIlluminationResponsive, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight).rgb;
#endif

            reprojectionFound = 1.0;
        }

        // Calculating previous worldspace position
        // by applying weighted bilinear to worldspace positions in taps.
        // Also calculating history length by using weighted bilinear

#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )
        historyLength = 255.0 * BilinearWithBinaryWeightsFloat2(gPrevHistoryLength, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
#else
        historyLength = 255.0 * BilinearWithBinaryWeightsFloat(gPrevHistoryLength, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
#endif

        prevNormal = BilinearWithBinaryWeightsImmediateFloat3(
            prevNormal00, prevNormal10, prevNormal01, prevNormal11,
            bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

#if( defined RELAX_SPECULAR )
        prevWorldPos = BilinearWithBinaryWeightsImmediateFloat3(
                prevWorldPos00, prevWorldPos10, prevWorldPos01, prevWorldPos11,
                bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        float2 gatherOrigin = (bilinearOrigin + 1.0) * gInvResourceSize;
        float4 prevReflectionHitTs = gPrevReflectionHitT.GatherRed(gNearestClamp, gatherOrigin).wzxy;

        prevReflectionHitT = BilinearWithBinaryWeightsImmediateFloat(
            prevReflectionHitTs.x, prevReflectionHitTs.y, prevReflectionHitTs.z, prevReflectionHitTs.w,
            bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        prevReflectionHitT = max(0.001, prevReflectionHitT);
#endif

        footprintQuality = interpolatedBinaryWeight;
    }
    return reprojectionFound;
}

// Returns specular reprojection search result based on virtual motion
#if( defined RELAX_SPECULAR)
float loadVirtualMotionBasedPrevData(
    int2 pixelPosOnScreen,
    float3 currentWorldPos,
    float3 currentNormal,
    float currentLinearZ,
    float currentReflectionHitT,
    float3 currentViewVector,
    float3 prevWorldPos,
    bool surfaceBicubicValid,
    float currentMaterialID,
    uint materialIDMask,
    out float4 prevSpecularIllumAnd2ndMoment,
    out float3 prevSpecularResponsiveIllum,
    out float3 prevNormal,
    out float prevRoughness,
    out float prevReflectionHitT,
    out float2 prevUVVMB)
{
    // Taking care of camera motion, because world space is always centered at camera position in NRD
    prevWorldPos += gPrevCameraPosition.xyz;
    currentWorldPos -= gPrevCameraPosition.xyz;

    // Calculating previous worldspace virtual position based on reflection hitT
    float3 virtualViewVector = normalize(currentViewVector) * currentReflectionHitT;
    float3 prevVirtualWorldPos = prevWorldPos + virtualViewVector;

    float currentViewVectorLength = length(currentViewVector);
    float accumulatedSpecularVMBZ = currentViewVectorLength + currentReflectionHitT;

    float4 prevVirtualClipPos = mul(gPrevWorldToClip, float4(prevVirtualWorldPos, 1.0));
    prevVirtualClipPos.xy /= prevVirtualClipPos.w;
    prevUVVMB = prevVirtualClipPos.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    float2 prevVirtualPixelPosOnScreen = prevUVVMB * gRectSizePrev;

    float jitterRadius = getJitterRadius(gJitterDelta, accumulatedSpecularVMBZ);
    float disocclusionThreshold = (gDisocclusionDepthThreshold + jitterRadius) * (gOrthoMode == 0 ? currentLinearZ : 1.0);

    // Consider reprojection to the same pixel index a small motion.
    // It is useful for skipping reprojection test for static camera when the jitter is the only source of motion.
    int2 prevVirtualPixelPosInt = int2(prevVirtualPixelPosOnScreen);
    bool isSmallVirtualMotion = all(prevVirtualPixelPosInt == pixelPosOnScreen);
    bool skipReprojectionTest = gSkipReprojectionTestWithoutMotion && isSmallVirtualMotion;

    // Calculating footprint origin and weights
    int2 bilinearOrigin = int2(floor(prevVirtualPixelPosOnScreen - 0.5));
    float2 bilinearWeights = frac(prevVirtualPixelPosOnScreen - 0.5);

    // Checking bilinear footprint
    float3 prevNormal00, prevNormal10, prevNormal01, prevNormal11;
    float prevRoughness00, prevRoughness10, prevRoughness01, prevRoughness11;
    int2 tapPos;
    float reprojectionTapValid;
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
    prevWorldPosInTap = GetPreviousWorldPosFromPixelPos(tapPos, prevLinearZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bilinearTapsValid.x = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 0);
    prevNormalRoughness = UnpackPrevNormalRoughness(gPrevNormalRoughness[tapPos]);
    prevNormal10 = prevNormalRoughness.rgb;
    prevRoughness10 = prevNormalRoughness.a;
    prevLinearZInTap = prevViewZs.y;
    prevWorldPosInTap = GetPreviousWorldPosFromPixelPos(tapPos, prevLinearZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bilinearTapsValid.y = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 1);
    prevNormalRoughness = UnpackPrevNormalRoughness(gPrevNormalRoughness[tapPos]);
    prevNormal01 = prevNormalRoughness.rgb;
    prevRoughness01 = prevNormalRoughness.a;
    prevLinearZInTap = prevViewZs.z;
    prevWorldPosInTap = GetPreviousWorldPosFromPixelPos(tapPos, prevLinearZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bilinearTapsValid.z = reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 1);
    prevNormalRoughness = UnpackPrevNormalRoughness(gPrevNormalRoughness[tapPos]);
    prevNormal11 = prevNormalRoughness.rgb;
    prevRoughness11 = prevNormalRoughness.a;
    prevLinearZInTap = prevViewZs.w;
    prevWorldPosInTap = GetPreviousWorldPosFromPixelPos(tapPos, prevLinearZInTap);
    CHECK_GEOMETRY;
    CHECK_MATERIAL_ID;
    bilinearTapsValid.w = reprojectionTapValid;

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
    prevReflectionHitT = gDenoisingRange;

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
#endif

void Preload(uint2 sharedPos, int2 globalPos)
{
    globalPos = clamp(globalPos, 0, gRectSize - 1.0);

#if( defined RELAX_SPECULAR)
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

#if( defined RELAX_DIFFUSE)
    bool diffHasData = true;
    if (gDiffCheckerboard != 2)
    {
        diffHasData = checkerboard == gDiffCheckerboard;
        checkerboardPixelPos.x >>= 1;
    }
#endif

#if( defined RELAX_SPECULAR)
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
    {
        return;
    }

    uint2 sharedMemoryIndex = threadPos.xy + int2(BORDER, BORDER);

    // Center data
#if( defined RELAX_DIFFUSE)
    float3 diffuseIllumination = gDiffuseIllumination[pixelPos.xy + gRectOrigin].rgb;
#endif

#if( defined RELAX_SPECULAR )
    float4 specularIllumination = sharedInSpecular[sharedMemoryIndex.y][sharedMemoryIndex.x];
    specularIllumination.a = max(0.001, min(gDenoisingRange, specularIllumination.a));
#endif

    // Reading current GBuffer data
    float4 currentNormalRoughness = sharedNormalRoughness[sharedMemoryIndex.y][sharedMemoryIndex.x];
    float3 currentNormal = currentNormalRoughness.xyz;
    float currentRoughness = currentNormalRoughness.w;

    // Handling materialID
    // Combining logic is valid even if non-combined denoisers are used
    // since undefined masks are zeroes in those cases
    float currentMaterialID;
    NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[gRectOrigin + pixelPos], currentMaterialID);
    currentMaterialID = floor( currentMaterialID * 3.0 + 0.5 ) / 255.0; // IMPORTANT: properly repack 2-bits to 8-bits

    // Getting current frame worldspace position and view vector for current pixel
    float3 currentWorldPos = GetCurrentWorldPosFromPixelPos(pixelPos, currentLinearZ);
    float3 currentViewVector = (gOrthoMode == 0) ?
        currentWorldPos :
        currentLinearZ * normalize(gFrustumForward.xyz);

#if( defined RELAX_SPECULAR)
    float4 specM1 = specularIllumination;
    float4 specM2 = specM1 * specM1;
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
#endif

    float3 currentNormalAveraged = currentNormal;
#if( defined RELAX_SPECULAR )
    float curvature = 0;
    float curvatureSum = 0;
#endif
    [unroll]
    for (int k = -1; k <= 1; k++)
    {
        [unroll]
        for (int l = -1; l <= 1; l++)
        {
            // Skipping center pixel
            if ((k == 0) && (l == 0)) continue;
            float3 pNormal = sharedNormalRoughness[sharedMemoryIndex.y + k][sharedMemoryIndex.x + l].xyz;
            currentNormalAveraged += pNormal;
#if( defined RELAX_SPECULAR )
            int2 p = pixelPos + int2(l, k);
            float3 x = GetCurrentWorldPosFromPixelPos(p, 1.0);
            float3 v = normalize(gOrthoMode ? gFrustumForward.xyz : x);
            float c = EstimateCurvature(pNormal, v, currentNormal, currentWorldPos);

            float w = exp2(-0.5 * STL::Math::LengthSquared(float2(k,l)));
            curvature += c * w;
            curvatureSum += w;
#endif
        }
    }
    currentNormalAveraged /= 9.0;

#if( defined RELAX_SPECULAR )
    curvature /= curvatureSum;
    curvature *= STL::Math::LinearStep(0.0, NRD_ENCODING_ERRORS.y, abs(curvature));
    float currentRoughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance(currentRoughness, currentNormalAveraged);
#endif

    // Computing 2nd moments of luminance
#if( defined RELAX_SPECULAR )
    float specular1stMoment = STL::Color::Luminance(specularIllumination.rgb);
    float specular2ndMoment = specular1stMoment * specular1stMoment;
#endif

#if( defined RELAX_DIFFUSE )
    float diffuse1stMoment = STL::Color::Luminance(diffuseIllumination.rgb);
    float diffuse2ndMoment = diffuse1stMoment * diffuse1stMoment;
#endif

    // Reading motion vector
    float3 motionVector = gMotion[gRectOrigin + pixelPos].xyz * gMotionVectorScale.xyy;

    // Loading previous data based on surface motion vectors
    float3 prevNormalSMB;
    float footprintQuality;
#if( defined RELAX_DIFFUSE )
    float4 prevDiffuseIlluminationAnd2ndMomentSMB;
    float3 prevDiffuseIlluminationAnd2ndMomentSMBResponsive;
#endif
#if( defined RELAX_SPECULAR )
    float4 prevSpecularIlluminationAnd2ndMomentSMB;
    float3 prevSpecularIlluminationAnd2ndMomentSMBResponsive;
    float  prevReflectionHitTSMB;
    float3 prevWorldPosSMB;
    float2 prevUVSMB;
#endif
#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )
    float2 historyLength;
#else
    float historyLength;
#endif

    float surfaceMotionBasedReprojectionFound = loadSurfaceMotionBasedPrevData(
        pixelPos,
        currentWorldPos,
        normalize(currentNormalAveraged),
        currentLinearZ,
#if( defined RELAX_SPECULAR)
        specularIllumination.a,
#endif
        motionVector,
        currentMaterialID,
        gDiffMaterialMask | gSpecMaterialMask, // TODO: improve?
        footprintQuality,
        historyLength,
#if( defined RELAX_DIFFUSE)
        prevDiffuseIlluminationAnd2ndMomentSMB,
        prevDiffuseIlluminationAnd2ndMomentSMBResponsive,
#endif
#if( defined RELAX_SPECULAR)
        prevSpecularIlluminationAnd2ndMomentSMB,
        prevSpecularIlluminationAnd2ndMomentSMBResponsive,
        prevWorldPosSMB,
        prevReflectionHitTSMB,
        prevUVSMB,
#endif
        prevNormalSMB
    );

     // History length is based on surface motion based disocclusion
    historyLength = historyLength + 1.0;
    historyLength = min(100.0, historyLength);

    // Avoid footprint momentary stretching due to changed viewing angle
    float3 prevWorldPos = currentWorldPos + motionVector * float(gIsWorldSpaceMotionEnabled != 0);
    float3 Vprev = normalize(prevWorldPos - gPrevCameraPosition.xyz);
    float VoNflat = abs(dot(currentNormalAveraged, normalize(currentViewVector))) + 1e-3;
    float VoNflatprev = abs(dot(currentNormalAveraged, Vprev)) + 1e-3;
    float sizeQuality = VoNflatprev / VoNflat; // this order because we need to fix stretching only, shrinking is OK
    sizeQuality *= sizeQuality;
    sizeQuality *= sizeQuality;
    footprintQuality *= lerp(0.1, 1.0, saturate(sizeQuality + abs(gOrthoMode)));

    // Minimize "getting stuck in history" effect when only fraction of bilinear footprint is valid
    // by shortening the history length
    if (footprintQuality < 1.0)
    {
        historyLength *= sqrt(footprintQuality);
        historyLength = max(historyLength, 1.0);
    }

    // Handling history reset if needed
    if (gResetHistory != 0) historyLength = 1.0;

#if( defined RELAX_DIFFUSE )
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
        float inDiffConfidence = gDiffConfidence[pixelPos];
        diffMaxAccumulatedFrameNum *= inDiffConfidence;
        diffMaxFastAccumulatedFrameNum *= inDiffConfidence;
    }

#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )
    float diffHistoryLength = historyLength.y;
#else
    float diffHistoryLength = historyLength;
#endif

    float diffuseAlpha = (surfaceMotionBasedReprojectionFound > 0) ? max(1.0 / (diffMaxAccumulatedFrameNum + 1.0), 1.0 / diffHistoryLength) : 1.0;
    float diffuseAlphaResponsive = (surfaceMotionBasedReprojectionFound > 0) ? max(1.0 / (diffMaxFastAccumulatedFrameNum + 1.0), 1.0 / diffHistoryLength) : 1.0;

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

#if( defined RELAX_SPECULAR )
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

#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )
    float specHistoryLength = historyLength.x;
#else
    float specHistoryLength = historyLength;
#endif
    float specHistoryFrames = min(specMaxAccumulatedFrameNum, specHistoryLength);
    float specHistoryResponsiveFrames = min(specMaxFastAccumulatedFrameNum, specHistoryLength);

    // Calculating surface parallax
    float parallax = ComputeParallax(currentWorldPos, currentWorldPos + motionVector * (gIsWorldSpaceMotionEnabled != 0 ? 1.0 : 0.0), gPrevCameraPosition, gOrthoMode != 0);
    float parallaxOrig = parallax;
    float hitDistToSurfaceRatio = saturate(prevReflectionHitTSMB / currentLinearZ);
    parallax *= hitDistToSurfaceRatio;
    float parallaxInPixels = GetParallaxInPixels(max(0.01, parallaxOrig), gUnproject);

    // Params required for surface motion based (SMB) specular reprojection
    float3 V = normalize(-currentViewVector);
    float NoV = saturate(dot(currentNormal, V));

    // Current specular signal ( surface motion )
    float specSurfaceFrames = GetSpecAccumSpeed(specHistoryFrames, max(0.05, currentRoughnessModified), NoV, parallax);
    float specSurfaceResponsiveFrames = GetSpecAccumSpeed(specHistoryResponsiveFrames, max(0.05, currentRoughnessModified), NoV, parallax);

    float specSurfaceAlpha = saturate(1.0 / specSurfaceFrames);
    float specSurfaceResponsiveAlpha = saturate(1.0 / specSurfaceResponsiveFrames);

    float surfaceHistoryConfidence = saturate(specSurfaceFrames / (specHistoryFrames + 1.0));

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
    float pixelSize = PixelRadiusToWorld(gUnproject, gOrthoMode, 1.0, currentLinearZ);
    curvature *= NoV / pixelSize;
    float hitDist = specularIllumination.a;
    hitDist = lerp(minHitDist3x3, minHitDist5x5, STL::Math::SmoothStep(0.04, 0.08, currentRoughnessModified));
    hitDist = clamp(hitDist, specM1.a - specSigma.a * 3.0, specM1.a + specSigma.a * 3.0);
    float divider = 0.5 + curvature * hitDist;
    float hitDistFocused = 0.5 * hitDist / (divider == 0 ? 0.5 : divider);

    // Limiting hitDist in ortho case to avoid extreme amounts of motion in reflection
    if(gOrthoMode != 0) hitDistFocused = min(currentLinearZ, hitDistFocused);

    if(abs(hitDistFocused) < 0.001) hitDistFocused = 0.001;

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
        currentViewVector,
        prevWorldPosSMB,
        surfaceMotionBasedReprojectionFound == 2.0 ? true : false,
        currentMaterialID,
        gSpecMaterialMask,
        prevSpecularIlluminationAnd2ndMomentVMB,
        prevSpecularIlluminationAnd2ndMomentVMBResponsive,
        prevNormalVMB,
        prevRoughnessVMB,
        prevReflectionHitTVMB,
        prevUVVMB
    );

    // Normal weight for virtual history
    float fresnelFactor = STL::BRDF::Pow5(NoV);
    float virtualLobeScale = lerp(0.5, 1.0, fresnelFactor);
    float lobeHalfAngle = STL::ImportanceSampling::GetSpecularLobeHalfAngle(currentRoughnessModified);
    lobeHalfAngle *= virtualLobeScale;
    lobeHalfAngle += NRD_ENCODING_ERRORS.x + STL::Math::DegToRad(1.5); // TODO: tune better?
    float virtualNormalWeight = GetEncodingAwareNormalWeight(currentNormal, prevNormalVMB, lobeHalfAngle);

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
    float2 roughnessParams = GetRoughnessWeightParams(currentRoughness, gRoughnessFraction);
    float rw = GetRoughnessWeight(roughnessParams, prevRoughnessVMB);
    float virtualRoughnessWeight = 0.75 + 0.25 * rw;
    virtualHistoryConfidence *= virtualRoughnessWeight;
    virtualHistoryAmount *= (gOrthoMode == 0) ? rw * 0.9 + 0.1 : 1.0;

    // Decreasing virtual history amount for ortho case
    virtualHistoryAmount *= (gOrthoMode == 0) ? 1.0 : 0.5;

    // Virtual history confidence - hit distance
    float maxDist = max(prevReflectionHitTVMB, prevReflectionHitTSMB);
    float dHitT = max(0, sqrt(abs(prevReflectionHitTVMB - prevReflectionHitTSMB)) - 0.0 * specSigma.a);
    float virtualHistoryHitDistConfidence = 1.0 - saturate(10.0 * dHitT / (currentLinearZ + maxDist));
    virtualHistoryConfidence *= virtualHistoryHitDistConfidence;

    // "Looking back" 1 and 2 frames and applying normal weight to decrease lags
    float pixelSizeOverCurvatureRadius = curvature * pixelSize;
    float avgCurvatureAngle = STL::Math::AtanApprox(abs(2.5 * pixelSizeOverCurvatureRadius));
    float uvDiffScale = lerp(10.0, 1.0, saturate(parallaxInPixels / 5.0)) / 2.0;
    float2 uvDiff = prevUVVMB - prevUVSMB;
    float2 backUV1 = prevUVVMB + 1.0 * uvDiff * uvDiffScale;
    float2 backUV2 = prevUVVMB + 2.0 * uvDiff * uvDiffScale;
    backUV1 *= (gInvResourceSize * gRectSizePrev); // Taking in account resolution scale
    backUV2 *= (gInvResourceSize * gRectSizePrev);
    float3 backNormal1 = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, backUV1, 0)).rgb;
    float3 backNormal2 = UnpackPrevNormalRoughness(gPrevNormalRoughness.SampleLevel(gLinearClamp, backUV2, 0)).rgb;
    float maxAngle1 = lobeHalfAngle + avgCurvatureAngle * 1.0;
    float maxAngle2 = lobeHalfAngle + avgCurvatureAngle * 2.0;
    float backNormalWeight1 = IsInScreen(backUV1) ? GetEncodingAwareNormalWeight(currentNormal, backNormal1, maxAngle1) : 1.0;
    float backNormalWeight2 = IsInScreen(backUV2) ? GetEncodingAwareNormalWeight(currentNormal, backNormal2, maxAngle2) : 1.0;
    float backNormalWeight = backNormalWeight1 * backNormalWeight2;
    virtualHistoryConfidence *= backNormalWeight;

    virtualHistoryAmount *= lerp(0.333, 1.0, backNormalWeight);

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
