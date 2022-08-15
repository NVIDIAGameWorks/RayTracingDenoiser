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
    NRD_CONSTANT( float3, gSpecLobeTrimmingParams )
    NRD_CONSTANT( float, gBlurRadiusScale )
NRD_CONSTANTS_END

#ifdef REBLUR_OCCLUSION
    #define DATA_TYPE_IN float2
    #define DATA_TYPE_OUT float
#else
    #define DATA_TYPE_IN float4
    #define DATA_TYPE_OUT float4
#endif

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Data1, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE_IN>, gIn_Diff, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE_IN>, gIn_Spec, t, 3 )
        #ifndef REBLUR_OCCLUSION
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 4 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh, t, 5 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh, t, 6 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Normal_Roughness, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE_OUT>, gOut_Diff, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE_OUT>, gOut_Spec, u, 2 )
        #ifdef REBLUR_NO_TEMPORAL_STABILIZATION
            NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_AccumSpeeds_MaterialID, u, 3 )
            #ifdef REBLUR_SH
                NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffSh, u, 4 )
                NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecSh, u, 5 )
            #endif
        #else
            #ifdef REBLUR_SH
                NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffSh, u, 3 )
                NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecSh, u, 4 )
            #endif
        #endif
    NRD_OUTPUT_TEXTURE_END

#elif( defined REBLUR_DIFFUSE )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Data1, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE_IN>, gIn_Diff, t, 2 )
        #ifndef REBLUR_OCCLUSION
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 3 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh, t, 4 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Normal_Roughness, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE_OUT>, gOut_Diff, u, 1 )
        #ifdef REBLUR_NO_TEMPORAL_STABILIZATION
            NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_AccumSpeeds_MaterialID, u, 2 )
            #ifdef REBLUR_SH
                NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffSh, u, 3 )
            #endif
        #else
            #ifdef REBLUR_SH
                NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffSh, u, 2 )
            #endif
        #endif
    NRD_OUTPUT_TEXTURE_END

#else

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Data1, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE_IN>, gIn_Spec, t, 2 )
        #ifndef REBLUR_OCCLUSION
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 3 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh, t, 4 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Normal_Roughness, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE_OUT>, gOut_Spec, u, 1 )
        #ifdef REBLUR_NO_TEMPORAL_STABILIZATION
            NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_AccumSpeeds_MaterialID, u, 2 )
            #ifdef REBLUR_SH
                NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecSh, u, 3 )
            #endif
        #else
            #ifdef REBLUR_SH
                NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecSh, u, 2 )
            #endif
        #endif
    NRD_OUTPUT_TEXTURE_END

#endif

// Macro magic
#define NRD_CTA_8X8
