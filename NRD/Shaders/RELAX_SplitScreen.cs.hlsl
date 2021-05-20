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
#include "RELAX_Config.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    uint2 gRectOrigin;
    float2 gInvRectSize;
    float gSplitScreen;
    float gInf;
};

#include "NRD_Common.hlsl"
#include "RELAX_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_Spec, t, 2, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_Diff, t, 3, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float3>, gOut_Spec, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float3>, gOut_Diff, u, 1, 0 );

[numthreads( 16, 16, 1)]
void NRD_CS_MAIN( uint2 pixelPos : SV_DispatchThreadId)
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    uint2 pixelPosUser = gRectOrigin + pixelPos;

    if( pixelUv.x > gSplitScreen )
        return;

    float viewZ = gIn_ViewZ[ pixelPosUser ];

    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ] );
    float roughness = normalAndRoughness.w;

    float3 specResult = gIn_Spec[ pixelPosUser ];
    gOut_Spec[ pixelPos ] = specResult * float( viewZ < gInf );

    float3 diffResult = gIn_Diff[ pixelPosUser ];
    gOut_Diff[ pixelPos ] = diffResult * float( viewZ < gInf );
}
