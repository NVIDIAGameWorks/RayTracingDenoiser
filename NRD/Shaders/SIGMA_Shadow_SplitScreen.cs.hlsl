/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "NRD.hlsl"
#include "STL.hlsl"
#include "SIGMA_Config.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    SIGMA_SHARED_CB_DATA;

    float gSplitScreen;
};

#include "NRD_Common.hlsl"
#include "SIGMA_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float2>, gIn_Hit_ViewZ, t, 0, 0 );

#ifdef SIGMA_TRANSLUCENT
    NRI_RESOURCE( Texture2D<float4>, gIn_Shadow_Translucency, t, 1, 0 );
#endif

// Outputs
NRI_RESOURCE( RWTexture2D<SIGMA_TYPE>, gOut_Shadow_Translucency, u, 0, 0 );

[numthreads( GROUP_X, GROUP_Y, 1)]
void NRD_CS_MAIN( uint2 pixelPos : SV_DispatchThreadId)
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    uint2 pixelPosUser = gRectOrigin + pixelPos;

    if( pixelUv.x > gSplitScreen )
        return;

    float2 data = gIn_Hit_ViewZ[ pixelPosUser ];
    float viewZ = abs( data.y ) / NRD_FP16_VIEWZ_SCALE;

    SIGMA_TYPE s;
    #ifdef SIGMA_TRANSLUCENT
        s = gIn_Shadow_Translucency[ pixelPosUser ];
    #else
        s = float( data.x == NRD_FP16_MAX );
    #endif

    gOut_Shadow_Translucency[ pixelPos ] = s * float( viewZ < gInf );
}
