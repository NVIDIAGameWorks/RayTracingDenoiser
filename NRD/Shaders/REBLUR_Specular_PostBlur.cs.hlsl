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

    float4x4 gWorldToView;
    float4 gRotator;
    float4 gSpecHitDistParams;
    float3 gSpecTrimmingParams;
    float gSpecBlurRadius;
    float gSpecBlurRadiusScale;
};

#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_InternalData, t, 1, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SpecTemporalAccumulationOutput, t, 4, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 0, 0 );

void Preload( int2 sharedId, int2 globalId )
{
    s_Normal_Roughness[ sharedId.y ][ sharedId.x ] = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalId ] );
    s_ViewZ[ sharedId.y ][ sharedId.x ] = gIn_ScaledViewZ[ globalId ] / NRD_FP16_VIEWZ_SCALE;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    PRELOAD_INTO_SMEM;

    // Debug
    #if( NRD_DEBUG == NRD_SHOW_MIPS )
        gOut_Spec[ pixelPos ] = gIn_Spec[ pixelPos ];
        return;
    #endif

    // Early out
    int2 smemPos = threadId + BORDER;
    float centerZ = s_ViewZ[ smemPos.y ][ smemPos.x ];

    [branch]
    if( abs( centerZ ) > abs( gInf ) )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Spec[ pixelPos ] = 0;
        #endif
        return;
    }

    // Normal and roughness
    float4 normalAndRoughness = s_Normal_Roughness[ smemPos.y ][ smemPos.x ];
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );
    float roughness = normalAndRoughness.w;

    // Accumulations speeds
    float2 specInternalData = UnpackSpecInternalData( gIn_InternalData[ pixelPos ], roughness );

    // Center data
    float3 centerPos = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, centerZ, gIsOrtho );
    float4 spec = gIn_Spec[ pixelPos ];
    float specCenterNormHitDist = spec.w;

    // Blur radius scale
    float2 specCurr = spec.xw;
    float2 specPrev = gIn_SpecTemporalAccumulationOutput[ pixelPos ].xw;
    float specError = GetColorErrorForAdaptiveRadiusScale( specCurr, specPrev, specInternalData.x, roughness );
    float specRadiusScale = POST_BLUR_RADIUS_SCALE + specError * gSpecBlurRadiusScale;
    float specRadiusBias = specError * gSpecBlurRadiusScale;

    // Blur radius
    float specHitDist = GetHitDist( specCenterNormHitDist, centerZ, gSpecHitDistParams, roughness );
    float specBlurRadius = GetBlurRadius( gSpecBlurRadius, roughness, specHitDist, centerPos, specInternalData.x );
    specBlurRadius *= GetBlurRadiusScaleBasingOnTrimming( roughness, gSpecTrimmingParams );
    specBlurRadius = specBlurRadius * specRadiusScale + specRadiusBias;
    float specWorldBlurRadius = PixelRadiusToWorld( specBlurRadius, centerZ );

    // Random rotation
    float4 rotator = GetBlurKernelRotation( POST_BLUR_ROTATOR_MODE, pixelPos, gRotator );

    // Edge detection
    float edge = DetectEdge( N, smemPos );

    // Denoising
    float2 specSum = 1.0;
    float specNormalWeightParams = GetNormalWeightParams( roughness, edge, specInternalData.x );
    float2 specHitDistanceWeightParams = GetHitDistanceWeightParams( specCenterNormHitDist, specInternalData.x, specHitDist, centerPos, roughness );
    float2 specRoughnessWeightParams = GetRoughnessWeightParams( roughness );
    float2x3 specTvBv = GetKernelBasis( centerPos, Nv, specWorldBlurRadius, specInternalData.x, roughness );

    float2 geometryWeightParams = GetGeometryWeightParams( centerPos, Nv, centerZ );

    UNROLL
    for( uint i = 0; i < POISSON_SAMPLE_NUM; i++ )
    {
        float3 offset = POISSON_SAMPLES[ i ];

        // Sample coordinates
        float2 uv = GetKernelSampleCoordinates( offset, centerPos, specTvBv[ 0 ], specTvBv[ 1 ], rotator );

        // Fetch data
        float4 s = gIn_Spec.SampleLevel( gNearestMirror, uv, 0 );
        float scaledViewZ = gIn_ScaledViewZ.SampleLevel( gNearestMirror, uv, 0 );
        float4 normal = gIn_Normal_Roughness.SampleLevel( gNearestMirror, uv, 0 );

        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, scaledViewZ / NRD_FP16_VIEWZ_SCALE, gIsOrtho );
        normal = _NRD_FrontEnd_UnpackNormalAndRoughness( normal );

        // Sample weight
        float w = GetGeometryWeight( geometryWeightParams, Nv, samplePos );
        w *= GetNormalWeight( specNormalWeightParams, N, normal.xyz );
        w *= GetRoughnessWeight( specRoughnessWeightParams, normal.w );

        float wh = GetHitDistanceWeight( specHitDistanceWeightParams, s.w );
        float2 ww = w * lerp( float2( 0.0, HIT_DIST_MIN_WEIGHT * 0.5 ), 1.0, wh );

        spec += s * ww.xxxy;
        specSum += ww;
    }

    spec *= STL::Math::PositiveRcp( specSum ).xxxy;

    // Special case for hit distance
    spec.w = lerp( spec.w, specCenterNormHitDist, HIT_DIST_INPUT_MIX );

    // Output
    gOut_Spec[ pixelPos ] = spec;
}
