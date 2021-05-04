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
    REBLUR_DIFF_SHARED_CB_DATA;

    float4x4 gWorldToView;
    float4 gRotator;
    float gDiffFastHistoryClampingColorBoxSigmaScale;
    uint gDiffAntiFirefly;
};

#if( REBLUR_USE_5X5_HISTORY_CLAMPING == 1 )
    #define NRD_USE_BORDER_2
#endif
#include "NRD_Common.hlsl"

#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float2>, gIn_InternalData, t, 1, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 2, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 3, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Fast_Diff, t, 4, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 0, 0 );

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    s_Diff[ sharedId.y ][ sharedId.x ] = gIn_Fast_Diff[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    PRELOAD_INTO_SMEM;

    // Early out
    float scaledViewZ = gIn_ScaledViewZ[ pixelPos ];
    float viewZ = scaledViewZ / NRD_FP16_VIEWZ_SCALE;

    [branch]
    if( viewZ > gInf )
        return;

    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float4 normalAndRoughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ] );
    float3 N = normalAndRoughness.xyz;

    float2 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ] );
    float4 diff = ReconstructHistoryDiff( diffInternalData, pixelPos, scaledViewZ, N, gRotator, gWorldToView, gIn_ScaledViewZ, gIn_Diff );

    // History clamping
    #if( REBLUR_USE_FAST_HISTORY == 1 )
        float4 diffM1 = 0;
        float4 diffM2 = 0;
        float4 diffMaxInput = -NRD_INF;
        float4 diffMinInput = NRD_INF;

        [unroll]
        for( int dy = 0; dy <= BORDER * 2; dy++ )
        {
            [unroll]
            for( int dx = 0; dx <= BORDER * 2; dx++ )
            {
                int2 pos = threadId + int2( dx, dy );

                float4 d = s_Diff[ pos.y ][ pos.x ];
                diffM1 += d;
                diffM2 += d * d;

                if( dx != BORDER || dy != BORDER )
                {
                    diffMaxInput = max( diffMaxInput, d );
                    diffMinInput = min( diffMinInput, d );
                }
            }
        }

        float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );

        #if( REBLUR_USE_ANTI_FIREFLY == 1 )
            [flatten]
            if( gDiffAntiFirefly != 0 )
                diff = clamp( diff, diffMinInput, diffMaxInput );
        #endif

        diffM1 *= invSum;
        diffM2 *= invSum;
        float4 diffSigma = GetStdDev( diffM1, diffM2 );
        float4 diffMin = diffM1 - gDiffFastHistoryClampingColorBoxSigmaScale * diffSigma;
        float4 diffMax = diffM1 + gDiffFastHistoryClampingColorBoxSigmaScale * diffSigma;
        float4 diffCenter = s_Diff[ threadId.y + BORDER ][ threadId.x + BORDER ];
        diffMin = min( diffMin, diffCenter );
        diffMax = max( diffMax, diffCenter );
        float4 diffClamped = clamp( diff, diffMin, diffMax );

        [flatten]
        if( gDiffMaxFastAccumulatedFrameNum < gDiffMaxAccumulatedFrameNum )
            diff = lerp( diffClamped, diff, diffInternalData.x );
    #endif

    gOut_Diff[ pixelPos ] = diff;
}
