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
    float2 padding;
    float gMetersToUnits;
    float gIsOrtho;
    float gUnproject;
    float gDebug;
    float gInf;
    uint gCheckerboard;
    uint gFrameIndex;
    uint gWorldSpaceMotion;

    float4x4 gWorldToViewPrev;
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4x4 gWorldToClip;
    float4 gFrustumPrev;
    float3 gCameraDelta;
    float gIsOrthoPrev;
    float4 gScalingParams;
    float3 gTrimmingParams;
    float gReference;
    float2 gScreenSize;
    float2 gMotionVectorScale;
    float gCheckerboardResolveAccumSpeed;
    float gDisocclusionThreshold;
    float gJitterDelta;
    float gMaxSpecAccumulatedFrameNum;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_SpecHit, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SpecHit, t, 4, 0 );
NRI_RESOURCE( Texture2D<uint2>, gIn_Prev_ViewZ_AccumSpeed, t, 5, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_SpecHit, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_InternalData, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float>, gOut_ScaledViewZ, u, 2, 0 );

groupshared float4 s_Normal_ViewZ[ BUFFER_Y ][ BUFFER_X ]; // TODO: add roughness? (needed for the center)
groupshared float4 s_Input[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    // TODO: use w = 0 if outside of the screen or use SampleLevel with Clamp sampler
    float4 t;
    t.xyz = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] ).xyz;
    t.w = gIn_ViewZ[ globalId ];

    s_Normal_ViewZ[ sharedId.y ][ sharedId.x ] = t;
    s_Input[ sharedId.y ][ sharedId.x ] = gIn_SpecHit[ globalId ];
}

float GetNormalAndRoughnessWeights( float3 N, float2 normalParams, float2 roughnessParams, float4 packedNormalAndRoughness )
{
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( packedNormalAndRoughness );
    float normalWeight = GetNormalWeight( normalParams, N, normalAndRoughness.xyz );
    float roughnessWeight = GetRoughnessWeight( roughnessParams, normalAndRoughness.w );
    roughnessWeight = STL::Math::LinearStep( 0.0, 0.8, roughnessWeight );

    return normalWeight * roughnessWeight;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Rename the 16x16 group into a 18x14 group + some idle threads in the end
    float linearId = ( threadIndex + 0.5 ) / BUFFER_X;
    int2 newId = int2( frac( linearId ) * BUFFER_X, linearId );
    int2 groupBase = pixelPos - threadId - BORDER;

    // Preload into shared memory
    if ( newId.y < RENAMED_GROUP_Y )
        Preload( newId, groupBase + newId );

    newId.y += RENAMED_GROUP_Y;

    if ( newId.y < BUFFER_Y )
        Preload( newId, groupBase + newId );

    GroupMemoryBarrierWithGroupSync( );

    // Early out
    int2 centerId = threadId + BORDER;
    float4 centerData = s_Normal_ViewZ[ centerId.y ][ centerId.x ];
    float viewZ = centerData.w;

    [branch]
    if ( abs( viewZ ) > gInf )
    {
        #if( SPEC_BLACK_OUT_INF_PIXELS == 1 )
            gOut_SpecHit[ pixelPos ] = 0;
        #endif
        gOut_InternalData[ pixelPos ] = PackSpecInternalData( MAX_ACCUM_FRAME_NUM, 0, 0 ); // MAX_ACCUM_FRAME_NUM is needed here to skip HistoryFix on INF pixels
        gOut_ScaledViewZ[ pixelPos ] = NRD_FP16_MAX;
        return;
    }

    // Center position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = STL::Geometry::RotateVector( gViewToWorld, -Xv ) * invDistToPoint;

    // Normal and roughness
    float4 normalAndRoughnessPacked = gIn_Normal_Roughness[ pixelPos ];
    float roughness = _NRD_FrontEnd_UnpackNormalAndRoughness( normalAndRoughnessPacked ).w;
    float3 N = centerData.xyz;

    // Calculate distribution of normals
    float4 input = s_Input[ centerId.y ][ centerId.x ];
    float4 m1 = input;
    float4 m2 = m1 * m1;
    float3 Nflat = N;
    float3 Nsum = N;
    float sum = 1.0;
    float avgNoV = abs( dot( N, V ) );

    float2 normalParams = GetNormalWeightParamsRoughEstimate( roughness );

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadId + int2( dx, dy );
            float4 data = s_Input[ pos.y ][ pos.x ];
            float4 normalAndViewZ = s_Normal_ViewZ[ pos.y ][ pos.x ];

            float w = GetBilateralWeight( normalAndViewZ.w, viewZ );
            w *= GetNormalWeight( normalParams, N, normalAndViewZ.xyz ); // TODO: add roughness weight?

            Nflat += normalAndViewZ.xyz; // yes, no weight // TODO: all 9? or 5 samples like it was before?

            Nsum += normalAndViewZ.xyz * w;
            avgNoV += abs( dot( normalAndViewZ.xyz, V ) ) * w;

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

    float trimmingFade = GetTrimmingFactor( roughness, gTrimmingParams );
    trimmingFade = STL::Math::LinearStep( 0.0, 0.1, trimmingFade ); // TODO: is it needed? Better settings?
    trimmingFade = lerp( 1.0, trimmingFade, roughnessRatio );

    // Normal and roughness weight parameters
    normalParams.x = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughnessModified );
    normalParams.x += SPEC_NORMAL_BANDING_FIX; // TODO: add before or after LOBE_STRICTNESS_FACTOR? ;)
    normalParams.x *= LOBE_STRICTNESS_FACTOR;
    normalParams.y = 1.0;

    float2 roughnessParams = GetRoughnessWeightParams( roughness );

    // Compute previous pixel position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Previous viewZ and accumulation speed for Catmull-Rom filter
    STL::Filtering::CatmullRom catmullRomFilterAtPrevPos = STL::Filtering::GetCatmullRomFilter( saturate( pixelUvPrev ), gScreenSize );
    float2 catmullRomFilterAtPrevPosGatherOrigin = catmullRomFilterAtPrevPos.origin * gInvScreenSize;
    uint4 prevPackedViewZAndAccumSpeed0 = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
    uint4 prevPackedViewZAndAccumSpeed1 = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 prevPackedViewZAndAccumSpeed2 = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 prevPackedViewZAndAccumSpeed3 = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;
    float4 viewZprev0 = UnpackViewZ( prevPackedViewZAndAccumSpeed0 );
    float4 viewZprev1 = UnpackViewZ( prevPackedViewZAndAccumSpeed1 );
    float4 viewZprev2 = UnpackViewZ( prevPackedViewZAndAccumSpeed2 );
    float4 viewZprev3 = UnpackViewZ( prevPackedViewZAndAccumSpeed3 );
    float4 accumSpeedsPrev0 = UnpackAccumSpeed( prevPackedViewZAndAccumSpeed0 );
    float4 accumSpeedsPrev1 = UnpackAccumSpeed( prevPackedViewZAndAccumSpeed1 );
    float4 accumSpeedsPrev2 = UnpackAccumSpeed( prevPackedViewZAndAccumSpeed2 );
    float4 accumSpeedsPrev3 = UnpackAccumSpeed( prevPackedViewZAndAccumSpeed3 );

    // Plane distance based disocclusion for surface motion
    float disocclusionThreshold = gDisocclusionThreshold;
    float jitterRadius = PixelRadiusToWorld( gJitterDelta, viewZ );
    float NoV = abs( dot( Nflat, X ) ) * invDistToPoint;
    disocclusionThreshold += jitterRadius * invDistToPoint / max( NoV, 0.05 );

    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev = dot( Nflat, Xprev ) * invDistToPoint; // = dot( Nvflatprev, Xvprev )
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) ); // = dot( Nvflatprev, Xvprev / Xvprev.z )
    float4 planeDist0 = abs( NoVprev * abs( viewZprev0 ) - NoXprev );
    float4 planeDist1 = abs( NoVprev * abs( viewZprev1 ) - NoXprev );
    float4 planeDist2 = abs( NoVprev * abs( viewZprev2 ) - NoXprev );
    float4 planeDist3 = abs( NoVprev * abs( viewZprev3 ) - NoXprev );
    float4 occlusion0 = saturate( isInScreen - step( disocclusionThreshold, planeDist0 ) );
    float4 occlusion1 = saturate( isInScreen - step( disocclusionThreshold, planeDist1 ) );
    float4 occlusion2 = saturate( isInScreen - step( disocclusionThreshold, planeDist2 ) );
    float4 occlusion3 = saturate( isInScreen - step( disocclusionThreshold, planeDist3 ) );

    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gScreenSize );

    // Modify occlusion to avoid averaging of specular for different roughness // TODO: ensure that it is safe in all cases!
    float2 bilinearFilterAtPrevPosGatherOrigin = ( bilinearFilterAtPrevPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackedNormalAndRoughness = gIn_Prev_ViewZ_AccumSpeed.GatherGreen( gNearestClamp, bilinearFilterAtPrevPosGatherOrigin ).wzxy;
    float prevRoughness00 = _NRD_FrontEnd_UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevPackedNormalAndRoughness.x, NORMAL_ROUGHNESS_BITS ) ).w;
    float prevRoughness10 = _NRD_FrontEnd_UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevPackedNormalAndRoughness.y, NORMAL_ROUGHNESS_BITS ) ).w;
    float prevRoughness01 = _NRD_FrontEnd_UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevPackedNormalAndRoughness.z, NORMAL_ROUGHNESS_BITS ) ).w;
    float prevRoughness11 = _NRD_FrontEnd_UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevPackedNormalAndRoughness.w, NORMAL_ROUGHNESS_BITS ) ).w;
    occlusion0.w *= GetRoughnessWeight( roughnessParams, prevRoughness00 );
    occlusion1.z *= GetRoughnessWeight( roughnessParams, prevRoughness10 );
    occlusion2.y *= GetRoughnessWeight( roughnessParams, prevRoughness01 );
    occlusion3.x *= GetRoughnessWeight( roughnessParams, prevRoughness11 );

    float4 occlusion = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );
    float4 accumSpeedsPrev = float4( accumSpeedsPrev0.w, accumSpeedsPrev1.z, accumSpeedsPrev2.y, accumSpeedsPrev3.x );
    float4 viewZprev = float4( viewZprev0.w, viewZprev1.z, viewZprev2.y, viewZprev3.x );
    float4 surfaceWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, occlusion );

    // Reconstruct previous world position
    if( !gWorldSpaceMotion )
    {
        float prevZestimation = STL::Filtering::ApplyBilinearCustomWeights( viewZprev.x, viewZprev.y, viewZprev.z, viewZprev.w, surfaceWeights ); // 0 if a disocclusion is detected
        Xvprev = STL::Geometry::ReconstructViewPosition( pixelUvPrev, gFrustumPrev, prevZestimation, gIsOrthoPrev );
        Xprev = STL::Geometry::RotateVectorInverse( gWorldToViewPrev, Xvprev ) + gCameraDelta; // = STL::Geometry::AffineTransform( gViewToWorldPrev, Xvprev )
    }

    // Compute parallax
    float3 Xt = Xprev - gCameraDelta;
    float2 uvt = STL::Geometry::GetScreenUv( gWorldToClip, Xt );
    float2 parallaxInUv = uvt - pixelUv;
    float parallaxInPixels = length( parallaxInUv * gScreenSize );
    float parallaxInUnits = PixelRadiusToWorld( parallaxInPixels, viewZ );
    float parallax = parallaxInUnits * invDistToPoint; // Tan of the angle between old and new view vectors in the world space
    parallax *= 60.0; // yes, tuned for 60 FPS to get auto-scaled solution if FPS is higher (more accumulation) or lower (less accumulation)

    // Biased modifications
    float parallaxInUnitsNoProj = length( X - Xt );
    float projScale = ( parallaxInUnits + 0.0001 ) / ( parallaxInUnitsNoProj + 0.0001 );
    float modNoV = lerp( 1.0, avgNoV, projScale * roughnessRatio );
    parallax *= lerp( roughnessRatio, 1.0, 0.25 ); // to not kill parallax completely...

    // Accumulation speed ( gets reset to 0 if a disocclusion is detected )
    float4 accumSpeeds = min( accumSpeedsPrev + 1.0, gMaxSpecAccumulatedFrameNum );
    float accumSpeed = STL::Filtering::ApplyBilinearCustomWeights( accumSpeeds.x, accumSpeeds.y, accumSpeeds.z, accumSpeeds.w, surfaceWeights );

    // Sample history ( surface motion )
    float2 catmullRomFilterAtPrevPosOrigin = ( catmullRomFilterAtPrevPos.origin + 0.5 ) * gInvScreenSize;
    float4 s00 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0 );
    float4 s10 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 0 ) );
    float4 s20 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 0 ) );
    float4 s30 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 0 ) );
    float4 s01 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 1 ) );
    float4 s11 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 1 ) );
    float4 s21 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 1 ) );
    float4 s31 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 1 ) );
    float4 s02 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 2 ) );
    float4 s12 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 2 ) );
    float4 s22 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 2 ) );
    float4 s32 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 2 ) );
    float4 s03 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 0, 3 ) );
    float4 s13 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 3 ) );
    float4 s23 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 3 ) );
    float4 s33 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 3, 3 ) );

    float4 historySurface = STL::Filtering::ApplyCatmullRomFilterWithCustomWeights( catmullRomFilterAtPrevPos, s00, s10, s20, s30, s01, s11, s21, s31, s02, s12, s22, s32, s03, s13, s23, s33, occlusion0, occlusion1, occlusion2, occlusion3 );
    if( any( occlusion != 1.0 ) || USE_CATMULLROM_RESAMPLING_IN_TA == 0 )
        historySurface = STL::Filtering::ApplyBilinearCustomWeights( s11, s21, s12, s22, surfaceWeights );

    // Current data with reconstruction (if needed)
    bool hasData = ApplyCheckerboard( pixelPos );
    if( !hasData )
    {
        #if( CHECKERBOARD_RESOLVE_MODE == SOFT )
            int3 pos = centerId.xyx + int3( -1, 0, 1 );

            float viewZ0 = s_Normal_ViewZ[ pos.y ][ pos.x ].w;
            float viewZ1 = s_Normal_ViewZ[ pos.y ][ pos.z ].w;

            float4 input0 = s_Input[ pos.y ][ pos.x ];
            float4 input1 = s_Input[ pos.y ][ pos.z ];

            float2 w = GetBilateralWeight( float2( viewZ0, viewZ1 ), viewZ );
            w *= CHECKERBOARD_SIDE_WEIGHT * 0.5;

            float invSum = STL::Math::PositiveRcp( w.x + w.y + 1.0 - CHECKERBOARD_SIDE_WEIGHT );

            input = input0 * w.x + input1 * w.y + input * ( 1.0 - CHECKERBOARD_SIDE_WEIGHT );
            input *= invSum;
        #endif

        // Mix with history ( optional )
        float2 motion = pixelUvPrev - pixelUv;
        float motionLength = length( motion );
        float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, accumSpeed, motionLength, STL::Math::Pow01( parallax, 0.25 ), roughnessModified );
        float historyWeight = gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

        input = lerp( input, historySurface, historyWeight );
    }

    // Current value ( surface motion )
    float2 accumSpeedsSurface = GetSpecAccumSpeed( accumSpeed, roughnessModified, modNoV, parallax );
    float accumSpeedSurface = 1.0 / ( trimmingFade * accumSpeedsSurface.x + 1.0 );

    float4 currentSurface;
    currentSurface.w = lerp( historySurface.w, input.w, max( accumSpeedSurface, MIN_HITDIST_ACCUM_SPEED ) );

    float hitDist = GetHitDistance( currentSurface.w, viewZ, gScalingParams, roughness );

    parallax *= saturate( hitDist * invDistToPoint );
    accumSpeedsSurface = GetSpecAccumSpeed( accumSpeed, roughnessModified, modNoV, parallax );
    accumSpeedSurface = 1.0 / ( trimmingFade * accumSpeedsSurface.x + 1.0 );

    currentSurface.xyz = lerp( historySurface.xyz, input.xyz, accumSpeedSurface );

    // Compute previous pixel position for virtual motion
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection( N, V, roughnessModified, SPEC_DOMINANT_DIRECTION );
    float3 Xvirtual = Xprev - V * hitDist * D.w;
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );

    // Disocclusion for virtual motion
    STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gScreenSize );
    float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackedViewZVirtual = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, gatherUvVirtualPrev ).wzxy;
    uint4 prevPackedNormalAndRoughnessVirtual = gIn_Prev_ViewZ_AccumSpeed.GatherGreen( gNearestClamp, gatherUvVirtualPrev ).wzxy;

    float4 prevViewZVirtual = UnpackViewZ( prevPackedViewZVirtual );
    float4 occlusionVirtual = abs( prevViewZVirtual - Xvprev.z ) * STL::Math::PositiveRcp( min( abs( Xvprev.z ), abs( prevViewZVirtual ) ) );
    float zThreshold = lerp( 0.03, 0.1, STL::Math::Sqrt01( 1.0 - flatNoV ) );
    occlusionVirtual = STL::Math::LinearStep( zThreshold, 0.02, occlusionVirtual );

    occlusionVirtual.x *= GetNormalAndRoughnessWeights( N, normalParams, roughnessParams, STL::Packing::UintToRgba( prevPackedNormalAndRoughnessVirtual.x, NORMAL_ROUGHNESS_BITS ) );
    occlusionVirtual.y *= GetNormalAndRoughnessWeights( N, normalParams, roughnessParams, STL::Packing::UintToRgba( prevPackedNormalAndRoughnessVirtual.y, NORMAL_ROUGHNESS_BITS ) );
    occlusionVirtual.z *= GetNormalAndRoughnessWeights( N, normalParams, roughnessParams, STL::Packing::UintToRgba( prevPackedNormalAndRoughnessVirtual.z, NORMAL_ROUGHNESS_BITS ) );
    occlusionVirtual.w *= GetNormalAndRoughnessWeights( N, normalParams, roughnessParams, STL::Packing::UintToRgba( prevPackedNormalAndRoughnessVirtual.w, NORMAL_ROUGHNESS_BITS ) );

    // Sample history ( virtual motion )
    float2 bilinearFilterAtPrevVirtualPosOrigin = ( bilinearFilterAtPrevVirtualPos.origin + 0.5 ) * gInvScreenSize;
    s00 = gIn_History_SpecHit.SampleLevel( gNearestClamp, bilinearFilterAtPrevVirtualPosOrigin, 0 );
    s10 = gIn_History_SpecHit.SampleLevel( gNearestClamp, bilinearFilterAtPrevVirtualPosOrigin, 0, int2( 1, 0 ) );
    s01 = gIn_History_SpecHit.SampleLevel( gNearestClamp, bilinearFilterAtPrevVirtualPosOrigin, 0, int2( 0, 1 ) );
    s11 = gIn_History_SpecHit.SampleLevel( gNearestClamp, bilinearFilterAtPrevVirtualPosOrigin, 0, int2( 1, 1 ) );

    float4 virtualWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevVirtualPos, occlusionVirtual );
    float4 historyVirtual = STL::Filtering::ApplyBilinearCustomWeights( s00, s10, s01, s11, virtualWeights );

    // Amount of virtual motion
    float2 temp = min( occlusionVirtual.xy, occlusionVirtual.zw );
    float virtualHistoryAmount = min( temp.x, temp.y );
    float isInScreenVirtual = float( all( saturate( pixelUvVirtualPrev ) == pixelUvVirtualPrev ) );
    virtualHistoryAmount *= isInScreenVirtual;
    virtualHistoryAmount *= 1.0 - STL::Math::SmoothStep( 0.2, 1.0, roughness ); // TODO: fade out to surface motion, because virtual motion is valid only for true mirrors
    virtualHistoryAmount *= 1.0 - gReference; // TODO: I would be glad to use virtual motion in the reference mode, but it requires denoised hit distances. Unfortunately, in the reference mode blur radius is set to 0

    // Hit distance based disocclusion for virtual motion
    float hitDistVirtual = GetHitDistance( historyVirtual.w, viewZ, gScalingParams, roughness );
    float relativeDelta = abs( hitDist - hitDistVirtual ) * STL::Math::PositiveRcp( min( hitDistVirtual, hitDist ) + abs( viewZ ) );

    float relativeDeltaThreshold = lerp( 0.01, 0.25, roughnessModified * roughnessModified );
    relativeDeltaThreshold += 0.02 * ( 1.0 - STL::Math::SmoothStep( 0.01, 0.2, parallax ) ); // increase the threshold if parallax is low (big disocclusions produced by dynamic objects will still be handled)

    float virtualHistoryCorrectness = step( relativeDelta, relativeDeltaThreshold );
    virtualHistoryCorrectness *= 1.0 - STL::Math::SmoothStep( 0.25, 1.0, parallax );

    float accumSpeedScale = lerp( roughnessModified, 1.0, virtualHistoryCorrectness );
    accumSpeedScale = lerp( accumSpeedScale, 1.0, 1.0 / ( 1.0 + accumSpeed ) );

    float minAccumSpeed = min( accumSpeed, 4.0 );
    accumSpeed = minAccumSpeed + ( accumSpeed - minAccumSpeed ) * lerp( 1.0, accumSpeedScale, virtualHistoryAmount );

    // Current value ( virtual motion )
    float2 accumSpeedsVirtual = GetSpecAccumSpeed( accumSpeed, roughnessModified, avgNoV, 0.0 );
    float accumSpeedVirtual = 1.0 / ( trimmingFade * accumSpeedsVirtual.x + 1.0 );

    float4 currentVirtual;
    currentVirtual.xyz = lerp( historyVirtual.xyz, input.xyz, accumSpeedVirtual );
    currentVirtual.w = lerp( historyVirtual.w, input.w, max( accumSpeedVirtual, MIN_HITDIST_ACCUM_SPEED ) );

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
    float4 result;
    result.xyz = lerp( currentSurface.xyz, currentVirtual.xyz, virtualHistoryAmount );
    result.w = currentSurface.w;

    float parallaxMod = parallax * ( 1.0 - virtualHistoryAmount );
    float2 specAccumSpeeds = GetSpecAccumSpeed( accumSpeed, roughnessModified, modNoV, parallaxMod );

    #if( SHOW_MIPS != 0 )
        specAccumSpeed = 1.0;
    #endif

    // Add low amplitude noise to fight with imprecision problems
    STL::Rng::Initialize( pixelPos, gFrameIndex + 781 );
    float rnd = STL::Rng::GetFloat2( ).x;
    float dither = 1.0 + ( rnd * 2.0 - 1.0 ) * DITHERING_AMPLITUDE;
    result *= dither;

    // Get rid of possible negative values
    result.xyz = _NRD_YCoCgToLinear( result.xyz );
    result.w = max( result.w, 0.0 );
    result.xyz = _NRD_LinearToYCoCg( result.xyz );

    // Output
    float scaledViewZ = clamp( viewZ * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );

    gOut_SpecHit[ pixelPos ] = result;
    gOut_InternalData[ pixelPos ] = PackSpecInternalData( float3( specAccumSpeeds, accumSpeed ), virtualHistoryAmount, parallaxMod );
    gOut_ScaledViewZ[ pixelPos ] = scaledViewZ;
}
