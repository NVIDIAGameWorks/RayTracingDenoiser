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

    float4x4 gWorldToViewPrev;
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4x4 gWorldToClip;
    float4 gFrustumPrev;
    float3 gCameraDelta;
    float gIsOrthoPrev;
    float4 gSpecScalingParams;
    float3 gSpecTrimmingParams;
    float gCheckerboardResolveAccumSpeed;
    float2 gMotionVectorScale;
    float gDisocclusionThreshold;
    float gJitterDelta;
    float gDiffMaxAccumulatedFrameNum;
    float gSpecMaxAccumulatedFrameNum;
    uint gDiffCheckerboard;
    uint gSpecCheckerboard;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<uint2>, gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Diff, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Spec, t, 5, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 6, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 7, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<uint>, gOut_InternalData, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 2, 0 );

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

    PRELOAD_INTO_SMEM;

    // Early out
    int2 smemPos = threadId + BORDER;
    float viewZ = s_ViewZ[ smemPos.y ][ smemPos.x ];

    [branch]
    if( abs( viewZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Diff[ pixelPos ] = 0;
            gOut_Spec[ pixelPos ] = 0;
        #endif
        gOut_InternalData[ pixelPos ] = PackDiffSpecInternalData( MAX_ACCUM_FRAME_NUM, MAX_ACCUM_FRAME_NUM, 0 ); // MAX_ACCUM_FRAME_NUM to skip HistoryFix on INF pixels
        return;
    }

    // Center position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = STL::Geometry::RotateVector( gViewToWorld, -Xv ) * invDistToPoint;

    // Normal and roughness
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Calculate distribution of normals and signal variance
    float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
    float4 m1 = spec;
    float4 m2 = m1 * m1;
    float3 Nflat = N;
    float3 Nsum = N;
    float sum = 1.0;
    float avgNoV = abs( dot( N, V ) );
    float normalParams = GetNormalWeightParamsRoughEstimate( roughness );
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
            float4 data = s_Spec[ pos.y ][ pos.x ];
            float4 normalAndRoughness = s_Normal_Roughness[ pos.y ][ pos.x ];
            float z = s_ViewZ[ pos.y ][ pos.x ];

            float w = GetBilateralWeight( z, viewZ );
            w *= GetNormalWeight( normalParams, N, normalAndRoughness.xyz );
            w *= GetRoughnessWeight( roughnessParams, normalAndRoughness.w );

            Nflat += normalAndRoughness.xyz; // yes, no weight
            Nsum += normalAndRoughness.xyz * w;
            avgNoV += abs( dot( normalAndRoughness.xyz, V ) ) * w;

            m1 += data * w;
            m2 += data * data * w;
            sum += w;
        }
    }

    float invSum = 1.0 / sum;
    m1 *= invSum;
    m2 *= invSum;
    float4 sigma = GetVariance( m1, m2 );

    Nflat = normalize( Nflat );

    avgNoV *= invSum;
    float flatNoV = abs( dot( Nflat, V ) );

    float3 Navg = Nsum * invSum;
    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );
    float roughnessRatio = ( roughness + 0.001 ) / ( roughnessModified + 0.001 );
    roughnessRatio = STL::Math::Pow01( roughnessRatio, SPEC_NORMAL_VARIANCE_SMOOTHNESS );

    float trimmingFade = GetTrimmingFactor( roughness, gSpecTrimmingParams );
    trimmingFade = STL::Math::LinearStep( 0.0, 0.1, trimmingFade ); // TODO: is it needed? Better settings?
    trimmingFade = lerp( 1.0, trimmingFade, roughnessRatio );

    // Normal and roughness weight parameters
    normalParams = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughnessModified );
    normalParams *= LOBE_STRICTNESS_FACTOR;
    normalParams += NORMAL_BANDING_FIX;

    // Compute previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) ); // TODO: ideally, isInScreen must be per pixel in 2x2 or 4x4 footprint
    float2 motion = pixelUvPrev - pixelUv;
    float motionLength = length( motion );
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );

    // Previous viewZ ( Catmull-Rom )
    STL::Filtering::CatmullRom catmullRomFilterAtPrevPos = STL::Filtering::GetCatmullRomFilter( saturate( pixelUvPrev ), gScreenSize );
    float2 catmullRomFilterAtPrevPosGatherOrigin = catmullRomFilterAtPrevPos.origin * gInvScreenSize;
    uint4 prevPackRed0 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
    uint4 prevPackRed1 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 prevPackRed2 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 prevPackRed3 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;
    float4 prevViewZ0 = UnpackViewZ( prevPackRed0 );
    float4 prevViewZ1 = UnpackViewZ( prevPackRed1 );
    float4 prevViewZ2 = UnpackViewZ( prevPackRed2 );
    float4 prevViewZ3 = UnpackViewZ( prevPackRed3 );
    float4 diffPrevAccumSpeeds = UnpackAccumSpeed( uint4( prevPackRed0.w, prevPackRed1.z, prevPackRed2.y, prevPackRed3.x ) );

    // Previous normal, roughness and accum speed ( bilinear )
    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gScreenSize );
    float2 bilinearFilterAtPrevPosGatherOrigin = ( bilinearFilterAtPrevPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackGreen = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, bilinearFilterAtPrevPosGatherOrigin ).wzxy;
    float4 specPrevAccumSpeeds;
    float4 prevNormalAndRoughness00 = UnpackNormalRoughnessAccumSpeed( prevPackGreen.x, specPrevAccumSpeeds.x );
    float4 prevNormalAndRoughness10 = UnpackNormalRoughnessAccumSpeed( prevPackGreen.y, specPrevAccumSpeeds.y );
    float4 prevNormalAndRoughness01 = UnpackNormalRoughnessAccumSpeed( prevPackGreen.z, specPrevAccumSpeeds.z );
    float4 prevNormalAndRoughness11 = UnpackNormalRoughnessAccumSpeed( prevPackGreen.w, specPrevAccumSpeeds.w );

    float3 prevNflat = prevNormalAndRoughness00.xyz + prevNormalAndRoughness10.xyz + prevNormalAndRoughness01.xyz + prevNormalAndRoughness11.xyz;
    prevNflat = normalize( prevNflat );

    // Plane distance based disocclusion for surface motion
    float parallax = ComputeParallax( pixelUv, roughnessRatio, Xprev, gCameraDelta, gWorldToClip );
    float2 disocclusionThresholds = GetDisocclusionThresholds( gDisocclusionThreshold, gJitterDelta, viewZ, parallax, Nflat, X, invDistToPoint );
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev1 = abs( dot( Nflat, Xprev ) ); // = dot( Nvflatprev, Xvprev ), "abs" is needed here only to get "max" absolute value in the next line
    float NoXprev2 = abs( dot( prevNflat, Xprev ) );
    float NoXprev = max( NoXprev1, NoXprev2 ) * invDistToPoint;
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) ); // = dot( Nvflatprev, Xvprev / Xvprev.z )
    float4 planeDist0 = abs( NoVprev * abs( prevViewZ0 ) - NoXprev );
    float4 planeDist1 = abs( NoVprev * abs( prevViewZ1 ) - NoXprev );
    float4 planeDist2 = abs( NoVprev * abs( prevViewZ2 ) - NoXprev );
    float4 planeDist3 = abs( NoVprev * abs( prevViewZ3 ) - NoXprev );
    float4 occlusion0 = saturate( isInScreen - step( disocclusionThresholds.x, planeDist0 ) );
    float4 occlusion1 = saturate( isInScreen - step( disocclusionThresholds.x, planeDist1 ) );
    float4 occlusion2 = saturate( isInScreen - step( disocclusionThresholds.x, planeDist2 ) );
    float4 occlusion3 = saturate( isInScreen - step( disocclusionThresholds.x, planeDist3 ) );

    // Ignore backfacing history
    float4 cosa;
    cosa.x = dot( N, prevNormalAndRoughness00.xyz );
    cosa.y = dot( N, prevNormalAndRoughness10.xyz );
    cosa.z = dot( N, prevNormalAndRoughness01.xyz );
    cosa.w = dot( N, prevNormalAndRoughness11.xyz );

    float4 frontFacing = STL::Math::LinearStep( disocclusionThresholds.y, 0.001, cosa );
    occlusion0.w *= frontFacing.x;
    occlusion1.z *= frontFacing.y;
    occlusion2.y *= frontFacing.z;
    occlusion3.x *= frontFacing.w;

    float4 diffOcclusion2x2 = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );

    // Sample diffuse history
    float2 catmullRomFilterAtPrevPosOrigin = ( catmullRomFilterAtPrevPos.origin + 0.5 ) * gInvScreenSize;
    float4 d10 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 0 ) );
    float4 d20 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 0 ) );
    float4 d01 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 1 ) );
    float4 d11 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 1 ) );
    float4 d21 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 1 ) );
    float4 d31 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 1 ) );
    float4 d02 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 2 ) );
    float4 d12 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 2 ) );
    float4 d22 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 2 ) );
    float4 d32 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 2 ) );
    float4 d13 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 3 ) );
    float4 d23 = gIn_History_Diff.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 3 ) );

    float4 diffWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, diffOcclusion2x2 );
    float4 diffHistory = STL::Filtering::ApplyBilinearCustomWeights( d11, d21, d12, d22, diffWeights );
    float4 diffHistoryCatRom = STL::Filtering::ApplyCatmullRomFilterNoCorners( catmullRomFilterAtPrevPos, d10, d20, d01, d11, d21, d31, d02, d12, d22, d32, d13, d23 );
    diffHistory = MixLinearAndCatmullRom( diffHistory, diffHistoryCatRom, occlusion0, occlusion1, occlusion2, occlusion3 );

    // Modify specular occlusion to avoid averaging of specular for different roughness
    float4 prevRoughness = float4( prevNormalAndRoughness00.w, prevNormalAndRoughness10.w, prevNormalAndRoughness01.w, prevNormalAndRoughness11.w );
    float4 prevRoughnessWeight = GetRoughnessWeight( roughnessParams, prevRoughness );
    prevRoughnessWeight = STL::Math::LinearStep( 0.1, 0.9, prevRoughnessWeight );
    occlusion0.w *= prevRoughnessWeight.x;
    occlusion1.z *= prevRoughnessWeight.y;
    occlusion2.y *= prevRoughnessWeight.z;
    occlusion3.x *= prevRoughnessWeight.w;

    float4 specOcclusion2x2 = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );

    // Sample specular history ( surface motion )
    // TODO: averaging of values with different compression can be dangerous... but no problems so far
    float4 s10 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 0 ) );
    float4 s20 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 0 ) );
    float4 s01 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 1 ) );
    float4 s11 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 1 ) );
    float4 s21 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 1 ) );
    float4 s31 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 1 ) );
    float4 s02 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 2 ) );
    float4 s12 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 2 ) );
    float4 s22 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 2 ) );
    float4 s32 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 2 ) );
    float4 s13 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 3 ) );
    float4 s23 = gIn_History_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 3 ) );

    float4 specWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, specOcclusion2x2 );
    float4 specHistorySurface = STL::Filtering::ApplyBilinearCustomWeights( s11, s21, s12, s22, specWeights );
    float4 specHistorySurfaceCatRom = STL::Filtering::ApplyCatmullRomFilterNoCorners( catmullRomFilterAtPrevPos, s10, s20, s01, s11, s21, s31, s02, s12, s22, s32, s13, s23 );
    specHistorySurface = MixLinearAndCatmullRom( specHistorySurface, specHistorySurfaceCatRom, occlusion0, occlusion1, occlusion2, occlusion3 );

    // Accumulation speeds
    diffPrevAccumSpeeds = min( diffPrevAccumSpeeds + 1.0, gDiffMaxAccumulatedFrameNum );
    float diffAccumSpeed = STL::Filtering::ApplyBilinearCustomWeights( diffPrevAccumSpeeds.x, diffPrevAccumSpeeds.y, diffPrevAccumSpeeds.z, diffPrevAccumSpeeds.w, diffWeights );

    specPrevAccumSpeeds = min( specPrevAccumSpeeds + 1.0, gSpecMaxAccumulatedFrameNum );
    float specAccumSpeed = STL::Filtering::ApplyBilinearCustomWeights( specPrevAccumSpeeds.x, specPrevAccumSpeeds.y, specPrevAccumSpeeds.z, specPrevAccumSpeeds.w, specWeights );

    // Noisy signal with reconstruction (if needed)
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    float4 diff = gIn_Diff[ pixelPos ];
    bool diffHasData = gDiffCheckerboard == 2 || checkerboard == gDiffCheckerboard;
    if( !diffHasData )
    {
        float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffAccumSpeed, motionLength );
        float historyWeight = gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

        diff = lerp( diff, diffHistory, historyWeight );
    }

    bool specHasData = gSpecCheckerboard == 2 || checkerboard == gSpecCheckerboard;
    if( !specHasData )
    {
        float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specAccumSpeed, motionLength, STL::Math::Pow01( parallax, 0.25 ), roughnessModified );
        float historyWeight = gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

        spec = lerp( spec, specHistorySurface, historyWeight );
    }

    // Current specular signal ( surface motion )
    float4 currentSurface;

    float2 accumSpeedsSurface = GetSpecAccumSpeed( specAccumSpeed, roughnessModified, avgNoV, parallax );
    float accumSpeedSurface = 1.0 / ( trimmingFade * accumSpeedsSurface.x + 1.0 );
    currentSurface.w = lerp( specHistorySurface.w, spec.w, max( accumSpeedSurface, MIN_HITDIST_ACCUM_SPEED ) );

    float hitDist = GetHitDistance( currentSurface.w, viewZ, gSpecScalingParams, roughness );
    parallax *= saturate( hitDist * invDistToPoint );
    accumSpeedsSurface = GetSpecAccumSpeed( specAccumSpeed, roughnessModified, avgNoV, parallax );
    accumSpeedSurface = 1.0 / ( trimmingFade * accumSpeedsSurface.x + 1.0 );
    currentSurface.xyz = lerp( specHistorySurface.xyz, spec.xyz, accumSpeedSurface );

    // Compute previous pixel position for virtual motion
    float3 Xvirtual = GetXvirtual( X, Xprev, N, V, roughnessModified, hitDist );
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );

    // Disocclusion for virtual motion
    STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gScreenSize );
    float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackRedVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, gatherUvVirtualPrev ).wzxy;
    uint4 prevPackGreenVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, gatherUvVirtualPrev ).wzxy;

    float4 prevViewZVirtual = UnpackViewZ( prevPackRedVirtual );
    float4 occlusionVirtual = abs( prevViewZVirtual - Xvprev.z ) * STL::Math::PositiveRcp( min( abs( Xvprev.z ), abs( prevViewZVirtual ) ) );
    float zThreshold = lerp( 0.03, 0.1, STL::Math::Sqrt01( 1.0 - flatNoV ) );
    occlusionVirtual = STL::Math::LinearStep( zThreshold, 0.02, occlusionVirtual );

    occlusionVirtual.x *= GetNormalAndRoughnessWeights( N, normalParams, roughnessParams, prevPackGreenVirtual.x );
    occlusionVirtual.y *= GetNormalAndRoughnessWeights( N, normalParams, roughnessParams, prevPackGreenVirtual.y );
    occlusionVirtual.z *= GetNormalAndRoughnessWeights( N, normalParams, roughnessParams, prevPackGreenVirtual.z );
    occlusionVirtual.w *= GetNormalAndRoughnessWeights( N, normalParams, roughnessParams, prevPackGreenVirtual.w );

    // Sample specular history ( virtual motion )
    float2 bilinearFilterAtPrevVirtualPosOrigin = ( bilinearFilterAtPrevVirtualPos.origin + 0.5 ) * gInvScreenSize;
    float4 s00 = gIn_History_Spec.SampleLevel( gNearestClamp, bilinearFilterAtPrevVirtualPosOrigin, 0 );
    s10 = gIn_History_Spec.SampleLevel( gNearestClamp, bilinearFilterAtPrevVirtualPosOrigin, 0, int2( 1, 0 ) );
    s01 = gIn_History_Spec.SampleLevel( gNearestClamp, bilinearFilterAtPrevVirtualPosOrigin, 0, int2( 0, 1 ) );
    s11 = gIn_History_Spec.SampleLevel( gNearestClamp, bilinearFilterAtPrevVirtualPosOrigin, 0, int2( 1, 1 ) );

    float4 virtualWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevVirtualPos, occlusionVirtual );
    float4 specHistoryVirtual = STL::Filtering::ApplyBilinearCustomWeights( s00, s10, s01, s11, virtualWeights );

    // Amount of virtual motion
    float2 temp = min( occlusionVirtual.xy, occlusionVirtual.zw );
    float virtualHistoryAmount = min( temp.x, temp.y );
    float isInScreenVirtual = float( all( saturate( pixelUvVirtualPrev ) == pixelUvVirtualPrev ) );
    virtualHistoryAmount *= isInScreenVirtual;
    virtualHistoryAmount *= 1.0 - STL::Math::SmoothStep( 0.2, 1.0, roughness ); // TODO: fade out to surface motion, because virtual motion is valid only for true mirrors
    virtualHistoryAmount *= 1.0 - gReference; // TODO: I would be glad to use virtual motion in the reference mode, but it requires denoised hit distances. Unfortunately, in the reference mode blur radius is set to 0

    // Hit distance based disocclusion for virtual motion
    float hitDistVirtual = GetHitDistance( specHistoryVirtual.w, viewZ, gSpecScalingParams, roughness );
    float relativeDelta = abs( hitDist - hitDistVirtual ) * STL::Math::PositiveRcp( min( hitDistVirtual, hitDist ) + abs( viewZ ) );

    float relativeDeltaThreshold = lerp( 0.01, 0.25, roughnessModified * roughnessModified );
    relativeDeltaThreshold += 0.02 * ( 1.0 - STL::Math::SmoothStep( 0.01, 0.2, parallax ) ); // increase the threshold if parallax is low (big disocclusions produced by dynamic objects will still be handled)

    float virtualHistoryCorrectness = step( relativeDelta, relativeDeltaThreshold );
    virtualHistoryCorrectness *= 1.0 - STL::Math::SmoothStep( 0.25, 1.0, parallax );

    float accumSpeedScale = lerp( roughnessModified, 1.0, virtualHistoryCorrectness );
    accumSpeedScale = lerp( accumSpeedScale, 1.0, 1.0 / ( 1.0 + specAccumSpeed ) );

    float minAccumSpeed = min( specAccumSpeed, 4.0 );
    specAccumSpeed = minAccumSpeed + ( specAccumSpeed - minAccumSpeed ) * lerp( 1.0, accumSpeedScale, virtualHistoryAmount );

    // Current specular signal ( virtual motion )
    float2 accumSpeedsVirtual = GetSpecAccumSpeed( specAccumSpeed, roughnessModified, avgNoV, 0.0 );
    float accumSpeedVirtual = 1.0 / ( trimmingFade * accumSpeedsVirtual.x + 1.0 );

    float4 currentVirtual;
    currentVirtual.xyz = lerp( specHistoryVirtual.xyz, spec.xyz, accumSpeedVirtual );
    currentVirtual.w = lerp( specHistoryVirtual.w, spec.w, max( accumSpeedVirtual, MIN_HITDIST_ACCUM_SPEED ) );

    // Color clamping
    float sigmaScale = 3.0 + TS_SIGMA_AMPLITUDE * STL::Math::SmoothStep( 0.04, 0.65, roughnessModified );
    float4 colorMin = m1 - sigma * sigmaScale;
    float4 colorMax = m1 + sigma * sigmaScale;
    float4 currentVirtualClamped = clamp( currentVirtual, colorMin, colorMax );
    float4 currentSurfaceClamped = clamp( currentSurface, colorMin, colorMax ); // TODO: use color clamping if surface motion based hit distance disocclusion is detected...

    float virtualClampingAmount = lerp( 1.0 - roughnessModified * roughnessModified, 0.0, virtualHistoryCorrectness );
    float surfaceClampingAmount = 1.0 - STL::Math::SmoothStep( 0.04, 0.4, roughnessModified );
    surfaceClampingAmount *= STL::Math::SmoothStep( 0.05, 0.3, parallax );
    surfaceClampingAmount *= 1.0 - gReference;

    currentVirtual = lerp( currentVirtual, currentVirtualClamped, virtualClampingAmount );
    currentSurface.xyz = lerp( currentSurface.xyz, currentSurfaceClamped.xyz, surfaceClampingAmount );

    // Final composition
    float4 specResult;
    specResult.xyz = lerp( currentSurface.xyz, currentVirtual.xyz, virtualHistoryAmount );
    specResult.w = currentSurface.w;

    float parallaxMod = parallax * ( 1.0 - virtualHistoryAmount );
    float2 specAccumSpeeds = GetSpecAccumSpeed( specAccumSpeed, roughnessModified, avgNoV, parallaxMod );

    // Diffuse accumulation
    float2 diffAccumSpeeds = GetSpecAccumSpeed( diffAccumSpeed, 1.0, 0.0, 0.0 );
    float diffHistoryAmount = 1.0 / ( diffAccumSpeeds.x + 1.0 );

    float4 diffResult;
    diffResult.xyz = lerp( diffHistory.xyz, diff.xyz, diffHistoryAmount );
    diffResult.w = lerp( diffHistory.w, diff.w, max( diffHistoryAmount, MIN_HITDIST_ACCUM_SPEED ) );

    // TODO: get rid of possible negative values?

    // Output
    gOut_InternalData[ pixelPos ] = PackDiffSpecInternalData( float3( diffAccumSpeeds, diffAccumSpeed ), float3( specAccumSpeeds, specAccumSpeed ), virtualHistoryAmount );
    gOut_Diff[ pixelPos ] = diffResult;
    gOut_Spec[ pixelPos ] = specResult;
}
