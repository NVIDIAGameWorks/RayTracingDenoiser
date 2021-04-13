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

    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4 gCameraDelta;
    float4 gSpecHitDistParams;
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
NRI_RESOURCE( Texture2D<float3>, gIn_InternalData, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Spec, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 5, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<uint2>, gOut_ViewZ_Normal_Roughness_AccumSpeeds, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec_Copy, u, 2, 0 );

groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    uint2 globalIdUser = gRectOrigin + globalId;

    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = abs( gIn_ViewZ[ globalIdUser ] );
    s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Spec[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
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
        gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( INF, 0.0, float3( 0, 0, 1 ), 1.0, 0.0 );
        return;
    }

    // Normal and roughness
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = -X * invDistToPoint;

    // Local variance
    float sum = 1.0;

    float viewZnearest = viewZ;
    int2 offseti = int2( BORDER, BORDER );

    float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
    float4 specM1 = spec;
    float4 specM2 = spec * spec;
    float specNormalParams = GetNormalWeightParamsRoughEstimate( roughness );
    float2 roughnessParams = GetRoughnessWeightParams( roughness );

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
            if( ( abs( t1.x ) + abs( t1.y ) == 1 ) && z < viewZnearest && gDebug > 0.0  )
            {
                viewZnearest = z;
                offseti = int2( dx, dy );
            }

            float w = GetBilateralWeight( z, viewZ );
            w *= GetNormalWeight( specNormalParams, N, n.xyz );
            w *= GetRoughnessWeight( roughnessParams, n.w );
            w = STL::Math::Pow01( w, REBLUR_TS_WEIGHT_BOOST_POWER );

            float4 s = s_Spec[ pos.y ][ pos.x ];
            specM1 += s * w;
            specM2 += s * s * w;

            sum += w;
        }
    }

    float invSum = STL::Math::PositiveRcp( sum );

    specM1 *= invSum;
    specM2 *= invSum;
    float4 specSigma = GetStdDev( specM1, specM2 );

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
    float4 specHistorySurface = BicubicFilterNoCorners( gIn_History_Spec, gLinearClamp, pixelPosPrev, gInvScreenSize );

    // Sample history ( virtual motion )
    float hitDist = GetHitDist( spec.w, viewZ, gSpecHitDistParams, roughness );
    float NoV = abs( dot( N, V ) );
    float3 Xvirtual = GetXvirtual( X, Xprev, V, NoV, roughness, hitDist );
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );
    float4 specHistoryVirtual = gIn_History_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev * gRectSizePrev * gInvScreenSize, 0 );

    // Compute parallax
    float parallax = ComputeParallax( X, Xprev, gCameraDelta.xyz );

    // Hit distance based disocclusion for virtual motion
    float hitDistDelta = abs( specHistoryVirtual.w - spec.w ) - specSigma.w * 0.5; // TODO: was 0.1
    float hitDistMax = max( specHistoryVirtual.w, spec.w );
    hitDistDelta = GetHitDist( hitDistDelta, viewZ, gSpecHitDistParams, roughness );
    hitDistMax = GetHitDist( hitDistMax, viewZ, gSpecHitDistParams, roughness );
    hitDistDelta *= STL::Math::PositiveRcp( hitDistMax + viewZ );

    float thresholdMin = 0.02 * STL::Math::LinearStep( 0.2, 0.01, parallax ); // TODO: thresholdMin needs to be set to 0, but it requires very clean hit distances
    float thresholdMax = lerp( 0.01, 0.25, roughness * roughness ) + thresholdMin;
    float virtualHistoryConfidence = STL::Math::LinearStep( thresholdMax, thresholdMin, hitDistDelta );
    virtualHistoryConfidence *= 1.0 - STL::Math::SmoothStep( 0.25, 1.0, parallax );

    // Clamp virtual history
    float sigmaScale = 1.0 + 0.125 * gFramerateScale;
    float4 specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual );

    float virtualForcedConfidence = lerp( 0.75, 0.95, STL::Math::LinearStep( 0.04, 0.25, roughness ) );
    float virtualUnclampedAmount = lerp( virtualHistoryConfidence * virtualForcedConfidence, 1.0, roughness * roughness );
    specHistoryVirtual = lerp( specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount );

    // Internal data
    float edge;
    float virtualHistoryAmount;
    float2 specInternalData = UnpackSpecInternalData( gIn_InternalData[ pixelPos ], roughness, edge, virtualHistoryAmount );

    // Adjust accumulation speed for virtual motion if confidence is low
    float accumSpeedScale = lerp( 1.0, virtualHistoryConfidence, virtualHistoryAmount );
    accumSpeedScale = lerp( accumSpeedScale, 1.0, roughness );
    accumSpeedScale = lerp( accumSpeedScale, 1.0, 1.0 / ( 1.0 + specInternalData.y ) );

    float specMinAccumSpeed = min( specInternalData.y, ( REBLUR_MIP_NUM - 1 ) * STL::Math::Sqrt01( roughness ) );
    specInternalData.y = specMinAccumSpeed + ( specInternalData.y - specMinAccumSpeed ) * accumSpeedScale;

    // Specular history
    float4 specHistory = lerp( specHistorySurface, specHistoryVirtual, virtualHistoryAmount );

    // History weight
    float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specInternalData.y, parallax, roughness, virtualHistoryAmount );

    // Antilag
    float specAntilag = ComputeAntilagScale( specInternalData.y, specHistory, specM1, specSigma, specTemporalAccumulationParams, gAntilag1, gAntilag2, roughness );

    // Clamp history and combine with the current frame
    specHistory = STL::Color::Clamp( specM1, specSigma * specTemporalAccumulationParams.y, specHistory );
    float specHistoryWeight = 1.0 - REBLUR_TS_MAX_HISTORY_WEIGHT * specAntilag * specTemporalAccumulationParams.x;

    float4 specResult;
    specResult.xyz = lerp( specHistory.xyz, spec.xyz, specHistoryWeight );
    specResult.w = lerp( specHistory.w, spec.w, max( specHistoryWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughness ) ) );

    // Output
    gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( viewZ, 0.0, N, roughness, specInternalData.y );
    gOut_Spec[ pixelPos ] = specResult;

    specResult = REBLUR_BackEnd_UnpackRadiance( specResult, viewZ, gSpecHitDistParams, roughness );

    #if( REBLUR_DEBUG == REBLUR_SHOW_ACCUM_SPEED  )
        specResult.w = saturate( specInternalData.y / ( gSpecMaxAccumulatedFrameNum + 1.0 ) );
    #elif( REBLUR_DEBUG == REBLUR_SHOW_VIRTUAL_HISTORY_AMOUNT )
        specResult.w = virtualHistoryAmount;
    #elif( REBLUR_DEBUG == REBLUR_SHOW_VIRTUAL_HISTORY_CONFIDENCE )
        specResult.w = virtualHistoryConfidence;
    #elif( REBLUR_DEBUG == REBLUR_SHOW_PARALLAX )
        diffResult.w = parallax;
    #elif( REBLUR_DEBUG == REBLUR_SHOW_EDGE )
        diffResult.w = edge;
    #endif

    gOut_Spec_Copy[ pixelPos ] = specResult;
}
