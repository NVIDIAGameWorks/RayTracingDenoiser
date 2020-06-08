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
    float gInvAverageFrameTime;
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

void Preload( int2 sharedId, int2 globalId )
{
    float4 t;
    t.xyz = UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] ).xyz;
    t.w = gIn_ViewZ[ globalId ];

    s_Normal_ViewZ[ sharedId.y ][ sharedId.x ] = t;
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
            sum += w;
        }
    }

    #if( USE_PSEUDO_FLAT_NORMALS )
        Nflat = normalize( Nflat );
    #endif

    float3 Navg = Nsum / sum;
    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );
    float roughnessRatio = STL::Math::Sqrt01( 2.0 * roughnessModified / ( roughness + roughnessModified ) - 1.0 );

    float trimmingFade = GetTrimmingFactor( roughness, gTrimmingParams );
    trimmingFade = STL::Math::LinearStep( 0.0, 0.1, trimmingFade ); // TODO: is it needed? Better settings?
    trimmingFade = lerp( trimmingFade, 1.0, roughnessRatio );

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Previous viewZ and accumulation speed
    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gScreenSize );
    float2 sampleUvNearestPrev = ( bilinearFilterAtPrevPos.origin + 0.5 ) * gInvScreenSize;
    float2 gatherUvPrev = ( bilinearFilterAtPrevPos.origin + 1.0 ) * gInvScreenSize;
    uint4 pack = gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, gatherUvPrev ).wzxy;
    float4 viewZprev = UnpackViewZ( pack );
    float4 accumSpeedsPrev = UnpackAccumSpeed( pack );

    // Compute disocclusion basing on plane distance
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev = dot( Nflat, Xprev ); // = dot( Nvflatprev, Xvprev )
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) ); // = dot( Nvflatprev, Xvprev / Xvprev.z )
    float4 planeDist = abs( NoVprev * abs( viewZprev ) - NoXprev );
    float4 occlusion = saturate( isInScreen - step( gDisocclusionThreshold, planeDist * invDistToPoint ) );
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
    parallax *= gInvAverageFrameTime;
    parallax *= 1.0 - roughnessRatio;

    // Accumulation speed ( gets reset to 0 if a disocclusion is detected )
    float4 accumSpeeds = min( accumSpeedsPrev + 1.0, gMaxSpecAccumulatedFrameNum );
    float accumSpeed = STL::Filtering::ApplyBilinearCustomWeights( accumSpeeds.x, accumSpeeds.y, accumSpeeds.z, accumSpeeds.w, surfaceWeights );

    // Sample history ( surface motion )
    float4 s00 = gIn_History_SpecHit.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0 );
    float4 s10 = gIn_History_SpecHit.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 0 ) );
    float4 s01 = gIn_History_SpecHit.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 0, 1 ) );
    float4 s11 = gIn_History_SpecHit.SampleLevel( gNearestClamp, sampleUvNearestPrev, 0, int2( 1, 1 ) );
    float4 historySurface = STL::Filtering::ApplyBilinearCustomWeights( s00, s10, s01, s11, surfaceWeights ); // TODO: Use BicubicFilterNoCorners if all pixels are valid? Should I care about pixels outside of 2x2 footprint?

    // Current data ( with signal reconstruction for checkerboard )
    float4 specHit = gIn_SpecHit[ pixelPos ];

    #if( CHECKERBOARD_SUPPORT == 1 )
        bool isDiffuse = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex ) != 0;
        if ( gCheckerboard != 0 && isDiffuse )
        {
            int3 pixelPosLR = int3( pixelPos.x - 1, pixelPos.x + 1, pixelPos.y );

            float viewZ_Left = s_Normal_ViewZ[ centerId.y ][ centerId.x - 1 ].w;
            float viewZ_Right = s_Normal_ViewZ[ centerId.y ][ centerId.x + 1 ].w;

            float4 specHit_left = gIn_SpecHit[ pixelPosLR.xz ];
            float4 specHit_right = gIn_SpecHit[ pixelPosLR.yz ];

            float2 zDelta = GetBilateralWeight( float2( viewZ_Left, viewZ_Right ), viewZ );
            float2 w = zDelta * STL::Math::PositiveRcp( zDelta.x + zDelta.y );
            float w00 = saturate( 1.0 - w.x - w.y );

            specHit = specHit_left * w.x + specHit_right * w.y + specHit * w00;

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

    float hitDist = GetHitDistance( currentSurface.w, abs( viewZ ), gScalingParams, roughness );

    parallax *= saturate( hitDist * invDistToPoint );
    accumSpeedsSurface = GetSpecAccumSpeed( accumSpeed, roughnessModified, NoV, parallax );
    accumSpeedSurface = 1.0 / ( trimmingFade * accumSpeedsSurface.x + 1.0 );

    currentSurface.xyz = lerp( historySurface.xyz, specHit.xyz, accumSpeedSurface );

    // Sample history ( virtual motion )
    float3 V = STL::Geometry::RotateVector( gViewToWorld, -normalize( Xv ) );
    float3 Xvirtual = Xprev - V * hitDist * ( 1.0 - roughnessModified );
    float4 clipVirtualPrev = STL::Geometry::ProjectiveTransform( gWorldToClipPrev, Xvirtual );
    float2 pixelUvVirtualPrev = ( clipVirtualPrev.xy / clipVirtualPrev.w ) * float2( 0.5, -0.5 ) + 0.5;

    // TODO: use BicubicFilterNoCorners if all pixels are valid? Should I care about pixels outside of 2x2 footprint which are not tracked by disocclusion detection?
    float4 historyVirtual = gIn_History_SpecHit.SampleLevel( gLinearClamp, pixelUvVirtualPrev, 0 );

    // Normal based disocclusion for virtual motion
    STL::Filtering::Bilinear bilinearFilterAtVirtualPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gScreenSize );
    float2 gatherUvVirtualPrev = ( bilinearFilterAtVirtualPrevPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevNormals = gIn_Prev_ViewZ_AccumSpeed.GatherGreen( gNearestClamp, gatherUvVirtualPrev ).wzxy;
    float3 n00 = UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevNormals.x, 8, 8, 8, 8 ), true ).xyz;
    float3 n10 = UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevNormals.y, 8, 8, 8, 8 ), true ).xyz;
    float3 n01 = UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevNormals.z, 8, 8, 8, 8 ), true ).xyz;
    float3 n11 = UnpackNormalAndRoughness( STL::Packing::UintToRgba( prevNormals.w, 8, 8, 8, 8 ), true ).xyz;

    float a0 = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughnessModified );
    a0 *= 0.5; // To avoid overblurring
    a0 += STL::Math::DegToRad( 2.5 );

    // TODO: replace V with Vprev = normalize( movementDelta - X )?
    // TODO: if this is used in spatial passes it makes the image more sharp, but spatial filtering starts to follow the view vector, what moves the image a bit away from the reference
    float3 D00 = STL::ImportanceSampling::GetSpecularDominantDirection( n00, V, roughnessModified );
    float3 D10 = STL::ImportanceSampling::GetSpecularDominantDirection( n10, V, roughnessModified );
    float3 D01 = STL::ImportanceSampling::GetSpecularDominantDirection( n01, V, roughnessModified );
    float3 D11 = STL::ImportanceSampling::GetSpecularDominantDirection( n11, V, roughnessModified );
    float3 D = STL::ImportanceSampling::GetSpecularDominantDirection( N, V, roughnessModified );

    float4 angularWeight;
    angularWeight.x = dot( D00, D );
    angularWeight.y = dot( D10, D );
    angularWeight.z = dot( D01, D );
    angularWeight.w = dot( D11, D );
    angularWeight = STL::Math::AcosApprox( saturate( angularWeight ) );
    angularWeight = 1.0 - STL::Math::SmoothStep( 0.0, a0, angularWeight );
    angularWeight *= float4( prevNormals != 0 );

    // ViewZ based disocclusion for virtual motion
    float4 prevViewZVirtual = UnpackViewZ( gIn_Prev_ViewZ_AccumSpeed.GatherRed( gNearestClamp, gatherUvVirtualPrev ).wzxy );
    float4 relativeDeltas = abs( prevViewZVirtual - Xvprev.z ) * STL::Math::PositiveRcp( min( abs( Xvprev.z ), abs( prevViewZVirtual ) ) );
    float zThreshold = lerp( 0.03, 0.1, sqrt( saturate( 1.0 - NoV ) ) );
    float4 zWeight = STL::Math::LinearStep( zThreshold, 0.02, relativeDeltas );

    // Amount of virtual motion
    float4 totalWeight = angularWeight * zWeight;
    totalWeight.xy = min( totalWeight.xy, totalWeight.zw );
    float virtualHistoryAmount = min( totalWeight.x, totalWeight.y );
    virtualHistoryAmount *= float( all( saturate( pixelUvVirtualPrev ) == pixelUvVirtualPrev ) );

    // Hit distance based disocclusion for virtual motion
    float hitDistVirtual = GetHitDistance( historyVirtual.w, abs( viewZ ), gScalingParams, roughness );
    float relativeDelta = abs( hitDist - hitDistVirtual ) * STL::Math::PositiveRcp( min( hitDistVirtual, hitDist ) + abs( viewZ ) );
    float threshold = lerp( 0.03, 1.0, roughnessModified * roughnessModified );
    float virtualHistoryCorrectness = STL::Math::LinearStep( threshold * 2.0, threshold, relativeDelta );
    virtualHistoryCorrectness = lerp( 1.0, virtualHistoryCorrectness, saturate( parallax * trimmingFade ) );

    float scale = lerp( roughnessModified * roughnessModified, 1.0, virtualHistoryCorrectness );
    accumSpeed *= lerp( 1.0, scale, virtualHistoryAmount );

    // Current value ( virtual motion )
    float2 accumSpeedsVirtual = GetSpecAccumSpeed( accumSpeed, roughnessModified, NoV, 0.0 );
    float accumSpeedVirtual = 1.0 / ( trimmingFade * accumSpeedsVirtual.x + 1.0 );

    float4 currentVirtual;
    currentVirtual.xyz = lerp( historyVirtual.xyz, specHit.xyz, accumSpeedVirtual );
    currentVirtual.w = lerp( historyVirtual.w, specHit.w, max( accumSpeedVirtual, MIN_HITDIST_ACCUM_SPEED ) );

    // Final composition
    normalAndRoughnessPacked.w = virtualHistoryAmount;

    float4 final;
    final.xyz = lerp( currentSurface.xyz, currentVirtual.xyz, virtualHistoryAmount );
    final.w = currentSurface.w;

    float parallaxMod = parallax * ( 1.0 - virtualHistoryAmount );
    float2 specAccumSpeeds = GetSpecAccumSpeed( accumSpeed, roughnessModified, NoV, parallaxMod );

    #if( SHOW_MIPS != 0 )
        specAccumSpeed = 1.0;
    #endif

    // Add low amplitude noise to fight with imprecision problems
    STL::Rng::Initialize( pixelPos, gFrameIndex + 781 );
    float rnd = STL::Rng::GetFloat2( ).x;
    float dither = 1.0 + ( rnd * 2.0 - 1.0 ) * DITHERING_AMPLITUDE;
    final *= dither;

    // Output
    float scaledViewZ = clamp( viewZ * NRD_FP16_VIEWZ_SCALE, -NRD_FP16_MAX, NRD_FP16_MAX );

    gOut_SpecHit[ pixelPos ] = final;
    gOut_InternalData[ pixelPos ] = PackSpecInternalData( float3( specAccumSpeeds, accumSpeed ), roughnessModified, parallaxMod );
    gOut_ViewZ_AccumSpeed[ pixelPos ] = uint2( PackViewZAndAccumSpeed( viewZ, accumSpeed ), STL::Packing::RgbaToUint( normalAndRoughnessPacked, 8, 8, 8, 8 ) );
    gOut_ScaledViewZ[ pixelPos ] = scaledViewZ;
}
