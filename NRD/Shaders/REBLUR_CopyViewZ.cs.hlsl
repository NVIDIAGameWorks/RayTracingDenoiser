/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "STL.hlsl"
#include "NRD.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    float gDebug;
};

// Inputs
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 0, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float>, gOut_ScaledViewZ, u, 0, 0 );

[numthreads( 16, 16, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float viewZ = abs( gIn_ViewZ[ pixelPos ] );
    float scaledViewZ = min( viewZ * NRD_FP16_VIEWZ_SCALE, NRD_FP16_MAX );

    gOut_ScaledViewZ[ pixelPos ] = scaledViewZ;
}
