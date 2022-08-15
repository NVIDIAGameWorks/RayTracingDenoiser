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

    // Early out
    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );

    [branch]
    if( viewZ > gDenoisingRange )
        return;

    // Checkerboard resolve
    bool2 hasData = true;
    uint2 checkerboardPixelPos = pixelPos.xx;
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    float viewZ0 = abs( gIn_ViewZ[ pixelPosUser + int2( -1, 0 ) ] );
    float viewZ1 = abs( gIn_ViewZ[ pixelPosUser + int2( 1, 0 ) ] );
    float2 wc = GetBilateralWeight( float2( viewZ0, viewZ1 ), viewZ );
    wc *= STL::Math::PositiveRcp( wc.x + wc.y );

    int3 checkerboardPos = pixelPos.xyx + int3( -1, 0, 1 );
    checkerboardPos.xz >>= 1;
    checkerboardPos += int3( gRectOrigin.xyx );

    // Normal and roughness
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVectorInverse( gViewToWorld, N );
    float roughness = normalAndRoughness.w;

    // Shared data
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float4 rotator = GetBlurKernelRotation( REBLUR_PRE_BLUR_ROTATOR_MODE, pixelPos, gRotator, gFrameIndex );

    #define REBLUR_SPATIAL_MODE REBLUR_PRE_BLUR

    #ifdef REBLUR_DIFFUSE
    {
        if( gDiffCheckerboard != 2 )
        {
            hasData.x = checkerboard == gDiffCheckerboard;
            checkerboardPixelPos.x >>= 1;
        }
        uint2 pos = gRectOrigin + uint2( checkerboardPixelPos.x, pixelPos.y );

        float4 diff = gIn_Diff[ pos ];
        float radius = gDiffPrepassBlurRadius;
        float diffData = 1.0;

        #ifdef REBLUR_SH
            float4 diffSh = gIn_DiffSh[ pos ];
        #endif

        #include "REBLUR_Common_DiffuseSpatialFilter.hlsli"
    }
    #endif

    #ifdef REBLUR_SPECULAR
    {
        if( gSpecCheckerboard != 2 )
        {
            hasData.y = checkerboard == gSpecCheckerboard;
            checkerboardPixelPos.y >>= 1;
        }
        uint2 pos = gRectOrigin + uint2( checkerboardPixelPos.y, pixelPos.y );

        float4 spec = gIn_Spec[ pos ];
        float radius = gSpecPrepassBlurRadius;
        float2 specData = 1.0;

        #ifdef REBLUR_SH
            float4 specSh = gIn_SpecSh[ pos ];
        #endif

        #include "REBLUR_Common_SpecularSpatialFilter.hlsli"
    }
    #endif
}
