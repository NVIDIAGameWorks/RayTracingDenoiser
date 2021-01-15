/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    float4x4 gWorldToViewPrev;
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4 gFrustum;
    float2 gScreenSize;
    float2 gInvScreenSize;
    float2 gMotionVectorScale;
    float gDisocclusionThreshold;
    float gJitterDelta;
    float gDiffuse;
    float gInf;
    float gIsOrtho;
    float gRadianceMaxAccumulatedFrameNum;
    float gMomentsMaxAccumulatedFrameNum;
    uint gWorldSpaceMotion;
    float gDebug;
};

#include "SVGF_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Signal, t, 0, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 2, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 3, 0 );
NRI_RESOURCE( Texture2D<uint2>, gIn_Prev_ViewZ_Normal_Roughness, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Signal, t, 5, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_History_Moments, t, 6, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_History_Length, t, 7, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_Signal, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float2>, gOut_Moments, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float>, gOut_HistoryLength, u, 2, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = ( pixelPos + 0.5 ) * gInvScreenSize;

    // Early out
    float viewZ = gIn_ViewZ[ pixelPos ];

    [branch]
    if( abs( viewZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Signal[ pixelPos ] = 0;
            gOut_Moments[ pixelPos ] = 0;
            gOut_HistoryLength[ pixelPos ] = 0;
        #endif
        return;
    }

    // Center position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );

    // Normal and roughness
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float roughness = normalAndRoughness.w;
    float3 N = normalAndRoughness.xyz;

    // Pseudo flat normal
    float3 Nflat = N;
    Nflat += _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos + int2( +1,  0 ) ] ).xyz;
    Nflat += _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos + int2( -1,  0 ) ] ).xyz;
    Nflat += _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos + int2(  0, -1 ) ] ).xyz;
    Nflat += _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos + int2(  0, +1 ) ] ).xyz;
    Nflat = normalize( Nflat );

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Previous viewZ, normal and roughness
    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gScreenSize );
    float2 gatherUv = ( bilinearFilterAtPrevPos.origin + 1.0 ) * gInvScreenSize;
    uint4 pack0 = gIn_Prev_ViewZ_Normal_Roughness.GatherRed( gNearestClamp, gatherUv ).wzxy;
    uint4 pack1 = gIn_Prev_ViewZ_Normal_Roughness.GatherGreen( gNearestClamp, gatherUv ).wzxy;
    float4 viewZprev = asfloat( pack0 );

    // Compute disocclusion based on plane distance
    float disocclusionThreshold = gDisocclusionThreshold; // TODO: take method from REBLUR
    float NoV = abs( dot( Nflat, X ) ) * invDistToPoint;
    float f = STL::Math::Pow01( 1.0 - NoV, 5.0 );
    disocclusionThreshold += gJitterDelta * f;

    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev = dot( Nflat, Xprev ) * invDistToPoint; // = dot( Nvflatprev, Xvprev )
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) ); // = dot( Nvflatprev, Xvprev / Xvprev.z )
    float4 planeDist = abs( NoVprev * abs( viewZprev ) - NoXprev );
    float4 occlusion = saturate( isInScreen - step( disocclusionThreshold, planeDist ) );

    // Compute disocclusion based on normals
    float4 n00 = UnpackNormalRoughness( pack1.x );
    float4 n01 = UnpackNormalRoughness( pack1.y );
    float4 n10 = UnpackNormalRoughness( pack1.z );
    float4 n11 = UnpackNormalRoughness( pack1.w );

    float2 normalParams;
    normalParams.x = STL::ImportanceSampling::GetSpecularLobeHalfAngle( 1.0 ); // TODO: yes, use 1.0 instead of roughness for both diffuse and specular
    normalParams.x *= 1.0; // yes, no narrowing here
    normalParams.x += NORMAL_BANDING_FIX;
    normalParams.y = 1.0;

    float4 normalOcclusion;
    normalOcclusion.x = GetNormalWeight( normalParams, N, n00.xyz );
    normalOcclusion.y = GetNormalWeight( normalParams, N, n10.xyz );
    normalOcclusion.z = GetNormalWeight( normalParams, N, n01.xyz );
    normalOcclusion.w = GetNormalWeight( normalParams, N, n11.xyz );
    occlusion *= normalOcclusion;

    // Sample history
    float2 sampleUvNearestPrev = ( bilinearFilterAtPrevPos.origin + 0.5 ) * gInvScreenSize;
    float4 weights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, occlusion );

    float4 a00 = gIn_History_Signal.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0 );
    float4 a10 = gIn_History_Signal.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 0 ) );
    float4 a01 = gIn_History_Signal.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 0, 1 ) );
    float4 a11 = gIn_History_Signal.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 1 ) );
    float4 inputHistory = STL::Filtering::ApplyBilinearCustomWeights( a00, a10, a01, a11, weights );

    float2 b00 = gIn_History_Moments.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0 );
    float2 b10 = gIn_History_Moments.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 0 ) );
    float2 b01 = gIn_History_Moments.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 0, 1 ) );
    float2 b11 = gIn_History_Moments.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 1 ) );
    float2 momentsHistory = STL::Filtering::ApplyBilinearCustomWeights( b00, b10, b01, b11, weights );

    float c00 = gIn_History_Length.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0 );
    float c10 = gIn_History_Length.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 0 ) );
    float c01 = gIn_History_Length.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 0, 1 ) );
    float c11 = gIn_History_Length.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 1 ) );
    float historyLength = STL::Filtering::ApplyBilinearCustomWeights( c00, c10, c01, c11, weights );

    // Sample current
    float4 input = gIn_Signal[ pixelPos ];

    // Compute second moments
    float2 lc = float2( STL::Color::Luminance( input.xyz ), input.w );
    float2 moments = lc * lc;

    // Temporal integration
    historyLength = UnpackHistoryLength( historyLength );

    float accumSpeed = min( historyLength, gRadianceMaxAccumulatedFrameNum );
    inputHistory = lerp( inputHistory, input, 1.0 / ( 1.0 + accumSpeed ) );

    accumSpeed = min( historyLength, gMomentsMaxAccumulatedFrameNum );
    momentsHistory = lerp( momentsHistory, moments, 1.0 / ( 1.0 + accumSpeed ) );

    // Update history length
    historyLength = historyLength + 1.0;

    // Output
    gOut_Signal[ pixelPos ] = inputHistory;
    gOut_Moments[ pixelPos ] = momentsHistory;
    gOut_HistoryLength[ pixelPos ] = PackHistoryLength( historyLength );
}
