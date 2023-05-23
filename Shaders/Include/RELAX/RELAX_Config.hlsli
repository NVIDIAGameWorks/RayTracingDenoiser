/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define RELAX_BLACK_OUT_INF_PIXELS                          0 // 1 can be used to avoid killing INF pixels during composition
#define RELAX_MAX_ACCUM_FRAME_NUM                           255
#define RELAX_SPEC_DOMINANT_DIRECTION                       STL_SPECULAR_DOMINANT_DIRECTION_G2
#define RELAX_NORMAL_ULP                                    atan( 1.5 / 255.0 )
#define RELAX_NORMAL_ENCODING_ERROR                         STL::Math::DegToRad( 0.5 )

// Shared constants common to all ReLAX denoisers
#define RELAX_SHARED_CB_DATA \
    NRD_CONSTANT( float4x4, gPrevWorldToClip ) \
    NRD_CONSTANT( float4x4, gPrevWorldToView ) \
    NRD_CONSTANT( float4x4, gWorldToClip ) \
    NRD_CONSTANT( float4x4, gWorldPrevToWorld ) \
    NRD_CONSTANT( float4x4, gViewToWorld ) \
    NRD_CONSTANT( float4, gFrustumRight ) \
    NRD_CONSTANT( float4, gFrustumUp ) \
    NRD_CONSTANT( float4, gFrustumForward ) \
    NRD_CONSTANT( float4, gPrevFrustumRight ) \
    NRD_CONSTANT( float4, gPrevFrustumUp ) \
    NRD_CONSTANT( float4, gPrevFrustumForward ) \
    NRD_CONSTANT( float4, gPrevCameraPosition ) \
    NRD_CONSTANT( float3, gMvScale ) \
    NRD_CONSTANT( float, gDebug ) \
    NRD_CONSTANT( float2, gResolutionScale) \
    NRD_CONSTANT( uint2, gRectOrigin ) \
    NRD_CONSTANT( float2, gRectOffset ) \
    NRD_CONSTANT( uint2, gRectSize ) \
    NRD_CONSTANT( float2, gInvResourceSize ) \
    NRD_CONSTANT( float2, gInvRectSize ) \
    NRD_CONSTANT( float2, gRectSizePrev ) \
    NRD_CONSTANT( uint, gIsWorldSpaceMotionEnabled ) \
    NRD_CONSTANT( float, gOrthoMode ) \
    NRD_CONSTANT( float, gUnproject ) \
    NRD_CONSTANT( uint, gFrameIndex ) \
    NRD_CONSTANT( float, gDenoisingRange ) \
    NRD_CONSTANT( float, gFramerateScale ) \
    NRD_CONSTANT( float, gCheckerboardResolveAccumSpeed ) \
    NRD_CONSTANT( float, gJitterDelta ) \
    NRD_CONSTANT( uint, gDiffMaterialMask ) \
    NRD_CONSTANT( uint, gSpecMaterialMask ) \
    NRD_CONSTANT( uint, gUseWorldPrevToWorld ) \
    NRD_CONSTANT( uint, gResetHistory ) \
    NRD_CONSTANT( uint, unused2 ) \
    NRD_CONSTANT( uint, unused3 )

#if( !defined RELAX_DIFFUSE && !defined RELAX_SPECULAR )
    #define RELAX_DIFFUSE
    #define RELAX_SPECULAR
#endif