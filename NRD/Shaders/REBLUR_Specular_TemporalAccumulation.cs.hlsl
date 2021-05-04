/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "NRD.hlsl"
#include "STL.hlsl"
#include "REBLUR_Config.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    REBLUR_SPEC_SHARED_CB_DATA;

    float4x4 gWorldToViewPrev;
    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float4x4 gWorldToClip;
    float4 gCameraDelta;
    float2 gMotionVectorScale;
    float gCheckerboardResolveAccumSpeed;
    float gDisocclusionThreshold;
    float gSpecMaxFastAccumulatedFrameNum;
    uint gSpecCheckerboard;
};

#define NRD_CTA_8X8
#include "NRD_Common.hlsl"

#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 2, 0 );
NRI_RESOURCE( Texture2D<uint2>, gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_History_Spec, t, 4, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_HistoryFast_Spec, t, 5, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 6, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<unorm float3>, gOut_InternalData, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float2>, gOut_Error, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 2, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Fast_Spec, u, 3, 0 );

groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    uint2 globalIdUser = gRectOrigin + globalId;

    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
    s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Spec[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );

    [branch]
    if( viewZ > gInf )
        return;

    // Center position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = -X * invDistToPoint;

    // Normal and roughness
    int2 smemPos = threadId + BORDER;
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Calculate distribution of normals and signal variance
    float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
    float4 specM1 = spec;
    float4 specM2 = spec * spec;
    float normalParams = GetNormalWeightParamsRoughEstimate( roughness );
    float2 roughnessParams = GetRoughnessWeightParams( roughness );

    float3 Nflat = N;

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

            Nflat += n.xyz;

            float4 s = s_Spec[ pos.y ][ pos.x ];
            specM1 += s;
            specM2 += s * s;
        }
    }

    float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );

    specM1 *= invSum;
    specM2 *= invSum;
    float4 specSigma = GetStdDev( specM1, specM2 );

    float3 Navg = Nflat * invSum;
    float edge = DetectEdge( Navg );
    float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );

    Nflat = normalize( Nflat );

    // Compute previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser ] * gMotionVectorScale.xyy; // TODO: use nearest MV
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = IsInScreen2x2( pixelUvPrev, gRectSizePrev );
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );

    // Previous data ( Catmull-Rom )
    STL::Filtering::CatmullRom catmullRomFilterAtPrevPos = STL::Filtering::GetCatmullRomFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float2 catmullRomFilterAtPrevPosGatherOrigin = catmullRomFilterAtPrevPos.origin * gInvScreenSize;
    uint4 prevPackRed0 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
    uint4 prevPackRed1 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 prevPackRed2 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 prevPackRed3 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;
    uint4 prevPackGreen0 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
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

    float3 prevNflat = prevNormal00 + prevNormal10 + prevNormal01 + prevNormal11;
    prevNflat = normalize( prevNflat );

    // Plane distance based disocclusion for surface motion
    float parallax = ComputeParallax( X, Xprev, gCameraDelta.xyz );
    float2 disocclusionThresholds = GetDisocclusionThresholds( gDisocclusionThreshold, gJitterDelta, viewZ, parallax, Nflat, X, invDistToPoint );
    disocclusionThresholds.x = lerp( -1.0, disocclusionThresholds.x, isInScreen ); // out-of-screen = occlusion
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev1 = abs( dot( Xprev, Nflat ) );
    float NoXprev2 = abs( dot( Xprev, prevNflat ) );
    float NoXprev = max( NoXprev1, NoXprev2 ) * invDistToPoint; // normalize here to save ALU
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) );
    float4 planeDist0 = abs( NoVprev * abs( prevViewZ0 ) - NoXprev );
    float4 planeDist1 = abs( NoVprev * abs( prevViewZ1 ) - NoXprev );
    float4 planeDist2 = abs( NoVprev * abs( prevViewZ2 ) - NoXprev );
    float4 planeDist3 = abs( NoVprev * abs( prevViewZ3 ) - NoXprev );
    float4 occlusion0 = step( planeDist0, disocclusionThresholds.x );
    float4 occlusion1 = step( planeDist1, disocclusionThresholds.x );
    float4 occlusion2 = step( planeDist2, disocclusionThresholds.x );
    float4 occlusion3 = step( planeDist3, disocclusionThresholds.x );

    // Avoid "got stuck in history" effect under slow motion when only 1 sample is valid from 2x2 footprint and there is a big difference between
    // foreground and background surfaces. Instead of final scalar accum speed scaling we can apply it to accum speeds from the previous frame
    float4 planeDist2x2 = float4( planeDist0.w, planeDist1.z, planeDist2.y, planeDist3.x );
    planeDist2x2 = STL::Math::LinearStep( 0.2, disocclusionThresholds.x, planeDist2x2 );

    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gRectSizePrev );
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
    float specMaxAccumSpeed = GetAccumSpeed( specPrevAccumSpeeds, specWeights, gSpecMaxAccumulatedFrameNum );

    // Noisy signal with reconstruction (if needed)
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    bool specHasData = gSpecCheckerboard == 2 || checkerboard == gSpecCheckerboard;
    if( !specHasData )
    {
        float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specMaxAccumSpeed, parallax, roughnessModified );
        float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;
        float4 specHistorySurfaceClamped = STL::Color::Clamp( specM1, specSigma * temporalAccumulationParams.y, specHistorySurface, REBLUR_USE_COLOR_CLAMPING_AABB ); // TODO: needed?

        spec.xyz = lerp( specHistorySurfaceClamped.xyz, spec.xyz, historyWeight );
        spec.w = lerp( specHistorySurfaceClamped.w, spec.w, max( historyWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );
    }

    // Current specular signal ( surface motion )
    float NoV = abs( dot( N, V ) );
    float accumSpeedSurface = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, parallax );
    float accumSpeedSurfaceNonLinear = 1.0 / ( accumSpeedSurface + 1.0 );

    float hitDistNorm = lerp( specHistorySurface.w, spec.w, max( accumSpeedSurfaceNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );
    float hitDist = GetHitDist( hitDistNorm, viewZ, gSpecHitDistParams, roughness );

    float parallaxOrig = parallax;
    float hitDistToSurfaceRatio = saturate( hitDist * invDistToPoint );
    parallax *= hitDistToSurfaceRatio;

    accumSpeedSurface = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, parallax ); // TODO: add on option to use color clamping if parallax is high (instead of accelerating the speed of accumulation)
    accumSpeedSurfaceNonLinear = 1.0 / ( accumSpeedSurface + 1.0 );

    float4 currentSurface;
    currentSurface.xyz = lerp( specHistorySurface.xyz, spec.xyz, accumSpeedSurfaceNonLinear );
    currentSurface.w = hitDistNorm;

    // Sample specular history ( virtual motion )
    float3 Xvirtual = GetXvirtual( X, Xprev, V, NoV, roughness, hitDist );
    float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual );
    float4 specHistoryVirtual = gIn_History_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev * gRectSizePrev * gInvScreenSize, 0 );

    STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gRectSizePrev );
    float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;
    uint4 prevPackRedVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, gatherUvVirtualPrev ).wzxy;
    uint4 prevPackGreenVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, gatherUvVirtualPrev ).wzxy;

    // Amount of virtual motion - out of screen & reference
    float virtualHistoryAmount = IsInScreen2x2( pixelUvVirtualPrev, gRectSizePrev );
    virtualHistoryAmount *= 1.0 - gReference; // no virtual motion in the reference mode (it's by design, useful for integration debugging)

    // Amount of virtual motion - surface
    float4 prevViewZsVirtual = UnpackViewZ( prevPackRedVirtual );
    float prevViewZVirtual = STL::Filtering::ApplyBilinearFilter( prevViewZsVirtual.x, prevViewZsVirtual.y, prevViewZsVirtual.z, prevViewZsVirtual.w, bilinearFilterAtPrevVirtualPos );

    float virtualZocclusion = abs( prevViewZVirtual - Xvprev.z ) / ( max( prevViewZVirtual, Xvprev.z ) + 0.001 );
    virtualHistoryAmount *= STL::Math::LinearStep( 0.1, 0.01, virtualZocclusion );

    // Amount of virtual motion - normal
    float fresnelFactor = STL::BRDF::Pow5( NoV );
    float virtualLobeScale = lerp( 0.5, 1.0, fresnelFactor );
    virtualLobeScale = lerp( virtualLobeScale, 0.05, edge );
    float specNormalParams = GetNormalWeightParams( virtualLobeScale, 0.0, 0.0, roughnessModified );

    float4 prevNormalAndRoughnessVirtual00 = UnpackNormalRoughness( prevPackGreenVirtual.x );
    float4 prevNormalAndRoughnessVirtual10 = UnpackNormalRoughness( prevPackGreenVirtual.y );
    float4 prevNormalAndRoughnessVirtual01 = UnpackNormalRoughness( prevPackGreenVirtual.z );
    float4 prevNormalAndRoughnessVirtual11 = UnpackNormalRoughness( prevPackGreenVirtual.w );

    float4 normalWeights;
    normalWeights.x = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual00.xyz );
    normalWeights.y = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual10.xyz );
    normalWeights.z = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual01.xyz );
    normalWeights.w = GetNormalWeight( specNormalParams, N, prevNormalAndRoughnessVirtual11.xyz );

    float4 prevNormalAndRoughnessVirtual = STL::Filtering::ApplyBilinearFilter( prevNormalAndRoughnessVirtual00, prevNormalAndRoughnessVirtual10, prevNormalAndRoughnessVirtual01, prevNormalAndRoughnessVirtual11, bilinearFilterAtPrevVirtualPos );
    prevNormalAndRoughnessVirtual.xyz = normalize( prevNormalAndRoughnessVirtual.xyz );

    float2 tempMax = max( normalWeights.xy, normalWeights.zw );
    float2 tempMin = min( normalWeights.xy, normalWeights.zw );
    float normalWeightMax = max( tempMax.x, tempMax.y );
    float normalWeightMin = min( tempMin.x, tempMin.y );
    float virtualNormalWeight = lerp( normalWeightMin, normalWeightMax, fresnelFactor * ( 1.0 - edge ) );

    float renorm = lerp( 0.9, 1.0, STL::Math::LinearStep( 0.0, 0.17, roughnessModified ) );
    virtualNormalWeight = saturate( virtualNormalWeight / renorm ); // mitigate imprecision problems introduced by normals encoded with different precision

    // TODO: if weight != 0 we can use something like...
    //virtualNormalWeight = lerp( virtualNormalWeight, 1.0, saturate( fresnelFactor * parallax ) );

    virtualHistoryAmount *= virtualNormalWeight;

    // Amount of virtual motion - dominant factor
    float4 D = STL::ImportanceSampling::GetSpecularDominantDirection( N, V, roughness, REBLUR_SPEC_DOMINANT_DIRECTION );
    virtualHistoryAmount *= D.w;

    // Amount of virtual motion - virtual motion correctness
    float3 R = reflect( -D.xyz, N );
    Xvirtual = X - R * hitDist * D.w;
    float2 uvVirtualExpected = STL::Geometry::GetScreenUv( gWorldToClip, Xvirtual );

    float4 Dvirtual = STL::ImportanceSampling::GetSpecularDominantDirection( prevNormalAndRoughnessVirtual.xyz, V, prevNormalAndRoughnessVirtual.w, REBLUR_SPEC_DOMINANT_DIRECTION );
    float3 Rvirtual = reflect( -Dvirtual.xyz, prevNormalAndRoughnessVirtual.xyz );
    float hitDistVirtual = GetHitDist( specHistoryVirtual.w, prevViewZVirtual, gSpecHitDistParams, prevNormalAndRoughnessVirtual.w );
    Xvirtual = X - Rvirtual * hitDistVirtual * Dvirtual.w;
    float2 uvVirtualAtSample = STL::Geometry::GetScreenUv( gWorldToClip, Xvirtual );

    float thresholdMax = GetParallaxInPixels( parallaxOrig );
    float thresholdMin = thresholdMax * 0.05;
    float parallaxVirtual = length( ( uvVirtualAtSample - uvVirtualExpected ) * gRectSize );
    virtualHistoryAmount *= STL::Math::LinearStep( thresholdMax + 0.001, thresholdMin, parallaxVirtual );

    // Virtual history confidence - normal
    float virtualHistoryConfidence = virtualNormalWeight;

    // Virtual history confidence - roughness
    virtualHistoryConfidence *= GetRoughnessWeight( roughnessParams, prevNormalAndRoughnessVirtual.w );

    // Virtual history confidence - hit distance
    float hitDistDelta = abs( hitDistVirtual - hitDist ); // no sigma substraction here - it's too noisy
    float hitDistMax = max( hitDistVirtual, hitDist );
    hitDistDelta *= STL::Math::PositiveRcp( hitDistMax + viewZ );

    thresholdMax = 0.25 * roughness * roughness + 0.01;
    virtualHistoryConfidence *= STL::Math::LinearStep( thresholdMax, 0.005, hitDistDelta * parallaxOrig );

    // Clamp virtual history
    float sigmaScale = 3.0 + REBLUR_TS_SIGMA_AMPLITUDE * STL::Math::SmoothStep( 0.0, 0.5, roughness );
    float4 specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual, REBLUR_USE_COLOR_CLAMPING_AABB );

    float virtualUnclampedAmount = lerp( virtualHistoryConfidence, 1.0, roughness * roughness );
    specHistoryVirtual = lerp( specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount );

    // Current specular signal ( virtual motion )
    float accumSpeedVirtual = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, 0.0 ); // parallax = 0 cancels NoV too

    float minAccumSpeed = min( accumSpeedVirtual, ( REBLUR_FRAME_NUM_WITH_HISTORY_FIX * 1.5 - 1.0 ) * STL::Math::Sqrt01( roughnessModified ) );
    accumSpeedVirtual = InterpolateAccumSpeeds( minAccumSpeed, accumSpeedVirtual, STL::Math::Sqrt01( virtualHistoryConfidence ) );

    float accumSpeedVirtualNonLinear = 1.0 / ( accumSpeedVirtual + 1.0 );

    float4 currentVirtual;
    currentVirtual.xyz = lerp( specHistoryVirtual.xyz, spec.xyz, accumSpeedVirtualNonLinear );
    currentVirtual.w = lerp( specHistoryVirtual.w, spec.w, max( accumSpeedVirtualNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

    // Final composition
    float4 specResult;
    specResult.xyz = lerp( currentSurface.xyz, currentVirtual.xyz, virtualHistoryAmount );
    specResult.w = lerp( currentSurface.w, currentVirtual.w, virtualHistoryAmount * hitDistToSurfaceRatio );

    float specAccumSpeed = InterpolateAccumSpeeds( accumSpeedSurface, accumSpeedVirtual, virtualHistoryAmount );

    // Output
    #if( REBLUR_DEBUG == REBLUR_SHOW_VIRTUAL_HISTORY_CONFIDENCE )
        virtualHistoryAmount = virtualHistoryConfidence;
    #endif

    gOut_InternalData[ pixelPos ] = PackSpecInternalData( specAccumSpeed, edge, virtualHistoryAmount );
    gOut_Spec[ pixelPos ] = specResult;

    // Error
    float4 specHistory;
    specHistory.xyz = lerp( specHistorySurface.xyz, specHistoryVirtual.xyz, virtualHistoryAmount );
    specHistory.w = lerp( specHistorySurface.w, specHistoryVirtual.w, virtualHistoryAmount * hitDistToSurfaceRatio );

    float bestAccumulatedFrameNum = GetSpecAccumulatedFrameNum( roughnessModified, 1.0 );
    float boost = saturate( 1.0 - ( specAccumSpeed + 0.1 ) / ( min( bestAccumulatedFrameNum, gSpecMaxAccumulatedFrameNum ) + 0.1 ) );
    boost *= saturate( parallaxOrig / 0.25 );

    float specError = GetColorErrorForAdaptiveRadiusScale( specResult, specHistory, 1.0 / ( 1.0 + specAccumSpeed ), lerp( 1.0, roughness, saturate( parallax ) ) );

    gOut_Error[ pixelPos ] = float2( specError, boost );

    // Fast history
    #if( REBLUR_USE_FAST_HISTORY == 1 )
        float4 s11f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 1 ) );
        float4 s21f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 1 ) );
        float4 s12f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 1, 2 ) );
        float4 s22f = gIn_HistoryFast_Spec.SampleLevel( gNearestClamp, catmullRomFilterAtPrevPosOrigin, 0, int2( 2, 2 ) );
        float4 specHistorySurfaceFast = STL::Filtering::ApplyBilinearCustomWeights( s11f, s21f, s12f, s22f, specWeights );

        float4 specHistoryVirtualFast = gIn_HistoryFast_Spec.SampleLevel( gLinearClamp, pixelUvVirtualPrev * gRectSizePrev * gInvScreenSize, 0 );
        float4 specHistoryVirtualClampedFast = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtualFast, REBLUR_USE_COLOR_CLAMPING_AABB );
        specHistoryVirtualFast = lerp( specHistoryVirtualClampedFast, specHistoryVirtualFast, virtualUnclampedAmount );

        // History fix (previous state)
        float specMinAccumSpeedFast = ( REBLUR_FRAME_NUM_WITH_HISTORY_FIX - 1 ) * STL::Math::Sqrt01( roughness );
        specHistorySurfaceFast = lerp( specHistorySurface, specHistorySurfaceFast, specAccumSpeed > specMinAccumSpeedFast );
        specHistoryVirtualFast = lerp( specHistoryVirtual, specHistoryVirtualFast, specAccumSpeed > specMinAccumSpeedFast );

        float maxFastAccumSpeedRoughnessAdjusted = gSpecMaxFastAccumulatedFrameNum * STL::Math::Sqrt01( roughnessModified );
        float accumSpeedSurfaceNonLinearFast = 1.0 / ( min( accumSpeedSurface, maxFastAccumSpeedRoughnessAdjusted ) + 1.0 );
        float accumSpeedVirtualNonLinearFast = 1.0 / ( min( accumSpeedVirtual, maxFastAccumSpeedRoughnessAdjusted ) + 1.0 );

        float4 currentSurfaceFast;
        currentSurfaceFast.xyz = lerp( specHistorySurfaceFast.xyz, spec.xyz, accumSpeedSurfaceNonLinearFast );
        currentSurfaceFast.w = lerp( specHistorySurfaceFast.w, spec.w, max( accumSpeedSurfaceNonLinearFast, 2.0 * REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

        float4 currentVirtualFast;
        currentVirtualFast.xyz = lerp( specHistoryVirtualFast.xyz, spec.xyz, accumSpeedVirtualNonLinearFast );
        currentVirtualFast.w = lerp( specHistoryVirtualFast.w, spec.w, max( accumSpeedVirtualNonLinearFast, 2.0 * REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );

        float4 specResultFast;
        specResultFast.xyz = lerp( currentSurfaceFast.xyz, currentVirtualFast.xyz, virtualHistoryAmount );
        specResultFast.w = lerp( currentSurfaceFast.w, currentVirtualFast.w, virtualHistoryAmount * hitDistToSurfaceRatio );

        gOut_Fast_Spec[ pixelPos ] = specResultFast;
    #endif
}
