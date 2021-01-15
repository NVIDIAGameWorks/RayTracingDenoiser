/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "09_Resources.hlsl"
#include "09_RaytracingShared.hlsl"

[shader( "closesthit" )]
void Main_rchit( inout Payload payload, IntersectionAttributes intersectionAttributes )
{
    payload = PackPayload( RayTCurrent( ), InstanceID( ), PrimitiveIndex( ), HitKind( ) == HIT_KIND_TRIANGLE_FRONT_FACE, intersectionAttributes.barycentrics );
}
