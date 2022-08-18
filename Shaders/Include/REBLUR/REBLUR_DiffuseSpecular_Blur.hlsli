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
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    // Early out
    #ifdef REBLUR_OCCLUSION
        float viewZ;

        #ifdef REBLUR_DIFFUSE
            float2 diffTemp = gIn_Diff[ pixelPos ];
            viewZ = diffTemp.y;
        #endif

        #ifdef REBLUR_SPECULAR
            float2 specTemp = gIn_Spec[ pixelPos ];
            viewZ = specTemp.y;
        #endif

        viewZ = UnpackViewZ( viewZ );
    #else
        float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );
    #endif

    // Output
    gOut_ViewZ[ pixelPos ] = PackViewZ( viewZ );

    [branch]
    if( viewZ > gDenoisingRange )
        return;

    // Normal and roughness
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVectorInverse( gViewToWorld, N );
    float roughness = normalAndRoughness.w;

    // Internal data
    float4 internalData1 = UnpackInternalData1( gIn_Data1[ pixelPos ] );

    // Shared data
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float4 rotator = GetBlurKernelRotation( REBLUR_BLUR_ROTATOR_MODE, pixelPos, gRotator, gFrameIndex );

    float3 Vv = GetViewVector( Xv, true );
    float NoV = abs( dot( Nv, Vv ) );

    // Spatial filtering
    #define REBLUR_SPATIAL_MODE REBLUR_BLUR

    #ifdef REBLUR_DIFFUSE
        #ifdef REBLUR_OCCLUSION
            float4 diff = diffTemp.x;
        #else
            float4 diff = gIn_Diff[ pixelPos ];
        #endif

        #ifdef REBLUR_SH
            float4 diffSh = gIn_DiffSh[ pixelPos ];
        #endif

        #include "REBLUR_Common_DiffuseSpatialFilter.hlsli"
    #endif

    #ifdef REBLUR_SPECULAR
        #ifdef REBLUR_OCCLUSION
            float4 spec = specTemp.x;
        #else
            float4 spec = gIn_Spec[ pixelPos ];
        #endif

        #ifdef REBLUR_SH
            float4 specSh = gIn_SpecSh[ pixelPos ];
        #endif

        #include "REBLUR_Common_SpecularSpatialFilter.hlsli"
    #endif
}
