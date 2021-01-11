/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

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
    float4 gSpecHitDistParams;
    float3 gCameraDelta;
    float gAntilag;
    float2 gMotionVectorScale;
    float2 gAntilagRadianceThreshold;
    float gDiffNoisinessBlurrinessBalance;
    float gSpecNoisinessBlurrinessBalance;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_InternalData, t, 3, 0 );
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
    #if( SHOW_MIPS )
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
    float virtualMotionAmount;
    float2x2 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], roughness, virtualMotionAmount );
    float2 diffInternalData = internalData[ 0 ];
    float2 specInternalData = internalData[ 1 ];

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = STL::Geometry::RotateVector( gViewToWorld, -Xv ) * invDistToPoint;

    // Local variance
    float2 sum = 0;
    float2 sum1 = 0;

    float4 diffM1 = 0;
    float4 diffM2 = 0;
    float4 diff = 0;
    float4 diffMaxInput = -INF;
    float4 diffMinInput = INF;
    float diffNormalParams = GetNormalWeightParamsRoughEstimate( 1.0 );

    float4 specM1 = 0;
    float4 specM2 = 0;
    float4 spec = 0;
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
            int2 pos = threadId + int2( dx, dy );
            float4 diffSignal = s_Diff[ pos.y ][ pos.x ];
            float4 normalAndRoughness = s_Normal_Roughness[ pos.y ][ pos.x ];
            float4 specSignal = s_Spec[ pos.y ][ pos.x ];
            float z = s_ViewZ[ pos.y ][ pos.x ];

            float2 w = 1.0;
            if( dx == BORDER && dy == BORDER )
            {
                diff = diffSignal;
                spec = specSignal;
            }
            else
            {
                w = GetBilateralWeight( z, centerZ );
                w.x *= GetNormalWeight( diffNormalParams, N, normalAndRoughness.xyz );

                float d = float( w.x == 0.0 ) * INF;
                diffMaxInput = max( diffSignal - d, diffMaxInput );
                diffMinInput = min( diffSignal + d, diffMinInput );

                w.y *= GetNormalWeight( specNormalParams, N, normalAndRoughness.xyz );
                w.y *= GetRoughnessWeight( roughnessParams, normalAndRoughness.w );

                float s = float( w.y == 0.0 ) * INF;
                specMaxInput = max( specSignal - s, specMaxInput );
                specMinInput = min( specSignal + s, specMinInput );
            }

            diffM1 += diffSignal * w.x;
            diffM2 += diffSignal * diffSignal * w.x;

            specM1 += specSignal * w.y;
            specM2 += specSignal * specSignal * w.y;

            sum += w;
            sum1 += float2( w > RCRS_THRESHOLD );
        }
    }

    float2 invSum = STL::Math::PositiveRcp( sum );
    diffM1 *= invSum.x;
    diffM2 *= invSum.x;
    float4 diffSigma = GetVariance( diffM1, diffM2 );

    specM1 *= invSum.y;
    specM2 *= invSum.y;
    float4 specSigma = GetVariance( specM1, specM2 );

    // Apply RCRS
    float2 rcrsWeight = ( sum1 - 1.0 ) / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) - 1.0 );
    rcrsWeight = STL::Math::Sqrt01( rcrsWeight );
    rcrsWeight *= float( !IsReference() ); // also turns off antilag in reference mode

    float4 diffRcrs = min( diff, diffMaxInput );
    diffRcrs = max( diffRcrs, diffMinInput );
    diff = lerp( diff, diffRcrs, rcrsWeight.x );

    float4 specRcrs = min( spec, specMaxInput );
    specRcrs = max( specRcrs, specMinInput );
    spec = lerp( spec, specRcrs, rcrsWeight.y );

    // Compute previous position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, IsWorldSpaceMotion() );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Sample history ( surface motion )
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float4 diffHistory = BicubicFilterNoCorners( gIn_History_Diff, gLinearClamp, pixelPosPrev, gInvScreenSize );
    float4 historySurface = BicubicFilterNoCorners( gIn_History_Spec, gLinearClamp, pixelPosPrev, gInvScreenSize );

    // Sample history ( virtual motion ) and mix
    float3 Xprev = X + motionVector * float( IsWorldSpaceMotion() );
    float hitDist = GetHitDistance( spec.w, centerZ, gSpecHitDistParams, roughness );
    float3 Xvirtual = GetXvirtual( X, Xprev, N, V, roughness, hitDist );
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );
    float4 historyVirtual = gIn_History_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev, 0.0 );
    float4 specHistory = lerp( historySurface, historyVirtual, virtualMotionAmount );

    // Compute parallax
    float parallax = ComputeParallax( pixelUv, 1.0, Xprev, gCameraDelta, gWorldToClip );
    float parallaxMod = parallax * ( 1.0 - virtualMotionAmount );

    // History weight
    float motionLength = length( pixelUvPrev - pixelUv );
    float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffInternalData.y, motionLength );
    float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specInternalData.y, motionLength, parallaxMod, roughness );

    // Antilag
    float diffAntiLag = 1.0;
    float specAntiLag = 1.0;
    if( gAntilag != 0.0 && USE_ANTILAG == 1 )
    {
        // TODO: if compression is used delta.x needs to be decompressed, but it doesn't affect the behavior, because heavily compressed values do not lag
        // TODO: Keep an eye on delta calculation! It was changed from "abs( Xhistory.xw - X.xw ) - Xsigma.xw * 2". It is a good change. Using just average
        // offers better propagation between frames and allows to reduce variance scale from 2 to 1. There is a chance that with 1 some places can look less stable...
        float2 diffDelta = abs( diffHistory.xw - diffM1.xw ) - diffSigma.xw * lerp( 2.0, 1.0, gDiffNoisinessBlurrinessBalance );
        diffDelta = STL::Math::LinearStep( float2( gAntilagRadianceThreshold.y, 0.1 ), float2( gAntilagRadianceThreshold.x, 0.01 ), diffDelta );
        diffDelta = STL::Math::Pow01( diffDelta, float2( 2.0, 8.0 ) );

        float diffFade = diffInternalData.y / ( 1.0 + diffInternalData.y );
        diffFade *= diffTemporalAccumulationParams.x;
        diffFade *= rcrsWeight.x;

        diffAntiLag = min( diffDelta.x, diffDelta.y );
        diffAntiLag = lerp( 1.0, diffAntiLag, diffFade );

        float2 specDelta = abs( specHistory.xw - specM1.xw ) - specSigma.xw * lerp( 2.0, 1.0, gSpecNoisinessBlurrinessBalance );
        specDelta = STL::Math::LinearStep( float2( gAntilagRadianceThreshold.y, 0.1 ), float2( gAntilagRadianceThreshold.x, 0.01 ), specDelta );
        specDelta = STL::Math::Pow01( specDelta, float2( 2.0, 8.0 ) );

        float specFade = specInternalData.y / ( 1.0 + specInternalData.y );
        specFade *= specTemporalAccumulationParams.x;
        specFade *= rcrsWeight.y;

        specAntiLag = min( specDelta.x, specDelta.y );
        specAntiLag = lerp( 1.0, specAntiLag, specFade );
    }

    // Doesn't allow more than some portion of average for sigma amplitude
    float da = diffSigma.x * diffTemporalAccumulationParams.y;
    float db = diffM1.x * TS_SIGMA_AMPLITUDE_CLAMP;
    diffTemporalAccumulationParams.y = min( da, db ) * STL::Math::PositiveRcp( diffSigma.x );

    float sa = specSigma.x * specTemporalAccumulationParams.y;
    float sb = specM1.x * TS_SIGMA_AMPLITUDE_CLAMP;
    specTemporalAccumulationParams.y = min( sa, sb ) * STL::Math::PositiveRcp( specSigma.x );

    // Clamp history and combine with the current frame
    float4 diffMin = diffM1 - diffSigma * diffTemporalAccumulationParams.y;
    float4 diffMax = diffM1 + diffSigma * diffTemporalAccumulationParams.y;
    diffHistory = clamp( diffHistory, diffMin, diffMax );

    float diffHistoryWeight = TS_MAX_HISTORY_WEIGHT * diffAntiLag;
    float4 diffResult = lerp( diff, diffHistory, diffHistoryWeight * diffTemporalAccumulationParams.x );

    float4 specMin = specM1 - specSigma * specTemporalAccumulationParams.y;
    float4 specMax = specM1 + specSigma * specTemporalAccumulationParams.y;
    specHistory = clamp( specHistory, specMin, specMax );

    float specHistoryWeight = TS_MAX_HISTORY_WEIGHT * specAntiLag;
    float4 specResult = lerp( spec, specHistory, specHistoryWeight * specTemporalAccumulationParams.x );

    // Get rid of possible negative values
    diffResult.xyz = _NRD_YCoCgToLinear( diffResult.xyz );
    diffResult.w = max( diffResult.w, 0.0 );
    diffResult.xyz = _NRD_LinearToYCoCg( diffResult.xyz );

    specResult.xyz = _NRD_YCoCgToLinear( specResult.xyz );
    specResult.w = max( specResult.w, 0.0 );
    specResult.xyz = _NRD_LinearToYCoCg( specResult.xyz );

    // Output
    diffInternalData.y *= diffAntiLag;
    specInternalData.y *= specAntiLag;

    gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( centerZ, diffInternalData.y, N, roughness, specInternalData.y );
    gOut_Diff[ pixelPos ] = diffResult;
    gOut_Spec[ pixelPos ] = specResult;

    #if( SHOW_ACCUM_SPEED == 1 )
        diffResult.w = saturate( diffInternalData.y / MAX_ACCUM_FRAME_NUM );
        specResult.w = saturate( specInternalData.y / MAX_ACCUM_FRAME_NUM );
    #elif( SHOW_ANTILAG == 1 )
        diffResult.w = diffAntiLag;
        specResult.w = specAntiLag;
    #endif

    gOut_Diff_Copy[ pixelPos ] = diffResult;
    gOut_Spec_Copy[ pixelPos ] = specResult;
}
