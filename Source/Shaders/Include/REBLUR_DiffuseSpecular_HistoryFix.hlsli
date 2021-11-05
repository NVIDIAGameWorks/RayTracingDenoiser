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
#include "REBLUR_DiffuseSpecular_HistoryFix.resources.hlsli"

NRD_DECLARE_CONSTANTS

#if( REBLUR_USE_5X5_HISTORY_CLAMPING == 1 )
    #define NRD_USE_BORDER_2
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
    #if( defined REBLUR_DIFFUSE )
        s_Diff[ sharedId.y ][ sharedId.x ] = gIn_Fast_Diff[ globalId ];
    #endif

    #if( defined REBLUR_SPECULAR )
        s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Fast_Spec[ globalId ];
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    float scaledViewZ = gIn_ScaledViewZ[ pixelPos ];
    float viewZ = scaledViewZ / NRD_FP16_VIEWZ_SCALE;

    [branch]
    if( viewZ > gInf )
        return;

    // Internal data
    #if( defined REBLUR_SPECULAR )
        float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ] );
        float2 diffInternalData = internalData.xy;
        float2 specInternalData = internalData.zw;
    #else
        float2 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ].xy );
    #endif

    // History reconstruction
    #if( defined REBLUR_DIFFUSE )
        float4 diff = ReconstructHistory( diffInternalData.y, 1.0, pixelPos, pixelUv, scaledViewZ, gDiffMaxFastAccumulatedFrameNum, gIn_ScaledViewZ, gIn_Diff );
    #endif

    #if( defined REBLUR_SPECULAR )
        float roughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ] ).w;
        float4 spec = ReconstructHistory( specInternalData.y, roughness, pixelPos, pixelUv, scaledViewZ, gSpecMaxFastAccumulatedFrameNum, gIn_ScaledViewZ, gIn_Spec );
    #endif

    // History clamping
    float4 diffM1 = 0;
    float4 diffM2 = 0;

    float4 specM1 = 0;
    float4 specM2 = 0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 pos = threadId + int2( dx, dy );

            #if( defined REBLUR_DIFFUSE )
                float4 d = s_Diff[ pos.y ][ pos.x ];
                diffM1 += d;
                diffM2 += d * d;
            #endif

            #if( defined REBLUR_SPECULAR )
                float4 s = s_Spec[ pos.y ][ pos.x ];
                specM1 += s;
                specM2 += s * s;
            #endif
        }
    }

    float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );

    #if( defined REBLUR_DIFFUSE )
        diffM1 *= invSum;
        diffM2 *= invSum;
        float4 diffSigma = GetStdDev( diffM1, diffM2 );
        float2 diffSigmaScale = gDiffFastHistoryClampingColorBoxSigmaScale * REBLUR_HIT_DIST_ACCELERATION;
        float4 diffMin = diffM1 - diffSigmaScale.xxxy * diffSigma;
        float4 diffMax = diffM1 + diffSigmaScale.xxxy * diffSigma;
        float4 diffCenter = s_Diff[ threadId.y + BORDER ][ threadId.x + BORDER ];
        diffMin = min( diffMin, diffCenter );
        diffMax = max( diffMax, diffCenter );
        float4 diffClamped = clamp( diff, diffMin, diffMax );

        diff = lerp( diffClamped, diff, GetFastHistoryFactor( diffInternalData.y, gDiffMaxFastAccumulatedFrameNum ) );
    #endif

    #if( defined REBLUR_SPECULAR )
        specM1 *= invSum;
        specM2 *= invSum;
        float4 specSigma = GetStdDev( specM1, specM2 );
        float2 specSigmaScale = gSpecFastHistoryClampingColorBoxSigmaScale * REBLUR_HIT_DIST_ACCELERATION;
        float4 specMin = specM1 - specSigmaScale.xxxy * specSigma;
        float4 specMax = specM1 + specSigmaScale.xxxy * specSigma;
        float4 specCenter = s_Spec[ threadId.y + BORDER ][ threadId.x + BORDER ];
        specMin = min( specMin, specCenter );
        specMax = max( specMax, specCenter );
        float4 specClamped = clamp( spec, specMin, specMax );

        float maxFastAccumSpeedRoughnessAdjusted = gSpecMaxFastAccumulatedFrameNum * GetSpecMagicCurve( roughness );
        spec = lerp( specClamped, spec, GetFastHistoryFactor( specInternalData.y, maxFastAccumSpeedRoughnessAdjusted ) );
    #endif

    // Output
    #if( defined REBLUR_DIFFUSE )
        gOut_Diff[ pixelPos ] = diff;
    #endif

    #if( defined REBLUR_SPECULAR )
        gOut_Spec[ pixelPos ] = spec;
    #endif
}
