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
        NRD_CONSTANT( uint2, gResourceSize )
        NRD_CONSTANT( uint, gHistoryThreshold )
        NRD_CONSTANT( float, gSpecularPhiLuminance )
        NRD_CONSTANT( float, gDiffusePhiLuminance )
        NRD_CONSTANT( float, gMaxDiffuseLuminanceRelativeDifference )
        NRD_CONSTANT( float, gMaxSpecularLuminanceRelativeDifference )
        NRD_CONSTANT( float, gDepthThreshold )
        NRD_CONSTANT( float, gDiffuseLobeAngleFraction )
        NRD_CONSTANT( float, gRoughnessFraction )
        NRD_CONSTANT( float, gSpecularLobeAngleFraction )
        NRD_CONSTANT( float, gSpecularLobeAngleSlack )
        NRD_CONSTANT( uint, gStepSize )
        NRD_CONSTANT( uint, gRoughnessEdgeStoppingEnabled )
        NRD_CONSTANT( float, gRoughnessEdgeStoppingRelaxation )
        NRD_CONSTANT( float, gNormalEdgeStoppingRelaxation )
        NRD_CONSTANT( float, gLuminanceEdgeStoppingRelaxation )
        NRD_CONSTANT( uint, gUseConfidenceInputs )
        NRD_CONSTANT( float, gConfidenceDrivenRelaxationMultiplier )
        NRD_CONSTANT( float, gConfidenceDrivenLuminanceEdgeStoppingRelaxation )
        NRD_CONSTANT( float, gConfidenceDrivenNormalEdgeStoppingRelaxation )
    NRD_CONSTANTS_END

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gTiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIlluminationAnd2ndMoment, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIlluminationAnd2ndMoment, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gHistoryLength, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gSpecularReprojectionConfidence, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gNormalRoughness, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gViewZ, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gSpecConfidence, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gDiffConfidence, t, 8 )
        #ifdef RELAX_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularSH1, t, 9 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseSH1, t, 10 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIlluminationAndVariance, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIlluminationAndVariance, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutNormalRoughness, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutMaterialID, u, 3 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutViewZ, u, 4 )
        #ifdef RELAX_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularSH1, u, 5 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseSH1, u, 6 )
        #endif

    NRD_OUTPUT_TEXTURE_END

#elif( defined RELAX_DIFFUSE )

    NRD_CONSTANTS_START
        RELAX_SHARED_CB_DATA
        NRD_CONSTANT( uint2, gResourceSize )
        NRD_CONSTANT( uint, gHistoryThreshold )
        NRD_CONSTANT( float, gDiffusePhiLuminance )
        NRD_CONSTANT( float, gMaxDiffuseLuminanceRelativeDifference )
        NRD_CONSTANT( float, gDepthThreshold )
        NRD_CONSTANT( float, gDiffuseLobeAngleFraction )
        NRD_CONSTANT( uint, gStepSize )
        NRD_CONSTANT( uint, gUseConfidenceInputs )
        NRD_CONSTANT( float, gConfidenceDrivenRelaxationMultiplier )
        NRD_CONSTANT( float, gConfidenceDrivenLuminanceEdgeStoppingRelaxation )
        NRD_CONSTANT( float, gConfidenceDrivenNormalEdgeStoppingRelaxation )
    NRD_CONSTANTS_END

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gTiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIlluminationAnd2ndMoment, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gHistoryLength, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gNormalRoughness, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gViewZ, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gDiffConfidence, t, 5 )
        #ifdef RELAX_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseSH1, t, 6 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIlluminationAndVariance, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutNormalRoughness, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutMaterialID, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutViewZ, u, 3 )
        #ifdef RELAX_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseSH1, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#elif( defined RELAX_SPECULAR )

    NRD_CONSTANTS_START
        RELAX_SHARED_CB_DATA
        NRD_CONSTANT( uint2, gResourceSize )
        NRD_CONSTANT( uint, gHistoryThreshold )
        NRD_CONSTANT( float, gSpecularPhiLuminance )
        NRD_CONSTANT( float, gMaxSpecularLuminanceRelativeDifference )
        NRD_CONSTANT( float, gDepthThreshold )
        NRD_CONSTANT( float, gDiffuseLobeAngleFraction )
        NRD_CONSTANT( float, gRoughnessFraction )
        NRD_CONSTANT( float, gSpecularLobeAngleFraction )
        NRD_CONSTANT( float, gSpecularLobeAngleSlack )
        NRD_CONSTANT( uint, gStepSize )
        NRD_CONSTANT( uint, gRoughnessEdgeStoppingEnabled )
        NRD_CONSTANT( float, gRoughnessEdgeStoppingRelaxation )
        NRD_CONSTANT( float, gNormalEdgeStoppingRelaxation )
        NRD_CONSTANT( float, gLuminanceEdgeStoppingRelaxation )
        NRD_CONSTANT( uint, gUseConfidenceInputs )
        NRD_CONSTANT( float, gConfidenceDrivenRelaxationMultiplier )
        NRD_CONSTANT( float, gConfidenceDrivenLuminanceEdgeStoppingRelaxation )
        NRD_CONSTANT( float, gConfidenceDrivenNormalEdgeStoppingRelaxation )
    NRD_CONSTANTS_END

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gTiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIlluminationAnd2ndMoment, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gHistoryLength, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gSpecularReprojectionConfidence, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gNormalRoughness, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gViewZ, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gSpecConfidence, t, 6 )
        #ifdef RELAX_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularSH1, t, 7 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIlluminationAndVariance, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutNormalRoughness, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutMaterialID, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutViewZ, u, 3 )
        #ifdef RELAX_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularSH1, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#endif

// Macro magic
#define GROUP_X 8
#define GROUP_Y 8
#define NRD_USE_BORDER_2
