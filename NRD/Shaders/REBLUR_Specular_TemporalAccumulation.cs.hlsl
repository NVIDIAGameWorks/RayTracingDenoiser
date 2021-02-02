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

    float4x4 gWorldToViewPrev;
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4x4 gWorldToClip;
    float4 gCameraDelta;
    float4 gFrustumPrev;
    float4 gSpecHitDistParams;
    float2 gMotionVectorScale;
    float gCheckerboardResolveAccumSpeed;
    float gIsOrthoPrev;
    float gDisocclusionThreshold;
    float gJitterDelta;
    float gSpecMaxAccumulatedFrameNum;
    float gSpecNoisinessBlurrinessBalance;
    uint gSpecCheckerboard;
};

#define USE_8x8
#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<uint2>, gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Spec, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 5, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float2>, gOut_InternalData, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 1, 0 );

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
    if( abs( viewZ ) > abs( gInf ) )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Spec[ pixelPos ] = 0;
        #endif
        gOut_InternalData[ pixelPos ] = PackSpecInternalData( );
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
    float4 specM1 = spec;
    float4 specM2 = spec * spec;

    float sum = 1.0;
    float3 Nflat = N;
    float3 Nsum = N;
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
            float4 n = s_Normal_Roughness[ pos.y ][ pos.x ];
            float z = s_ViewZ[ pos.y ][ pos.x ];

            float w = GetBilateralWeight( z, viewZ );
            w *= GetNormalWeight( normalParams, N, n.xyz );
            w *= GetRoughnessWeight( roughnessParams, n.w );

            Nflat += n.xyz; // yes, no weight
            Nsum += n.xyz * w;
            avgNoV += abs( dot( n.xyz, V ) ) * w;

            float4 s = s_Spec[ pos.y ][ pos.x ];
            specM1 += s * w;
            specM2 += s * s * w;

            sum += w;
        }
    }

    float invSum = 1.0 / sum;
    specM1 *= invSum;
    specM2 *= invSum;
    float4 specSigma = GetVariance( specM1, specM2 );

    Nflat = normalize( Nflat );
    avgNoV *= invSum;

    float3 Navg = Nsum * invSum;
    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );

    // Compute previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, IsWorldSpaceMotion() );
    float isInScreen = IsInScreen( pixelUvPrev );
    float2 pixelMotion = pixelUvPrev - pixelUv;
    float3 Xprev = X + motionVector * float( IsWorldSpaceMotion() );

    // Previous data ( Catmull-Rom )
    STL::Filtering::CatmullRom catmullRomFilterAtPrevPos = STL::Filtering::GetCatmullRomFilter( saturate( pixelUvPrev ), gScreenSize );
    float2 catmullRomFilterAtPrevPosGatherOrigin = catmullRomFilterAtPrevPos.origin * gInvScreenSize;
    uint4 prevPackRed0 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
    uint4 prevPackRed1 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 prevPackRed2 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 prevPackRed3 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;
    uint4 prevPackGreen0 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy; // TODO: try to get rid of complicated roughness checks, use a single Gather like in diffuse
    uint4 prevPackGreen1 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 prevPackGreen2 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 prevPackGreen3 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;

    float4 prevViewZ0 = UnpackViewZ( prevPackRed0 );
    float4 prevViewZ1 = UnpackViewZ( prevPackRed1 );
    float4 prevViewZ2 = UnpackViewZ( prevPackRed2 );
    float4 prevViewZ3 = UnpackViewZ( prevPackRed3 );

    float4 specPrevAccumSpeeds;
    float3 prevNormal00 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen0.w, specPrevAccumSpeeds.x ).xyz;
    float3 prevNormal10 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen1.z, specPrevAccumSpeeds.y ).xyz;
    float3 prevNormal01 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen2.y, specPrevAccumSpeeds.z ).xyz;
    float3 prevNormal11 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen3.x, specPrevAccumSpeeds.w ).xyz;

    float4 prevRoughness0 = UnpackRoughness( prevPackGreen0 );
    float4 prevRoughness1 = UnpackRoughness( prevPackGreen1 );
    float4 prevRoughness2 = UnpackRoughness( prevPackGreen2 );
    float4 prevRoughness3 = UnpackRoughness( prevPackGreen3 );

    float3 prevNflat = prevNormal00 + prevNormal10 + prevNormal01 + prevNormal11;
    prevNflat = normalize( prevNflat );

    // Plane distance based disocclusion for surface motion
    float parallax = ComputeParallax( pixelUv, Xprev, gCameraDelta.xyz, gWorldToClip );
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

    // Avoid "got stuck in history" effect under slow motion when only 1 sample is valid from 2x2 footprint and there is a big difference between
    // foreground and background surfaces. Instead of final scalar accum speed scaling we can apply it to accum speeds from the previous frame
    float4 planeDist2x2 = float4( planeDist0.w, planeDist1.z, planeDist2.y, planeDist3.x );
    planeDist2x2 = STL::Math::LinearStep( 0.2, disocclusionThresholds.x, planeDist2x2 );

    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gScreenSize );
    float footprintAvg = STL::Filtering::ApplyBilinearFilter( planeDist2x2.x, planeDist2x2.y, planeDist2x2.z, planeDist2x2.w, bilinearFilterAtPrevPos );
    float fmin = min( bilinearFilterAtPrevPos.weights.x, bilinearFilterAtPrevPos.weights.y ) + 0.01;
    float fmax = max( bilinearFilterAtPrevPos.weights.x, bilinearFilterAtPrevPos.weights.y ) + 0.01;
    footprintAvg = lerp( footprintAvg, 1.0, STL::Math::LinearStep( 0.05, 0.5, fmin / fmax ) );

    specPrevAccumSpeeds *= footprintAvg;

    // Ignore backfacing history
    float4 cosa;
    cosa.x = dot( N, prevNormal00.xyz );
    cosa.y = dot( N, prevNormal10.xyz );
    cosa.z = dot( N, prevNormal01.xyz );
    cosa.w = dot( N, prevNormal11.xyz );

    float4 frontFacing = STL::Math::LinearStep( disocclusionThresholds.y, 0.001, cosa );
    occlusion0.w *= frontFacing.x;
    occlusion1.z *= frontFacing.y;
    occlusion2.y *= frontFacing.z;
    occlusion3.x *= frontFacing.w;

    float4 diffOcclusion2x2 = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );

    // Modify specular occlusion to avoid averaging of specular for different roughness
    float4 prevRoughnessWeight0 = GetRoughnessWeight( roughnessParams, prevRoughness0 );
    float4 prevRoughnessWeight1 = GetRoughnessWeight( roughnessParams, prevRoughness1 );
    float4 prevRoughnessWeight2 = GetRoughnessWeight( roughnessParams, prevRoughness2 );
    float4 prevRoughnessWeight3 = GetRoughnessWeight( roughnessParams, prevRoughness3 );
    occlusion0 *= STL::Math::LinearStep( 0.1, 0.9, prevRoughnessWeight0 );
    occlusion1 *= STL::Math::LinearStep( 0.1, 0.9, prevRoughnessWeight1 );
    occlusion2 *= STL::Math::LinearStep( 0.1, 0.9, prevRoughnessWeight2 );
    occlusion3 *= STL::Math::LinearStep( 0.1, 0.9, prevRoughnessWeight3 );

    // Sample specular history ( surface motion )
    // Averaging of values with different compression can be dangerous, especially in case of CatRom with negative lobes
    float2 catmullRomFilterAtPrevPosOrigin = ( catmullRomFilterAtPrevPos.origin + 0.5 ) * gInvScreenSize;
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

    float4 specOcclusion2x2 = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );
    float4 specWeights = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, specOcclusion2x2 );
    float4 specHistorySurface = STL::Filtering::ApplyBilinearCustomWeights( s11, s21, s12, s22, specWeights );
    float4 specHistorySurfaceCatRom = STL::Filtering::ApplyCatmullRomFilterNoCorners( catmullRomFilterAtPrevPos, s10, s20, s01, s11, s21, s31, s02, s12, s22, s32, s13, s23 );
    specHistorySurface = MixLinearAndCatmullRom( specHistorySurface, specHistorySurfaceCatRom, occlusion0, occlusion1, occlusion2, occlusion3 );

    // Accumulation speeds
    float specAccumSpeed;
    float specAccumSpeedFade = GetAccumSpeed( specPrevAccumSpeeds, specWeights, gSpecMaxAccumulatedFrameNum, gSpecNoisinessBlurrinessBalance, roughnessModified, specAccumSpeed );

    // Noisy signal with reconstruction (if needed)
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    bool specHasData = gSpecCheckerboard == 2 || checkerboard == gSpecCheckerboard;
    if( !specHasData )
    {
        float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specAccumSpeed, parallax, roughnessModified );
        float historyWeight = gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

        float4 specMin = specM1 - specSigma * temporalAccumulationParams.y;
        float4 specMax = specM1 + specSigma * temporalAccumulationParams.y;
        float4 specHistorySurfaceClamped = clamp( specHistorySurface, specMin, specMax );

        spec = lerp( spec, specHistorySurfaceClamped, historyWeight );
    }

    // Current specular signal ( surface motion )
    float4 currentSurface;

    float accumSpeedSurface = GetSpecAccumSpeed( specAccumSpeed, roughnessModified, avgNoV, parallax );
    float accumSpeedSurfaceNonLinear = 1.0 / ( specAccumSpeedFade * accumSpeedSurface + 1.0 );
    currentSurface.w = lerp( specHistorySurface.w, spec.w, max( accumSpeedSurfaceNonLinear, HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

    float hitDist = GetHitDist( currentSurface.w, viewZ, gSpecHitDistParams, roughness );
    float parallaxOrig = parallax;
    float hitDistToSurfaceRatio = saturate( hitDist * invDistToPoint );
    parallax *= hitDistToSurfaceRatio;

    accumSpeedSurface = GetSpecAccumSpeed( specAccumSpeed, roughnessModified, avgNoV, parallax );
    accumSpeedSurfaceNonLinear = 1.0 / ( specAccumSpeedFade * accumSpeedSurface + 1.0 );
    currentSurface.xyz = lerp( specHistorySurface.xyz, spec.xyz, accumSpeedSurfaceNonLinear );

    // Sample specular history ( virtual motion )
    float3 Xvirtual = GetXvirtual( X, Xprev, V, avgNoV, roughnessModified, hitDist );
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );

    float4 specHistoryVirtual = gIn_History_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev, 0 );

    STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gScreenSize );
    float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackGreenVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, gatherUvVirtualPrev ).wzxy;

    float4 prevNormalAndRoughnessVirtual00 = UnpackNormalRoughness( prevPackGreenVirtual.x );
    float4 prevNormalAndRoughnessVirtual10 = UnpackNormalRoughness( prevPackGreenVirtual.y );
    float4 prevNormalAndRoughnessVirtual01 = UnpackNormalRoughness( prevPackGreenVirtual.z );
    float4 prevNormalAndRoughnessVirtual11 = UnpackNormalRoughness( prevPackGreenVirtual.w );

    // TODO: for IQ it's better to do all computation for each sample and then average with custom weights
    float4 prevNormalAndRoughnessVirtual = STL::Filtering::ApplyBilinearFilter( prevNormalAndRoughnessVirtual00, prevNormalAndRoughnessVirtual10, prevNormalAndRoughnessVirtual01, prevNormalAndRoughnessVirtual11, bilinearFilterAtPrevVirtualPos );
    prevNormalAndRoughnessVirtual.xyz = normalize( prevNormalAndRoughnessVirtual.xyz );

    // Disocclusion for virtual motion - normal
    float fresnelFactor = STL::BRDF::Pow5( avgNoV );
    normalParams = GetNormalWeightParams( roughnessModified, 0.0, lerp( 0.25, 1.0, fresnelFactor ) );
    float occlusionVirtual = GetNormalWeight( normalParams, N, prevNormalAndRoughnessVirtual.xyz );
    occlusionVirtual = lerp( occlusionVirtual, 1.0, saturate( fresnelFactor * parallax ) );

    // Disocclusion for virtual motion - roughness
    float virtualRoughnessWeight = GetRoughnessWeight( roughnessParams, prevNormalAndRoughnessVirtual.w );
    occlusionVirtual *= lerp( virtualRoughnessWeight, 1.0, saturate( parallax ) );

    // Disocclusion for virtual motion - virtual motion correctness
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection( N, V, roughness, SPEC_DOMINANT_DIRECTION );
    float3 R = reflect( -D.xyz, N );
    Xvirtual = X - R * hitDist * D.w;
    float2 uvVirtualExpected = STL::Geometry::GetScreenUv( gWorldToClip, Xvirtual );

    D = STL::ImportanceSampling::GetSpecularDominantDirection( prevNormalAndRoughnessVirtual.xyz, V, prevNormalAndRoughnessVirtual.w, SPEC_DOMINANT_DIRECTION );
    R = reflect( -D.xyz, prevNormalAndRoughnessVirtual.xyz );
    Xvirtual = X - R * GetHitDist( specHistoryVirtual.w, viewZ, gSpecHitDistParams, prevNormalAndRoughnessVirtual.w ) * D.w;
    float2 uvVirtualAtSample = STL::Geometry::GetScreenUv( gWorldToClip, Xvirtual );

    float thresholdMax = GetParallaxInPixels( parallaxOrig );
    float thresholdMin = thresholdMax * 0.05;
    float parallaxVirtual = length( ( uvVirtualAtSample - uvVirtualExpected ) * gScreenSize );
    occlusionVirtual *= STL::Math::LinearStep( thresholdMax + 0.00001, thresholdMin, parallaxVirtual );

    // Amount of virtual motion
    float isInScreenVirtual = float( all( saturate( pixelUvVirtualPrev ) == pixelUvVirtualPrev ) );
    float specDominantFactor = STL::ImportanceSampling::GetSpecularDominantFactor( avgNoV, roughnessModified, SPEC_DOMINANT_DIRECTION );
    float virtualHistoryAmount = isInScreenVirtual;
    virtualHistoryAmount *= specDominantFactor;
    virtualHistoryAmount *= occlusionVirtual;
    virtualHistoryAmount *= float( !IsReference() ); // no virtual motion in reference mode (it's by design, useful for integration debugging)

    // Hit distance based disocclusion for virtual motion
    float hitDistDelta = abs( specHistoryVirtual.w - currentSurface.w ) - specSigma.w * 0.5; // TODO: was 0.1
    float hitDistMin = max( specHistoryVirtual.w, currentSurface.w );
    hitDistDelta = GetHitDist( hitDistDelta, viewZ, gSpecHitDistParams, roughness );
    hitDistMin = GetHitDist( hitDistMin, viewZ, gSpecHitDistParams, roughness );
    hitDistDelta *= STL::Math::PositiveRcp( hitDistMin + abs( viewZ ) );

    thresholdMin = 0.02 * STL::Math::LinearStep( 0.2, 0.01, parallax ); // TODO: thresholdMin needs to be set to 0, but it requires very clean hit distances
    thresholdMax = lerp( 0.01, 0.25, roughnessModified * roughnessModified ) + thresholdMin;
    float virtualHistoryConfidence = STL::Math::LinearStep( thresholdMax, thresholdMin, hitDistDelta );
    virtualHistoryConfidence *= STL::Math::SmoothStep( 0.0, 0.5, virtualRoughnessWeight );

    // Adjust virtual motion amount if surface history is confident
    // TODO: visually it's better to fallback to potentially laggy surface motion then see wrong virtual motion. How to make virtual motion more correct?
    float surfaceHistoryIncorrectness = saturate( parallax );
    surfaceHistoryIncorrectness = lerp( surfaceHistoryIncorrectness, 1.0, fresnelFactor );
    surfaceHistoryIncorrectness = lerp( hitDistToSurfaceRatio, surfaceHistoryIncorrectness, STL::Math::LinearStep( 0.1, 0.3, roughness ) );
    virtualHistoryAmount *= lerp( virtualHistoryConfidence, 1.0, surfaceHistoryIncorrectness );

    // Adjust accumulation speed for virtual motion if confidence is low // TODO: is it needed?
    /*
    float accumSpeedScale = lerp( 1.0, virtualHistoryConfidence, virtualHistoryAmount );
    accumSpeedScale = lerp( accumSpeedScale, 1.0, roughness );
    accumSpeedScale = lerp( accumSpeedScale, 1.0, 1.0 / ( 1.0 + specAccumSpeed ) );

    float specMinAccumSpeed = min( specAccumSpeed, ( MIP_NUM - 1 ) * STL::Math::Sqrt01( roughness ) );
    specAccumSpeed = specMinAccumSpeed + ( specAccumSpeed - specMinAccumSpeed ) * accumSpeedScale;
    */

    // Current specular signal ( virtual motion )
    float accumSpeedVirtual = GetSpecAccumSpeed( specAccumSpeed, roughnessModified, avgNoV, 0.0 ); // parallax = 0 cancels NoV too
    float accumSpeedVirtualNonLinear = 1.0 / ( specAccumSpeedFade * accumSpeedVirtual + 1.0 );

    float sigmaScale = 1.0 + TS_SIGMA_AMPLITUDE * STL::Math::SmoothStep( 0.0, 0.5, roughnessModified );
    float4 specMin = specM1 - specSigma * sigmaScale;
    float4 specMax = specM1 + specSigma * sigmaScale;
    float4 specHistoryVirtualClamped = clamp( specHistoryVirtual, specMin, specMax );

    float virtualUnclampedAmount = lerp( virtualHistoryConfidence * SPEC_FORCED_VIRTUAL_CLAMPING, 1.0, roughnessModified * roughnessModified );
    specHistoryVirtual = lerp( specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount );

    float4 currentVirtual;
    currentVirtual.xyz = lerp( specHistoryVirtual.xyz, spec.xyz, accumSpeedVirtualNonLinear );
    currentVirtual.w = lerp( specHistoryVirtual.w, spec.w, max( accumSpeedVirtualNonLinear, HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

    // Final composition
    float4 specResult;
    specResult.xyz = lerp( currentSurface.xyz, currentVirtual.xyz, virtualHistoryAmount );
    specResult.w = lerp( currentSurface.w, currentVirtual.w, virtualHistoryAmount * hitDistToSurfaceRatio );

    float specAccumSpeedNonLinear = lerp( accumSpeedSurfaceNonLinear, accumSpeedVirtualNonLinear, virtualHistoryAmount );
    specAccumSpeed = 1.0 / specAccumSpeedNonLinear - 1.0;

    // Get rid of possible negative values
    specResult.xyz = _NRD_YCoCgToLinear( specResult.xyz );
    specResult.w = max( specResult.w, 0.0 );
    specResult.xyz = _NRD_LinearToYCoCg( specResult.xyz );

    // Output
    gOut_InternalData[ pixelPos ] = PackSpecInternalData( specAccumSpeed, virtualHistoryAmount );
    gOut_Spec[ pixelPos ] = specResult;
}
