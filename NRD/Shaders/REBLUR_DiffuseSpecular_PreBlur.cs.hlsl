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
    REBLUR_DIFF_SPEC_SHARED_CB_DATA;

    float4x4 gWorldToView;
    float4 gRotator;
    float4 gDiffHitDistParams;
    float4 gSpecHitDistParams;
    float3 gSpecTrimmingParams;
    float gSpecBlurRadius;
    uint gSpecCheckerboard;
    float gDiffBlurRadius;
    uint gDiffCheckerboard;
};

#include "NRD_Common.hlsl"
#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 3, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float>, gOut_ScaledViewZ, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 2, 0 );

void Preload( int2 sharedId, int2 globalId )
{
    uint2 globalIdUser = gRectOrigin + globalId;

    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = abs( gIn_ViewZ[ globalIdUser ] );
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Checkerboard
    bool2 hasData = true;
    uint2 checkerboardPixelPos = pixelPos.xx;
    uint checkerboard = STL::Sequence::CheckerBoard( pixelPos, gFrameIndex );

    if( gDiffCheckerboard != 2 )
    {
        hasData.x = checkerboard == gDiffCheckerboard;
        checkerboardPixelPos.x >>= 1;
    }

    if( gSpecCheckerboard != 2 )
    {
        hasData.y = checkerboard == gSpecCheckerboard;
        checkerboardPixelPos.y >>= 1;
    }

    // Early out
    int2 smemPos = threadId + BORDER;
    float viewZ = s_ViewZ[ smemPos.y ][ smemPos.x ];

    float scaledViewZ = min( viewZ * NRD_FP16_VIEWZ_SCALE, NRD_FP16_MAX );
    gOut_ScaledViewZ[ pixelPos ] = scaledViewZ;

    [branch]
    if( viewZ > gInf )
        return;

    // Center data
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float4 diff = gIn_Diff[ gRectOrigin + uint2( checkerboardPixelPos.x, pixelPos.y ) ];
    float4 spec = gIn_Spec[ gRectOrigin + uint2( checkerboardPixelPos.y, pixelPos.y ) ];

    int3 smemCheckerboardPos = smemPos.xyx + int3( -1, 0, 1 );
    float viewZ0 = s_ViewZ[ smemCheckerboardPos.y ][ smemCheckerboardPos.x ];
    float viewZ1 = s_ViewZ[ smemCheckerboardPos.y ][ smemCheckerboardPos.z ];
    float2 w = GetBilateralWeight( float2( viewZ0, viewZ1 ), viewZ );
    w *= STL::Math::PositiveRcp( w.x + w.y );

    int3 checkerboardPos = pixelPos.xyx + int3( -1, 0, 1 );
    checkerboardPos.xz >>= 1;
    checkerboardPos += gRectOrigin.xyx;

    float4 d0 = gIn_Diff[ checkerboardPos.xy ];
    float4 d1 = gIn_Diff[ checkerboardPos.zy ];
    if( !hasData.x )
    {
        diff *= saturate( 1.0 - w.x - w.y );
        diff += d0 * w.x + d1 * w.y;
    }
    float diffCenterNormHitDist = diff.w;

    float4 s0 = gIn_Spec[ checkerboardPos.xy ];
    float4 s1 = gIn_Spec[ checkerboardPos.zy ];
    if( !hasData.y )
    {
        spec *= saturate( 1.0 - w.x - w.y );
        spec += s0 * w.x + s1 * w.y;
    }
    float specCenterNormHitDist = spec.w;

    // Normal and roughness
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Blur radius
    float diffHitDist = GetHitDist( diffCenterNormHitDist, viewZ, gDiffHitDistParams );
    float diffBlurRadius = GetBlurRadius( gDiffBlurRadius, 1.0, diffHitDist, Xv, REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED, REBLUR_PRE_BLUR_RADIUS_SCALE( 1.0 ) );
    float diffWorldBlurRadius = PixelRadiusToWorld( gUnproject, gIsOrtho, diffBlurRadius, viewZ );

    float specHitDist = GetHitDist( specCenterNormHitDist, viewZ, gSpecHitDistParams, roughness );
    float specBlurRadius = GetBlurRadius( gSpecBlurRadius, roughness, specHitDist, Xv, REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED, REBLUR_PRE_BLUR_RADIUS_SCALE( roughness ) );
    specBlurRadius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gSpecTrimmingParams );
    float specWorldBlurRadius = PixelRadiusToWorld( gUnproject, gIsOrtho, specBlurRadius, viewZ );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( REBLUR_PRE_BLUR_ROTATOR_MODE, pixelPos, gRotator, gFrameIndex );

    // Edge detection
    float edge = DetectEdge( N, smemPos );

    // Denoising
    float2 diffSum = 1.0;
    float diffNormalWeightParams = GetNormalWeightParams( viewZ, 1.0, edge, REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED );
    float2 diffHitDistanceWeightParams = GetHitDistanceWeightParams( diffCenterNormHitDist, REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED, diffHitDist, Xv );
    float2x3 diffTvBv = GetKernelBasis( Xv, Nv, diffWorldBlurRadius, edge );

    float2 specSum = 1.0;
    float specNormalWeightParams = GetNormalWeightParams( viewZ, roughness, edge, REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED );
    float2 specHitDistanceWeightParams = GetHitDistanceWeightParams( specCenterNormHitDist, REBLUR_PRE_BLUR_NON_LINEAR_ACCUM_SPEED, specHitDist, Xv, roughness );
    float2 specRoughnessWeightParams = GetRoughnessWeightParams( roughness );
    float2x3 specTvBv = GetKernelBasis( Xv, Nv, specWorldBlurRadius, edge, roughness );

    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, Xv, Nv, viewZ );

    [unroll]
    for( uint i = 0; i < REBLUR_POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = REBLUR_POISSON_SAMPLES[ i ];

        // Diffuse
        {
            // Sample coordinates
            float2 uv = GetKernelSampleCoordinates( gViewToClip, offset, Xv, diffTvBv[ 0 ], diffTvBv[ 1 ], rotator );

            // Handle half res input in the checkerboard mode
            float2 checkerboardUv = uv;
            if( gDiffCheckerboard != 2 )
                checkerboardUv = ApplyCheckerboard( uv, gDiffCheckerboard, i, gRectSize, gInvRectSize, gFrameIndex );

            // Fetch data
            float2 uvScaled = uv * gResolutionScale + gRectOffset;
            float2 checkerboardUvScaled = checkerboardUv * gResolutionScale + gRectOffset;

            float4 d = gIn_Diff.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            float z = abs( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled, 0 ) );
            float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uvScaled, 0 );

            float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );
            normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

            // Sample weight
            float w = GetGeometryWeight( geometryWeightParams, Nv, samplePos );
            w *= GetNormalWeight( diffNormalWeightParams, N, normal.xyz );
            w *= IsInScreen( uv );

            float wh = GetHitDistanceWeight( diffHitDistanceWeightParams, d.w );
            float2 ww = w * lerp( float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 2.0 ), 1.0, wh );

            diff += d * ww.xxxy;
            diffSum += ww;
        }

        // Specular
        {
            // Sample coordinates
            float2 uv = GetKernelSampleCoordinates( gViewToClip, offset, Xv, specTvBv[ 0 ], specTvBv[ 1 ], rotator );

            // Handle half res input in the checkerboard mode
            float2 checkerboardUv = uv;
            if( gSpecCheckerboard != 2 )
                checkerboardUv = ApplyCheckerboard( uv, gSpecCheckerboard, i, gRectSize, gInvRectSize, gFrameIndex );

            // Fetch data
            float2 uvScaled = uv * gResolutionScale + gRectOffset;
            float2 checkerboardUvScaled = checkerboardUv * gResolutionScale + gRectOffset;

            float4 s = gIn_Spec.SampleLevel( gNearestMirror, checkerboardUvScaled, 0 );
            float z = abs( gIn_ViewZ.SampleLevel( gNearestMirror, uvScaled, 0 ) );
            float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uvScaled, 0 );

            float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gIsOrtho );
            normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

            // Sample weight
            float w = GetGeometryWeight( geometryWeightParams, Nv, samplePos );
            w *= GetNormalWeight( specNormalWeightParams, N, normal.xyz );
            w *= GetRoughnessWeight( specRoughnessWeightParams, normal.w );
            w *= IsInScreen( uv );

            float wh = GetHitDistanceWeight( specHitDistanceWeightParams, s.w );
            float2 ww = w * lerp( float2( 0.0, REBLUR_HIT_DIST_MIN_WEIGHT * 2.0 ), 1.0, wh );

            spec += s * ww.xxxy;
            specSum += ww;
        }
    }

    diff /= diffSum.xxxy;
    spec /= specSum.xxxy;

    // Special case for hit distance
    diff.w = lerp( diff.w, diffCenterNormHitDist, REBLUR_HIT_DIST_INPUT_MIX );
    spec.w = lerp( spec.w, specCenterNormHitDist, REBLUR_HIT_DIST_INPUT_MIX );

    // Output
    gOut_Diff[ pixelPos ] = diff;
    gOut_Spec[ pixelPos ] = spec;
}
