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
    REBLUR_SHARED_CB_DATA
    NRD_CONSTANT( float4, gRotator )
    NRD_CONSTANT( float4, gSpecLobeTrimmingParams )
    NRD_CONSTANT( uint, gDiffCheckerboard )
    NRD_CONSTANT( uint, gSpecCheckerboard )
NRD_CONSTANTS_END

#ifdef REBLUR_OCCLUSION
    #define DATA_TYPE float
#else
    #define DATA_TYPE float4
#endif

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Diff, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Spec, t, 3 )
        #ifdef REBLUR_HAS_DIRECTION_PDF
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff_DirectionPdf, t, 4 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec_DirectionPdf, t, 5 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh, t, 4 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh, t, 5 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Diff, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Spec, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Spec_MinHitDist, u, 2 )
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffSh, u, 3 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecSh, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#elif( defined REBLUR_DIFFUSE )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Diff, t, 2 )
        #ifdef REBLUR_HAS_DIRECTION_PDF
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff_DirectionPdf, t, 3 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh, t, 3 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Diff, u, 0 )
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffSh, u, 1 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#else

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Spec, t, 2 )
        #ifdef REBLUR_HAS_DIRECTION_PDF
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec_DirectionPdf, t, 3 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh, t, 3 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Spec, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Spec_MinHitDist, u, 1 )
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecSh, u, 2 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#endif
