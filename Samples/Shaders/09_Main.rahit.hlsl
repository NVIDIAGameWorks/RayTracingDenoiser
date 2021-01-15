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

[shader( "anyhit" )]
void Main_rahit( inout Payload payload, IntersectionAttributes intersectionAttributes )
{
    UnpackedPayload unpackedPayload = UnpackPayload( RayTCurrent( ), InstanceID( ), PrimitiveIndex( ), HitKind( ) == HIT_KIND_TRIANGLE_FRONT_FACE, intersectionAttributes.barycentrics, payload.GetMipAndCone( ) );

    if( unpackedPayload.IsTransparent() )
        return; // TODO: add proper handling of transparent objects!

    GeometryProps geometryProps = GetGeometryProps( unpackedPayload, WorldRayOrigin( ), WorldRayDirection( ) );

    uint baseTexture = geometryProps.GetBaseTexture();
    float3 mips = GetRealMip( baseTexture, geometryProps.mip );
    float alpha = gIn_Textures[ baseTexture ].SampleLevel( gLinearMipmapLinearSampler, geometryProps.uv, mips.x ).w;

    if( alpha < 0.5 )
        IgnoreHit( );
}
