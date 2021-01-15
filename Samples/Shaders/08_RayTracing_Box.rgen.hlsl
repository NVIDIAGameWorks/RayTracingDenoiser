/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

RWTexture2D<float4> outputImage : register( u0, space0 );
RaytracingAccelerationStructure topLevelAS : register( t1, space0 );

struct Payload
{
    float3 hitValue;
};

[shader( "raygeneration" )]
void raygen( )
{
    uint2 dispatchRaysIndex = DispatchRaysIndex( ).xy;
    uint2 dispatchRaysDimensions = DispatchRaysDimensions( ).xy;

    const float2 pixelCenter = float2( dispatchRaysIndex.xy ) + float2( 0.5, 0.5 );
    const float2 inUV = pixelCenter / float2( dispatchRaysDimensions.xy );

    float2 d = inUV * 2.0 - 1.0;
    float aspectRatio = float( dispatchRaysDimensions.x ) / float( dispatchRaysDimensions.y );

    RayDesc rayDesc;
    rayDesc.Origin = float3( 0, 0, -2.0 );
    rayDesc.Direction = normalize( float3( d.x * aspectRatio, -d.y, 1 ) );
    rayDesc.TMin = 0.001;
    rayDesc.TMax = 1000.0;

    uint rayFlags = RAY_FLAG_FORCE_OPAQUE;
    uint instanceInclusionMask = 0xff;
    uint rayContributionToHitGroupIndex = 0;
    uint multiplierForGeometryContributionToHitGroupIndex = 1;
    uint missShaderIndex = 0;

    Payload payload = (Payload)0;
    TraceRay( topLevelAS, rayFlags, instanceInclusionMask, rayContributionToHitGroupIndex, multiplierForGeometryContributionToHitGroupIndex, missShaderIndex, rayDesc, payload );

    outputImage[dispatchRaysIndex] = float4( payload.hitValue, 0 );
}
