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

NRD_INPUT_TEXTURE_START
    NRD_OUTPUT_TEXTURE( Texture2D<float4>, gIn_Input, t, 0 )
NRD_INPUT_TEXTURE_END

NRD_OUTPUT_TEXTURE_START
    NRD_OUTPUT_TEXTURE( RWTexture2D<float4>, gOut_Output, u, 0 )
NRD_OUTPUT_TEXTURE_END
