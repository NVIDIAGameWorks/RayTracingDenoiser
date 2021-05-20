/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "NRD.hlsl"
#include "STL.hlsl"
#include "REBLUR_Config.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    REBLUR_SPEC_SHARED_CB_DATA;

    float4x4 gWorldToView;
    float4 gRotator;
    float4 gSpecTrimmingParams;
    float gSpecBlurRadiusScale;
    float gNormalWeightStrictness;
};

#include "NRD_Common.hlsl"
#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_InternalData, t, 1, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 3, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<unorm float4>, gInOut_Error, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 1, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void NRD_CS_MAIN( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    // Early out
    float viewZ = gIn_ScaledViewZ[ pixelPos ] / NRD_FP16_VIEWZ_SCALE;

    [branch]
    if( viewZ > gInf )
        return;

    // Normal and roughness
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Shared data
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float4 rotator = GetBlurKernelRotation( REBLUR_POST_BLUR_ROTATOR_MODE, pixelPos, gRotator, gFrameIndex );

    // Internal data
    float edge;
    float2 specInternalData = UnpackSpecInternalData( gIn_InternalData[ pixelPos ], roughness, edge );

    // Center data
    float4 spec = gIn_Spec[ pixelPos ];
    float4 error = gInOut_Error[ pixelPos ];

    // Spatial filtering
    #define REBLUR_SPATIAL_MODE REBLUR_POST_BLUR

    #include "REBLUR_Common_SpecularSpatialFilter.hlsl"
}
