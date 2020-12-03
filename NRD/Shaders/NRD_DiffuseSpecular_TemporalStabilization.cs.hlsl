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
    float gMetersToUnits;
    float gIsOrtho;
    float gUnproject;
    float gDebug;
    float gInf;
    float gReference;
    uint gFrameIndex;
    uint gWorldSpaceMotion;

    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4x4 gWorldToClip;
    float4 gSpecScalingParams;
    float3 gCameraDelta;
    float gAntilag;
    float2 gMotionVectorScale;
    float2 gAntilagRadianceThreshold;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<uint>, gIn_InternalData, t, 3, 0 );
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
    if( abs( centerZ ) > gInf )
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
    float2x3 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], roughness, virtualMotionAmount );
    float diffAccumSpeed = internalData[ 0 ].z;
    float specAccumSpeed = internalData[ 1 ].z;

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = STL::Geometry::RotateVector( gViewToWorld, -Xv ) * invDistToPoint;

    // Local variance
    float2 sum = 0;

    float4 diffM1 = 0;
    float4 diffM2 = 0;
    float4 diff = 0;
    float4 diffMaxInput = -INF;
    float4 diffMinInput = INF;

    float4 specM1 = 0;
    float4 specM2 = 0;
    float4 spec = 0;
    float4 specMaxInput = -INF;
    float4 specMinInput = INF;

    float diffNormalParams = GetNormalWeightParamsRoughEstimate( 1.0 );
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
            float4 specSignal = s_Spec[ pos.y ][ pos.x ];
            float4 normalAndRoughness = s_Normal_Roughness[ pos.y ][ pos.x ];
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
                w.y *= GetNormalWeight( specNormalParams, N, normalAndRoughness.xyz );
                w.y *= GetRoughnessWeight( roughnessParams, normalAndRoughness.w );

                float a = float( w.x == 0.0 ) * INF;
                diffMaxInput = max( diffSignal - a, diffMaxInput );
                diffMinInput = min( diffSignal + a, diffMinInput );

                a = float( w.y == 0.0 ) * INF;
                specMaxInput = max( specSignal - a, specMaxInput );
                specMinInput = min( specSignal + a, specMinInput );
            }

            diffM1 += diffSignal * w.x;
            diffM2 += diffSignal * diffSignal * w.x;
            specM1 += specSignal * w.y;
            specM2 += specSignal * specSignal * w.y;

            sum += w;
        }
    }

    float2 invSum = STL::Math::PositiveRcp( sum );
    diffM1 *= invSum.x;
    diffM2 *= invSum.x;
    specM1 *= invSum.y;
    specM2 *= invSum.y;

    float4 diffSigma = GetVariance( diffM1, diffM2 );
    float4 specSigma = GetVariance( specM1, specM2 );

    // Apply RCRS
    float2 rcrsWeight = ( sum - 1.0 ) / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) - 1.0 );
    rcrsWeight = STL::Math::Sqrt01( rcrsWeight );
    rcrsWeight *= 1.0 - gReference;

    float4 rcrsResult = min( diff, diffMaxInput );
    rcrsResult = max( rcrsResult, diffMinInput );
    diff = lerp( diff, rcrsResult, rcrsWeight.x );

    rcrsResult = min( spec, specMaxInput );
    rcrsResult = max( rcrsResult, specMinInput );
    spec = lerp( spec, rcrsResult, rcrsWeight.y );

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );

    // Virtual position
    float hitDist = GetHitDistance( spec.w, centerZ, gSpecScalingParams, roughness );
    float3 Xvirtual = GetXvirtual( X, Xprev, N, V, roughness, hitDist );

    // Sample history ( surface motion )
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float4 diffHistory = BicubicFilterNoCorners( gIn_History_Diff, gLinearClamp, pixelPosPrev, gInvScreenSize );
    float4 historySurface = BicubicFilterNoCorners( gIn_History_Spec, gLinearClamp, pixelPosPrev, gInvScreenSize );

    // Sample history ( virtual motion )
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );
    float4 historyVirtual = gIn_History_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev, 0.0 );

    // Mix histories
    float4 specHistory = lerp( historySurface, historyVirtual, virtualMotionAmount );

    // Compute parallax
    float parallax = ComputeParallax( pixelUv, 1.0, Xprev, gCameraDelta, gWorldToClip );
    float parallaxMod = parallax * ( 1.0 - virtualMotionAmount );

    // History weight
    float motionLength = length( pixelUvPrev - pixelUv );
    float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffAccumSpeed, motionLength );
    float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specAccumSpeed, motionLength, parallaxMod, roughness );

    // Antilag
    float2 antiLag = 1.0;
    if( gAntilag != 0.0 && USE_ANTILAG == 1 )
    {
        // TODO: if compression is used delta.x needs to be decompressed, but it doesn't affect the behavior, because heavily compressed values do not lag
        {
            float2 delta = abs( diffHistory.xw - diff.xw ) - diffSigma.xw * 2.0;
            delta = STL::Math::LinearStep( float2( gAntilagRadianceThreshold.y, 0.1 ), float2( gAntilagRadianceThreshold.x, 0.01 ), delta );
            delta = STL::Math::Pow01( delta, float2( 2.0, 8.0 ) );

            float fade = diffAccumSpeed / ( 1.0 + diffAccumSpeed );
            fade *= diffTemporalAccumulationParams.x;
            fade *= rcrsWeight.x;

            antiLag.x = min( delta.x, delta.y );
            antiLag.x = lerp( 1.0, antiLag.x, fade );
        }

        {
            float2 delta = abs( specHistory.xw - spec.xw ) - specSigma.xw * 2.0;
            delta = STL::Math::LinearStep( float2( gAntilagRadianceThreshold.y, 0.1 ), float2( gAntilagRadianceThreshold.x, 0.01 ), delta );
            delta = STL::Math::Pow01( delta, float2( 2.0, 8.0 ) );

            float fade = specAccumSpeed / ( 1.0 + specAccumSpeed );
            fade *= specTemporalAccumulationParams.x;
            fade *= rcrsWeight.y;

            antiLag.y = min( delta.x, delta.y );
            antiLag.y = lerp( 1.0, antiLag.y, fade );
        }
    }

    // Clamp history and combine with the current frame
    float4 diffMin = diffM1 - diffSigma * diffTemporalAccumulationParams.y;
    float4 diffMax = diffM1 + diffSigma * diffTemporalAccumulationParams.y;
    diffHistory = clamp( diffHistory, diffMin, diffMax );

    float4 specMin = specM1 - specSigma * specTemporalAccumulationParams.y;
    float4 specMax = specM1 + specSigma * specTemporalAccumulationParams.y;
    specHistory = clamp( specHistory, specMin, specMax );

    float2 historyWeight = TS_MAX_HISTORY_WEIGHT * antiLag;
    float4 diffResult = lerp( diff, diffHistory, historyWeight.x * diffTemporalAccumulationParams.x );
    float4 specResult = lerp( spec, specHistory, historyWeight.y * specTemporalAccumulationParams.x );

    // Get rid of possible negative values
    diffResult.xyz = _NRD_YCoCgToLinear( diffResult.xyz );
    diffResult.w = max( diffResult.w, 0.0 );
    diffResult.xyz = _NRD_LinearToYCoCg( diffResult.xyz );

    specResult.xyz = _NRD_YCoCgToLinear( specResult.xyz );
    specResult.w = max( specResult.w, 0.0 );
    specResult.xyz = _NRD_LinearToYCoCg( specResult.xyz );

    // Output
    diffAccumSpeed *= antiLag.x;
    specAccumSpeed *= antiLag.y;

    gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( centerZ, diffAccumSpeed, N, roughness, specAccumSpeed );
    gOut_Diff[ pixelPos ] = diffResult;
    gOut_Spec[ pixelPos ] = specResult;

    #if( SHOW_ACCUM_SPEED == 1 )
        diffResult.w = saturate( diffAccumSpeed / MAX_ACCUM_FRAME_NUM );
        specResult.w = saturate( specAccumSpeed / MAX_ACCUM_FRAME_NUM );
    #elif( SHOW_ANTILAG == 1 )
        diffResult.w = antiLag.x;
        specResult.w = antiLag.y;
    #endif

    gOut_Diff_Copy[ pixelPos ] = diffResult;
    gOut_Spec_Copy[ pixelPos ] = specResult;
}
