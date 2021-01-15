/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

struct Payload
{
    float3 hitValue;
};

struct IntersectionAttributes
{
    float2 barycentrics;
};

[shader( "closesthit" )]
void closest_hit( inout Payload payload : SV_RayPayload, in IntersectionAttributes intersectionAttributes : SV_IntersectionAttributes )
{
    float2 barycentrics = intersectionAttributes.barycentrics;

    payload.hitValue = float3( 1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y );
}
