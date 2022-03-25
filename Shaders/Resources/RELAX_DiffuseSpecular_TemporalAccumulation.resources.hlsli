/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

NRD_SAMPLER_START
    NRD_SAMPLER( SamplerState, gNearestClamp, s, 0 )
    NRD_SAMPLER( SamplerState, gNearestMirror, s, 1 )
    NRD_SAMPLER( SamplerState, gLinearClamp, s, 2 )
    NRD_SAMPLER( SamplerState, gLinearMirror, s, 3 )
NRD_SAMPLER_END

#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )

    NRD_CONSTANTS_START
        RELAX_SHARED_CB_DATA
        NRD_CONSTANT( float, gSpecularMaxAccumulatedFrameNum )
        NRD_CONSTANT( float, gSpecularMaxFastAccumulatedFrameNum )
        NRD_CONSTANT( float, gDiffuseMaxAccumulatedFrameNum )
        NRD_CONSTANT( float, gDiffuseMaxFastAccumulatedFrameNum )
        NRD_CONSTANT( uint, gDiffCheckerboard )
        NRD_CONSTANT( uint, gSpecCheckerboard )
        NRD_CONSTANT( float, gDisocclusionDepthThreshold )
        NRD_CONSTANT( float, gRoughnessFraction )
        NRD_CONSTANT( float, gSpecularVarianceBoost )
        NRD_CONSTANT( uint, gVirtualHistoryClampingEnabled )
        NRD_CONSTANT( uint, gSkipReprojectionTestWithoutMotion )
        NRD_CONSTANT( uint, gResetHistory )
        NRD_CONSTANT( float, gRejectDiffuseHistoryNormalThreshold )
        NRD_CONSTANT( uint, gUseConfidenceInputs )
    NRD_CONSTANTS_END

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIllumination, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIllumination, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gMotion, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gNormalRoughness, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gViewZ, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevSpecularIlluminationResponsive, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevDiffuseIlluminationResponsive, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevSpecularIllumination, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevDiffuseIllumination, t, 8 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevNormalRoughness, t, 9 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gPrevViewZ, t, 10 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gPrevReflectionHitT, t, 11 )
        NRD_INPUT_TEXTURE( Texture2D<float2>, gPrevHistoryLength, t, 12 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gPrevMaterialID, t, 13 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gSpecConfidence, t, 14 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gDiffConfidence, t, 15 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIllumination, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIllumination, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIlluminationResponsive, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIlluminationResponsive, u, 3 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutReflectionHitT, u, 4 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float2>, gOutHistoryLength, u, 5 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutSpecularReprojectionConfidence, u, 6 )
    NRD_OUTPUT_TEXTURE_END

#elif( defined RELAX_DIFFUSE )

    NRD_CONSTANTS_START
        RELAX_SHARED_CB_DATA
        NRD_CONSTANT( float, gDiffuseMaxAccumulatedFrameNum )
        NRD_CONSTANT( float, gDiffuseMaxFastAccumulatedFrameNum )
        NRD_CONSTANT( uint, gDiffCheckerboard )
        NRD_CONSTANT( float, gDisocclusionDepthThreshold )
        NRD_CONSTANT( uint, gSkipReprojectionTestWithoutMotion )
        NRD_CONSTANT( uint, gResetHistory )
        NRD_CONSTANT( float, gRejectDiffuseHistoryNormalThreshold )
        NRD_CONSTANT( uint, gUseConfidenceInputs )
    NRD_CONSTANTS_END

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIllumination, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gMotion, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gNormalRoughness, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gViewZ, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevDiffuseIlluminationResponsive, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevDiffuseIllumination, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevNormalRoughness, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gPrevViewZ, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gPrevHistoryLength, t, 8 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gPrevMaterialID, t, 9 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gDiffConfidence, t, 10 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIllumination, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIlluminationResponsive, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutHistoryLength, u, 2 )
    NRD_OUTPUT_TEXTURE_END

#elif( defined RELAX_SPECULAR )

    NRD_CONSTANTS_START
        RELAX_SHARED_CB_DATA
        NRD_CONSTANT( float, gSpecularMaxAccumulatedFrameNum )
        NRD_CONSTANT( float, gSpecularMaxFastAccumulatedFrameNum )
        NRD_CONSTANT( uint, gSpecCheckerboard )
        NRD_CONSTANT( float, gDisocclusionDepthThreshold )
        NRD_CONSTANT( float, gRoughnessFraction )
        NRD_CONSTANT( float, gSpecularVarianceBoost )
        NRD_CONSTANT( uint, gVirtualHistoryClampingEnabled )
        NRD_CONSTANT( uint, gSkipReprojectionTestWithoutMotion )
        NRD_CONSTANT( uint, gResetHistory )
        NRD_CONSTANT( uint, gUseConfidenceInputs )
    NRD_CONSTANTS_END

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIllumination, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gMotion, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gNormalRoughness, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gViewZ, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevSpecularIlluminationResponsive, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevSpecularIllumination, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gPrevNormalRoughness, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gPrevViewZ, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gPrevReflectionHitT, t, 8 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gPrevHistoryLength, t, 9 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gPrevMaterialID, t, 10 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gSpecConfidence, t, 11 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIllumination, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIlluminationResponsive, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutReflectionHitT, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutHistoryLength, u, 3 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutSpecularReprojectionConfidence, u, 4 )
    NRD_OUTPUT_TEXTURE_END

#endif

// Macro magic
#define NRD_CTA_8X8
#define NRD_USE_BORDER_2
