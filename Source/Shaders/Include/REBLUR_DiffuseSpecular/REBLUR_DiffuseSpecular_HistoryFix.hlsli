/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.hlsli"
#include "STL.hlsli"
#include "REBLUR_DiffuseSpecular/REBLUR_DiffuseSpecular_HistoryFix.resources.hlsli"

NRD_DECLARE_CONSTANTS

#include "NRD_Common.hlsli"

NRD_DECLARE_SAMPLERS

#include "REBLUR_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    // Copy history
    int2 prevHalfRectSize = int2( gRectSizePrev * 0.5 + 0.5 );

    if( all( gRectSize >= gRectSizePrev ) )
    {
        #if( defined REBLUR_DIFFUSE )
            gOut_HistoryStabilized_Diff[ pixelPos ] = gIn_HistoryStabilized_Diff[ pixelPos ];
        #endif

        #if( defined REBLUR_SPECULAR )
            gOut_HistoryStabilized_Spec[ pixelPos ] = gIn_HistoryStabilized_Spec[ pixelPos ];
        #endif
    }
    else if( pixelPos.x < prevHalfRectSize.x && pixelPos.y < prevHalfRectSize.y )
    {
        [unroll]
        for( int i = 0; i < 2; i++ )
        {
            [unroll]
            for( int j = 0; j < 2; j++ )
            {
                int2 p = pixelPos * 2 + int2( i, j );

                #if( defined REBLUR_DIFFUSE )
                    gOut_HistoryStabilized_Diff[ p ] = gIn_HistoryStabilized_Diff[ p ];
                #endif

                #if( defined REBLUR_SPECULAR )
                    gOut_HistoryStabilized_Spec[ p ] = gIn_HistoryStabilized_Spec[ p ];
                #endif
            }
        }
    }

    // Early out
    float scaledViewZ = gIn_ScaledViewZ[ pixelPos ];
    float viewZ = scaledViewZ / NRD_FP16_VIEWZ_SCALE;

    [branch]
    if( viewZ > gDenoisingRange || REBLUR_USE_HISTORY_FIX == 0 )
        return;

    // Internal data
    float unused;
    uint bits;
    float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], unused, bits );
    float2 diffInternalData = internalData.xy;
    float2 specInternalData = internalData.zw;

    // History reconstruction // materialID support? migrate to 5x5 filter?
    #if( defined REBLUR_DIFFUSE )
        float diffMipLevel = GetMipLevel( 1.0 ) * gHistoryFixStrength;
        ReconstructHistory( scaledViewZ, diffInternalData.y, diffMipLevel, pixelPos, pixelUv, gOut_Diff, gIn_Diff, gIn_ScaledViewZ );
    #endif

    #if( defined REBLUR_SPECULAR )
        float specMipLevel = float( bits & 7 ) * gHistoryFixStrength;
        ReconstructHistory( scaledViewZ, specInternalData.y, specMipLevel, pixelPos, pixelUv, gOut_Spec, gIn_Spec, gIn_ScaledViewZ );
    #endif
}
