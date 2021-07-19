/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsl"
#include "STL.hlsl"
#include "REBLUR_DiffuseSpecular_TemporalStabilization.resources.hlsl"

NRD_DECLARE_CONSTANTS

#define NRD_CTA_8X8
#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS

#include "REBLUR_Common.hlsl"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];
groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    uint2 globalIdUser = gRectOrigin + globalId;

    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = abs( gIn_ViewZ[ globalIdUser ] );
    s_Diff[ sharedId.y ][ sharedId.x ] = gIn_Diff[ globalId ];
    s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Spec[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
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
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = -X * invDistToPoint;

    // Local variance
    float2 sum = 1.0;

    float viewZnearest = viewZ;
    int2 offseti = int2( BORDER, BORDER );

    float4 diff = s_Diff[ smemPos.y ][ smemPos.x ];
    float4 diffM1 = diff;
    float4 diffM2 = diff * diff;
    float diffNormalParams = GetNormalWeightParamsRoughEstimate( 1.0 );

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
            if( ( abs( t1.x ) + abs( t1.y ) == 1 ) && z < viewZnearest )
            {
                viewZnearest = z;
                offseti = int2( dx, dy );
            }

            // Weights are needed to avoid getting 1 pixel wide outline under motion on contrast objects
            float2 w = GetBilateralWeight( z, viewZ );
            w.x *= GetNormalWeight( diffNormalParams, N, n.xyz );
            w.y *= GetNormalWeight( specNormalParams, N, n.xyz );
            w.y *= GetRoughnessWeight( roughnessParams, n.w );
            w = STL::Math::Pow01( w, REBLUR_TS_WEIGHT_BOOST_POWER );

            float4 d = s_Diff[ pos.y ][ pos.x ];
            diffM1 += d * w.x;
            diffM2 += d * d * w.x;

            float4 s = s_Spec[ pos.y ][ pos.x ];
            specM1 += s * w.y;
            specM2 += s * s * w.y;

            sum += w;
        }
    }

    float2 invSum = 1.0 / sum;

    diffM1 *= invSum.x;
    diffM2 *= invSum.x;
    float4 diffSigma = GetStdDev( diffM1, diffM2 );

    specM1 *= invSum.y;
    specM2 *= invSum.y;
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
    float4 diffHistory = BicubicFilterNoCorners( gIn_History_Diff, gLinearClamp, pixelPosPrev, gInvScreenSize );
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
    float hitDistVirtual = GetHitDist( specHistoryVirtual.w, viewZ, gSpecHitDistParams, roughness ); // TODO: not virtually sampled viewZ and roughness are used for simplicty
    float hitDistDelta = abs( hitDistVirtual - hitDist ); // no sigma substraction
    float hitDistMax = max( hitDistVirtual, hitDist );
    hitDistDelta *= STL::Math::PositiveRcp( hitDistMax + viewZ );

    float thresholdMin = lerp( 0.005, 0.03, STL::Math::Sqrt01( roughness ) );
    float thresholdMax = 0.25 * roughness * roughness;
    float virtualHistoryHitDistConfidence = STL::Math::LinearStep( thresholdMin + thresholdMax, thresholdMin, hitDistDelta * parallax );
    float virtualHistoryHitDistConfidenceNoParallax = STL::Math::LinearStep( 0.005 + thresholdMax, 0.005, hitDistDelta );

    // Clamp virtual history
    float virtualHistoryConfidence = gIn_Error[ pixelPos ].w;
    float sigmaScale = lerp( lerp( 1.0, 3.0, roughness ), 3.0, virtualHistoryConfidence ) + gFramerateScale * GetSpecMagicCurve( roughness ) * virtualHistoryConfidence;
    float4 specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual );

    float virtualUnclampedAmount = lerp( virtualHistoryConfidence * virtualHistoryHitDistConfidenceNoParallax, 1.0, roughness * roughness );
    specHistoryVirtual = lerp( specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount );

    // Internal data
    float edge;
    float virtualHistoryAmount;
    float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], edge, virtualHistoryAmount );
    float2 diffInternalData = internalData.xy;
    float2 specInternalData = internalData.zw;

    // Adjust accumulation speed for virtual motion if confidence is low
    float specMinAccumSpeed = min( specInternalData.y, GetMipLevel( roughness, gSpecMaxFastAccumulatedFrameNum ) );
    float specAccumSpeedScale = lerp( 1.0, virtualHistoryHitDistConfidence, virtualHistoryAmount );
    specInternalData.y = InterpolateAccumSpeeds( specMinAccumSpeed, specInternalData.y, specAccumSpeedScale );

    // Specular history
    float hitDistToSurfaceRatio = saturate( hitDist * invDistToPoint );
    float4 specHistory = InterpolateSurfaceAndVirtualMotion( specHistorySurface, specHistoryVirtual, virtualHistoryAmount, hitDistToSurfaceRatio );

    // History weight
    float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffInternalData.y, parallax );
    float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specInternalData.y, parallax, roughness, virtualHistoryAmount );

    // Antilag
    float diffAntilag = ComputeAntilagScale( diffInternalData.y, diffHistory, diffM1, diffSigma, diffTemporalAccumulationParams, gDiffMaxFastAccumulatedFrameNum, gDiffAntilag1, gDiffAntilag2 );
    float specAntilag = ComputeAntilagScale( specInternalData.y, specHistory, specM1, specSigma, specTemporalAccumulationParams, gSpecMaxFastAccumulatedFrameNum, gSpecAntilag1, gSpecAntilag2, roughness );

    // Clamp history and combine with the current frame
    diffHistory = STL::Color::Clamp( diffM1, diffSigma * diffTemporalAccumulationParams.y, diffHistory );
    float diffHistoryWeight = 1.0 - REBLUR_TS_MAX_HISTORY_WEIGHT * diffAntilag * diffTemporalAccumulationParams.x;

    float4 diffResult;
    diffResult.xyz = lerp( diffHistory.xyz, diff.xyz, diffHistoryWeight );
    diffResult.w = lerp( diffHistory.w, diff.w, max( diffHistoryWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );
    diffResult = Sanitize( diffResult, diff );

    specHistory = STL::Color::Clamp( specM1, specSigma * specTemporalAccumulationParams.y, specHistory );
    float specHistoryWeight = 1.0 - REBLUR_TS_MAX_HISTORY_WEIGHT * specAntilag * specTemporalAccumulationParams.x;

    float4 specResult;
    specResult.xyz = lerp( specHistory.xyz, spec.xyz, specHistoryWeight );
    specResult.w = lerp( specHistory.w, spec.w, max( specHistoryWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughness ) ) );
    specResult = Sanitize( specResult, spec );

    // Output
    gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( viewZ, diffInternalData.y, N, roughness, specInternalData.y );
    gOut_Diff[ pixelPos ] = diffResult;
    gOut_Spec[ pixelPos ] = specResult;

    #if( REBLUR_DEBUG == REBLUR_SHOW_ACCUM_SPEED )
        diffResult.w = saturate( diffInternalData.y / ( gDiffMaxAccumulatedFrameNum + 1.0 ) );
        specResult.w = saturate( specInternalData.y / ( gSpecMaxAccumulatedFrameNum + 1.0 ) );
    #elif( REBLUR_DEBUG == REBLUR_SHOW_VIRTUAL_HISTORY_AMOUNT || REBLUR_DEBUG == REBLUR_SHOW_VIRTUAL_HISTORY_CONFIDENCE )
        specResult.w = virtualHistoryAmount;
    #elif( REBLUR_DEBUG == REBLUR_SHOW_PARALLAX )
        specResult.w = parallax;
    #elif( REBLUR_DEBUG == REBLUR_SHOW_EDGE )
        diffResult.w = edge;
    #endif

    gOut_Diff_Copy[ pixelPos ] = diffResult;
    gOut_Spec_Copy[ pixelPos ] = specResult;
}
