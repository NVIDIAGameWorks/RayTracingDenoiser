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
    float4x4 gViewToClip;
    float4 gFrustum;
    float2 gInvScreenSize;
    float2 gScreenSize;
    uint gBools;
    float gIsOrtho;
    float gUnproject;
    float gDebug;
    float gInf;
    float gPlaneDistSensitivity;
    uint gFrameIndex;
    float gFramerateScale;

    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4x4 gWorldToClip;
    float4 gCameraDelta;
    float4 gSpecHitDistParams;
    float4 gAntilag1;
    float4 gAntilag2;
    float2 gMotionVectorScale;
    float gSpecMaxAccumulatedFrameNum;
};

#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_InternalData, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Spec, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 5, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<uint2>, gOut_ViewZ_Normal_Roughness_AccumSpeeds, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec_Copy, u, 2, 0 );

groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = gIn_ViewZ[ globalId ];
    s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Spec[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Debug
    #if( NRD_DEBUG == NRD_SHOW_MIPS )
        float4 s = gIn_Spec[ pixelPos ];
        gOut_Spec[ pixelPos ] = s;
        gOut_Spec_Copy[ pixelPos ] = s;
        return;
    #endif

    PRELOAD_INTO_SMEM;

    // Early out
    int2 smemPos = threadId + BORDER;
    float centerZ = s_ViewZ[ smemPos.y ][ smemPos.x ];

    [branch]
    if( abs( centerZ ) > abs( gInf ) )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Spec[ pixelPos ] = 0;
            gOut_Spec_Copy[ pixelPos ] = 0;
        #endif
        gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( INF, 0.0, float3( 0, 0, 1 ), 1.0, 0.0 );
        return;
    }

    // Normal and roughness
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Internal data
    float virtualHistoryAmount;
    float2 specInternalData = UnpackSpecInternalData( gIn_InternalData[ pixelPos ], roughness, virtualHistoryAmount );

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = STL::Geometry::RotateVector( gViewToWorld, -Xv ) * invDistToPoint;

    // Local variance
    float sum = 1.0;
    float3 Nsum = N;

    float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
    float4 specM1 = spec;
    float4 specM2 = spec * spec;
    float4 specMaxInput = -INF;
    float4 specMinInput = INF;
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

            float w = GetBilateralWeight( z, centerZ );
            w *= GetNormalWeight( specNormalParams, N, n.xyz );
            w *= GetRoughnessWeight( roughnessParams, n.w );
            w = STL::Math::Pow01( w, TS_WEIGHT_BOOST_POWER );

            Nsum += n.xyz * w;

            float4 s = s_Spec[ pos.y ][ pos.x ];
            float st = float( w == 0.0 ) * INF;
            specMaxInput = max( s - st, specMaxInput );
            specMinInput = min( s + st, specMinInput );

            specM1 += s * w;
            specM2 += s * s * w;

            sum += w;
        }
    }

    float invSum = STL::Math::PositiveRcp( sum );
    specM1 *= invSum;
    specM2 *= invSum;
    float4 specSigma = GetVariance( specM1, specM2 );

    float3 Navg = Nsum * invSum;
    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );

    // Apply RCRS
    float rcrsWeight = ( sum - 1.0 ) / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) - 1.0 );

    float4 specRcrs = min( spec, specMaxInput );
    specRcrs = max( specRcrs, specMinInput );
    spec = lerp( spec, specRcrs, rcrsWeight * float( !IsReference() ) );

    // Compute previous position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, IsWorldSpaceMotion() );
    float isInScreen = IsInScreen( pixelUvPrev );
    float3 Xprev = X + motionVector * float( IsWorldSpaceMotion() );

    // Sample history ( surface motion )
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float4 specHistorySurface = BicubicFilterNoCorners( gIn_History_Spec, gLinearClamp, pixelPosPrev, gInvScreenSize );

    // Sample history ( virtual motion )
    float hitDist = GetHitDist( spec.w, centerZ, gSpecHitDistParams, roughness );
    float NoV = abs( dot( N, V ) );
    float3 Xvirtual = GetXvirtual( X, Xprev, V, NoV, roughnessModified, hitDist );
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );
    float4 specHistoryVirtual = gIn_History_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev, 0.0 );

    float sigmaScale = 1.0 + 0.125 * gFramerateScale;
    float4 specMin = specM1 - specSigma * sigmaScale;
    float4 specMax = specM1 + specSigma * sigmaScale;
    float4 specHistoryVirtualClamped = clamp( specHistoryVirtual, specMin, specMax );

    // Compute parallax
    float parallax = ComputeParallax( pixelUv, Xprev, gCameraDelta.xyz, gWorldToClip );

    // Hit distance based disocclusion for virtual motion
    float hitDistDelta = abs( specHistoryVirtual.w - spec.w ) - specSigma.w * 0.5; // TODO: was 0.1
    float hitDistMin = max( specHistoryVirtual.w, spec.w );
    hitDistDelta = GetHitDist( hitDistDelta, centerZ, gSpecHitDistParams, roughness );
    hitDistMin = GetHitDist( hitDistMin, centerZ, gSpecHitDistParams, roughness );
    hitDistDelta *= STL::Math::PositiveRcp( hitDistMin + abs( centerZ ) );

    float thresholdMin = 0.02 * STL::Math::LinearStep( 0.2, 0.01, parallax ); // TODO: thresholdMin needs to be set to 0, but it requires very clean hit distances
    float thresholdMax = lerp( 0.01, 0.25, roughnessModified * roughnessModified ) + thresholdMin;
    float virtualHistoryConfidence = STL::Math::LinearStep( thresholdMax, thresholdMin, hitDistDelta );
    virtualHistoryConfidence *= 1.0 - STL::Math::SmoothStep( 0.25, 1.0, parallax );

    // Adjust accumulation speed for virtual motion if confidence is low
    float accumSpeedScale = lerp( 1.0, virtualHistoryConfidence, virtualHistoryAmount );
    accumSpeedScale = lerp( accumSpeedScale, 1.0, roughness );
    accumSpeedScale = lerp( accumSpeedScale, 1.0, 1.0 / ( 1.0 + specInternalData.y ) );

    float specMinAccumSpeed = min( specInternalData.y, ( MIP_NUM - 1 ) * STL::Math::Sqrt01( roughness ) );
    specInternalData.y = specMinAccumSpeed + ( specInternalData.y - specMinAccumSpeed ) * accumSpeedScale;

    // Specular history
    float virtualForcedConfidence = lerp( 0.75, 0.95, STL::Math::LinearStep( 0.04, 0.25, roughness ) );
    float virtualUnclampedAmount = lerp( virtualHistoryConfidence * virtualForcedConfidence, 1.0, roughness * roughness );
    specHistoryVirtual = lerp( specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount );

    float4 specHistory = lerp( specHistorySurface, specHistoryVirtual, virtualHistoryAmount );

    // History weight
    float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specInternalData.y, parallax, roughnessModified, virtualHistoryAmount );
    specTemporalAccumulationParams.y = 1.0 + ( specTemporalAccumulationParams.y - 1.0 ) * rcrsWeight;

    // Antilag
    float specAntilag = ComputeAntilagScale( specInternalData.y, specHistory.xw, specM1.xw, specSigma.xw, specTemporalAccumulationParams, gAntilag1, gAntilag2, roughness );

    // Clamp history and combine with the current frame
    specMin = specM1 - specSigma * specTemporalAccumulationParams.y;
    specMax = specM1 + specSigma * specTemporalAccumulationParams.y;
    specHistory = clamp( specHistory, specMin, specMax );

    float specHistoryWeight = TS_MAX_HISTORY_WEIGHT * specAntilag;
    float4 specResult = lerp( spec, specHistory, specHistoryWeight * specTemporalAccumulationParams.x );

    // Get rid of possible negative values
    specResult.xyz = _NRD_YCoCgToLinear( specResult.xyz );
    specResult.w = max( specResult.w, 0.0 );
    specResult.xyz = _NRD_LinearToYCoCg( specResult.xyz );

    // Output
    gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( centerZ, 0.0, N, roughness, specInternalData.y );
    gOut_Spec[ pixelPos ] = specResult;

    #if( NRD_DEBUG == NRD_SHOW_ACCUM_SPEED  )
        specResult.w = saturate( specInternalData.y / ( gSpecMaxAccumulatedFrameNum + 1.0 ) );
    #elif( NRD_DEBUG == NRD_SHOW_VIRTUAL_HISTORY_AMOUNT )
        specResult.w = virtualHistoryAmount;
    #elif( NRD_DEBUG == NRD_SHOW_VIRTUAL_HISTORY_CONFIDENCE )
        specResult.w = virtualHistoryConfidence;
    #elif( NRD_DEBUG == NRD_SHOW_PARALLAX )
        specResult.w = parallax;
    #endif

    gOut_Spec_Copy[ pixelPos ] = specResult;
}
