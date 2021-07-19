/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsl"
#include "STL.hlsl"
#include "REBLUR_Diffuse_HistoryFix.resources.hlsl"

NRD_DECLARE_CONSTANTS

#if( REBLUR_USE_5X5_HISTORY_CLAMPING == 1 )
    #define NRD_USE_BORDER_2
#endif
#include "NRD_Common.hlsl"
NRD_DECLARE_SAMPLERS

#include "REBLUR_Common.hlsl"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    s_Diff[ sharedId.y ][ sharedId.x ] = gIn_Fast_Diff[ globalId ];
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    float scaledViewZ = gIn_ScaledViewZ[ pixelPos ];
    float viewZ = scaledViewZ / NRD_FP16_VIEWZ_SCALE;

    [branch]
    if( viewZ > gInf )
        return;

    // History reconstruction
    float2 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ] );
    float4 diff = ReconstructHistory( diffInternalData.y, 1.0, pixelPos, pixelUv, scaledViewZ, gDiffMaxFastAccumulatedFrameNum, gIn_ScaledViewZ, gIn_Diff );

    // History clamping & anti-firefly
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

            // Diffuse
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

        [flatten]
        if( gDiffAntiFirefly != 0 && REBLUR_USE_ANTI_FIREFLY == 1 )
        {
            float4 diffAntifirefly = clamp( diff, diffMinInput, diffMaxInput );
            diff = lerp( diffAntifirefly, diff, diffInternalData.x );
        }
    #endif

    gOut_Diff[ pixelPos ] = diff;
}
