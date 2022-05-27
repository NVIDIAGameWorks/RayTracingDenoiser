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
NRD_CONSTANTS_END

#ifdef REBLUR_OCCLUSION
    #define DATA_TYPE float2
#else
    #define DATA_TYPE float4
#endif

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Diff, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Spec, t, 1 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Diff_x2, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Spec_x2, u, 1 )
        #ifndef REBLUR_OCCLUSION
            NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_ScaledViewZ_x2, u, 2 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Diff, u, 3 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Spec, u, 4 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gIn_ScaledViewZ, u, 5 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#elif( defined REBLUR_DIFFUSE )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Diff, t, 0 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Diff_x2, u, 0 )
        #ifndef REBLUR_OCCLUSION
            NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_ScaledViewZ_x2, u, 1 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Diff, u, 2 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gIn_ScaledViewZ, u, 3 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#else

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Spec, t, 0 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Spec_x2, u, 0 )
        #ifndef REBLUR_OCCLUSION
            NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_ScaledViewZ_x2, u, 1 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Spec, u, 2 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gIn_ScaledViewZ, u, 3 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#endif

// Macro magic
#if( REBLUR_USE_5X5_ANTI_FIREFLY == 1 )
    #define NRD_USE_BORDER_2
#endif
