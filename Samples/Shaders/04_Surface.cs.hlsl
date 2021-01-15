/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE( RWTexture2D<unorm float4>, g_Result, u, 0, 0 );

[numthreads( 16, 16, 1 )]
void main( uint2 pixelID : SV_DISPATCHTHREADID )
{
    float3 result = float3( 0.0, 0.0, 0.0 );

    for( float i = 1.0; i < 200.0; i += 0.1 )
    {
        const float2 uv = float2( pixelID ) * i * 0.1;

        result.x += sin( uv.x * 0.1 ) + sin( uv.y * 0.3 );
        result.y += sin( uv.x * 0.2 ) + sin( uv.y * 0.2 );
        result.z += sin( uv.x * 0.3 ) + sin( uv.y * 0.1 );
    }

    result = result * 0.5 + 0.5;
    float norm = max( result.x, max( result.y, result.z ) );
    result /= norm;

    g_Result[pixelID] = float4( result * 0.5 + 0.5, 1.0 );
}
