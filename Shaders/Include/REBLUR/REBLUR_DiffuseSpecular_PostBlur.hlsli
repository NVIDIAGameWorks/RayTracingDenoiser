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

        #if( defined REBLUR_DIFFUSE )
            float2 diffTemp = gIn_Diff[ pixelPos ];
            viewZ = diffTemp.y / NRD_FP16_VIEWZ_SCALE;
        #endif

        #if( defined REBLUR_SPECULAR )
            float2 specTemp = gIn_Spec[ pixelPos ];
            viewZ = specTemp.y / NRD_FP16_VIEWZ_SCALE;
        #endif
    #else
        float viewZ = gIn_ScaledViewZ[ pixelPos ] / NRD_FP16_VIEWZ_SCALE;
    #endif

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
    float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ] );
    float2 diffInternalData = internalData.xy;
    float2 specInternalData = internalData.zw;

    // Output
    #ifdef REBLUR_OCCLUSION
        gOut_ViewZ_DiffAccumSpeed[ pixelPos ] = PackViewZAccumSpeed( viewZ, diffInternalData.y );
        gOut_Normal_SpecAccumSpeed[ pixelPos ] = PackNormalAccumSpeedMaterialID( N, specInternalData.y, materialID );
    #endif

    #if( defined REBLUR_SPECULAR )
        gOut_Roughness[ pixelPos ] = roughness;
    #endif

    // Shared data
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float4 rotator = GetBlurKernelRotation( REBLUR_POST_BLUR_ROTATOR_MODE, pixelPos, gRotator, gFrameIndex );

    // Spatial filtering
    #define REBLUR_SPATIAL_MODE REBLUR_POST_BLUR

    #if( defined REBLUR_DIFFUSE )
        #ifdef REBLUR_OCCLUSION
            float4 diff = diffTemp.x;
        #else
            float4 diff = gIn_Diff[ pixelPos ];
        #endif
        float diffData = gIn_DiffData[ pixelPos ];

        #include "REBLUR_Common_DiffuseSpatialFilter.hlsli"
    #endif

    #if( defined REBLUR_SPECULAR )
        #ifdef REBLUR_OCCLUSION
            float4 spec = specTemp.x;
        #else
            float4 spec = gIn_Spec[ pixelPos ];
        #endif
        float2 specData = gIn_SpecData[ pixelPos ].xy;

        #include "REBLUR_Common_SpecularSpatialFilter.hlsli"
    #endif
}
