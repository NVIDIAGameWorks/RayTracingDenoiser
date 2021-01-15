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
    float4 gSpecHitDistParams;
    float4 gCameraDelta;
    float4 gIntensityAntilag;
    float4 gHitDistanceAntilag;
    float2 gMotionVectorScale;
};

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
    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = gIn_ViewZ[ globalId ];
    s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Spec[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Debug
    #if( SHOW_MIPS )
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
    float virtualMotionAmount;
    float2 specInternalData = UnpackSpecInternalData( gIn_InternalData[ pixelPos ], roughness, virtualMotionAmount );

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = STL::Geometry::RotateVector( gViewToWorld, -Xv ) * invDistToPoint;

    // Local variance
    float sum = 0;

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
            float4 normalAndRoughness = s_Normal_Roughness[ pos.y ][ pos.x ];
            float4 specSignal = s_Spec[ pos.y ][ pos.x ];
            float z = s_ViewZ[ pos.y ][ pos.x ];

            float w = 1.0;
            if( dx == BORDER && dy == BORDER )
                spec = specSignal;
            else
            {
                w = GetBilateralWeight( z, centerZ );
                w *= GetNormalWeight( specNormalParams, N, normalAndRoughness.xyz );
                w *= GetRoughnessWeight( roughnessParams, normalAndRoughness.w );

                float s = float( w == 0.0 ) * INF;
                specMaxInput = max( specSignal - s, specMaxInput );
                specMinInput = min( specSignal + s, specMinInput );
            }

            w = STL::Math::Pow01( w, TS_WEIGHT_BOOST_POWER );

            specM1 += specSignal * w;
            specM2 += specSignal * specSignal * w;

            sum += w;
        }
    }

    float invSum = STL::Math::PositiveRcp( sum );
    specM1 *= invSum;
    specM2 *= invSum;
    float4 specSigma = GetVariance( specM1, specM2 );

    // Apply RCRS
    float rcrsWeight = ( sum - 1.0 ) / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) - 1.0 );

    float4 specRcrs = min( spec, specMaxInput );
    specRcrs = max( specRcrs, specMinInput );
    spec = lerp( spec, specRcrs, rcrsWeight * float( !IsReference() ) );

    // Compute previous position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, IsWorldSpaceMotion() );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Sample history ( surface motion )
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float4 historySurface = BicubicFilterNoCorners( gIn_History_Spec, gLinearClamp, pixelPosPrev, gInvScreenSize );

    // Sample history ( virtual motion ) and mix
    float3 Xprev = X + motionVector * float( IsWorldSpaceMotion() );
    float hitDist = GetHitDistance( spec.w, centerZ, gSpecHitDistParams, roughness );
    float3 Xvirtual = GetXvirtual( X, Xprev, N, V, roughness, hitDist );
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );
    float4 historyVirtual = gIn_History_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev, 0.0 );
    float4 specHistory = lerp( historySurface, historyVirtual, virtualMotionAmount );

    // Compute parallax
    float parallax = ComputeParallax( pixelUv, 1.0, Xprev, gCameraDelta.xyz, gWorldToClip );
    float parallaxMod = parallax * ( 1.0 - virtualMotionAmount );

    // History weight
    float motionLength = length( pixelUvPrev - pixelUv );

    float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specInternalData.y, motionLength, parallaxMod, roughness );
    specTemporalAccumulationParams.y = 1.0 + ( specTemporalAccumulationParams.y - 1.0 ) * rcrsWeight;

    // Antilag
    float specAntilag = 1.0;

    #if( USE_ANTILAG == 1 )
        // TODO: if compression is used delta.x needs to be decompressed, but it doesn't affect the behavior, because heavily compressed values do not lag
        float2 specDelta = abs( specHistory.xw - specM1.xw ) - specSigma.xw * float2( gIntensityAntilag.x, gHitDistanceAntilag.x );
        specDelta = STL::Math::LinearStep( float2( gIntensityAntilag.z, gHitDistanceAntilag.z ), float2( gIntensityAntilag.y, gHitDistanceAntilag.y ), specDelta );
        specDelta *= specDelta;

        float specFade = specInternalData.y / ( 1.0 + specInternalData.y );
        specFade *= specTemporalAccumulationParams.x;

        specAntilag = min( specDelta.x, specDelta.y );
        specAntilag = lerp( 1.0, specAntilag, specFade );
    #endif

    #if( USE_LIMITED_ANTILAG == 1 )
        float specMinAccumSpeed = min( specInternalData.y, GetMipLevel( 0.0, roughness ) );
        specInternalData.y = specMinAccumSpeed + ( specInternalData.y - specMinAccumSpeed ) * specAntilag;
    #else
        specInternalData.y *= specAntilag;
    #endif

    // Clamp history and combine with the current frame
    float4 specMin = specM1 - specSigma * specTemporalAccumulationParams.y;
    float4 specMax = specM1 + specSigma * specTemporalAccumulationParams.y;
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

    #if( SHOW_ACCUM_SPEED == 1 )
        specResult.w = saturate( specInternalData.y / MAX_ACCUM_FRAME_NUM );
    #elif( SHOW_ANTILAG == 1 )
        specResult.w = specAntilag;
    #endif

    gOut_Spec_Copy[ pixelPos ] = specResult;
}
