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
    NRD_CONSTANT( float4x4, gWorldToView )
    NRD_CONSTANT( float4, gRotator )
    NRD_CONSTANT( float3, gSpecLobeTrimmingParams )
    NRD_CONSTANT( float, gBlurRadiusScale )
NRD_CONSTANTS_END

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_InternalData, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_Diff, t, 2 )
        NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_Spec, t, 3 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<unorm float4>, gInOut_Error, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_ViewZ_DiffAccumSpeed, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Normal_SpecAccumSpeed, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Roughness, u, 3 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Diff, u, 4 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Spec, u, 5 )
    NRD_OUTPUT_TEXTURE_END

#elif( defined REBLUR_DIFFUSE )

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_InternalData, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_Diff, t, 2 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<unorm float4>, gInOut_Error, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_ViewZ_DiffAccumSpeed, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Normal_SpecAccumSpeed, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Diff, u, 3 )
    NRD_OUTPUT_TEXTURE_END

#else

    NRD_INPUT_TEXTURE_START
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_Normal_Roughness, t, 0 )
        NRD_INPUT_TEXTURE( Texture2D<float4>, gIn_InternalData, t, 1 )
        NRD_INPUT_TEXTURE( Texture2D<float2>, gIn_Spec, t, 2 )
    NRD_INPUT_TEXTURE_END

    NRD_OUTPUT_TEXTURE_START
        NRD_OUTPUT_TEXTURE( RWTexture2D<unorm float4>, gInOut_Error, u, 0 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_ViewZ_DiffAccumSpeed, u, 1 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<uint>, gOut_Normal_SpecAccumSpeed, u, 2 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Roughness, u, 3 )
        NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Spec, u, 4 )
    NRD_OUTPUT_TEXTURE_END

#endif
