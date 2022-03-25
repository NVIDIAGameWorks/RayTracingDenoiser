/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    int2 pixelPosUser = gRectOrigin + pixelPos;

    // Checkerboard
    bool2 hasData = true;
    uint2 checkerboardPixelPos = pixelPos.xx;
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    #if( defined REBLUR_DIFFUSE )
        if( gDiffCheckerboard != 2 )
        {
            hasData.x = checkerboard == gDiffCheckerboard;
            checkerboardPixelPos.x >>= 1;
        }
    #endif

    #if( defined REBLUR_SPECULAR )
        if( gSpecCheckerboard != 2 )
        {
            hasData.y = checkerboard == gSpecCheckerboard;
            checkerboardPixelPos.y >>= 1;
        }
    #endif

    // Early out
    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );

    [branch]
    if( viewZ > gDenoisingRange )
        return;

    // Checkerboard resolve // TODO: materialID support?
    float viewZ0 = abs( gIn_ViewZ[ pixelPosUser + int2( -1, 0 ) ] );
    float viewZ1 = abs( gIn_ViewZ[ pixelPosUser + int2( 1, 0 ) ] );
    float2 w = GetBilateralWeight( float2( viewZ0, viewZ1 ), viewZ );
    w *= STL::Math::PositiveRcp( w.x + w.y );

    int3 checkerboardPos = pixelPos.xyx + int3( -1, 0, 1 );
    checkerboardPos.xz >>= 1;
    checkerboardPos += int3(gRectOrigin.xyx);

    #if( defined REBLUR_DIFFUSE )
        float4 diff = gIn_Diff[ gRectOrigin + uint2( checkerboardPixelPos.x, pixelPos.y ) ];
        float4 d0 = gIn_Diff[ checkerboardPos.xy ];
        float4 d1 = gIn_Diff[ checkerboardPos.zy ];
        if( !hasData.x )
        {
            diff *= saturate( 1.0 - w.x - w.y );
            diff += d0 * w.x + d1 * w.y;
        }
    #endif

    #if( defined REBLUR_SPECULAR )
        float4 spec = gIn_Spec[ gRectOrigin + uint2( checkerboardPixelPos.y, pixelPos.y ) ];
        float4 s0 = gIn_Spec[ checkerboardPos.xy ];
        float4 s1 = gIn_Spec[ checkerboardPos.zy ];
        if( !hasData.y )
        {
            spec *= saturate( 1.0 - w.x - w.y );
            spec += s0 * w.x + s1 * w.y;
        }
    #endif

    // Spatial filtering
    [branch]
    if( gSpatialFiltering == 0.0 )
    {
        #if( defined REBLUR_DIFFUSE )
            gOut_Diff[ pixelPos ] = diff;
        #endif
        #if( defined REBLUR_SPECULAR )
            gOut_Spec[ pixelPos ] = spec;
        #endif
        return;
    }

    // Normal and roughness
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Shared data
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float4 rotator = GetBlurKernelRotation( REBLUR_PRE_BLUR_ROTATOR_MODE, pixelPos, gRotator, gFrameIndex );
    float4 error = 1;
    float curvature = 0;

    #define REBLUR_SPATIAL_MODE REBLUR_PRE_BLUR

    #if( defined REBLUR_DIFFUSE )
        #include "REBLUR_Common_DiffuseSpatialFilter.hlsli"
    #endif

    #if( defined REBLUR_SPECULAR )
        #include "REBLUR_Common_SpecularSpatialFilter.hlsli"
    #endif
}
