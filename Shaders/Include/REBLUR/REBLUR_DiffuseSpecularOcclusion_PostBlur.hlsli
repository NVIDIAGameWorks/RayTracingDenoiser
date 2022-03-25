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
    float viewZ;

    #if( defined REBLUR_DIFFUSE )
        float2 diffData = gIn_Diff[ pixelPos ];
        viewZ = diffData.y / NRD_FP16_VIEWZ_SCALE;
    #endif

    #if( defined REBLUR_SPECULAR )
        float2 specData = gIn_Spec[ pixelPos ];
        viewZ = specData.y / NRD_FP16_VIEWZ_SCALE;
    #endif

    [branch]
    if( viewZ > gDenoisingRange )
        return;

    // Normal and roughness
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Internal data
    float curvature;
    float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], curvature );
    float2 diffInternalData = internalData.xy;
    float2 specInternalData = internalData.zw;

    // Output
    gOut_ViewZ_DiffAccumSpeed[ pixelPos ] = PackViewZAccumSpeed( viewZ, diffInternalData.y );
    gOut_Normal_SpecAccumSpeed[ pixelPos ] = PackNormalAccumSpeedMaterialID( N, specInternalData.y, materialID );

    #if( defined REBLUR_SPECULAR )
        gOut_Roughness[ pixelPos ] = roughness;
    #endif

    // Shared data
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float4 rotator = GetBlurKernelRotation( REBLUR_POST_BLUR_ROTATOR_MODE, pixelPos, gRotator, gFrameIndex );
    float4 error = gInOut_Error[ pixelPos ];

    // Spatial filtering
    #define REBLUR_SPATIAL_MODE REBLUR_POST_BLUR

    #if( defined REBLUR_DIFFUSE )
        float diff = diffData.x;

        #include "REBLUR_Common_DiffuseOcclusionSpatialFilter.hlsli"
    #endif

    #if( defined REBLUR_SPECULAR )
        float spec = specData.x;

        #include "REBLUR_Common_SpecularOcclusionSpatialFilter.hlsli"
    #endif

    // Output
    // no output to "gInOut_Error" because error is not used in next passes
}
