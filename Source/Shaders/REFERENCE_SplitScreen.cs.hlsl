/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"
#include "REFERENCE_SplitScreen.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"

NRD_DECLARE_SAMPLERS

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( uint2 pixelPos : SV_DispatchThreadId )
{
    gOut_Output[ pixelPos ] = gIn_Input[ pixelPos ];
}
