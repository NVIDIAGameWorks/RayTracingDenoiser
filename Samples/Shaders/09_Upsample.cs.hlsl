/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "09_Resources.hlsl"

NRI_RESOURCE( Texture2D<float4>, gIn_Image, t, 0, 1 );

NRI_RESOURCE( RWTexture2D<float3>, gOut_Image, u, 1, 1 );

[numthreads( 16, 16, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Clamp UV to prevent sampling from "invalid" regions
    pixelUv = clamp( pixelUv, 1.5 * gInvRectSize, 1.0 - 1.5 * gInvRectSize );

    // Upsampling
    float2 uvScaled = pixelUv * gRectSize;
    float3 upsampled = BicubicFilterNoCorners( gIn_Image, gLinearSampler, uvScaled, gInvScreenSize, 0.66 ).xyz;
    float3 input = gIn_Image.SampleLevel( gNearestMipmapNearestSampler, uvScaled * gInvScreenSize, 0 ).xyz;

    // Split screen - noisy input / denoised output
    float3 result = pixelUv.x < gSeparator ? input : upsampled;

    // Split screen - vertical line
    float verticalLine = saturate( 1.0 - abs( pixelUv.x - gSeparator ) * gScreenSize.x / 3.5 );
    verticalLine = saturate( verticalLine / 0.5 );
    verticalLine *= float( gSeparator != 0.0 );

    const float3 nvColor = float3( 118.0, 185.0, 0.0 ) / 255.0;
    result = lerp( result, nvColor * verticalLine, verticalLine );

    // Output
    gOut_Image[ pixelPos ] = result;
}