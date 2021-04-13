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
#include "REBLUR_Config.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    REBLUR_DIFF_SHARED_CB_DATA;

    float4 gDiffHitDistParams;
    uint gDiffCheckerboard;
    float gSplitScreen;
};

#include "NRD_Common.hlsl"
#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 0, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 1, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 0, 0 );

[numthreads( GROUP_X, GROUP_Y, 1)]
void main( uint2 pixelPos : SV_DispatchThreadId)
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    uint2 pixelPosUser = gRectOrigin + pixelPos;

    if( pixelUv.x > gSplitScreen )
        return;

    float viewZ = gIn_ViewZ[ pixelPosUser ];

    uint2 checkerboardPos = pixelPos;
    checkerboardPos.x = pixelPos.x >> ( gDiffCheckerboard != 2 ? 1 : 0 );
    float4 diffResult = gIn_Diff[ gRectOrigin + checkerboardPos ];
    diffResult = REBLUR_BackEnd_UnpackRadiance( diffResult, viewZ, gDiffHitDistParams );
    gOut_Diff[ pixelPos ] = diffResult * float( viewZ < gInf );
}
