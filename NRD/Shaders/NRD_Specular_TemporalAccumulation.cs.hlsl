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
    float4x4 gWorldToViewPrev;
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4 gFrustum;
    float4 gFrustumPrev;
    float3 gCameraDelta;
    float gInf;
    float4 gScalingParams;
    float3 gTrimmingParams;
    float gIsOrtho;
    float2 gInvScreenSize;
    float2 gScreenSize;
    float2 gJitter;
    float2 gMotionVectorScale;
    float gIsOrthoPrev;
    float gDisocclusionThreshold;
    float gMaxSpecAccumulatedFrameNum;
    float gReference;
    uint gFrameIndex;
    uint gCheckerboard;
    uint gWorldSpaceMotion;
    float gDebug;
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
NRI_RESOURCE( RWTexture2D<uint2>, gOut_ViewZ_AccumSpeed, u, 2, 0 );
NRI_RESOURCE( RWTexture2D<float>, gOut_ScaledViewZ, u, 3, 0 );

#define BORDER 1
#define GROUP_X 16
#define GROUP_Y 16
#define BUFFER_X ( GROUP_X + BORDER * 2 )
#define BUFFER_Y ( GROUP_Y + BORDER * 2 )
#define RENAMED_GROUP_Y ( ( GROUP_X * GROUP_Y ) / BUFFER_X )

groupshared float4 s_Normal_ViewZ[ BUFFER_Y ][ BUFFER_X ]; // TODO: add roughness? (needed for the center)
groupshared float4 s_Input[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    float4 t;
    t.xyz = UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] ).xyz;
    t.w = gIn_ViewZ[ globalId ];

    s_Normal_ViewZ[ sharedId.y ][ sharedId.x ] = t;
    s_Input[ sharedId.y ][ sharedId.x ] = gIn_SpecHit[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = ( float2( pixelPos ) + 0.5 ) * gInvScreenSize;
    float2 sampleUv = pixelUv + gJitter;

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
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_SpecHit[ pixelPos ] = 0;
        #endif
        gOut_InternalData[ pixelPos ] = PackSpecInternalData( MAX_ACCUM_FRAME_NUM, 0, 0 ); // MAX_ACCUM_FRAME_NUM is needed here to skip HistoryFix on INF pixels
        gOut_ViewZ_AccumSpeed[ pixelPos ] = uint2( PackViewZAndAccumSpeed( NRD_FP16_MAX, 0 ), 0 );
        gOut_ScaledViewZ[ pixelPos ] = NRD_FP16_MAX;
        return;
    }

    // Center position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );

    // Normal and roughness
    float4 normalAndRoughnessPacked = gIn_Normal_Roughness[ pixelPos ];
    float roughness = UnpackNormalAndRoughness( normalAndRoughnessPacked ).w;
    float3 N = centerData.xyz;

    // Calculate distribution of normals
    float4 specHit = s_Input[ centerId.y ][ centerId.x ];
    float4 m1 = specHit;
    float4 m2 = m1 * m1;
    float3 Nflat = N;
    float3 Nsum = N;
    float sum = 1.0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if ( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadId + int2( dx, dy );
            float4 normalAndViewZ = s_Normal_ViewZ[ pos.y ][ pos.x ];

            float d = abs( normalAndViewZ.w - viewZ ) * STL::Math::PositiveRcp( min( abs( normalAndViewZ.w ), abs( viewZ ) ) );
            float w = 1.0 - step( 0.01, d );

            #if( USE_PSEUDO_FLAT_NORMALS )
                // TODO: all 9? or 5 samples like it was before?
                Nflat += normalAndViewZ.xyz; // yes, no weight
            #endif

            Nsum += normalAndViewZ.xyz * w;

            float4 input = s_Input[ pos.y ][ pos.x ];
            m1 += input * w;
            m2 += input * input * w;

            sum += w;
        }
    }

    float invSum = 1.0 / sum;
    m1 *= invSum;
    m2 *= invSum;
    float4 sigma = sqrt( abs( m2 - m1 * m1 ) );

    #if( USE_PSEUDO_FLAT_NORMALS )
        Nflat = normalize( Nflat );
    #endif

    float3 Navg = Nsum * invSum;
    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );
    float roughnessRatio = STL::Math::Sqrt01( 2.0 * roughnessModified / ( roughness + roughnessModified ) - 1.0 );

    float trimmingFade = GetTrimmingFactor( roughness, gTrimmingParams );
    trimmingFade = STL::Math::LinearStep( 0.0, 0.1, trimmingFade ); // TODO: is it needed? Better settings?
    trimmingFade = lerp( trimmingFade, 1.0, roughnessRatio );

    // Compute previous pixel position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Previous viewZ and accumulation speed for Catmull-Rom filter
    STL::Filtering::CatmullRom catmullRomFilterAtPrevPos = STL::Filtering::GetCatmullRomFilter( saturate( pixelUvPrev ), gScreenSize );
    float2 catmullRomFilterAtPrevPosGatherOrigin = catmullRomFilterAtPrevPos.origin * gInvScreenSize;
    uint4 packedViewZAndAccumSpeed0 = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
    uint4 packedViewZAndAccumSpeed1 = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 packedViewZAndAccumSpeed2 = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 packedViewZAndAccumSpeed3 = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;
    float4 viewZprev0 = UnpackViewZ( packedViewZAndAccumSpeed0 );
    float4 viewZprev1 = UnpackViewZ( packedViewZAndAccumSpeed1 );
    float4 viewZprev2 = UnpackViewZ( packedViewZAndAccumSpeed2 );
    float4 viewZprev3 = UnpackViewZ( packedViewZAndAccumSpeed3 );
    float4 accumSpeedsPrev0 = UnpackAccumSpeed( packedViewZAndAccumSpeed0 );
    float4 accumSpeedsPrev1 = UnpackAccumSpeed( packedViewZAndAccumSpeed1 );
    float4 accumSpeedsPrev2 = UnpackAccumSpeed( packedViewZAndAccumSpeed2 );
    float4 accumSpeedsPrev3 = UnpackAccumSpeed( packedViewZAndAccumSpeed3 );

    // Compute disocclusion basing on plane distance
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev = dot( Nflat, Xprev ) * invDistToPoint; // = dot( Nvflatprev, Xvprev )
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) ); // = dot( Nvflatprev, Xvprev / Xvprev.z )
    float4 planeDist0 = abs( NoVprev * abs( viewZprev0 ) - NoXprev );
    float4 planeDist1 = abs( NoVprev * abs( viewZprev1 ) - NoXprev );
    float4 planeDist2 = abs( NoVprev * abs( viewZprev2 ) - NoXprev );
    float4 planeDist3 = abs( NoVprev * abs( viewZprev3 ) - NoXprev );
    float4 occlusion0 = saturate( isInScreen - step( gDisocclusionThreshold, planeDist0 ) );
    float4 occlusion1 = saturate( isInScreen - step( gDisocclusionThreshold, planeDist1 ) );
    float4 occlusion2 = saturate( isInScreen - step( gDisocclusionThreshold, planeDist2 ) );
    float4 occlusion3 = saturate( isInScreen - step( gDisocclusionThreshold, planeDist3 ) );

    float4 occlusion = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );
    float4 accumSpeedsPrev = float4( accumSpeedsPrev0.w, accumSpeedsPrev1.z, accumSpeedsPrev2.y, accumSpeedsPrev3.x );
    float4 viewZprev = float4( viewZprev0.w, viewZprev1.z, viewZprev2.y, viewZprev3.x );
    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gScreenSize );
    float4 surfaceWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, occlusion );

    // Reconstruct previous world position
    if( !gWorldSpaceMotion )
    {
        float prevZestimation = STL::Filtering::ApplyBilinearCustomWeights( viewZprev.x, viewZprev.y, viewZprev.z, viewZprev.w, surfaceWeights ); // 0 if a disocclusion is detected
        Xvprev = STL::Geometry::ReconstructViewPosition( pixelUvPrev, gFrustumPrev, prevZestimation, gIsOrthoPrev );
        Xprev = STL::Geometry::RotateVectorInverse( gWorldToViewPrev, Xvprev ) + gCameraDelta; // = STL::Geometry::AffineTransform( gViewToWorldPrev, Xvprev )
    }

    // Compute parallax
    float3 Nvflat = STL::Geometry::RotateVectorInverse( gViewToWorld, N );  // TODO: is it better to use "Nflat" here?
    float NoV = abs( dot( Nvflat, Xv ) ) * invDistToPoint;
    float3 movementDelta = X - ( Xprev - gCameraDelta ); // Xprev = can be shifted back by object motion, if we are lucky "Xprev - gCameraDelta" can be equal to "X"
    float parallax = length( movementDelta ) * invDistToPoint; // Tan of angle between old and new view vector in world space (sine works worse, tan helps to kill accumulation if parallax is >>1)
    parallax *= 60.0; // yes, tuned for 60 FPS to get auto-scaled solution if FPS is higher (more accumulation) or lower (less accumulation)
    parallax *= 1.0 - roughnessRatio;

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

    // Current data ( with signal reconstruction for checkerboard )
    #if( CHECKERBOARD_SUPPORT == 1 )
        bool isNoData = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex ) == gCheckerboard;
        if ( gCheckerboard != 0 && isNoData )
        {
            float viewZ_Left = s_Normal_ViewZ[ centerId.y ][ centerId.x - 1 ].w;
            float viewZ_Right = s_Normal_ViewZ[ centerId.y ][ centerId.x + 1 ].w;

            float4 specHit_Left = s_Input[ centerId.y ][ centerId.x - 1 ];
            float4 specHit_Right = s_Input[ centerId.y ][ centerId.x + 1 ];

            float2 zDelta = GetBilateralWeight( float2( viewZ_Left, viewZ_Right ), viewZ );
            float2 w = zDelta * STL::Math::PositiveRcp( zDelta.x + zDelta.y );
            float w00 = saturate( 1.0 - w.x - w.y );

            specHit = specHit_Left * w.x + specHit_Right * w.y + specHit * w00;

            // Mix with history ( optional )
            float2 motion = pixelUvPrev - pixelUv;
            float motionLength = length( motion );
            float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, accumSpeed, motionLength, STL::Math::Pow01( parallax, 0.25 ), roughnessModified );
            float historyWeight = temporalAccumulationParams.x * roughnessRatio;

            specHit = lerp( specHit, historySurface, min( historyWeight, 0.5 ) );
        }
    #endif

    // Current value ( surface motion )
    float2 accumSpeedsSurface = GetSpecAccumSpeed( accumSpeed, roughnessModified, NoV, parallax );
    float accumSpeedSurface = 1.0 / ( trimmingFade * accumSpeedsSurface.x + 1.0 );

    float4 currentSurface;
    currentSurface.w = lerp( historySurface.w, specHit.w, max( accumSpeedSurface, MIN_HITDIST_ACCUM_SPEED ) );

    float hitDist = GetHitDistance( currentSurface.w, viewZ, gScalingParams, roughness );

    parallax *= saturate( hitDist * invDistToPoint );
    accumSpeedsSurface = GetSpecAccumSpeed( accumSpeed, roughnessModified, NoV, parallax );
    accumSpeedSurface = 1.0 / ( trimmingFade * accumSpeedsSurface.x + 1.0 );

    currentSurface.xyz = lerp( historySurface.xyz, specHit.xyz, accumSpeedSurface );

    // Compute previous pixel position for virtual motion
    float3 V = STL::Geometry::RotateVector( gViewToWorld, -normalize( Xv ) );
    float3 Xvirtual = Xprev - V * hitDist * ( 1.0 - roughness );
    float4 clipVirtualPrev = STL::Geometry::ProjectiveTransform( gWorldToClipPrev, Xvirtual );
    float2 pixelUvVirtualPrev = ( clipVirtualPrev.xy / clipVirtualPrev.w ) * float2( 0.5, -0.5 ) + 0.5;
    float isInScreenVirtual = float( all( saturate( pixelUvVirtualPrev ) == pixelUvVirtualPrev ) );

    // Normal and roughness based disocclusion for virtual motion
    STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gScreenSize );
    float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevNormals = gIn_Prev_ViewZ_AccumSpeed.GatherGreen( gNearestClamp, gatherUvVirtualPrev ).wzxy;

    float2 normalParams;
    normalParams.x = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
    normalParams.x *= 0.333; // To avoid overblurring
    normalParams.x += STL::Math::DegToRad( 2.5 );
    normalParams.y = 1.0;

    float2 roughnessParams = GetRoughnessWeightParams( roughness );

    float4 Nvirtual00 = UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevNormals.x, 8, 8, 8, 8 ), false );
    float4 Nvirtual10 = UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevNormals.y, 8, 8, 8, 8 ), false );
    float4 Nvirtual01 = UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevNormals.z, 8, 8, 8, 8 ), false );
    float4 Nvirtual11 = UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevNormals.w, 8, 8, 8, 8 ), false );

    float4 specularLobeOcclusion;
    specularLobeOcclusion.x = GetNormalWeight( normalParams, N, Nvirtual00.xyz );
    specularLobeOcclusion.y = GetNormalWeight( normalParams, N, Nvirtual10.xyz );
    specularLobeOcclusion.z = GetNormalWeight( normalParams, N, Nvirtual01.xyz );
    specularLobeOcclusion.w = GetNormalWeight( normalParams, N, Nvirtual11.xyz );

    float4 roughnessWeight;
    roughnessWeight.x = GetRoughnessWeight( roughnessParams, Nvirtual00.w );
    roughnessWeight.y = GetRoughnessWeight( roughnessParams, Nvirtual10.w );
    roughnessWeight.z = GetRoughnessWeight( roughnessParams, Nvirtual01.w );
    roughnessWeight.w = GetRoughnessWeight( roughnessParams, Nvirtual11.w );
    roughnessWeight = STL::Math::LinearStep( 0.0, 0.8, roughnessWeight );
    specularLobeOcclusion *= roughnessWeight;

    // ViewZ based disocclusion for virtual motion
    STL::Filtering::CatmullRom catmullRomFilterAtPrevVirtualPos = STL::Filtering::GetCatmullRomFilter( saturate( pixelUvVirtualPrev ), gScreenSize );
    float2 catmullRomFilterAtPrevVirtualPosGatherOrigin = catmullRomFilterAtPrevVirtualPos.origin * gInvScreenSize;
    float4 prevViewZVirtual0 = UnpackViewZ( gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevVirtualPosGatherOrigin, float2( 1, 1 ) ).wzxy );
    float4 prevViewZVirtual1 = UnpackViewZ( gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevVirtualPosGatherOrigin, float2( 3, 1 ) ).wzxy );
    float4 prevViewZVirtual2 = UnpackViewZ( gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevVirtualPosGatherOrigin, float2( 1, 3 ) ).wzxy );
    float4 prevViewZVirtual3 = UnpackViewZ( gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, catmullRomFilterAtPrevVirtualPosGatherOrigin, float2( 3, 3 ) ).wzxy );
    float4 occlusionVirtual0 = abs( prevViewZVirtual0 - Xvprev.z ) * STL::Math::PositiveRcp( min( abs( Xvprev.z ), abs( prevViewZVirtual0 ) ) );
    float4 occlusionVirtual1 = abs( prevViewZVirtual1 - Xvprev.z ) * STL::Math::PositiveRcp( min( abs( Xvprev.z ), abs( prevViewZVirtual1 ) ) );
    float4 occlusionVirtual2 = abs( prevViewZVirtual2 - Xvprev.z ) * STL::Math::PositiveRcp( min( abs( Xvprev.z ), abs( prevViewZVirtual2 ) ) );
    float4 occlusionVirtual3 = abs( prevViewZVirtual3 - Xvprev.z ) * STL::Math::PositiveRcp( min( abs( Xvprev.z ), abs( prevViewZVirtual3 ) ) );
    float zThreshold = lerp( 0.03, 0.1, sqrt( saturate( 1.0 - NoV ) ) );
    occlusionVirtual0 = STL::Math::LinearStep( zThreshold, 0.02, occlusionVirtual0 );
    occlusionVirtual1 = STL::Math::LinearStep( zThreshold, 0.02, occlusionVirtual1 );
    occlusionVirtual2 = STL::Math::LinearStep( zThreshold, 0.02, occlusionVirtual2 );
    occlusionVirtual3 = STL::Math::LinearStep( zThreshold, 0.02, occlusionVirtual3 );

    // Sample history ( virtual motion )
    float2 catmullRomFilterAtPrevVirtualPosOrigin = ( catmullRomFilterAtPrevVirtualPos.origin + 0.5 ) * gInvScreenSize;
    s00 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0 );
    s10 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 1, 0 ) );
    s20 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 2, 0 ) );
    s30 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 3, 0 ) );
    s01 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 0, 1 ) );
    s11 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 1, 1 ) );
    s21 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 2, 1 ) );
    s31 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 3, 1 ) );
    s02 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 0, 2 ) );
    s12 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 1, 2 ) );
    s22 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 2, 2 ) );
    s32 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 3, 2 ) );
    s03 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 0, 3 ) );
    s13 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 1, 3 ) );
    s23 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 2, 3 ) );
    s33 = gIn_History_SpecHit.SampleLevel( gNearestClamp, catmullRomFilterAtPrevVirtualPosOrigin, 0, int2( 3, 3 ) );

    float4 occlusionVirtual = float4( occlusionVirtual0.w, occlusionVirtual1.z, occlusionVirtual2.y, occlusionVirtual3.x );
    specularLobeOcclusion *= occlusionVirtual;

    float4 historyVirtual = STL::Filtering::ApplyCatmullRomFilterWithCustomWeights( catmullRomFilterAtPrevVirtualPos, s00, s10, s20, s30, s01, s11, s21, s31, s02, s12, s22, s32, s03, s13, s23, s33, occlusionVirtual0, occlusionVirtual1, occlusionVirtual2, occlusionVirtual3 );
    if( any( occlusionVirtual != 1.0 ) || USE_CATMULLROM_RESAMPLING_IN_TA == 0 )
    {
        float4 virtualWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevVirtualPos, occlusionVirtual );
        historyVirtual = STL::Filtering::ApplyBilinearCustomWeights( s11, s21, s12, s22, virtualWeights );
    }

    // Amount of virtual motion
    float2 temp = min( specularLobeOcclusion.xy, specularLobeOcclusion.zw );
    float virtualHistoryAmount = min( temp.x, temp.y );
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
    float2 accumSpeedsVirtual = GetSpecAccumSpeed( accumSpeed, roughnessModified, NoV, 0.0 );
    float accumSpeedVirtual = 1.0 / ( trimmingFade * accumSpeedsVirtual.x + 1.0 );

    float4 currentVirtual;
    currentVirtual.xyz = lerp( historyVirtual.xyz, specHit.xyz, accumSpeedVirtual );
    currentVirtual.w = lerp( historyVirtual.w, specHit.w, max( accumSpeedVirtual, MIN_HITDIST_ACCUM_SPEED ) );

    // Virtual history color clamping
    float sigmaScale = 3.0 + TS_SIGMA_AMPLITUDE * STL::Math::SmoothStep( 0.04, 0.65, roughnessModified );
    float4 colorMin = m1 - sigma * sigmaScale;
    float4 colorMax = m1 + sigma * sigmaScale;
    float4 currentVirtualClamped = clamp( currentVirtual, colorMin, colorMax );

    float scale = lerp( roughnessModified * roughnessModified, 1.0, virtualHistoryCorrectness );
    currentVirtual = lerp( currentVirtualClamped, currentVirtual, scale );

    // Final composition
    float4 result;
    result.xyz = lerp( currentSurface.xyz, currentVirtual.xyz, virtualHistoryAmount );
    result.w = currentSurface.w;

    float parallaxMod = parallax * ( 1.0 - virtualHistoryAmount );
    float2 specAccumSpeeds = GetSpecAccumSpeed( accumSpeed, roughnessModified, NoV, parallaxMod );

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
    gOut_ViewZ_AccumSpeed[ pixelPos ] = uint2( PackViewZAndAccumSpeed( viewZ, accumSpeed ), STL::Packing::RgbaToUint( normalAndRoughnessPacked, 8, 8, 8, 8 ) );
    gOut_ScaledViewZ[ pixelPos ] = scaledViewZ;
}
