/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "02_Resources.hlsl"

[earlydepthstencil]
float4 main( in Attributes input ) : SV_Target
{
    PS_INPUT;
    float4 output = Shade( float4( albedo, diffuse.w ), Rf0, roughness, emissive, N, L, V, Clight, FAKE_AMBIENT );

    output.xyz = STL::Color::HdrToLinear( output.xyz * exposure );
    return output;
}
