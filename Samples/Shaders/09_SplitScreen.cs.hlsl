/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "09_Resources.hlsl"

NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 1 );
NRI_RESOURCE( Texture2D<float2>, gIn_Unfiltered_Shadow, t, 1, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_Unfiltered_DiffA, t, 2, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_Unfiltered_DiffB, t, 3, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_Unfiltered_SpecHit, t, 4, 1 );
NRI_RESOURCE( Texture2D<float3>, gIn_Unfiltered_Translucency, t, 5, 1 );

NRI_RESOURCE( RWTexture2D<float4>, gOut_Shadow, u, 6, 1 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_DiffHit, u, 7, 1 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_SpecHit, u, 8, 1 );

[numthreads( 16, 16, 1)]
void main( uint2 pixelPos : SV_DISPATCHTHREADID)
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;
    float2 sampleUv = pixelUv + gJitter;

    if( pixelUv.x > gSeparator )
        return;

    // Normal
    float4 normalAndRoughness = gIn_Normal_Roughness[ pixelPos ];
    float isGround = float( dot( normalAndRoughness.xyz, normalAndRoughness.xyz ) != SKY_MARK );
    normalAndRoughness = UnpackNormalAndRoughness( normalAndRoughness );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    if( isGround == 0.0 )
        return;

    // Split screen - noisy input / denoised output
    uint2 checkerboardPos = pixelPos;
    checkerboardPos.x >>= gSvgf ? 0 : gCheckerboard;

    float s = gIn_Unfiltered_Shadow[ pixelPos ].x;
    float3 translucency = gIn_Unfiltered_Translucency[ pixelPos ];
    float shadow = float( s == NRD_FP16_MAX );

    float4 diffA = gIn_Unfiltered_DiffA[ checkerboardPos ];
    float4 diffB = gIn_Unfiltered_DiffB[ checkerboardPos ];
    float4 diffHit = _NRD_BackEnd_UnpackDiffuse( diffA, diffB, N );

    float4 specHit = gIn_Unfiltered_SpecHit[ checkerboardPos ];

    if( gSvgf )
    {
        shadow = 0;
        diffHit = diffA;
    }

    gOut_Shadow[ pixelPos ] = float4( shadow, translucency );
    gOut_DiffHit[ pixelPos ] = diffHit;
    gOut_SpecHit[ pixelPos ] = specHit;
}
