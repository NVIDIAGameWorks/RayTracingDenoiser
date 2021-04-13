/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "09_Resources.hlsl"

NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 0, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_TransparentLighting, t, 1, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_ComposedLighting_ViewZ, t, 2, 1 );

NRI_RESOURCE( RWTexture2D<float>, gOut_ViewZ, u, 3, 1 );
NRI_RESOURCE( RWTexture2D<float2>, gOut_SurfaceMotion, u, 4, 1 );
NRI_RESOURCE( RWTexture2D<float3>, gOut_FinalImage, u, 5, 1 );

[numthreads( 16, 16, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = ( float2( pixelPos ) + 0.5 ) * gInvScreenSize;

    // ViewZ to depth
    float viewZ = gOut_ViewZ[ pixelPos ];
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gCameraFrustum, viewZ, gIsOrtho );
    float4 clipPos = STL::Geometry::ProjectiveTransform( gViewToClip, Xv );
    gOut_ViewZ[ pixelPos ] = clipPos.z / clipPos.w;

    // Object to surface motion
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * ( gWorldSpaceMotion ? 1.0 : gInvScreenSize.xyy );
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float2 pixelMotion = ( pixelUvPrev - pixelUv ) * gScreenSize;
    gOut_SurfaceMotion[ pixelPos ] = pixelMotion;

    // Post lighting composition
    float3 Lsum = gIn_ComposedLighting_ViewZ[ pixelPos ].xyz;
    Lsum = ApplyPostLightingComposition( pixelPos, Lsum, gIn_TransparentLighting );

    // Dithering
    STL::Rng::Initialize( pixelPos, gFrameIndex );
    float rnd = STL::Rng::GetFloat2( ).x;
    float luma = STL::Color::Luminance( Lsum );
    float amplitude = lerp( 0.2, 0.005, STL::Math::Sqrt01( luma ) );
    float dither = 1.0 + ( rnd - 0.5 ) * amplitude;
    Lsum *= dither;

    // Output
    gOut_FinalImage[ pixelPos ] = Lsum;
}