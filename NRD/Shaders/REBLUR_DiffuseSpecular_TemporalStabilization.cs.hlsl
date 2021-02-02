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
    float4 gDiffHitDistParams;
    float4 gSpecHitDistParams;
    float4 gAntilagThresholds;
    float2 gAntilagSigmaScale;
    float2 gMotionVectorScale;
    float gDiffMaxAccumulatedFrameNum;
    float gSpecMaxAccumulatedFrameNum;
};

#define USE_8x8
#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_InternalData, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Diff, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 5, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Spec, t, 6, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 7, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<uint2>, gOut_ViewZ_Normal_Roughness_AccumSpeeds, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff_Copy, u, 2, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 3, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec_Copy, u, 4, 0 );

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];
groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = gIn_ViewZ[ globalId ];
    s_Diff[ sharedId.y ][ sharedId.x ] = gIn_Diff[ globalId ];
    s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Spec[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Debug
    #if( NRD_DEBUG == NRD_SHOW_MIPS )
        float4 d = gIn_Diff[ pixelPos ];
        gOut_Diff[ pixelPos ] = d;
        gOut_Diff_Copy[ pixelPos ] = d;

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
            gOut_Diff[ pixelPos ] = 0;
            gOut_Diff_Copy[ pixelPos ] = 0;

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
    float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], roughness, virtualHistoryAmount );
    float2 diffInternalData = internalData.xy;
    float2 specInternalData = internalData.zw;

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = STL::Geometry::RotateVector( gViewToWorld, -Xv ) * invDistToPoint;

    // Local variance
    float2 sum = 1.0;
    float3 Nsum = N;

    float4 diff = s_Diff[ smemPos.y ][ smemPos.x ];
    float4 diffM1 = diff;
    float4 diffM2 = diff * diff;
    float4 diffMaxInput = -INF;
    float4 diffMinInput = INF;
    float diffNormalParams = GetNormalWeightParamsRoughEstimate( 1.0 );

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

            float2 w = GetBilateralWeight( z, centerZ );
            w.x *= GetNormalWeight( diffNormalParams, N, n.xyz );
            w.y *= GetNormalWeight( specNormalParams, N, n.xyz );
            w.y *= GetRoughnessWeight( roughnessParams, n.w );
            w = STL::Math::Pow01( w, TS_WEIGHT_BOOST_POWER );

            Nsum += n.xyz * w.y;

            float4 d = s_Diff[ pos.y ][ pos.x ];
            float dt = float( w.x == 0.0 ) * INF;
            diffMaxInput = max( d - dt, diffMaxInput );
            diffMinInput = min( d + dt, diffMinInput );

            diffM1 += d * w.x;
            diffM2 += d * d * w.x;

            float4 s = s_Spec[ pos.y ][ pos.x ];
            float st = float( w.y == 0.0 ) * INF;
            specMaxInput = max( s - st, specMaxInput );
            specMinInput = min( s + st, specMinInput );

            specM1 += s * w.y;
            specM2 += s * s * w.y;

            sum += w;
        }
    }

    float2 invSum = STL::Math::PositiveRcp( sum );
    diffM1 *= invSum.x;
    diffM2 *= invSum.x;
    float4 diffSigma = GetVariance( diffM1, diffM2 );

    specM1 *= invSum.y;
    specM2 *= invSum.y;
    float4 specSigma = GetVariance( specM1, specM2 );

    float3 Navg = Nsum * invSum.y;
    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );

    // Apply RCRS
    float2 rcrsWeight = ( sum - 1.0 ) / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) - 1.0 );

    float4 diffRcrs = min( diff, diffMaxInput );
    diffRcrs = max( diffRcrs, diffMinInput );
    diff = lerp( diff, diffRcrs, rcrsWeight.x * float( !IsReference() ) );

    float4 specRcrs = min( spec, specMaxInput );
    specRcrs = max( specRcrs, specMinInput );
    spec = lerp( spec, specRcrs, rcrsWeight.y * float( !IsReference() ) );

    // Compute previous position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, IsWorldSpaceMotion() );
    float isInScreen = IsInScreen( pixelUvPrev );
    float2 pixelMotion = pixelUvPrev - pixelUv;
    float3 Xprev = X + motionVector * float( IsWorldSpaceMotion() );

    // Sample history ( surface motion )
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float4 diffHistory = BicubicFilterNoCorners( gIn_History_Diff, gLinearClamp, pixelPosPrev, gInvScreenSize );
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

    // Adjust accumulation speed for virtual motion if confidence is low
    float accumSpeedScale = lerp( 1.0, virtualHistoryConfidence, virtualHistoryAmount );
    accumSpeedScale = lerp( accumSpeedScale, 1.0, roughness );
    accumSpeedScale = lerp( accumSpeedScale, 1.0, 1.0 / ( 1.0 + specInternalData.y ) );

    float specMinAccumSpeed = min( specInternalData.y, ( MIP_NUM - 1 ) * STL::Math::Sqrt01( roughness ) );
    specInternalData.y = specMinAccumSpeed + ( specInternalData.y - specMinAccumSpeed ) * accumSpeedScale;

    // Specular history
    float virtualUnclampedAmount = lerp( virtualHistoryConfidence * SPEC_FORCED_VIRTUAL_CLAMPING, 1.0, roughness * roughness );
    specHistoryVirtual = lerp( specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount );

    float4 specHistory = lerp( specHistorySurface, specHistoryVirtual, virtualHistoryAmount );

    // History weight
    float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffInternalData.y, parallax );
    diffTemporalAccumulationParams.y = 1.0 + ( diffTemporalAccumulationParams.y - 1.0 ) * rcrsWeight.x;

    float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specInternalData.y, parallax, roughnessModified, virtualHistoryAmount );
    specTemporalAccumulationParams.y = 1.0 + ( specTemporalAccumulationParams.y - 1.0 ) * rcrsWeight.y;

    // Antilag
    float diffAntilag = 1.0;
    float specAntilag = 1.0;

    #if( USE_ANTILAG == 1 )
        float2 antilagSensitivityToSmallValues = gAntilagThresholds.zw * 5.0 + 0.000001;

        float2 diffDelta = abs( diffHistory.xw - diffM1.xw ) - diffSigma.xw * gAntilagSigmaScale;
        diffDelta /= min( diffM1.xw, diffHistory.xw ) + diffSigma.xw * gAntilagSigmaScale + antilagSensitivityToSmallValues;
        diffDelta = STL::Math::LinearStep( gAntilagThresholds.zw, gAntilagThresholds.xy, diffDelta );
        diffDelta *= diffDelta;

        float diffFade = diffInternalData.y / ( 1.0 + diffInternalData.y );
        diffFade *= diffTemporalAccumulationParams.x;

        diffAntilag = diffDelta.x * diffDelta.y;
        diffAntilag = lerp( 1.0, diffAntilag, diffFade );

        float2 specDelta = abs( specHistory.xw - specM1.xw ) - specSigma.xw * gAntilagSigmaScale;
        specDelta /= min( specM1.xw, specHistory.xw ) + specSigma.xw * gAntilagSigmaScale + antilagSensitivityToSmallValues;
        specDelta = STL::Math::LinearStep( gAntilagThresholds.zw, gAntilagThresholds.xy, specDelta );
        specDelta *= specDelta;

        float specFade = specInternalData.y / ( 1.0 + specInternalData.y );
        specFade *= specTemporalAccumulationParams.x;

        specAntilag = specDelta.x * specDelta.y;
        specAntilag = lerp( 1.0, specAntilag, specFade );
    #endif

    #if( USE_LIMITED_ANTILAG == 1 )
        float diffMinAccumSpeed = min( diffInternalData.y, GetMipLevel( 0.0 ) );
        diffInternalData.y = diffMinAccumSpeed + ( diffInternalData.y - diffMinAccumSpeed ) * diffAntilag;

        specInternalData.y = specMinAccumSpeed + ( specInternalData.y - specMinAccumSpeed ) * specAntilag;
    #else
        diffInternalData.y *= diffAntilag;
        specInternalData.y *= specAntilag;
    #endif

    // Clamp history and combine with the current frame
    float4 diffMin = diffM1 - diffSigma * diffTemporalAccumulationParams.y;
    float4 diffMax = diffM1 + diffSigma * diffTemporalAccumulationParams.y;
    diffHistory = clamp( diffHistory, diffMin, diffMax );

    float diffHistoryWeight = TS_MAX_HISTORY_WEIGHT * diffAntilag;
    float4 diffResult = lerp( diff, diffHistory, diffHistoryWeight * diffTemporalAccumulationParams.x );

    specMin = specM1 - specSigma * specTemporalAccumulationParams.y;
    specMax = specM1 + specSigma * specTemporalAccumulationParams.y;
    specHistory = clamp( specHistory, specMin, specMax );

    float specHistoryWeight = TS_MAX_HISTORY_WEIGHT * specAntilag;
    float4 specResult = lerp( spec, specHistory, specHistoryWeight * specTemporalAccumulationParams.x );

    // Get rid of possible negative values
    diffResult.xyz = _NRD_YCoCgToLinear( diffResult.xyz );
    diffResult.w = max( diffResult.w, 0.0 );
    diffResult.xyz = _NRD_LinearToYCoCg( diffResult.xyz );

    specResult.xyz = _NRD_YCoCgToLinear( specResult.xyz );
    specResult.w = max( specResult.w, 0.0 );
    specResult.xyz = _NRD_LinearToYCoCg( specResult.xyz );

    // Output
    gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( centerZ, diffInternalData.y, N, roughness, specInternalData.y );
    gOut_Diff[ pixelPos ] = diffResult;
    gOut_Spec[ pixelPos ] = specResult;

    #if( NRD_DEBUG == NRD_SHOW_ACCUM_SPEED  )
        diffResult.w = saturate( diffInternalData.y / ( gDiffMaxAccumulatedFrameNum + 1.0 ) );
        specResult.w = saturate( specInternalData.y / ( gSpecMaxAccumulatedFrameNum + 1.0 ) );
    #elif( NRD_DEBUG == NRD_SHOW_ANTILAG )
        diffResult.w = diffAntilag;
        specResult.w = specAntilag;
    #elif( NRD_DEBUG == NRD_SHOW_VIRTUAL_HISTORY_AMOUNT )
        specResult.w = virtualHistoryAmount;
    #elif( NRD_DEBUG == NRD_SHOW_VIRTUAL_HISTORY_CONFIDENCE )
        specResult.w = virtualHistoryConfidence;
    #endif

    gOut_Diff_Copy[ pixelPos ] = diffResult;
    gOut_Spec_Copy[ pixelPos ] = specResult;
}
