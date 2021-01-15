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
    float2 gScreenSize;
    float2 gInvScreenSize;
    float gZdeltaScale;
    float gVarianceScale;
    float gDiffuse;
    float gInf;
    uint gStepSize;
    float gDebug;
};

#include "SVGF_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_HistoryLength, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Signal, t, 3, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_Variance, t, 4, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_Signal, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float2>, gOut_Variance, u, 1, 0 );

float2 ComputeVarianceCenter( int2 pixelPos ) // TODO: needs to be bilateral, use shared memory!
{
    const float kernel[ 2 ][ 2 ] = {
        { 1.0 / 4.0, 1.0 / 8.0 },
        { 1.0 / 8.0, 1.0 / 16.0 }
    };

    float2 sum = 0.0;

    for( int y = -1; y <= 1; y++ )
    {
        for( int x = -1; x <= 1; x++ )
        {
            int2 p = pixelPos + int2( x, y );
            float k = kernel[ abs(x) ][ abs(y) ];
            sum += gIn_Variance[ p ] * k;
        }
    }

    return sum;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    // Center Z
    float centerZ = gIn_ViewZ[ pixelPos ];

    // Early out
    [branch]
    if( abs( centerZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Signal[ pixelPos ] = 0;
            gOut_Variance[ pixelPos ] = 0;
        #endif
        return;
    }

    // Normal and roughness
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] );
    float roughness = normalAndRoughness.w;
    float3 N = normalAndRoughness.xyz;

    float2 v = ComputeVarianceCenter( pixelPos );
    float2 sum = 1.0;
    float4 result = gIn_Signal[ pixelPos ];
    float2 variance = gIn_Variance[ pixelPos ];
    float2 lc = float2( STL::Color::Luminance( result.xyz ), result.w );

    float2 lWeight = STL::Math::PositiveRcp( gVarianceScale * sqrt( max( 0.0, v ) ) );
    float zWeight = gZdeltaScale / ( float( 1 << gStepSize ) + 1.0 );

    float2 normalParams;
    normalParams.x = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
    normalParams.x += NORMAL_BANDING_FIX;
    normalParams.x *= LOBE_STRICTNESS_FACTOR;
    normalParams.y = 1.0;

    float historyLength = UnpackHistoryLength( gIn_HistoryLength[ pixelPos ].x );
    float weightFade = 1.0 / ( 1.0 + historyLength ); // TODO: it's correct, but do it better!

    #if( ATROUS_RADIUS == 1 )
        const float2 kernelWeights = float2( 0.44198, 0.27901 );
    #else
        const float3 kernelWeights = float3( 0.38774, 0.24477, 0.06136 );
    #endif

    [unroll]
    for( int y = -ATROUS_RADIUS; y <= ATROUS_RADIUS; y++ )
    {
        [unroll]
        for( int x = -ATROUS_RADIUS; x <= ATROUS_RADIUS; x++ )
        {
            if( x == 0 && y == 0 )
                continue;

            int2 p = pixelPos + int2( x, y ) * ( 1 << gStepSize );

            float2 v = gIn_Variance[ p ];
            float4 s = gIn_Signal[ p ];
            float2 l = float2( STL::Color::Luminance( s.xyz ), s.w );

            float z = gIn_ViewZ[ p ];
            float4 n = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ p ] );

            float2 w = ComputeWeight( centerZ, z, zWeight, N, n.xyz, normalParams, lc, l, lWeight, weightFade );
            w *= kernelWeights[ abs( x ) ] * kernelWeights[ abs( y ) ];

            bool isValid = all( p >= 0 ) && all( p < gScreenSize );
            w *= float( isValid );

            sum += w;
            result += s * w.xxxy;
            variance += v * w * w;
        }
    }

    result /= sum.xxxy;
    variance /= sum * sum;

    // Output
    gOut_Signal[ pixelPos ] = result;
    gOut_Variance[ pixelPos ] = variance;
}
