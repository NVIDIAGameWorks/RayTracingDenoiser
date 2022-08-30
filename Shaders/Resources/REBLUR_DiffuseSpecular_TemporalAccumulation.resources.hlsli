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
    NRD_CONSTANT( float4, gCameraDelta )
    NRD_CONSTANT( float2, gMotionVectorScale )
    NRD_CONSTANT( float, gCheckerboardResolveAccumSpeed )
    NRD_CONSTANT( float, gDisocclusionThreshold )
    NRD_CONSTANT( uint, gDiffCheckerboard )
    NRD_CONSTANT( uint, gSpecCheckerboard )
    NRD_CONSTANT( uint, gIsPrepassEnabled )
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
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Prev_ViewZ, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Prev_Normal_Roughness, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_AccumSpeeds_MaterialID, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Spec_MinHitDist, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Diff_Confidence, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Spec_Confidence, t, 8 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Diff, t, 9 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Spec, t, 10 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Diff_History, t, 11 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Spec_History, t, 12 )
        #ifndef REBLUR_OCCLUSION
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_DiffFast_History, t, 13 )
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_SpecFast_History, t, 14 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh, t, 15 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh, t, 16 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh_History, t, 17 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh_History, t, 18 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Diff, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Spec, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Data1, u, 2 )
        #ifndef REBLUR_OCCLUSION
            NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_DiffFast, u, 3 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_SpecFast, u, 4 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Data2, u, 5 )
        #endif
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffSh, u, 6 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecSh, u, 7 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#elif( defined REBLUR_DIFFUSE )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Prev_ViewZ, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Prev_Normal_Roughness, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_AccumSpeeds_MaterialID, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Diff_Confidence, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Diff, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Diff_History, t, 8 )
        #ifndef REBLUR_OCCLUSION
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_DiffFast_History, t, 9 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh, t, 10 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_DiffSh_History, t, 11 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Diff, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Data1, u, 1 )
        #ifndef REBLUR_OCCLUSION
            NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_DiffFast, u, 2 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Data2, u, 3 )
        #endif
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffSh, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#else

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Prev_ViewZ, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Prev_Normal_Roughness, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<uint>, gIn_Prev_AccumSpeeds_MaterialID, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Spec_MinHitDist, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_Spec_Confidence, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Spec, t, 8 )
        NRD_INPUT_TEXTURE( Texture2D<DATA_TYPE>, gIn_Spec_History, t, 9 )
        #ifndef REBLUR_OCCLUSION
            NRD_INPUT_TEXTURE( Texture2D<float>, gIn_SpecFast_History, t, 10 )
        #endif
        #ifdef REBLUR_SH
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh, t, 11 )
            NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_SpecSh_History, t, 12 )
        #endif
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<DATA_TYPE>, gOut_Spec, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Data1, u, 1 )
        #ifndef REBLUR_OCCLUSION
            NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_SpecFast, u, 2 )
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Data2, u, 3 )
        #endif
        #ifdef REBLUR_SH
            NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecSh, u, 4 )
        #endif
    NRD_OUTPUT_TEXTURE_END

#endif

// Macro magic
#define NRD_CTA_8X8
