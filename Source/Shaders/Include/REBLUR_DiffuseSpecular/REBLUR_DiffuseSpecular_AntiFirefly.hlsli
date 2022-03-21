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
#include "REBLUR_DiffuseSpecular/REBLUR_DiffuseSpecular_AntiFirefly.resources.hlsli"

NRD_DECLARE_CONSTANTS

#if( REBLUR_USE_5X5_ANTI_FIREFLY == 1 )
    #define NRD_USE_BORDER_2
#endif

#include "NRD_Common.hlsli"

NRD_DECLARE_SAMPLERS

#include "REBLUR_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];
groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );

    #if( defined REBLUR_DIFFUSE )
        s_Diff[ sharedPos.y ][ sharedPos.x ] = gIn_Diff[ globalPos ];
    #endif

    #if( defined REBLUR_SPECULAR )
        s_Spec[ sharedPos.y ][ sharedPos.x ] = gIn_Spec[ globalPos ];
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    PRELOAD_INTO_SMEM;

    // Early out
    float scaledViewZ = gIn_ScaledViewZ[ pixelPos ];
    float viewZ = scaledViewZ / NRD_FP16_VIEWZ_SCALE;

    [branch]
    if( viewZ > gDenoisingRange )
        return;

    // Anti-firefly ( not needed for hit distance )
    float3 diffMaxInput = -NRD_INF;
    float3 diffMinInput = NRD_INF;

    float3 specMaxInput = -NRD_INF;
    float3 specMinInput = NRD_INF;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadPos + int2( dx, dy );

            #if( defined REBLUR_DIFFUSE )
                float3 d = s_Diff[ pos.y ][ pos.x ].xyz;
                diffMaxInput = max( diffMaxInput, d );
                diffMinInput = min( diffMinInput, d );
            #endif

            #if( defined REBLUR_SPECULAR )
                float3 s = s_Spec[ pos.y ][ pos.x ].xyz;
                specMaxInput = max( specMaxInput, s );
                specMinInput = min( specMinInput, s );
            #endif
        }
    }

    #if( defined REBLUR_DIFFUSE )
        float4 diff = s_Diff[ threadPos.y + BORDER ][ threadPos.x + BORDER ];

        float3 diffClamped = clamp( diff.xyz, diffMinInput, diffMaxInput );
        diff.xyz = diffClamped;

        gOut_Diff[ pixelPos ] = diff;
    #endif

    #if( defined REBLUR_SPECULAR )
        uint2 pixelPosUser = gRectOrigin + pixelPos;
        float roughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ] ).w;

        float4 spec = s_Spec[ threadPos.y + BORDER ][ threadPos.x + BORDER ];

        float3 specClamped = clamp( spec.xyz, specMinInput, specMaxInput );
        spec.xyz = lerp( spec.xyz, specClamped, GetSpecMagicCurve( roughness ) );

        gOut_Spec[ pixelPos ] = spec;
    #endif
}
