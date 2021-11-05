/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"
#include "REBLUR_DiffuseSpecularOcclusion_TemporalAccumulation.resources.hlsli"

NRD_DECLARE_CONSTANTS

#if( defined REBLUR_SPECULAR )
    #define NRD_CTA_8X8
#endif

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS

#include "REBLUR_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ gRectOrigin + globalId ] );

    #if( defined REBLUR_SPECULAR )
        globalId.x >>= gSpecCheckerboard != 2 ? 1 : 0;

        s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Spec[ gRectOrigin + globalId ];
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    float viewZ = abs( gIn_ViewZ[ pixelPosUser ] );
    float scaledViewZ = min( viewZ * NRD_FP16_VIEWZ_SCALE, NRD_FP16_MAX );

    [branch]
    if( viewZ > gInf )
    {
        #if( defined REBLUR_DIFFUSE )
            gOut_Diff[ pixelPos ] = float2( 0, scaledViewZ );
        #endif

        #if( defined REBLUR_SPECULAR )
            gOut_Spec[ pixelPos ] = float2( 0, scaledViewZ );
        #endif

        return;
    }

    // Normal and roughness
    int2 smemPos = threadId + BORDER;
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Calculate distribution of normals
    #if( defined REBLUR_SPECULAR )
        float spec = s_Spec[ smemPos.y ][ smemPos.x ];
        float specM1 = spec;
        float specM2 = spec * spec;
    #endif

    float3 Nflat = N;
    float curvature = 0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadId + int2( dx, dy );
            float3 n = s_Normal_Roughness[ pos.y ][ pos.x ].xyz;

            Nflat += n;

            // https://computergraphics.stackexchange.com/questions/1718/what-is-the-simplest-way-to-compute-principal-curvature-for-a-mesh-triangle
            // TODO: ideally, curvature must be computed along the direction of camera motion ( averaging doesn't work well for surfaces with regularly oriented normals )
            curvature += length( n - N ) * rsqrt( STL::Math::LengthSquared( float2( dx, dy ) - BORDER ) ); // TODO: theoretically sign of curvature can be useful

            // TODO: using weights leads to instabilities on thin objects
            #if( defined REBLUR_SPECULAR )
                float s = s_Spec[ pos.y ][ pos.x ];
                specM1 += s;
                specM2 += s * s;
            #endif
        }
    }

    float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );
    float3 Navg = Nflat * invSum;

    curvature /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) - 1;

    // Only needed to mitigate banding
    curvature = STL::Math::LinearStep( NRD_ENCODING_ERRORS.y, 1.0, curvature );

    #if( defined REBLUR_SPECULAR )
        specM1 *= invSum;
        specM2 *= invSum;
        float specSigma = GetStdDev( specM1, specM2 );

        float roughnessModified = STL::Filtering::GetModifiedRoughnessFromNormalVariance( roughness, Navg );
    #endif

    Nflat = normalize( Nflat );

    // Current position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );

    // Previous position for surface motion
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser ] * gMotionVectorScale.xyy; // TODO: use nearest MV
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    float isInScreen = IsInScreen2x2( pixelUvPrev, gRectSizePrev );
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );

    // Previous data ( 4x4, surface motion )
    STL::Filtering::CatmullRom catmullRomFilterAtPrevPos = STL::Filtering::GetCatmullRomFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float2 catmullRomFilterAtPrevPosGatherOrigin = catmullRomFilterAtPrevPos.origin * gInvScreenSize;
    uint4 prevPackRed0 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
    uint4 prevPackRed1 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
    uint4 prevPackRed2 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
    uint4 prevPackRed3 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;

    float4 prevViewZ0 = UnpackViewZ( prevPackRed0 );
    float4 prevViewZ1 = UnpackViewZ( prevPackRed1 );
    float4 prevViewZ2 = UnpackViewZ( prevPackRed2 );
    float4 prevViewZ3 = UnpackViewZ( prevPackRed3 );

    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gRectSizePrev );
    #if( defined REBLUR_SPECULAR )
        uint4 prevPackGreen0 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 1 ) ).wzxy;
        uint4 prevPackGreen1 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 1 ) ).wzxy;
        uint4 prevPackGreen2 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 1, 3 ) ).wzxy;
        uint4 prevPackGreen3 = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, catmullRomFilterAtPrevPosGatherOrigin, float2( 3, 3 ) ).wzxy;

        float4 specPrevAccumSpeeds;
        float3 prevNormal00 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen0.w, specPrevAccumSpeeds.x ).xyz;
        float3 prevNormal10 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen1.z, specPrevAccumSpeeds.y ).xyz;
        float3 prevNormal01 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen2.y, specPrevAccumSpeeds.z ).xyz;
        float3 prevNormal11 = UnpackNormalRoughnessSpecAccumSpeed( prevPackGreen3.x, specPrevAccumSpeeds.w ).xyz;
    #else
        float2 bilinearFilterAtPrevPosGatherOrigin = ( bilinearFilterAtPrevPos.origin + 1.0 ) * gInvScreenSize;
        uint4 prevPackGreen = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, bilinearFilterAtPrevPosGatherOrigin ).wzxy;
        float3 prevNormal00 = UnpackNormalRoughness( prevPackGreen.x ).xyz;
        float3 prevNormal10 = UnpackNormalRoughness( prevPackGreen.y ).xyz;
        float3 prevNormal01 = UnpackNormalRoughness( prevPackGreen.z ).xyz;
        float3 prevNormal11 = UnpackNormalRoughness( prevPackGreen.w ).xyz;
    #endif

    #if( defined REBLUR_DIFFUSE )
        float4 diffPrevAccumSpeeds = UnpackDiffAccumSpeed( uint4( prevPackRed0.w, prevPackRed1.z, prevPackRed2.y, prevPackRed3.x ) );
    #endif

    float3 prevNflat = prevNormal00 + prevNormal10 + prevNormal01 + prevNormal11;
    prevNflat = normalize( prevNflat );

    // Plane distance based disocclusion for surface motion
    float parallax = ComputeParallax( X, Xprev, gCameraDelta.xyz );
    float disocclusionThreshold = GetDisocclusionThreshold( gDisocclusionThreshold, gJitterDelta, viewZ, Nflat, X, invDistToPoint );
    disocclusionThreshold = lerp( -1.0, disocclusionThreshold, isInScreen ); // out-of-screen = occlusion
    float3 Xvprev = STL::Geometry::AffineTransform( gWorldToViewPrev, Xprev );
    float NoXprev1 = abs( dot( Xprev, Nflat ) );
    float NoXprev2 = abs( dot( Xprev, prevNflat ) );
    float NoXprev = max( NoXprev1, NoXprev2 ) * invDistToPoint; // normalize here to save ALU
    float NoVprev = NoXprev * STL::Math::PositiveRcp( abs( Xvprev.z ) );
    float4 planeDist0 = abs( NoVprev * abs( prevViewZ0 ) - NoXprev );
    float4 planeDist1 = abs( NoVprev * abs( prevViewZ1 ) - NoXprev );
    float4 planeDist2 = abs( NoVprev * abs( prevViewZ2 ) - NoXprev );
    float4 planeDist3 = abs( NoVprev * abs( prevViewZ3 ) - NoXprev );
    float4 occlusion0 = step( planeDist0, disocclusionThreshold );
    float4 occlusion1 = step( planeDist1, disocclusionThreshold );
    float4 occlusion2 = step( planeDist2, disocclusionThreshold );
    float4 occlusion3 = step( planeDist3, disocclusionThreshold );

    // Avoid "got stuck in history" effect under slow motion when only 1 sample is valid from 2x2 footprint and there is a big difference between
    // foreground and background surfaces. Instead of final scalar accum speed scaling we can apply it to accum speeds from the previous frame
    float4 planeDist2x2 = float4( planeDist0.w, planeDist1.z, planeDist2.y, planeDist3.x );
    planeDist2x2 = STL::Math::LinearStep( 0.2, disocclusionThreshold, planeDist2x2 );

    float footprintAvg = STL::Filtering::ApplyBilinearFilter( planeDist2x2.x, planeDist2x2.y, planeDist2x2.z, planeDist2x2.w, bilinearFilterAtPrevPos );
    float fmin = min( bilinearFilterAtPrevPos.weights.x, bilinearFilterAtPrevPos.weights.y ) + 0.01;
    float fmax = max( bilinearFilterAtPrevPos.weights.x, bilinearFilterAtPrevPos.weights.y ) + 0.01;
    footprintAvg = lerp( footprintAvg, 1.0, STL::Math::LinearStep( 0.05, 0.5, fmin / fmax ) );

    #if( defined REBLUR_DIFFUSE )
        diffPrevAccumSpeeds *= footprintAvg;
    #endif

    #if( defined REBLUR_SPECULAR )
        specPrevAccumSpeeds *= footprintAvg;
    #endif

    // Ignore backfacing history
    float4 cosa;
    cosa.x = dot( N, prevNormal00 );
    cosa.y = dot( N, prevNormal10 );
    cosa.z = dot( N, prevNormal01 );
    cosa.w = dot( N, prevNormal11 );

    float cosAngleMin = lerp( cos( STL::Math::DegToRad( -170.0 ) ), 0.0, SaturateParallax( parallax ) );
    float4 frontFacing = STL::Math::LinearStep( cosAngleMin, 0.01, cosa );
    occlusion0.w *= frontFacing.x;
    occlusion1.z *= frontFacing.y;
    occlusion2.y *= frontFacing.z;
    occlusion3.x *= frontFacing.w;

    float surfaceOcclusionAvg = step( 15.5, dot( occlusion0 + occlusion1 + occlusion2 + occlusion3, 1.0 ) ) * REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TA;
    float4 surfaceOcclusion2x2 = float4( occlusion0.w, occlusion1.z, occlusion2.y, occlusion3.x );
    float4 surfaceWeightsWithOcclusion = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, surfaceOcclusion2x2 );

    // Sample history ( surface motion )
    #if( defined REBLUR_DIFFUSE )
        float diffMaxAccumSpeed = GetAccumSpeed( diffPrevAccumSpeeds, surfaceWeightsWithOcclusion, gDiffMaxAccumulatedFrameNum );

        #if( defined REBLUR_PROVIDED_CONFIDENCE )
            diffMaxAccumSpeed *= gIn_DiffConfidence[ pixelPosUser ];
        #endif

        float diffHistoryFast;
        float diffHistory = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Diff, gIn_HistoryFast_Diff, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            surfaceWeightsWithOcclusion, surfaceOcclusionAvg == 1.0,
            diffHistoryFast
        );
    #endif

    #if( defined REBLUR_SPECULAR )
        float specMaxAccumSpeed = GetAccumSpeed( specPrevAccumSpeeds, surfaceWeightsWithOcclusion, gSpecMaxAccumulatedFrameNum );

        #if( defined REBLUR_PROVIDED_CONFIDENCE )
            specMaxAccumSpeed *= gIn_SpecConfidence[ pixelPosUser ];
        #endif

        float specHistorySurfaceFast;
        float specHistorySurface = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Spec, gIn_HistoryFast_Spec, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            surfaceWeightsWithOcclusion, surfaceOcclusionAvg == 1.0,
            specHistorySurfaceFast
        );
    #endif

    // Noisy signal with reconstruction (if needed)
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );
    int3 checkerboardPos = pixelPosUser.xyx + int3( -1, 0, 1 );
    float viewZ0 = gIn_ViewZ[ checkerboardPos.xy ];
    float viewZ1 = gIn_ViewZ[ checkerboardPos.zy ];
    float2 neighboorWeight = GetBilateralWeight( float2( viewZ0, viewZ1 ), viewZ );
    neighboorWeight *= STL::Math::PositiveRcp( neighboorWeight.x + neighboorWeight.y );

    #if( defined REBLUR_DIFFUSE )
        bool diffHasData = gDiffCheckerboard == 2 || checkerboard == gDiffCheckerboard;

        uint shift = gDiffCheckerboard != 2 ? 1 : 0;
        float diff = gIn_Diff[ gRectOrigin + uint2( pixelPos.x >> shift, pixelPos.y ) ];
        float d0 = gIn_Diff[ gRectOrigin + uint2( ( pixelPos.x - 1 ) >> shift, pixelPos.y ) ];
        float d1 = gIn_Diff[ gRectOrigin + uint2( ( pixelPos.x + 1 ) >> shift, pixelPos.y ) ];

        if( !diffHasData && gResetHistory == 0 )
        {
            diff *= saturate( 1.0 - neighboorWeight.x - neighboorWeight.y );
            diff += d0 * neighboorWeight.x + d1 * neighboorWeight.y;

            float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffMaxAccumSpeed, parallax );
            float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;

            diff = lerp( diffHistory, diff, max( historyWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );
        }
    #endif

    #if( defined REBLUR_SPECULAR )
        bool specHasData = gSpecCheckerboard == 2 || checkerboard == gSpecCheckerboard;

        float s0 = s_Spec[ smemPos.y ][ smemPos.x - 1 ];
        float s1 = s_Spec[ smemPos.y ][ smemPos.x + 1 ];

        if( !specHasData && gResetHistory == 0 )
        {
            spec *= saturate( 1.0 - neighboorWeight.x - neighboorWeight.y );
            spec += s0 * neighboorWeight.x + s1 * neighboorWeight.y;

            float2 temporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specMaxAccumSpeed, parallax, roughnessModified, roughness );
            float historyWeight = 1.0 - gCheckerboardResolveAccumSpeed * temporalAccumulationParams.x;
            float specHistorySurfaceClamped = STL::Color::Clamp( specM1, specSigma * temporalAccumulationParams.y, specHistorySurface ); // TODO: needed?

            spec = lerp( specHistorySurfaceClamped, spec, max( historyWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );
        }
    #endif

    // Diffuse
    #if( defined REBLUR_DIFFUSE )
        // Accumulation
        float diffAccumSpeed = GetSpecAccumSpeed( diffMaxAccumSpeed, 1.0, 0.0, 0.0 );
        float diffAccumSpeedNonLinear = 1.0 / ( diffAccumSpeed + 1.0 );

        float diffResult = lerp( diffHistory, diff, max( diffAccumSpeedNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );
        diffResult = Sanitize( diffResult, diff );

        gOut_Diff[ pixelPos ] = float2( diffResult, scaledViewZ );

        // Internal data
        float diffError = GetColorErrorForAdaptiveRadiusScale( diffResult, diffHistory, diffAccumSpeedNonLinear, 1.0, true );

        #if( !defined REBLUR_SPECULAR )
            gOut_InternalData[ pixelPos ] = PackDiffInternalData( diffAccumSpeed, curvature );
            gOut_Error[ pixelPos ] = float2( diffError, ( surfaceOcclusionAvg * 1.0 ) / 255.0 );
        #endif

        // Fast history
        float diffAccumSpeedNonLinearFast = 1.0 / ( REBLUR_HIT_DIST_ACCELERATION.y * min( diffAccumSpeed, REBLUR_HIT_DIST_ACCELERATION.y * gDiffMaxFastAccumulatedFrameNum ) + 1.0 );

        diffHistoryFast = lerp( diffHistory, diffHistoryFast, GetFastHistoryFactor( gDiffMaxFastAccumulatedFrameNum, diffAccumSpeed ) ); // fix history using the previous state

        float diffResultFast = lerp( diffHistoryFast, diff, max( diffAccumSpeedNonLinearFast, 2.0 * REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );
        diffResultFast = Sanitize( diffResultFast, diff );

        gOut_Fast_Diff[ pixelPos ] = diffResultFast;
    #endif

    // Specular
    #if( defined REBLUR_SPECULAR )
        // Hit distance ( surface motion )
        float3 V = -X * invDistToPoint;
        float NoV = abs( dot( Nflat, V ) );
        float accumSpeedSurface = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, parallax );
        float accumSpeedSurfaceNonLinear = 1.0 / ( accumSpeedSurface + 1.0 );

        float hitDist = GetHitDist( spec, viewZ, gSpecHitDistParams, roughness );

        float hitDistToSurfaceRatio = saturate( hitDist * invDistToPoint );
        parallax *= hitDistToSurfaceRatio; // TODO: needed? with fast or slow hitDist?

        // Previous data ( 2x2, virtual motion )
        float4 Xvirtual = GetXvirtual( X, V, NoV, roughness, hitDist, viewZ, curvature );
        float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual.xyz );

        STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gRectSizePrev );
        float2 gatherUvVirtualPrev = ( bilinearFilterAtPrevVirtualPos.origin + 1.0 ) * gInvScreenSize;

        // Virtual motion amount ( pixel ) - out of screen
        float virtualHistoryAmount = IsInScreen2x2( pixelUvVirtualPrev, gRectSizePrev );

        // Virtual motion amount ( pixel ) - reference
        virtualHistoryAmount *= 1.0 - gReference; // no virtual motion in the reference mode (it's by design, useful for integration debugging)

        // Virtual motion amount ( pixel ) - dominant factor
        virtualHistoryAmount *= STL::ImportanceSampling::GetSpecularDominantFactor( NoV, roughness, REBLUR_SPEC_DOMINANT_DIRECTION );

        // Virtual motion amount ( footprint ) - surface similarity
        uint4 prevPackRedVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherRed( gNearestClamp, gatherUvVirtualPrev ).wzxy;
        float4 prevViewZsVirtual = UnpackViewZ( prevPackRedVirtual );
        float3 VvprevNonNorm = STL::Geometry::ReconstructViewPosition( pixelUvVirtualPrev, gFrustumPrev, 1.0, gIsOrtho );
        float3 Nvprev = STL::Geometry::RotateVector( gWorldToViewPrev, N );
        float ka = dot( Nvprev, VvprevNonNorm );
        float kb = dot( Nvprev, Xvprev );
        float4 f = abs( ka * prevViewZsVirtual - kb ) * invDistToPoint;
        float4 weightsAmount = STL::Math::LinearStep( 0.015, 0.005, f );

        // Virtual motion amount ( footprint ) - roughness
        uint4 prevPackGreenVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, gatherUvVirtualPrev ).wzxy;
        float4 prevNormalAndRoughnessVirtual00 = UnpackNormalRoughness( prevPackGreenVirtual.x );
        float4 prevNormalAndRoughnessVirtual10 = UnpackNormalRoughness( prevPackGreenVirtual.y );
        float4 prevNormalAndRoughnessVirtual01 = UnpackNormalRoughness( prevPackGreenVirtual.z );
        float4 prevNormalAndRoughnessVirtual11 = UnpackNormalRoughness( prevPackGreenVirtual.w );

        float4 prevRoughnessVirtual = float4( prevNormalAndRoughnessVirtual00.w, prevNormalAndRoughnessVirtual10.w, prevNormalAndRoughnessVirtual01.w, prevNormalAndRoughnessVirtual11.w );
        float2 specRoughnessParams = GetRoughnessWeightParamsRoughEstimate( roughness );
        weightsAmount *= GetRoughnessWeight( specRoughnessParams, prevRoughnessVirtual ); // TODO: affecting amount then confidence looks better. Verify?

        // Virtual history confidence ( footprint ) - normal
        float fresnelFactor = STL::BRDF::Pow5( NoV );
        float normalWeightRenorm = lerp( 0.9, 1.0, STL::Math::LinearStep( 0.0, 0.15, roughnessModified ) ); // mitigate imprecision problems introduced by normals encoded with different precision (test #6 and #12)
        float virtualLobeScale = lerp( 0.5, 1.0, fresnelFactor );
        float specNormalParams = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughnessModified );
        specNormalParams *= virtualLobeScale;
        specNormalParams = 1.0 / max( specNormalParams, NRD_ENCODING_ERRORS.x );
        float4 normalWeights = GetNormalWeight4( specNormalParams, N, prevNormalAndRoughnessVirtual00.xyz, prevNormalAndRoughnessVirtual10.xyz, prevNormalAndRoughnessVirtual01.xyz, prevNormalAndRoughnessVirtual11.xyz );
        float4 weightsConfidence = saturate( normalWeights / normalWeightRenorm );

        // Virtual motion amount ( pixel ) - interpolate
        virtualHistoryAmount *= STL::Filtering::ApplyBilinearFilter( weightsAmount.x, weightsAmount.y, weightsAmount.z, weightsAmount.w, bilinearFilterAtPrevVirtualPos );

        // Virtual history confidence ( pixel ) - interpolate
        float virtualHistoryConfidence = STL::Filtering::ApplyBilinearFilter( weightsConfidence.x, weightsConfidence.y, weightsConfidence.z, weightsConfidence.w, bilinearFilterAtPrevVirtualPos );

        // Sample history ( virtual motion )
        // Due to mismatch between jitter and / or when the linear filter perfectly resolves to the nearest, all custom weights can be canceled out. Currently virtual history amount gets set to 0.
        // TODO: another solution - add a small random jitter ( or "prevJitter - currJitter" ) to "pixelUvVirtualPrev" to de-jitter prev data access, but history access should stay non-jittered.
        // TODO: another variant - set confidence to 0 and use simple bilinear filter
        float4 weightsVirtual = weightsAmount * weightsConfidence;
        float4 bilinearWeightsWithOcclusionVirtual = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevVirtualPos, weightsVirtual );
        virtualHistoryAmount *= float( dot( bilinearWeightsWithOcclusionVirtual, 1.0 ) != 0 );
        float virtualOcclusionAvg = step( 3.5, dot( weightsVirtual, 1.0 ) ) * REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TA;
        virtualOcclusionAvg *= surfaceOcclusionAvg;

        float specHistoryVirtualFast;
        float specHistoryVirtual = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_History_Spec, gIn_HistoryFast_Spec, gLinearClamp,
            saturate( pixelUvVirtualPrev ) * gRectSizePrev, gInvScreenSize,
            bilinearWeightsWithOcclusionVirtual, virtualOcclusionAvg == 1.0,
            specHistoryVirtualFast
        );

        // Virtual history confidence ( pixel ) - hit distance
        // TODO: parallax at first hit is needed in case of two bounces or not?
        // TODO: since hit distances are normalized many values can be represented as 1. It may break this test
        float hitDistNorm = lerp( specHistorySurface, spec, max( accumSpeedSurfaceNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );
        hitDist = GetHitDist( hitDistNorm, viewZ, gSpecHitDistParams, roughness );

        float hitDistVirtual = GetHitDist( specHistoryVirtual, viewZ, gSpecHitDistParams, roughness ); // TODO: roughness and geometry tests allow to use not virtually sampled roughness and even viewZ (viewZ is a bit more risky...)
        float hitDistDelta = abs( hitDistVirtual - hitDist ); // TODO: no sigma subtraction, but subtracting at least 10-50% can be helpful
        float hitDistMax = max( hitDistVirtual, hitDist );
        hitDistDelta *= STL::Math::PositiveRcp( hitDistMax + viewZ );

        float thresholdMin = 0.0; //0.02 * roughnessModified * roughnessModified;
        float thresholdMax = 0.2 * roughnessModified + thresholdMin;
        float2 virtualHistoryHitDistConfidence = 1.0 - STL::Math::SmoothStep( thresholdMin, thresholdMax, hitDistDelta * parallax * REBLUR_TS_SIGMA_AMPLITUDE * float2( 0.25, 1.0 ) );

        virtualHistoryAmount *= lerp( Xvirtual.w, 1.0, virtualHistoryHitDistConfidence.x );
        virtualHistoryHitDistConfidence = lerp( 1.0, virtualHistoryHitDistConfidence, Xvirtual.w );

        // Virtual history confidence ( pixel ) - fix trails if radiance on a flat surface is taken from a sloppy surface
        float2 pixelUvDelta = pixelUvVirtualPrev - pixelUvPrev;
        float2 pixelUvVirtualPrevPrev = pixelUvVirtualPrev + pixelUvDelta * 2.0; // TODO: is the ratio between "prev" and "prev-prev" rect size needed here?
        prevPackGreenVirtual = gIn_Prev_ViewZ_Normal_Roughness_AccumSpeeds.GatherGreen( gNearestClamp, pixelUvVirtualPrevPrev * gRectSizePrev * gInvScreenSize ).wzxy;

        normalWeights = GetNormalWeight4( specNormalParams, N, UnpackNormalRoughness( prevPackGreenVirtual.x ).xyz, UnpackNormalRoughness( prevPackGreenVirtual.y ).xyz, UnpackNormalRoughness( prevPackGreenVirtual.z ).xyz, UnpackNormalRoughness( prevPackGreenVirtual.w ).xyz );
        float virtualNormalWeight = MixVirtualNormalWeight( normalWeights, fresnelFactor, normalWeightRenorm );
        virtualHistoryAmount *= saturate( virtualNormalWeight / 0.25 );
        virtualHistoryConfidence *= virtualNormalWeight;

        // Clamp virtual history
        float smc = GetSpecMagicCurve( roughnessModified );
        float sigmaScale = lerp( 1.0, 3.0, smc ) * ( 1.0 + smc * REBLUR_TS_SIGMA_AMPLITUDE * 2.0 * virtualHistoryHitDistConfidence.x );
        float specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual );
        float specHistoryVirtualClampedFast = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtualFast );

        float virtualUnclampedAmount = lerp( virtualHistoryConfidence * virtualHistoryHitDistConfidence.y, 1.0, roughnessModified * roughnessModified );
        specHistoryVirtual = lerp( specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount );
        specHistoryVirtualFast = lerp( specHistoryVirtualClampedFast, specHistoryVirtualFast, virtualUnclampedAmount );

        virtualHistoryConfidence *= virtualHistoryHitDistConfidence.y;

        // Final composition
        float specHistoryFast = InterpolateSurfaceAndVirtualMotion( specHistorySurfaceFast.xxxx, specHistoryVirtualFast.xxxx, virtualHistoryAmount, hitDistToSurfaceRatio ).w;
        float specHistory = InterpolateSurfaceAndVirtualMotion( specHistorySurface.xxxx, specHistoryVirtual.xxxx, virtualHistoryAmount, hitDistToSurfaceRatio ).w;

        float specAccumSpeed = GetSpecAccumSpeed( specMaxAccumSpeed, roughnessModified, NoV, parallax * ( 1.0 - virtualHistoryAmount ) );
        float specMinAccumSpeed = min( specAccumSpeed, GetMipLevel( roughnessModified, gSpecMaxFastAccumulatedFrameNum ) );
        float specAccumSpeedScale = lerp( 1.0, virtualHistoryHitDistConfidence.x, virtualHistoryAmount );
        specAccumSpeed = InterpolateAccumSpeeds( specMinAccumSpeed, specAccumSpeed, specAccumSpeedScale );

        float accumSpeedNonLinear = 1.0 / ( specAccumSpeed + 1.0 );

        float specResult = lerp( specHistory, spec, max( accumSpeedNonLinear, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );
        specResult = Sanitize( specResult, spec );

        gOut_Spec[ pixelPos ] = float2( specResult, scaledViewZ );

        // Internal data
        #if( defined REBLUR_DIFFUSE )
            gOut_InternalData[ pixelPos ] = PackDiffSpecInternalData( diffAccumSpeed, specAccumSpeed, curvature, roughness );
        #else
            gOut_InternalData[ pixelPos ] = PackDiffSpecInternalData( 0, specAccumSpeed, curvature, roughness );
        #endif

        float specError = GetColorErrorForAdaptiveRadiusScale( specResult, specHistory, accumSpeedNonLinear, roughness, true );

        #if( defined REBLUR_DIFFUSE )
            gOut_Error[ pixelPos ] = float4( diffError, ( surfaceOcclusionAvg * 1.0 + virtualOcclusionAvg * 2.0 ) / 255.0, specError, virtualHistoryConfidence );
        #else
            gOut_Error[ pixelPos ] = float4( 0, ( surfaceOcclusionAvg * 1.0 + virtualOcclusionAvg * 2.0 ) / 255.0, specError, virtualHistoryConfidence );
        #endif

        // Fast history
        float maxFastAccumSpeedRoughnessAdjusted = gSpecMaxFastAccumulatedFrameNum * smc;
        float specAccumSpeedNonLinearFast = 1.0 / ( REBLUR_HIT_DIST_ACCELERATION.y * min( specAccumSpeed, REBLUR_HIT_DIST_ACCELERATION.y * maxFastAccumSpeedRoughnessAdjusted ) + 1.0 );

        specHistoryFast = lerp( specHistory, specHistoryFast, GetFastHistoryFactor( maxFastAccumSpeedRoughnessAdjusted, specAccumSpeed ) ); // fix history using the previous state

        float specResultFast = lerp( specHistoryFast, spec, max( specAccumSpeedNonLinearFast, 2.0 * REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughnessModified ) ) );
        specResultFast = Sanitize( specResultFast, spec );

        gOut_Fast_Spec[ pixelPos ] = specResultFast;
    #endif
}
