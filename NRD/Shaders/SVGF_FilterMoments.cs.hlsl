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
    float2 gScreenSize;
    float gZdeltaScale;
    float gDiffuse;
    float gInf;
    float gDebug;
};

#include "SVGF_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Signal, t, 2, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_Moments, t, 3, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_HistoryLength, t, 4, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<uint2>, gOut_Prev_ViewZ_Normal_Roughness, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Signal, u, 1, 0 );
NRI_RESOURCE( RWTexture2D<float2>, gOut_Variance, u, 2, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    // Center Z
    float centerZ = gIn_ViewZ[ pixelPos ];

    // Normal and roughness
    float4 normalAndRoughnessPacked = gIn_Normal_Roughness[ pixelPos ];
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( normalAndRoughnessPacked );
    float roughness = normalAndRoughness.w;
    float3 N = normalAndRoughness.xyz;

    // Pack current viewZ, normal and roughness for the next frame
    gOut_Prev_ViewZ_Normal_Roughness[ pixelPos ] = PackViewZNormalRoughness( centerZ, normalAndRoughnessPacked );

    // Early out
    [branch]
    if ( abs( centerZ ) > gInf )
    {
        #if( BLACK_OUT_INF_PIXELS == 1 )
            gOut_Signal[ pixelPos ] = 0;
        #endif
        return;
    }

    // History length
    float historyLength = UnpackHistoryLength( gIn_HistoryLength[ pixelPos ].x );
    float4 input = gIn_Signal[ pixelPos ];
    float2 moments = gIn_Moments[ pixelPos ];
    float2 lc = float2( STL::Color::Luminance( input.xyz ), input.w );

    // History fix
    if( historyLength < MAX_FRAME_NUM_WITH_HISTORY_FIX )
    {
        float2 normalParams;
        normalParams.x = STL::ImportanceSampling::GetSpecularLobeHalfAngle( roughness );
        normalParams.x *= 1.0; // yes, no narrowing here
        normalParams.x += NORMAL_BANDING_FIX;
        normalParams.y = 1.0;

        float2 sum = 1.0;

        float weightFade = saturate( historyLength / MAX_FRAME_NUM_WITH_HISTORY_FIX ); // TODO: it's correct, but do it better!

        [unroll]
        for( int y = -VARIANCE_ESTIMATION_RADIUS; y <= VARIANCE_ESTIMATION_RADIUS; y++ )
        {
            [unroll]
            for( int x = -VARIANCE_ESTIMATION_RADIUS; x <= VARIANCE_ESTIMATION_RADIUS; x++ )
            {
                if( x == 0 && y == 0 )
                    continue;

                int2 p = pixelPos + int2( x, y );

                float4 s = gIn_Signal[ p ];
                float2 m = gIn_Moments[ p ];
                float2 l = float2( STL::Color::Luminance( s.xyz ), s.w );

                float z = gIn_ViewZ[ p ];
                float4 n = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ p ] );

                float2 w = ComputeWeight( centerZ, z, gZdeltaScale * weightFade, N, n.xyz, normalParams, lc, l, 0.0, weightFade );

                bool isValid = all( p >= 0 ) && all( p < gScreenSize );
                w *= float( isValid );

                sum += w;
                input += s * w.xxxy;
                moments += m * w;
            }
        }

        float2 invSum = 1.0 / sum;
        input *= invSum.xxxy;
        moments *= invSum;

        lc = float2( STL::Color::Luminance( input.xyz ), input.w );
    }

    float2 variance = abs( moments - lc * lc );

    // Give the variance a boost for the first frames
    variance *= max( 1.0, MAX_FRAME_NUM_WITH_VARIANCE_BOOST / ( historyLength + 1.0 ) );

    // Output
    #if( SHOW_ACCUM_SPEED == 1 )
        input.w = saturate( historyLength / 31.0 );
    #endif

    gOut_Signal[ pixelPos ] = input;
    gOut_Variance[ pixelPos ] = variance;
}
