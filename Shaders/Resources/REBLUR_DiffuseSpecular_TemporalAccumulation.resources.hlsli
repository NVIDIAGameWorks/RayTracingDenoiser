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
    NRD_CONSTANT( float4x4, gWorldToViewPrev )
    NRD_CONSTANT( float4x4, gWorldToClipPrev )
    NRD_CONSTANT( float4x4, gWorldToClip )
    NRD_CONSTANT( float4x4, gWorldPrevToWorld )
    NRD_CONSTANT( float4, gFrustumPrev )
    NRD_CONSTANT( float3, gCameraDelta )
    NRD_CONSTANT( float, gDisocclusionThreshold )
    NRD_CONSTANT( float, gDisocclusionThresholdAlternate )
    NRD_CONSTANT( float, gCheckerboardResolveAccumSpeed )
    NRD_CONSTANT( uint, gDiffCheckerboard )
    NRD_CONSTANT( uint, gSpecCheckerboard )
    NRD_CONSTANT( uint, gIsPrepassEnabled )
    NRD_CONSTANT( uint, gHasHistoryConfidence )
    NRD_CONSTANT( uint, gHasDisocclusionThresholdMix )
NRD_CONSTANTS_END

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Tiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_Mv, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Prev_ViewZ, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Prev_Normal_Roughness, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_InternalData, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_DisocclusionThresholdMix, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Diff_Confidence, t, 8 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Spec_Confidence, t, 9 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Diff, t, 10 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Spec, t, 11 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Diff_History, t, 12 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Spec_History, t, 13 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_FAST_TYPE>, gIn_DiffFast_History, t, 14 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_FAST_TYPE>, gIn_SpecFast_History, t, 15 )
        #ifndef REBLUR_OCCLUSION
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Spec_HitDistForTracking, t, 16 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_DiffSh, t, 17 )
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_SpecSh, t, 18 )
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_DiffSh_History, t, 19 )
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_SpecSh_History, t, 20 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_TYPE>, gOut_Diff, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_TYPE>, gOut_Spec, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Data1, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_FAST_TYPE>, gOut_DiffFast, u, 3 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_FAST_TYPE>, gOut_SpecFast, u, 4 )
        #ifndef REBLUR_OCCLUSION
            NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Data2, u, 5 )
        #endif
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_SH_TYPE>, gOut_DiffSh, u, 6 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_SH_TYPE>, gOut_SpecSh, u, 7 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#elif( defined REBLUR_DIFFUSE )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Tiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_Mv, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Prev_ViewZ, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Prev_Normal_Roughness, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_InternalData, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_DisocclusionThresholdMix, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Diff_Confidence, t, 8 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Diff, t, 9 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Diff_History, t, 10 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_FAST_TYPE>, gIn_DiffFast_History, t, 11 )
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_DiffSh, t, 12 )
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_DiffSh_History, t, 13 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_TYPE>, gOut_Diff, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Data1, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_FAST_TYPE>, gOut_DiffFast, u, 2 )
        #ifndef REBLUR_OCCLUSION
            NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Data2, u, 3 )
        #endif
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_SH_TYPE>, gOut_DiffSh, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#else

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Tiles, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_Mv, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Prev_ViewZ, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Prev_Normal_Roughness, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_InternalData, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_DisocclusionThresholdMix, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Spec_Confidence, t, 8 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Spec, t, 9 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_TYPE>, gIn_Spec_History, t, 10 )
        NRD_INPUT_TEXTURE( Texture2D<REBLUR_FAST_TYPE>, gIn_SpecFast_History, t, 11 )
        #ifndef REBLUR_OCCLUSION
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Spec_HitDistForTracking, t, 12 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_SpecSh, t, 13 )
            NRD_INPUT_TEXTURE( Texture2D<REBLUR_SH_TYPE>, gIn_SpecSh_History, t, 14 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_TYPE>, gOut_Spec, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Data1, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_FAST_TYPE>, gOut_SpecFast, u, 2 )
        #ifndef REBLUR_OCCLUSION
            NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Data2, u, 3 )
        #endif
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<REBLUR_SH_TYPE>, gOut_SpecSh, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#endif

// Macro magic
#define GROUP_X 8
#define GROUP_Y 8
