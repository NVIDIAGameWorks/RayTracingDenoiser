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
    REBLUR_DIFF_SHARED_CB_DATA;

    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4 gCameraDelta;
    float4 gAntilag1;
    float4 gAntilag2;
    float2 gMotionVectorScale;
};

#include "NRD_Common.hlsl"
#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_InternalData, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Diff, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 5, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<uint2>, gOut_ViewZ_Normal_Roughness_AccumSpeeds, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff_Copy, u, 2, 0 );

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    uint2 globalIdUser = gRectOrigin + globalId;

    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = abs( gIn_ViewZ[ globalIdUser ] );
    s_Diff[ sharedId.y ][ sharedId.x ] = gIn_Diff[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void NRD_CS_MAIN( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    int2 smemPos = threadId + BORDER;
    float viewZ = s_ViewZ[ smemPos.y ][ smemPos.x ];

    [branch]
    if( viewZ > gInf )
    {
        gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( NRD_INF, 0.0, float3( 0, 0, 1 ), 1.0, 0.0 );
        return;
    }

    // Normal and roughness
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );

    // Local variance
    float sum = 1.0;

    float viewZnearest = viewZ;
    int2 offseti = int2( BORDER, BORDER );

    float4 diff = s_Diff[ smemPos.y ][ smemPos.x ];
    float4 diffM1 = diff;
    float4 diffM2 = diff * diff;
    float diffNormalParams = GetNormalWeightParamsRoughEstimate( 1.0 );

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadId + int2( dx, dy );
            float4 n = s_Normal_Roughness[ pos.y ][ pos.x ];
            float z = s_ViewZ[ pos.y ][ pos.x ];

            int2 t1 = int2( dx, dy ) - BORDER;
            if( ( abs( t1.x ) + abs( t1.y ) == 1 ) && z < viewZnearest )
            {
                viewZnearest = z;
                offseti = int2( dx, dy );
            }

            float w = GetBilateralWeight( z, viewZ );
            w *= GetNormalWeight( diffNormalParams, N, n.xyz );
            w = STL::Math::Pow01( w, REBLUR_TS_WEIGHT_BOOST_POWER );

            float4 d = s_Diff[ pos.y ][ pos.x ];
            diffM1 += d * w;
            diffM2 += d * d * w;

            sum += w;
        }
    }

    float invSum = STL::Math::PositiveRcp( sum );

    diffM1 *= invSum;
    diffM2 *= invSum;
    float4 diffSigma = GetStdDev( diffM1, diffM2 );

    // Compute previous pixel position
    offseti -= BORDER;
    float2 offset = float2( offseti ) * gInvRectSize;
    float3 Xvnearest = STL::Geometry::ReconstructViewPosition( pixelUv + offset, gFrustum, viewZnearest, gIsOrtho );
    float3 Xnearest = STL::Geometry::AffineTransform( gViewToWorld, Xvnearest );
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser + offseti ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv + offset, Xnearest, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    pixelUvPrev -= offset;

    float isInScreen = IsInScreen2x2( pixelUvPrev, gRectSizePrev );
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );

    // Sample history ( surface motion )
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gRectSizePrev;
    float4 diffHistory = BicubicFilterNoCorners( gIn_History_Diff, gLinearClamp, pixelPosPrev, gInvScreenSize );

    // Internal data
    float edge;
    float2 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ], edge );

    // History weight
    float parallax = ComputeParallax( X, Xprev, gCameraDelta.xyz );
    float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffInternalData.y, parallax );

    // Antilag
    float diffAntilag = ComputeAntilagScale( diffInternalData.y, diffHistory, diffM1, diffSigma, diffTemporalAccumulationParams, gAntilag1, gAntilag2 );

    // Clamp history and combine with the current frame
    diffHistory = STL::Color::Clamp( diffM1, diffSigma * diffTemporalAccumulationParams.y, diffHistory );
    float diffHistoryWeight = 1.0 - REBLUR_TS_MAX_HISTORY_WEIGHT * diffAntilag * diffTemporalAccumulationParams.x;

    float4 diffResult;
    diffResult.xyz = lerp( diffHistory.xyz, diff.xyz, diffHistoryWeight );
    diffResult.w = lerp( diffHistory.w, diff.w, max( diffHistoryWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );

    // Output
    gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( viewZ, diffInternalData.y, N, 1.0, 0.0 );
    gOut_Diff[ pixelPos ] = diffResult;

    #if( REBLUR_DEBUG == REBLUR_SHOW_ACCUM_SPEED  )
        diffResult.w = saturate( diffInternalData.y / ( gDiffMaxAccumulatedFrameNum + 1.0 ) );
    #elif( REBLUR_DEBUG == REBLUR_SHOW_EDGE )
        diffResult.w = edge;
    #endif

    gOut_Diff_Copy[ pixelPos ] = diffResult;
}
