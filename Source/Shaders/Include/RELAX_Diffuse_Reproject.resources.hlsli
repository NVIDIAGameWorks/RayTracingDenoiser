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
    NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIllumination, t, 0 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gMotion, t, 1 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gNormalRoughness, t, 2 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gViewZ, t, 3 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevDiffuseIlluminationResponsive, t, 4 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevDiffuseIllumination, t, 5 ) \
    NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevNormalRoughness, t, 6 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gPrevViewZ, t, 7 ) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gPrevDiffuseHistoryLength, t, 8) \
    NRD_INPUT_TEXTURE( Texture2D<float>, gDiffConfidence, t, 9)

#define NRD_DECLARE_OUTPUT_TEXTURES \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIllumination, u, 0 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIlluminationResponsive, u, 1 ) \
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutDiffuseHistoryLength, u, 2 )


#define NRD_DECLARE_CONSTANTS \
    NRD_CONSTANTS_START \
        RELAX_SHARED_CB_DATA \
        NRD_CONSTANT( float, gDiffuseMaxAccumulatedFrameNum ) \
        NRD_CONSTANT( float, gDiffuseMaxFastAccumulatedFrameNum ) \
        NRD_CONSTANT( uint, gDiffCheckerboard ) \
        NRD_CONSTANT( float, gDisocclusionDepthThreshold ) \
        NRD_CONSTANT( uint, gSkipReprojectionTestWithoutMotion ) \
        NRD_CONSTANT( uint, gResetHistory ) \
        NRD_CONSTANT( float, gRejectDiffuseHistoryNormalThreshold ) \
        NRD_CONSTANT( uint, gUseConfidenceInputs ) \
    NRD_CONSTANTS_END

#define NRD_DECLARE_SAMPLERS \
    NRD_COMMON_SAMPLERS
