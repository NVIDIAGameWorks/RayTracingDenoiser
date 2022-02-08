/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "RELAX_Config.hlsli"

#define NRD_DECLARE_INPUT_TEXTURES \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIllumination, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIllumination, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gMotion, t, 2 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gNormalRoughness, t, 3 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gViewZ, t, 4 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevSpecularIlluminationResponsive, t, 5 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevDiffuseIlluminationResponsive, t, 6 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevSpecularIllumination, t, 7 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevDiffuseIllumination, t, 8 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevNormalRoughness, t, 9 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gPrevViewZ, t, 10 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gPrevReflectionHitT, t, 11 ) \
    NRD_INPUT_TEXTURE( Texture2D<float2>, gPrevSpecularAndDiffuseHistoryLength, t, 12) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gSpecConfidence, t, 13 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gDiffConfidence, t, 14 )

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIllumination, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIllumination, u, 1 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIlluminationResponsive, u, 2 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIlluminationResponsive, u, 3 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutReflectionHitT, u, 4 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOutSpecularAndDiffuseHistoryLength, u, 5 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutSpecularReprojectionConfidence, u, 6 )

#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        RELAX_SHARED_CB_DATA \
        NRD_CONSTANT( float, gSpecularMaxAccumulatedFrameNum ) \
        NRD_CONSTANT( float, gSpecularMaxFastAccumulatedFrameNum ) \
        NRD_CONSTANT( float, gDiffuseMaxAccumulatedFrameNum ) \
        NRD_CONSTANT( float, gDiffuseMaxFastAccumulatedFrameNum ) \
        NRD_CONSTANT( uint, gDiffCheckerboard ) \
        NRD_CONSTANT( uint, gSpecCheckerboard ) \
        NRD_CONSTANT( float, gDisocclusionDepthThreshold ) \
        NRD_CONSTANT( float, gSpecularVarianceBoost ) \
        NRD_CONSTANT( uint, gVirtualHistoryClampingEnabled ) \
        NRD_CONSTANT( uint, gSkipReprojectionTestWithoutMotion ) \
        NRD_CONSTANT( uint, gResetHistory ) \
        NRD_CONSTANT( float, gRejectDiffuseHistoryNormalThreshold ) \
        NRD_CONSTANT( uint, gUseConfidenceInputs ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
