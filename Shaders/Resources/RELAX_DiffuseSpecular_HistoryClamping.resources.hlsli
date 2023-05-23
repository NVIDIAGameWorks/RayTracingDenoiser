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

NRD_CONSTANTS_START
    RELAX_SHARED_CB_DATA
    NRD_CONSTANT( float, gColorBoxSigmaScale )
    NRD_CONSTANT( float, gHistoryFixFrameNum )
    #if( defined RELAX_SPECULAR )
        NRD_CONSTANT( uint, gSpecFastHistory )
    #endif
    #if( defined RELAX_DIFFUSE )
        NRD_CONSTANT( uint, gDiffFastHistory )
    #endif
NRD_CONSTANTS_END

#if( defined RELAX_DIFFUSE && defined RELAX_SPECULAR )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gTiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIllumination, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIllumination, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIlluminationResponsive, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIlluminationResponsive, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gHistoryLength, t, 5 )
        #ifdef RELAX_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularSH1, t, 6 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseSH1, t, 7 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularResponsiveSH1, t, 8 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseResponsiveSH1, t, 9 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIllumination, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIllumination, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIlluminationResponsive, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIlluminationResponsive, u, 3 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutHistoryLength, u, 4 )
        #ifdef RELAX_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularSH1, u, 5 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseSH1, u, 6 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularResponsiveSH1, u, 7 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseResponsiveSH1, u, 8 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#elif( defined RELAX_DIFFUSE )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gTiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIllumination, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseIlluminationResponsive, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gHistoryLength, t, 3 )
        #ifdef RELAX_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseSH1, t, 4 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gDiffuseResponsiveSH1, t, 5 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIllumination, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseIlluminationResponsive, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutHistoryLength, u, 2 )
        #ifdef RELAX_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseSH1, u, 3 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutDiffuseResponsiveSH1, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#elif( defined RELAX_SPECULAR )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gTiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIllumination, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularIlluminationResponsive, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gHistoryLength, t, 3 )
        #ifdef RELAX_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularSH1, t, 4 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gSpecularResponsiveSH1, t, 5 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIllumination, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularIlluminationResponsive, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOutHistoryLength, u, 2 )
        #ifdef RELAX_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularSH1, u, 3 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOutSpecularResponsiveSH1, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#endif

// Macro magic
#define GROUP_X 8
#define GROUP_Y 8
#define NRD_USE_BORDER_2
