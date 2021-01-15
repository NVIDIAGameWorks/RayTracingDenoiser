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
    float4 gIntensityAntilag;
    float4 gHitDistanceAntilag;
    float2 gMotionVectorScale;
};

#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_InternalData, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Diff, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 5, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<uint2>, gOut_ViewZ_Normal_Roughness_AccumSpeeds, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff_Copy, u, 2, 0 );

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = gIn_ViewZ[ globalId ];
    s_Diff[ sharedId.y ][ sharedId.x ] = gIn_Diff[ globalId ];
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
        #endif
        gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( INF, 0.0, float3( 0, 0, 1 ), 1.0, 0.0 );
        return;
    }

    // Normal and roughness
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Internal data
    float2 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ] );

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );

    // Local variance
    float sum = 0;

    float4 diffM1 = 0;
    float4 diffM2 = 0;
    float4 diff = 0;
    float4 diffMaxInput = -INF;
    float4 diffMinInput = INF;
    float diffNormalParams = GetNormalWeightParamsRoughEstimate( 1.0 );

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 pos = threadId + int2( dx, dy );
            float4 diffSignal = s_Diff[ pos.y ][ pos.x ];
            float4 normalAndRoughness = s_Normal_Roughness[ pos.y ][ pos.x ];
            float z = s_ViewZ[ pos.y ][ pos.x ];

            float w = 1.0;
            if( dx == BORDER && dy == BORDER )
                diff = diffSignal;
            else
            {
                w = GetBilateralWeight( z, centerZ );
                w *= GetNormalWeight( diffNormalParams, N, normalAndRoughness.xyz );

                float d = float( w == 0.0 ) * INF;
                diffMaxInput = max( diffSignal - d, diffMaxInput );
                diffMinInput = min( diffSignal + d, diffMinInput );
            }

            w = STL::Math::Pow01( w, TS_WEIGHT_BOOST_POWER );

            diffM1 += diffSignal * w;
            diffM2 += diffSignal * diffSignal * w;

            sum += w;
        }
    }

    float invSum = STL::Math::PositiveRcp( sum );
    diffM1 *= invSum;
    diffM2 *= invSum;
    float4 diffSigma = GetVariance( diffM1, diffM2 );

    // Apply RCRS
    float rcrsWeight = ( sum - 1.0 ) / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) - 1.0 );

    float4 diffRcrs = min( diff, diffMaxInput );
    diffRcrs = max( diffRcrs, diffMinInput );
    diff = lerp( diff, diffRcrs, rcrsWeight * float( !IsReference() ) );

    // Compute previous position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, IsWorldSpaceMotion() );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Sample history ( surface motion )
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float4 diffHistory = BicubicFilterNoCorners( gIn_History_Diff, gLinearClamp, pixelPosPrev, gInvScreenSize );

    // History weight
    float motionLength = length( pixelUvPrev - pixelUv );

    float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffInternalData.y, motionLength );
    diffTemporalAccumulationParams.y = 1.0 + ( diffTemporalAccumulationParams.y - 1.0 ) * rcrsWeight.x;

    // Antilag
    float diffAntilag = 1.0;

    #if( USE_ANTILAG == 1 )
        // TODO: if compression is used delta.x needs to be decompressed, but it doesn't affect the behavior, because heavily compressed values do not lag
        float2 diffDelta = abs( diffHistory.xw - diffM1.xw ) - diffSigma.xw * float2( gIntensityAntilag.x, gHitDistanceAntilag.x );
        diffDelta = STL::Math::LinearStep( float2( gIntensityAntilag.z, gHitDistanceAntilag.z ), float2( gIntensityAntilag.y, gHitDistanceAntilag.y ), diffDelta );
        diffDelta *= diffDelta;

        float diffFade = diffInternalData.y / ( 1.0 + diffInternalData.y );
        diffFade *= diffTemporalAccumulationParams.x;

        diffAntilag = min( diffDelta.x, diffDelta.y );
        diffAntilag = lerp( 1.0, diffAntilag, diffFade );
    #endif

    #if( USE_LIMITED_ANTILAG == 1 )
        float diffMinAccumSpeed = min( diffInternalData.y, GetMipLevel( 0.0 ) );
        diffInternalData.y = diffMinAccumSpeed + ( diffInternalData.y - diffMinAccumSpeed ) * diffAntilag;
    #else
        diffInternalData.y *= diffAntilag;
    #endif

    // Clamp history and combine with the current frame
    float4 diffMin = diffM1 - diffSigma * diffTemporalAccumulationParams.y;
    float4 diffMax = diffM1 + diffSigma * diffTemporalAccumulationParams.y;
    diffHistory = clamp( diffHistory, diffMin, diffMax );

    float diffHistoryWeight = TS_MAX_HISTORY_WEIGHT * diffAntilag;
    float4 diffResult = lerp( diff, diffHistory, diffHistoryWeight * diffTemporalAccumulationParams.x );

    // Get rid of possible negative values
    diffResult.xyz = _NRD_YCoCgToLinear( diffResult.xyz );
    diffResult.w = max( diffResult.w, 0.0 );
    diffResult.xyz = _NRD_LinearToYCoCg( diffResult.xyz );

    // Output
    gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( centerZ, diffInternalData.y, N, 1.0, 0.0 );
    gOut_Diff[ pixelPos ] = diffResult;

    #if( SHOW_ACCUM_SPEED == 1 )
        diffResult.w = saturate( diffInternalData.y / MAX_ACCUM_FRAME_NUM );
    #elif( SHOW_ANTILAG == 1 )
        diffResult.w = diffAntilag;
    #endif

    gOut_Diff_Copy[ pixelPos ] = diffResult;
}
