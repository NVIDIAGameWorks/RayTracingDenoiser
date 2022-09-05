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
    NRD_CONSTANT( float4x4, gWorldToClip )
    NRD_CONSTANT( float4x4, gWorldToClipPrev )
    NRD_CONSTANT( float4, gAntilagMinMaxThreshold )
    NRD_CONSTANT( float3, gCameraDelta )
    NRD_CONSTANT( float, gStabilizationStrength )
    NRD_CONSTANT( float2, gAntilagSigmaScale )
    NRD_CONSTANT( float2, gMotionVectorScale )
NRD_CONSTANTS_END

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Data1, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Data2, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff_StabilizedHistory, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec_StabilizedHistory, t, 8 )
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh, t, 9 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh, t, 10 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh_StabilizedHistory, t, 11 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh_StabilizedHistory, t, 12 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_InternalData, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 2 )
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffSh, u, 3 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecSh, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#elif( defined REBLUR_DIFFUSE )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Data1, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Data2, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff_StabilizedHistory, t, 6 )
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh, t, 7 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh_StabilizedHistory, t, 8 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_InternalData, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 1 )
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffSh, u, 2 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#else

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Data1, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Data2, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec_StabilizedHistory, t, 6 )
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh, t, 7 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh_StabilizedHistory, t, 8 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_InternalData, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 1 )
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecSh, u, 2 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#endif

// Macro magic
#define NRD_CTA_8X8
