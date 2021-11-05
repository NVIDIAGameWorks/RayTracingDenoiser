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
#include "REBLUR_DiffuseSpecular_TemporalStabilization.resources.hlsli"

NRD_DECLARE_CONSTANTS

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
    #define NRD_CTA_8X8
#endif

#include "NRD_Common.hlsli"
NRD_DECLARE_SAMPLERS

#include "REBLUR_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];
groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    uint2 globalIdUser = gRectOrigin + globalId;

    s_ViewZ[ sharedId.y ][ sharedId.x ] = abs( gIn_ViewZ[ globalIdUser ] );

    #if( defined REBLUR_DIFFUSE )
        s_Diff[ sharedId.y ][ sharedId.x ] = gIn_Diff[ globalId ];
    #endif

    #if( defined REBLUR_SPECULAR )
        s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Spec[ globalId ];
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    int2 smemPos = threadId + BORDER;
    float viewZ = s_ViewZ[ smemPos.y ][ smemPos.x ];

    [branch]
    if( viewZ > gInf )
    {
        gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( NRD_INF, 0.0, float3( 0, 0, 1 ), 1.0, 0.0 );
        return;
    }

    // Normal and roughness
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ] );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );
    float invDistToPoint = STL::Math::Rsqrt( STL::Math::LengthSquared( Xv ) );
    float3 V = -X * invDistToPoint;

    // Local variance
    float viewZnearest = viewZ;
    int2 offseti = int2( BORDER, BORDER );
    float sum = 1.0;

    #if( defined REBLUR_DIFFUSE )
        float4 diff = s_Diff[ smemPos.y ][ smemPos.x ];
        float4 diffM1 = diff;
        float4 diffM2 = diff * diff;
    #endif

    #if( defined REBLUR_SPECULAR )
        float4 spec = s_Spec[ smemPos.y ][ smemPos.x ];
        float4 specM1 = spec;
        float4 specM2 = spec * spec;
    #endif

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadId + int2( dx, dy );
            float z = s_ViewZ[ pos.y ][ pos.x ];

            int2 t1 = int2( dx, dy ) - BORDER;
            if( ( abs( t1.x ) + abs( t1.y ) == 1 ) && z < viewZnearest )
            {
                viewZnearest = z;
                offseti = int2( dx, dy );
            }

            // Weights are needed to avoid getting 1 pixel wide outline under motion on contrast objects
            float w = GetBilateralWeight( z, viewZ );
            sum += w;

            #if( defined REBLUR_DIFFUSE )
                float4 d = s_Diff[ pos.y ][ pos.x ];
                diffM1 += d * w;
                diffM2 += d * d * w;
            #endif

            #if( defined REBLUR_SPECULAR )
                float4 s = s_Spec[ pos.y ][ pos.x ];
                specM1 += s * w;
                specM2 += s * s * w;
            #endif
        }
    }

    float invSum = 1.0 / sum;

    #if( defined REBLUR_DIFFUSE )
        diffM1 *= invSum;
        diffM2 *= invSum;
        float4 diffSigma = GetStdDev( diffM1, diffM2 );
    #endif

    #if( defined REBLUR_SPECULAR )
        specM1 *= invSum;
        specM2 *= invSum;
        float4 specSigma = GetStdDev( specM1, specM2 );
    #endif

    // Compute previous pixel position
    offseti -= BORDER;
    float2 offset = float2( offseti ) * gInvRectSize;
    float3 Xvnearest = STL::Geometry::ReconstructViewPosition( pixelUv + offset, gFrustum, viewZnearest, gIsOrtho );
    float3 Xnearest = STL::Geometry::AffineTransform( gViewToWorld, Xvnearest );
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser + offseti ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv + offset, Xnearest, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    pixelUvPrev -= offset;

    float isInScreen = IsInScreen2x2( pixelUvPrev, gRectSizePrev );
    float3 Xprev = X + motionVector * float( gWorldSpaceMotion != 0 );

    // Compute parallax
    float parallax = ComputeParallax( X, Xprev, gCameraDelta.xyz );

    // Internal data
    float curvature, virtualHistoryAmount;
    #if( defined REBLUR_SPECULAR )
        float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], curvature, virtualHistoryAmount );
        float2 diffInternalData = internalData.xy;
        float2 specInternalData = internalData.zw;
    #else
        float2 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ], curvature );
    #endif

    float4 error = gIn_Error[ pixelPos ];
    uint bits = uint( error.y * 255.0 + 0.5 );
    float2 occlusionAvg = float2( ( bits & uint2( 1, 2 ) ) != 0 );

    STL::Filtering::Bilinear bilinearFilterAtPrevPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvPrev ), gRectSizePrev );
    float4 bilinearWeightsWithOcclusion = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevPos, 1.0 );

    // Main
    #if( defined REBLUR_DIFFUSE )
        // Sample history
        float4 diffHistory = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_HistoryStabilized_Diff, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            bilinearWeightsWithOcclusion, occlusionAvg.x == 1.0 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS
        );

        // Antilag
        ComputeAntilagScale( diffInternalData.y, diffHistory, diff, diffM1, diffSigma, gDiffAntilag1, gDiffAntilag2, gDiffMaxFastAccumulatedFrameNum, curvature );

        // Clamp history and combine with the current frame
        float2 diffTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, diffInternalData.y, parallax );

        diffHistory = STL::Color::Clamp( diffM1, diffSigma * diffTemporalAccumulationParams.y, diffHistory );
        float diffHistoryWeight = 1.0 - REBLUR_TS_MAX_HISTORY_WEIGHT * diffTemporalAccumulationParams.x;

        float4 diffResult;
        diffResult.xyz = lerp( diffHistory.xyz, diff.xyz, diffHistoryWeight );
        diffResult.w = lerp( diffHistory.w, diff.w, max( diffHistoryWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( 1.0 ) ) );
        diffResult = Sanitize( diffResult, diff );

        // Output
        gOut_Diff[ pixelPos ] = diffResult;

        // User-visible debug output
        #if( REBLUR_DEBUG != 0 )
            uint diffMode = REBLUR_DEBUG;
            if( diffMode == 1 )
            {
                // Accumulated frame num
                diffResult.w = saturate( diffInternalData.y / ( gDiffMaxAccumulatedFrameNum + 1.0 ) );
            }
            else if( diffMode == 2 )
            {
                // Curvature
                diffResult.w = curvature;
            }
            else if( diffMode == 3 )
            {
                // Error
                diffResult.w = error.x;
            }
            else if( diffMode == 4 )
            {
                // Real vs Stabilized history difference ( color )
                float l0 = STL::Color::Luminance( diffHistory.xyz );
                float l1 = STL::Color::Luminance( lerp( diffM1.xyz, diff.xyz, curvature ) );
                float s = STL::Color::Luminance( diffSigma.xyz ) * lerp( 1.0, 2.0, curvature );

                float err = abs( l0 - l1 ) - s;
                err /= max( l0, l1 ) + s + 0.00001;
                err = STL::Math::LinearStep( 0.0, REBLUR_DEBUG_ERROR_NORMALIZATION, err );

                diffResult.w = err;
            }
            else if( diffMode == 5 )
            {
                // Real vs Stabilized history difference ( hit distance )
                float l0 = diffHistory.w;
                float l1 = lerp( diffM1.w, diff.w, curvature );
                float s = diffSigma.w * lerp( 1.0, 2.0, curvature );

                float err = abs( l0 - l1 ) - s;
                err /= max( l0, l1 ) + s + 0.00001;
                err = STL::Math::LinearStep( 0.0, REBLUR_DEBUG_ERROR_NORMALIZATION, err );

                diffResult.w = err;
            }

            diffResult.xyz = STL::Color::ColorizeZucconi( diffResult.w );
        #endif

        gOut_Diff_Copy[ pixelPos ] = diffResult;
    #endif

    #if( defined REBLUR_SPECULAR )
        // Sample history ( virtual motion )
        float hitDist = GetHitDist( spec.w, viewZ, gSpecHitDistParams, roughness );
        float NoV = abs( dot( N, V ) );
        float4 Xvirtual = GetXvirtual( X, V, NoV, roughness, hitDist, viewZ, curvature );
        float2 pixelUvVirtualPrev = STL::Geometry::GetScreenUv( gWorldToClipPrev, Xvirtual.xyz );

        STL::Filtering::Bilinear bilinearFilterAtPrevVirtualPos = STL::Filtering::GetBilinearFilter( saturate( pixelUvVirtualPrev ), gRectSizePrev );
        float4 bilinearWeightsWithOcclusionVirtual = STL::Filtering::GetBilinearCustomWeights( bilinearFilterAtPrevVirtualPos, 1.0 );

        float4 specHistoryVirtual = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_HistoryStabilized_Spec, gLinearClamp,
            saturate( pixelUvVirtualPrev ) * gRectSizePrev, gInvScreenSize,
            bilinearWeightsWithOcclusionVirtual, occlusionAvg.y == 1.0 && REBLUR_USE_CATROM_FOR_VIRTUAL_MOTION_IN_TS
        );

        // Hit distance based disocclusion for virtual motion
        // TODO: parallax at first hit is needed in case of two bounces or not?
        float hitDistVirtual = GetHitDist( specHistoryVirtual.w, viewZ, gSpecHitDistParams, roughness ); // TODO: not virtually sampled viewZ is used
        float hitDistDelta = abs( hitDistVirtual - hitDist ); // TODO: no sigma subtraction, but subtracting at least 10-50% can be helpful
        float hitDistMax = max( hitDistVirtual, hitDist );
        hitDistDelta *= STL::Math::PositiveRcp( hitDistMax + viewZ );

        float thresholdMin = 0.0; //0.02 * roughnessModified * roughnessModified;
        float thresholdMax = 0.2 * roughness + thresholdMin;
        float2 virtualHistoryHitDistConfidence = 1.0 - STL::Math::SmoothStep( thresholdMin, thresholdMax, hitDistDelta * parallax * REBLUR_TS_SIGMA_AMPLITUDE * float2( 0.5, 2.0 ) );

        virtualHistoryHitDistConfidence = lerp( 1.0, virtualHistoryHitDistConfidence, Xvirtual.w );

        // Clamp virtual history
        float smc = GetSpecMagicCurve( roughness );
        float virtualHistoryConfidence = error.w;
        float sigmaScale = lerp( 1.0, 3.0, smc ) + smc * gFramerateScale * virtualHistoryConfidence * virtualHistoryHitDistConfidence.x;
        float4 specHistoryVirtualClamped = STL::Color::Clamp( specM1, specSigma * sigmaScale, specHistoryVirtual );

        float virtualUnclampedAmount = lerp( virtualHistoryConfidence * virtualHistoryHitDistConfidence.y, 1.0, roughness * roughness );
        specHistoryVirtual = lerp( specHistoryVirtualClamped, specHistoryVirtual, virtualUnclampedAmount );

        // Adjust accumulation speed for virtual motion if confidence is low
        float specMinAccumSpeed = min( specInternalData.y, GetMipLevel( roughness, gSpecMaxFastAccumulatedFrameNum ) );
        float specAccumSpeedScale = lerp( 1.0, virtualHistoryHitDistConfidence.x, virtualHistoryAmount );
        specInternalData.y = InterpolateAccumSpeeds( specMinAccumSpeed, specInternalData.y, specAccumSpeedScale );

        // Sample history ( surface motion )
        float4 specHistorySurface = BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights(
            gIn_HistoryStabilized_Spec, gLinearClamp,
            saturate( pixelUvPrev ) * gRectSizePrev, gInvScreenSize,
            bilinearWeightsWithOcclusion, occlusionAvg.x == 1.0 && REBLUR_USE_CATROM_FOR_SURFACE_MOTION_IN_TS
        );

        // Combine surface and virtual motion
        float hitDistToSurfaceRatio = saturate( hitDist * invDistToPoint );
        float4 specHistory = InterpolateSurfaceAndVirtualMotion( specHistorySurface, specHistoryVirtual, virtualHistoryAmount, hitDistToSurfaceRatio );

        // Antilag
        ComputeAntilagScale( specInternalData.y, specHistory, spec, specM1, specSigma, gSpecAntilag1, gSpecAntilag2, gSpecMaxFastAccumulatedFrameNum, curvature, roughness );

        // Clamp history and combine with the current frame
        float2 specTemporalAccumulationParams = GetTemporalAccumulationParams( isInScreen, specInternalData.y, parallax, roughness, roughness, virtualHistoryAmount );
        specTemporalAccumulationParams.x *= lerp( 1.0, virtualHistoryConfidence, SaturateParallax( parallax * ( 1.0 - virtualHistoryAmount ) * REBLUR_TS_SIGMA_AMPLITUDE ) ); // TODO: is there a better solution?

        specHistory = STL::Color::Clamp( specM1, specSigma * specTemporalAccumulationParams.y, specHistory );
        float specHistoryWeight = 1.0 - REBLUR_TS_MAX_HISTORY_WEIGHT * specTemporalAccumulationParams.x;

        float4 specResult;
        specResult.xyz = lerp( specHistory.xyz, spec.xyz, specHistoryWeight );
        specResult.w = lerp( specHistory.w, spec.w, max( specHistoryWeight, REBLUR_HIT_DIST_MIN_ACCUM_SPEED( roughness ) ) );
        specResult = Sanitize( specResult, spec );

        // Output
        gOut_Spec[ pixelPos ] = specResult;

        // User-visible debug output
        #if( REBLUR_DEBUG != 0 )
            uint specMode = REBLUR_DEBUG;
            if( specMode == 1 )
            {
                // Accumulated frame num
                specResult.w = saturate( specInternalData.y / ( gSpecMaxAccumulatedFrameNum + 1.0 ) );
            }
            else if( specMode == 2 )
            {
                // Curvature
                specResult.w = curvature;
            }
            else if( specMode == 3 )
            {
                // Error
                specResult.w = error.z;
            }
            else if( specMode == 4 )
            {
                // Real vs Stabilized history difference ( color )
                float l0 = STL::Color::Luminance( specHistory.xyz );
                float l1 = STL::Color::Luminance( lerp( specM1.xyz, spec.xyz, curvature ) );
                float s = STL::Color::Luminance( specSigma.xyz ) * lerp( 1.0, 2.0, curvature );

                float err = abs( l0 - l1 ) - s;
                err /= max( l0, l1 ) + s + 0.00001;
                err = STL::Math::LinearStep( 0.0, REBLUR_DEBUG_ERROR_NORMALIZATION, err );

                specResult.w = err;
            }
            else if( specMode == 5 )
            {
                // Real vs Stabilized history difference ( hit distance )
                float l0 = specHistory.w;
                float l1 = lerp( specM1.w, spec.w, curvature );
                float s = specSigma.w * lerp( 1.0, 2.0, curvature );

                float err = abs( l0 - l1 ) - s;
                err /= max( l0, l1 ) + s + 0.00001;
                err = STL::Math::LinearStep( 0.0, REBLUR_DEBUG_ERROR_NORMALIZATION, err );

                specResult.w = err;
            }
            else if( specMode == 6 )
            {
                // Virtual history amount
                specResult.w = virtualHistoryAmount;
            }
            else if( specMode == 7 )
            {
                // Virtual history confidence
                specResult.w = virtualHistoryConfidence;
            }
            else if( specMode == 8 )
            {
                // Parallax
                specResult.w = parallax;
            }

            // Show how colorization represents 0-1 range on the bottom
            specResult.xyz = STL::Color::ColorizeZucconi( pixelUv.y > 0.95 ? pixelUv.x : specResult.w );
        #endif

        gOut_Spec_Copy[ pixelPos ] = specResult;
    #endif

    // Output
    #if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
        gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( viewZ, diffInternalData.y, N, roughness, specInternalData.y );
    #elif( defined REBLUR_DIFFUSE )
        gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( viewZ, diffInternalData.y, N, 1.0, 0.0 );
    #else
        gOut_ViewZ_Normal_Roughness_AccumSpeeds[ pixelPos ] = PackViewZNormalRoughnessAccumSpeeds( viewZ, 0.0, N, roughness, specInternalData.y );
    #endif
}
