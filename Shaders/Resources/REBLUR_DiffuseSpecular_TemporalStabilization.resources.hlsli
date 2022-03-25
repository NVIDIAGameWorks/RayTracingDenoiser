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
    NRD_CONSTANT( float4x4, gWorldToClipPrev )
    NRD_CONSTANT( float4x4, gViewToWorld )
    NRD_CONSTANT( float4, gCameraDelta )
    NRD_CONSTANT( float4, gAntilagMinMaxThreshold )
    NRD_CONSTANT( float2, gAntilagSigmaScale )
    NRD_CONSTANT( float2, gMotionVectorScale )
    NRD_CONSTANT( float, gStabilizationStrength )
NRD_CONSTANTS_END

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_InternalData, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Error, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 6 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_HistoryStabilized_Diff, t, 7 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_HistoryStabilized_Spec, t, 8 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_ViewZ_DiffAccumSpeed, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Normal_SpecAccumSpeed, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 3 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffCopy, u, 4 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecCopy, u, 5 )
    NRD_OUTPUT_TEXTURE_END

#elif( defined REBLUR_DIFFUSE )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_InternalData, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Error, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Diff, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_HistoryStabilized_Diff, t, 6 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_ViewZ_DiffAccumSpeed, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Normal_SpecAccumSpeed, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Diff, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_DiffCopy, u, 3 )
    NRD_OUTPUT_TEXTURE_END

#else

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float3>, gIn_ObjectMotion, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_InternalData, t, 3 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Error, t, 4 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Spec, t, 5 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_HistoryStabilized_Spec, t, 6 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_ViewZ_DiffAccumSpeed, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Normal_SpecAccumSpeed, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Spec, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_SpecCopy, u, 3 )
    NRD_OUTPUT_TEXTURE_END

#endif

// Macro magic
#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
    #define NRD_CTA_8X8
#endif
