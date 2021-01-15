/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE(cbuffer, globalConstants, b, 0, 0)
{
    float4x4 gClipToView;
    float4x4 gViewToClip;
    float4x4 gClipToWorld;
    float4x4 gPrevClipToWorld;
    float4x4 gPrevWorldToClip;

    float3   gPrevCameraPosition;
    float    gJitterDelta;

    float2   gMotionVectorScale;
    uint2    gResolution;
    float2   gInvViewSize;

    float    gUseBicubic;
    float    gSpecularAlpha;
    float    gSpecularResponsiveAlpha;
    float    gSpecularVarianceBoost;

    float    gDiffuseAlpha;
    float    gDiffuseResponsiveAlpha;
    float    gWorldSpaceMotion;
    float    gIsOrtho;

    float    gUnproject;
    float    gNeedHistoryReset;
};

#include "RELAX_Common.hlsl"

// Inputs
NRI_RESOURCE(Texture2D<float4>, gSpecularIllumination, t, 0, 0);
NRI_RESOURCE(Texture2D<float4>, gDiffuseIllumination, t, 1, 0);
NRI_RESOURCE(Texture2D<float4>, gMotion, t, 2, 0);
NRI_RESOURCE(Texture2D<uint2>,  gNormalRoughnessDepth, t, 3, 0);
NRI_RESOURCE(Texture2D<uint2>,  gPrevSpecularAndDiffuseIlluminationLogLuv, t, 4, 0);
NRI_RESOURCE(Texture2D<uint2>,  gPrevSpecularAndDiffuseIlluminationResponsiveLogLuv, t, 5, 0);
NRI_RESOURCE(Texture2D<float4>, gPrevSpecularIlluminationUnpacked, t, 6, 0);
NRI_RESOURCE(Texture2D<float4>, gPrevDiffuseIlluminationUnpacked, t, 7, 0);
NRI_RESOURCE(Texture2D<float2>, gPrevSpecularAndDiffuse2ndMoments, t, 8, 0);
NRI_RESOURCE(Texture2D<uint2>,  gPrevNormalRoughnessDepth, t, 9, 0);
NRI_RESOURCE(Texture2D<float>,  gPrevReflectionHitT, t, 10, 0);
NRI_RESOURCE(Texture2D<float>,  gPrevHistoryLength, t, 11, 0);

// Outputs
NRI_RESOURCE(RWTexture2D<uint2>,  gOutSpecularAndDiffuseIlluminationLogLuv, u, 0, 0);
NRI_RESOURCE(RWTexture2D<uint2>,  gOutSpecularAndDiffuseIlluminationResponsiveLogLuv, u, 1, 0);
NRI_RESOURCE(RWTexture2D<float2>, gOutSpecularAndDiffuse2ndMoments, u, 2, 0);
NRI_RESOURCE(RWTexture2D<float>,  gOutReflectionHitT, u, 3, 0);
NRI_RESOURCE(RWTexture2D<float>,  gOutHistoryLength, u, 4, 0);
NRI_RESOURCE(RWTexture2D<float>,  gOutSpecularReprojectionConfidence, u, 5, 0);

// Helper functions
float getJitterRadius(float jitterDelta, float linearZ)
{
    return jitterDelta * gUnproject * (gIsOrtho > 0 ? 1.0 : linearZ);
}

float getLinearZFromDepth(int2 ipos, float depth)
{
    float2 uv = (float2(ipos) + 0.5) * gInvViewSize.xy;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1);
    float4 viewPos = mul(gClipToView, clipPos);
    viewPos.z /= viewPos.w;
    return viewPos.z;
}

float3 getCurrentWorldPos(int2 pixelPos, float depth)
{
    float2 uv = ((float2)pixelPos + float2(0.5, 0.5)) * gInvViewSize;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1);
    float4 worldPos = mul(gClipToWorld, clipPos);
    return worldPos.xyz / worldPos.w;
}

float3 getPreviousWorldPos(int2 pixelPos, float depth)
{
    float2 uv = ((float2)pixelPos + float2(0.5, 0.5)) * gInvViewSize;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1);
    float4 worldPos = mul(gPrevClipToWorld, clipPos);
    return worldPos.xyz / worldPos.w;
}

float2 getRoughnessWeightParams(float roughness0)
{
    float a = 1.0 / (roughness0 * 0.5 * 0.999 + 0.001);
    float b = roughness0 * a;
    return float2(a, b);
}

float getRoughnessWeight(float2 params0, float roughness)
{
    return saturate(1.0 - abs(params0.y - roughness * params0.x));
}

float getSpecularLobeHalfAngle(float roughness)
{
    // Defines a cone angle, where micro-normals are distributed
    float r2 = roughness * roughness;
    float r3 = roughness * r2;

    // Approximation of https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf (page 72)
    // for [0..1] domain:

    // float k = 0.75; // % of NDF volume. Is it the trimming factor from VNDF sampling?
    // return atan(m * k / (1.0 - k));

    return 3.141592 * r2 / (1.0 + 0.5*r2 + r3);
}

float getSpecularNormalWeight(float2 params0, float3 n0, float3 n)
{
    // Assuming that "n0" is normalized and "n" is not!
    float cosa = saturate(dot(n0, n));// *STL::Math::Rsqrt(STL::Math::LengthSquared(n)));
    float a = acos(cosa);
    a = 1.0 - STL::Math::SmoothStep(0.0, params0.x, a);

    return saturate(1.0 + (a - 1.0) * params0.y);
}

float getModifiedRoughnessFromNormalVariance(float roughness, float3 nonNormalizedAverageNormal)
{
    // https://blog.selfshadow.com/publications/s2013-shading-course/rad/s2013_pbs_rad_notes.pdf (page 20)
    float l = length(nonNormalizedAverageNormal);
    float kappa = saturate(1.0 - l * l) / (l * (3.0 - l * l));

    return sqrt(roughness * roughness + kappa);
}

float isReprojectionTapValid(int2 pixelCoord, float currentLinearZ, float3 currentWorldPos, float3 previousWorldPos, float3 currentNormal, float3 previousNormal, float jitterRadius)
{

    // Check whether reprojected pixel is inside of the screen
    if (any(pixelCoord < int2(0, 0)) || any(pixelCoord > int2(gResolution) - int2(1, 1))) return 0;

    // Check if plane distance is acceptable
    float maxDot = max(abs(dot(currentWorldPos - previousWorldPos, previousNormal)), abs(dot(currentWorldPos - previousWorldPos, currentNormal)));
    if ((maxDot / currentLinearZ) > 0.01 + jitterRadius * 2.0) return 0;

    // No need to check normals, as plane distance check is conservative enough!
    // if (dot(currentNormal, previousNormal) < 0.5) return 0;

    return 1.0;
}


float getSpecularAlphaAdjustment(float roughness, float cosa)
{
    // This is the main parameter - cone angle
    float angle = getSpecularLobeHalfAngle(roughness);

    // Mitigate banding introduced by errors caused by normals being stored in octahedral 8+8 (Oct16) format
    // See http://jcgt.org/published/0003/02/01/ "A Survey of Efficient Representations for Independent Unit Vectors"
    angle += 0.94 * 3.141592 / 180.0;

    float a = acos(cosa);
    a = STL::Math::SmoothStep(0.0, angle, a);
    return saturate(a);
}


// Returns reprojection search result based on surface motion:
// 2 - 16 taps in 4x4 footprint are good, bicubic filtering used
// 1 - some of 4 taps in 2x2 footprint are good, weighted bilinear used
// 0 - candidate taps for reprojection not found, therefore reprojection is not valid = not found
//
// Also returns reprojected data from previous frame calculated using filtering based on filters above.
// For better performance, some data is filtered using cross-bilateral filtering instead of bicubic even if all bicubic taps are valid.
int loadSurfaceMotionBasedPrevData(
    int2 pixelPosOnScreen,
    float3 currentWorldPos,
    float3 currentNormal,
    float currentLinearZ,
    float currentReflectionHitT,
    out float3 prevSpecularIllum,
    out float3 prevDiffuseIllum,
    out float3 prevSpecularResponsiveIllum,
    out float3 prevDiffuseResponsiveIllum,
    out float2 prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments,
    out float3 prevWorldPos,
    out float  prevReflectionHitT,
    out float  historyLength,
    out float3 debugOut)
{
    // Setting default values for output
    prevSpecularIllum = 0;
    prevDiffuseIllum = 0;
    prevSpecularResponsiveIllum = 0;
    prevDiffuseResponsiveIllum = 0;
    prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments = 0;
    prevWorldPos = 0;
    prevReflectionHitT = currentReflectionHitT;
    historyLength = 0;
    debugOut = 0;

    // Reading motion vector
    float3 motionVector = gMotion[pixelPosOnScreen].xyz * gMotionVectorScale.xyy;

    // Calculating jitter margin radius in world space
    float jitterRadius = getJitterRadius(gJitterDelta, currentLinearZ);

    // Calculating previous pixel position and UV
    float2 pixelUV = (pixelPosOnScreen + 0.5) * gInvViewSize;
    float2 prevPixelUV = STL::Geometry::GetPrevUvFromMotion(pixelUV, currentWorldPos, gPrevWorldToClip, motionVector, gWorldSpaceMotion);
    float2 prevPixelPosOnScreen = prevPixelUV * gResolution;

    motionVector *= gWorldSpaceMotion > 0 ? 1.0 : 0.0;

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
    int reprojectionTapValid;

    float3 prevWorldPos00, prevWorldPos10, prevWorldPos01, prevWorldPos11;

    // 1st row
    tapPos = bilinearOrigin + int2(0, -1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, -1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // 2nd row
    tapPos = bilinearOrigin + int2(-1, 0);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 0);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.x = reprojectionTapValid ? 1.0 : 0.0;
    prevWorldPos00 = prevWorldPosInTap;

    tapPos = bilinearOrigin + int2(1, 0);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.y = reprojectionTapValid ? 1.0 : 0.0;
    prevWorldPos10 = prevWorldPosInTap;

    tapPos = bilinearOrigin + int2(2, 0);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // 3rd row
    tapPos = bilinearOrigin + int2(-1, 1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.z = reprojectionTapValid ? 1.0 : 0.0;
    prevWorldPos01 = prevWorldPosInTap;

    tapPos = bilinearOrigin + int2(1, 1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;
    bilinearTapsValid.w = reprojectionTapValid ? 1.0 : 0.0;
    prevWorldPos11 = prevWorldPosInTap;

    tapPos = bilinearOrigin + int2(2, 1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // 4th row
    tapPos = bilinearOrigin + int2(0, 2);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    tapPos = bilinearOrigin + int2(1, 2);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessDontCare, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap) - motionVector;
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bicubicFootprintValid *= reprojectionTapValid;

    // Calculating interpolated binary weight for bilinear taps in advance
    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;
    float interpolatedBinaryWeight = STL::Filtering::ApplyBilinearFilter(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w, bilinear);
    interpolatedBinaryWeight = max(1e-2, interpolatedBinaryWeight);

    //Applying reprojection filters
    int reprojectionFound = 0;

    // Trying to apply bicubic filter first
    if (gUseBicubic && (bicubicFootprintValid > 0))
    {
        // Bicubic for illumination and 2nd moments
        // BicubicSampleCatmullRomFromPackedLogLuvX2(prevSpecularIllum, prevDiffuseIllum, gPrevSpecularAndDiffuseIlluminationLogLuv, prevPixelPosOnScreen);
        // ^- unfortunately this is very slow, so passing uncompressed temporally accumulated data and using bicubic with hardware bilinear fetches is a net win.

        prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments = max(0, BicubicSampleCatmullRomFloat2UsingBilinear(gPrevSpecularAndDiffuse2ndMoments, gLinearClamp, prevPixelPosOnScreen, gInvViewSize));
        prevSpecularIllum = max(0, BicubicSampleCatmullRomFloat4UsingBilinear(gPrevSpecularIlluminationUnpacked, gLinearClamp, prevPixelPosOnScreen, gInvViewSize).rgb);
        prevDiffuseIllum = max(0, BicubicSampleCatmullRomFloat4UsingBilinear(gPrevDiffuseIlluminationUnpacked, gLinearClamp, prevPixelPosOnScreen, gInvViewSize).rgb);

        // Bilinear for responsive illumination
        LinearInterpolationWithBinaryWeightsFromPackedLogLuvX2(prevSpecularResponsiveIllum, prevDiffuseResponsiveIllum, gPrevSpecularAndDiffuseIlluminationResponsiveLogLuv, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        reprojectionFound = 2;
    }

    // If no success with the bicubic, but any bilinear taps are valid, then do weighted bilinear
    if (any(bilinearTapsValid) && (reprojectionFound == 0))
    {

        LinearInterpolationWithBinaryWeightsFromPackedLogLuvX2(prevSpecularIllum, prevDiffuseIllum, gPrevSpecularAndDiffuseIlluminationLogLuv, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        LinearInterpolationWithBinaryWeightsFromPackedLogLuvX2(prevSpecularResponsiveIllum, prevDiffuseResponsiveIllum, gPrevSpecularAndDiffuseIlluminationResponsiveLogLuv, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments = max(0, LinearInterpolationWithBinaryWeightsFloat2(gPrevSpecularAndDiffuse2ndMoments, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight));

        reprojectionFound = 1;
    }

    // If reprojection was found, calculating previousd worldspace position
    // by applying weighted bilinear to worldspace positions in taps.
    // Also calculating history length by using weighted bilinear
    if (reprojectionFound > 0)
    {

        prevWorldPos = LinearInterpolationWithBinaryWeightsImmediateFloat3(
                                prevWorldPos00, prevWorldPos10, prevWorldPos01, prevWorldPos11,
                                bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        historyLength = LinearInterpolationWithBinaryWeightsFloat(gPrevHistoryLength, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        prevReflectionHitT = LinearInterpolationWithBinaryWeightsFloat(gPrevReflectionHitT, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
    }
    else
    {
        historyLength = 0;
        prevWorldPos = currentWorldPos;
    }

    return reprojectionFound;
}

// Returns specular reprojection search result based on virtual motion
int loadVirtualMotionBasedPrevData(
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
    out float prevReflectionHitT,
    out float3 debugOut)
{
    // Setting default values for output
    prevSpecularIllum = 0;
    prevSpecularResponsiveIllum = 0;
    prevSpecular2ndMoment = 0;
    prevNormal = currentNormal;
    prevRoughness = 0;
    prevReflectionHitT = currentReflectionHitT;
    debugOut = 0;

    // Calculating previous worldspace virtual position based on reflection hitT
    float3 virtualViewVector = normalize(currentViewVector) * currentReflectionHitT;
    float3 prevVirtualWorldPos = prevWorldPos + virtualViewVector;

    float currentViewVectorLength = length(currentViewVector);
    float currentVirtualZ = currentViewVectorLength + currentReflectionHitT;

    float4 prevVirtualClipPos = mul(gPrevWorldToClip, float4(prevVirtualWorldPos, 1.0));
    prevVirtualClipPos.xy /= prevVirtualClipPos.w;
    float2 prevVirtualUV = prevVirtualClipPos.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    float2 prevVirtualPixelPosOnScreen = prevVirtualUV * gResolution;

    // Calculating jitter margin radius in world space
    float jitterRadius = getJitterRadius(gJitterDelta, currentVirtualZ);

    // Calculating footprint origin and weights
    int2 bilinearOrigin = int2(floor(prevVirtualPixelPosOnScreen - 0.5) + 0.5);
    float2 bilinearWeights = frac(prevVirtualPixelPosOnScreen - 0.5);

    // Checking bilinear footprint
    float4 bilinearTapsValid = 0;


    float3 prevNormalInTap;
    float prevRoughnessInTap;
    float prevDepthInTap;
    float3 prevWorldPosInTap;
    int2 tapPos;
    float3 prevNormal00, prevNormal10, prevNormal01, prevNormal11;
    float prevRoughness00, prevRoughness10, prevRoughness01, prevRoughness11;
    bool reprojectionTapValid;

    tapPos = bilinearOrigin + int2(0, 0);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessInTap, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bilinearTapsValid.x = reprojectionTapValid;
    prevNormal00 = prevNormalInTap;
    prevRoughness00 = prevRoughnessInTap;

    tapPos = bilinearOrigin + int2(1, 0);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessInTap, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bilinearTapsValid.y = reprojectionTapValid;
    prevNormal10 = prevNormalInTap;
    prevRoughness10 = prevRoughnessInTap;

    tapPos = bilinearOrigin + int2(0, 1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessInTap, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bilinearTapsValid.z = reprojectionTapValid;
    prevNormal01 = prevNormalInTap;
    prevRoughness01 = prevRoughnessInTap;

    tapPos = bilinearOrigin + int2(1, 1);
    UnpackNormalRoughnessDepth(prevNormalInTap, prevRoughnessInTap, prevDepthInTap, gPrevNormalRoughnessDepth[tapPos]);
    prevWorldPosInTap = getPreviousWorldPos(tapPos, prevDepthInTap);
    reprojectionTapValid = isReprojectionTapValid(tapPos, currentLinearZ, currentWorldPos, prevWorldPosInTap, currentNormal, prevNormalInTap, jitterRadius);
    bilinearTapsValid.w = reprojectionTapValid;
    prevNormal11 = prevNormalInTap;
    prevRoughness11 = prevRoughnessInTap;


    // Calculating interpolated binary weight for bilinear taps in advance
    STL::Filtering::Bilinear bilinear;
    bilinear.weights = bilinearWeights;
    float interpolatedBinaryWeight = STL::Filtering::ApplyBilinearFilter(bilinearTapsValid.x, bilinearTapsValid.y, bilinearTapsValid.z, bilinearTapsValid.w, bilinear);
    interpolatedBinaryWeight = max(1e-2, interpolatedBinaryWeight);

    //Applying reprojection
    int reprojectionFound = 0;

    // Weighted bilinear for prev specular data based on virtual motion
    if (any(bilinearTapsValid))
    {
        float3 diffuseIllumDontCare = 0;
        LinearInterpolationWithBinaryWeightsFromPackedLogLuvX2(prevSpecularIllum, diffuseIllumDontCare, gPrevSpecularAndDiffuseIlluminationLogLuv, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        LinearInterpolationWithBinaryWeightsFromPackedLogLuvX2(prevSpecularResponsiveIllum, diffuseIllumDontCare, gPrevSpecularAndDiffuseIlluminationResponsiveLogLuv, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        prevSpecular2ndMoment = max(0, LinearInterpolationWithBinaryWeightsFloat2(gPrevSpecularAndDiffuse2ndMoments, bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight)).r;
        prevNormal = LinearInterpolationWithBinaryWeightsImmediateFloat3(
                                    prevNormal00, prevNormal10, prevNormal01, prevNormal11,
                                    bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);
        prevRoughness = LinearInterpolationWithBinaryWeightsImmediateFloat(
                                    prevRoughness00, prevRoughness10, prevRoughness01, prevRoughness11,
                                    bilinearOrigin, bilinearWeights, bilinearTapsValid, interpolatedBinaryWeight);

        prevReflectionHitT = LinearInterpolationWithBinaryWeightsFloat(gPrevReflectionHitT, bilinearOrigin, bilinearWeights, float4(1.0, 1.0, 1.0, 1.0), 1.0);

        reprojectionFound = 1;
    }

    debugOut = frac(prevVirtualWorldPos);
    return reprojectionFound;
}

//
// Main
//

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gResolution)) return;

    int2 ipos = int2(dispatchThreadId.xy);

    // Reading raw noisy specular and diffuse.
    // Specular has noisy HitT in its alpha channel
    float4 specularIllumination = gSpecularIllumination[ipos].rgba;
    float3 diffuseIllumination = gDiffuseIllumination[ipos].rgb;
    specularIllumination.a = max(0.01, min(50.0, specularIllumination.a));

    specularIllumination.rgb = specularIllumination.rgb;
    diffuseIllumination.rgb = diffuseIllumination.rgb;

    // Reading current GBuffer data
    float3 currentNormal;
    float currentRoughness;
    float currentDepth;
    UnpackNormalRoughnessDepth(currentNormal, currentRoughness, currentDepth, gNormalRoughnessDepth[ipos]);
    float currentLinearZ = getLinearZFromDepth(ipos, currentDepth);

    // Calculating average normal in 3x3 area around current pixel
    // TODO: move this to shmem?
    float3 averageNormal = currentNormal;
    float sumW = 1.0;
    for (int i = -1; i <= 1; i++)
    {
        for (int j = -1; j <= 1; j++)
        {
            // Skipping center pixel
            if ((i == 0) && (j == 0)) continue;

            int2 p = ipos + int2(i, j);
            float3 pNormal;
            float pRoughness;
            float pDepth;
            UnpackNormalRoughnessDepth(pNormal, pRoughness, pDepth, gNormalRoughnessDepth[p]);
            float pZ = getLinearZFromDepth(p, pDepth);

            float w = abs(pZ - currentLinearZ) / (min(abs(currentLinearZ), abs(pZ)) + 0.001) < 0.05 ? 1.0 : 0.0;

            averageNormal += pNormal * w;
            sumW += w;
        }
    }
    float invSumW = 1.0 / sumW;
    averageNormal *= invSumW;

    // Calculating modified roughness that takes normal variation in account
    float currentRoughnessModified = getModifiedRoughnessFromNormalVariance(currentRoughness, averageNormal);

    // Computing 2nd moments of luminance
    float specular1stMoment = STL::Color::Luminance(specularIllumination.rgb);
    float specular2ndMoment = specular1stMoment * specular1stMoment;

    float diffuse1stMoment = STL::Color::Luminance(diffuseIllumination.rgb);
    float diffuse2ndMoment = diffuse1stMoment * diffuse1stMoment;

    // Getting current frame worldspace position and view vector for current pixel
    float3 currentWorldPos = getCurrentWorldPos(ipos, currentDepth);
    float3 currentViewVector = currentWorldPos;

    // Loading previous data based on surface motion vectors
    float3 prevSurfaceMotionBasedSpecularIllumination;
    float3 prevDiffuseIllumination;
    float3 prevSurfaceMotionBasedSpecularResponsiveIllumination;
    float3 prevDiffuseResponsiveIllumination;
    float2 prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments;
    float  prevSurfaceMotionBasedReflectionHitT;
    float3 prevSurfaceMotionBasedWorldPos;
    float  historyLength;
    float3 debugOut1;

    int surfaceMotionBasedReprojectionFound = loadSurfaceMotionBasedPrevData(ipos.xy,
                                                            currentWorldPos,
                                                            currentNormal,
                                                            currentLinearZ,
                                                            specularIllumination.a,
                                                            prevSurfaceMotionBasedSpecularIllumination,
                                                            prevDiffuseIllumination,
                                                            prevSurfaceMotionBasedSpecularResponsiveIllumination,
                                                            prevDiffuseResponsiveIllumination,
                                                            prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments,
                                                            prevSurfaceMotionBasedWorldPos,
                                                            prevSurfaceMotionBasedReflectionHitT,
                                                            historyLength,
                                                            debugOut1
                                                            );

    // History length is based on surface motion based disocclusion
    historyLength = max(0, floor(historyLength)) + 1.0;
    historyLength = min(100.0, historyLength);

    // Handling history reset if needed
    if (gNeedHistoryReset != 0) historyLength = 1.0;

    // This adjusts the alpha for the case where insufficient history is available.
    // It boosts the temporal accumulation to give the samples equal weights in
    // the beginning.
    float specularAlpha = surfaceMotionBasedReprojectionFound ? max(gSpecularAlpha, 1.0 / historyLength) : 1.0;
    float specularAlphaResponsive = surfaceMotionBasedReprojectionFound ? max(gSpecularResponsiveAlpha, 1.0 / historyLength) : 1.0;

    float diffuseAlpha = surfaceMotionBasedReprojectionFound ? max(gDiffuseAlpha, 1.0 / historyLength) : 1.0;
    float diffuseAlphaResponsive = surfaceMotionBasedReprojectionFound ? max(gDiffuseResponsiveAlpha, 1.0 / historyLength) : 1.0;

    // Temporal accumulation of reflection HitT will happen later in the shader
    float accumulatedReflectionHitT = prevSurfaceMotionBasedReflectionHitT;

    // Calculating surface motion based previous view vector
    float3 prevSurfaceMotionBasedV = normalize(prevSurfaceMotionBasedWorldPos - gPrevCameraPosition);
    float3 currentV = normalize(currentViewVector);
    float prevSurfaceMotionVDotCurrentV = saturate(dot(prevSurfaceMotionBasedV, currentV));

    // Loading specular data based on virtual motion
    float3 prevVirtualMotionBasedSpecularIllumination;
    float3 prevVirtualMotionBasedSpecularResponsiveIllumination;
    float  prevVirtualMotionBasedSpecular2ndMoment;
    float3 prevVirtualMotionBasedNormal;
    float  prevVirtualMotionBasedRoughness;
    float  prevVirtualMotionBasedReflectionHitT;
    float3 debugOut2;

    int virtualMotionBasedReprojectionFound = loadVirtualMotionBasedPrevData(ipos.xy,
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
                                                            prevVirtualMotionBasedReflectionHitT,
                                                            debugOut2);

    // Estimating confidence of virtual motion based specular reprojection,
    // starting with the fact of finding the valid reprojection
    float virtualMotionBasedReprojectionConfidence = virtualMotionBasedReprojectionFound ? 1.0 : 0.0;

    // Adjusting confidence of virtual motion based specular reprojection
    // based on current reflection HitT vs previous reflection HitT:
    // since the HitT is quite noisy, we'll decrease confidence to 0 if difference is quite large: 3x of current hitT value.
    virtualMotionBasedReprojectionConfidence *= 1.0 - saturate(3.0*abs(prevVirtualMotionBasedReflectionHitT - accumulatedReflectionHitT) / (min(accumulatedReflectionHitT, prevVirtualMotionBasedReflectionHitT) + 0.01));

    // Adjusting confidence of virtual motion based specular reprojection
    // based on current unmodified roughness vs previous roughness: if roughness changes, we decrease confidence
    float2 roughnessWeightParams = getRoughnessWeightParams(currentRoughness);
    virtualMotionBasedReprojectionConfidence *= getRoughnessWeight(roughnessWeightParams, prevVirtualMotionBasedRoughness);

    // Adjusting confidence of virtual motion based specular reprojection
    // based on current normal vs previous normal, and current roughness:
    // we decrease confidence if reprojected normal falls outside cone defined by current surface roughness.
    // TODO: increase current roughness based on current normal variance
    float2 normalParams;
    normalParams.x = getSpecularLobeHalfAngle(currentRoughnessModified);
    normalParams.x *= 0.33;
    normalParams.x += 2.5 * 3.141592 / 180.0; // adding 2.5 deg of slack to increase reprojection confidence
    normalParams.y = 1.0;
    virtualMotionBasedReprojectionConfidence *= getSpecularNormalWeight(normalParams, currentNormal, prevVirtualMotionBasedNormal);



    // Adjusting accumulation factors for virtual motion based specular
    float specularAlphaVirtualMotionBased = lerp(1.0, specularAlpha, virtualMotionBasedReprojectionConfidence);
    float specularAlphaResponsiveVirtualMotionBased = lerp(1.0, specularAlphaResponsive, virtualMotionBasedReprojectionConfidence);

    // Accumulating using virtual motion based specular reprojection
    float3 virtualMotionBasedSpecularIllumination = lerp(prevVirtualMotionBasedSpecularIllumination.rgb, specularIllumination.rgb, specularAlphaVirtualMotionBased);
    float3 virtualMotionBasedSpecularResponsiveIllumination = lerp(prevVirtualMotionBasedSpecularResponsiveIllumination.rgb, specularIllumination.rgb, specularAlphaResponsiveVirtualMotionBased);
    float  virtualMotionBasedSpecular2ndMoment = lerp(prevVirtualMotionBasedSpecular2ndMoment, specular2ndMoment, specularAlphaVirtualMotionBased);

    // Estimating confidence of surface motion based specular reprojection,
    // starting with the fact of finding the valid reprojection
    float surfaceMotionBasedReprojectionConfidence = surfaceMotionBasedReprojectionFound ? 1.0 : 0.0;

    // Adjusting confidence of surface motion based specular reprojection
    // based on current roughness vs previous roughness for virtual motion: if roughness changes, we decrease confidence,
    // but not much, by 75% only
    surfaceMotionBasedReprojectionConfidence *= 0.25 + 0.75*getRoughnessWeight(roughnessWeightParams, prevVirtualMotionBasedRoughness);

    // Adjusting confidence of surface motion based specular reprojection
    // based on current view vector vs previous view vector and current roughness
    surfaceMotionBasedReprojectionConfidence *= 1.0 - getSpecularAlphaAdjustment(currentRoughness, prevSurfaceMotionVDotCurrentV);

    // Adjusting accumulation factors for surface motion based specular
    float specularAlphaSurfaceMotionBased = lerp(1.0, specularAlpha, surfaceMotionBasedReprojectionConfidence);
    float specularAlphaResponsiveSurfaceMotionBased = lerp(1.0, specularAlphaResponsive, surfaceMotionBasedReprojectionConfidence);

    // Accumulating using surface motion based specular reprojection
    float3 surfaceMotionBasedSpecularIllumination = lerp(prevSurfaceMotionBasedSpecularIllumination.rgb, specularIllumination.rgb, specularAlphaSurfaceMotionBased);
    float3 surfaceMotionBasedSpecularResponsiveIllumination = lerp(prevSurfaceMotionBasedSpecularResponsiveIllumination.rgb, specularIllumination.rgb, specularAlphaResponsiveSurfaceMotionBased);
    float  surfaceMotionBasedSpecular2ndMoment = lerp(prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments.r, specular2ndMoment, specularAlphaSurfaceMotionBased);

    // Now weighing between virtual and surface motion based specular reprojection
    float virtualVsSurfaceWeight = virtualMotionBasedReprojectionConfidence;

    // Adjusting weighing between virtual motion based and surface motion based reprojection for specular,
    // based on current roughness: virtual motion is perfectly valid for perfect mirrors only
    virtualVsSurfaceWeight *= 1.0 - STL::Math::SmoothStep(0.4, 1.0, currentRoughness);

    //virtualVsSurfaceWeight = 1.0;

    // Calculating overall specular reprojection confidence
    float specularConfidence = lerp(surfaceMotionBasedReprojectionConfidence,
                                    virtualMotionBasedReprojectionConfidence,
                                    virtualVsSurfaceWeight);

    // Calculating alphas for reflection hitT accumulation, fastest is 1.0 if reprojection confidence is 0,
    // slowest is 0.1
    float specularHitTAlphaVirtualMotionBased = lerp(1.0, 0.1, virtualMotionBasedReprojectionConfidence);
    float specularHitTAlphaSurfaceMotionBased = lerp(1.0, 0.1, surfaceMotionBasedReprojectionConfidence);

    // Temporal accumulation of specular hitT
    float surfaceMotionBasedAccumulatedReflectionHitT = lerp(prevSurfaceMotionBasedReflectionHitT, specularIllumination.a, specularHitTAlphaSurfaceMotionBased);
    float virtualMotionBasedAccumulatedReflectionHitT = lerp(prevVirtualMotionBasedReflectionHitT, specularIllumination.a, specularHitTAlphaVirtualMotionBased);

    accumulatedReflectionHitT = virtualMotionBasedAccumulatedReflectionHitT;
                                    /*
                                    lerp(surfaceMotionBasedAccumulatedReflectionHitT,
                                    virtualMotionBasedAccumulatedReflectionHitT,
                                    virtualVsSurfaceWeight);
                                    */

    accumulatedReflectionHitT = min(1000.0, max(0.01, accumulatedReflectionHitT));

    // Temporal accumulation of specular illumination
    float3 accumulatedSpecularIllumination = lerp(surfaceMotionBasedSpecularIllumination,
                                                    virtualMotionBasedSpecularIllumination,
                                                    virtualVsSurfaceWeight);

    float3 accumulatedSpecularIlluminationResponsive = lerp(surfaceMotionBasedSpecularResponsiveIllumination,
                                                            virtualMotionBasedSpecularResponsiveIllumination,
                                                            virtualVsSurfaceWeight);
    // Temporal accumulation of specular moments
    float accumulatedSpecular2ndMoment =     lerp(surfaceMotionBasedSpecular2ndMoment,
                                             virtualMotionBasedSpecular2ndMoment,
                                             virtualVsSurfaceWeight);

    // Artificially increasing specular variance for pixels with low reprojection confidence
    accumulatedSpecular2ndMoment *= 1.0 + gSpecularVarianceBoost * (1.0 - specularConfidence);
    // If zero specular sample (color = 0), artificially adding variance for pixels with low reprojection confidence
    if(accumulatedSpecular2ndMoment == 0) accumulatedSpecular2ndMoment = gSpecularVarianceBoost * (1.0 - specularConfidence);

    // Temporal accumulation of diffuse 2nd moment
    float accumulatedDiffuse2ndMoment = lerp(prevSurfaceMotionBasedSpecularAndDiffuse2ndMoments.g, diffuse2ndMoment, diffuseAlpha);

    // Temporal accumulation of diffuse illumination
    float3 accumulatedDiffuseIllumination = lerp(prevDiffuseIllumination.rgb, diffuseIllumination.rgb, diffuseAlpha);
    float3 accumulatedDiffuseIlluminationResponsive = lerp(prevDiffuseResponsiveIllumination.rgb, diffuseIllumination.rgb, diffuseAlphaResponsive);

    // Write out the results
    gOutSpecularAndDiffuseIlluminationLogLuv[ipos] = PackSpecularAndDiffuseToLogLuvUint2(accumulatedSpecularIllumination, accumulatedDiffuseIllumination);
    gOutSpecularAndDiffuseIlluminationResponsiveLogLuv[ipos] = PackSpecularAndDiffuseToLogLuvUint2(accumulatedSpecularIlluminationResponsive, accumulatedDiffuseIlluminationResponsive);

    gOutSpecularAndDiffuse2ndMoments[ipos] = float2(accumulatedSpecular2ndMoment, accumulatedDiffuse2ndMoment);

    gOutReflectionHitT[ipos] = accumulatedReflectionHitT;

    gOutHistoryLength[ipos] = historyLength;

    gOutSpecularReprojectionConfidence[ipos] = specularConfidence;
}
