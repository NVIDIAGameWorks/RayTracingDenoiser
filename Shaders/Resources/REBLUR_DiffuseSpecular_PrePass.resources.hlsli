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
    NRD_CONSTANT( uint, gDiffCheckerboard )
    NRD_CONSTANT( uint, gSpecCheckerboard )
NRD_CONSTANTS_END

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Tiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Diff, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Spec, t, 4 )
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_DiffSh, t, 5 )
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_SpecSh, t, 6 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_TYPE>, gOut_Diff, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_TYPE>, gOut_Spec, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Spec_HitDistForTracking, u, 2 )
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_SH_TYPE>, gOut_DiffSh, u, 3 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_SH_TYPE>, gOut_SpecSh, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#elif( defined REBLUR_DIFFUSE )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Tiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Diff, t, 3 )
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_DiffSh, t, 4 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_TYPE>, gOut_Diff, u, 0 )
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_SH_TYPE>, gOut_DiffSh, u, 1 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#else

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Tiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Spec, t, 3 )
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_SpecSh, t, 4 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_TYPE>, gOut_Spec, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Spec_HitDistForTracking, u, 1 )
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_SH_TYPE>, gOut_SpecSh, u, 2 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#endif
