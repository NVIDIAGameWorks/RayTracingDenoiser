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
#include "REBLUR_DiffuseSpecularOcclusion_HistoryFix.resources.hlsli"

NRD_DECLARE_CONSTANTS

/*
#if( REBLUR_USE_5X5_HISTORY_CLAMPING == 1 ) // TODO: not needed for occlusion?
    #define NRD_USE_BORDER_2
#endif
*/

#include "NRD_Common.hlsli"

NRD_DECLARE_SAMPLERS

#include "REBLUR_Common.hlsli"

NRD_DECLARE_INPUT_TEXTURES
NRD_DECLARE_OUTPUT_TEXTURES

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
    #define TYPE float2
#else
    #define TYPE float
#endif

groupshared TYPE s_FastHistory[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    #if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
        s_FastHistory[ sharedId.y ][ sharedId.x ] = float2( gIn_Fast_Diff[ globalId ], gIn_Fast_Spec[ globalId ] );
    #elif( defined REBLUR_DIFFUSE )
        s_FastHistory[ sharedId.y ][ sharedId.x ] = gIn_Fast_Diff[ globalId ];
    #else
        s_FastHistory[ sharedId.y ][ sharedId.x ] = gIn_Fast_Spec[ globalId ];
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    float scaledViewZ;

    #if( defined REBLUR_DIFFUSE )
        float2 diffData = gIn_Diff[ pixelPos ];
        scaledViewZ = diffData.y;
    #endif

    #if( defined REBLUR_SPECULAR )
        float2 specData = gIn_Spec[ pixelPos ];
        scaledViewZ = specData.y;
    #endif

    [branch]
    if( scaledViewZ / NRD_FP16_VIEWZ_SCALE > gInf )
        return;

    // Internal data
    float curvature, roughness; // yes, roughness not virtualMotionAmount!
    #if( defined REBLUR_SPECULAR )
        float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], curvature, roughness );
        float2 diffInternalData = internalData.xy;
        float2 specInternalData = internalData.zw;
    #else
        float2 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ].xy, curvature );
    #endif

    // History reconstruction
    #if( defined REBLUR_DIFFUSE )
        float diff = ReconstructHistory( diffData, diffInternalData.y, 1.0, pixelPos, pixelUv, gDiffMaxFastAccumulatedFrameNum, gIn_Diff );
    #endif

    #if( defined REBLUR_SPECULAR )
        float spec = ReconstructHistory( specData, specInternalData.y, roughness, pixelPos, pixelUv, gSpecMaxFastAccumulatedFrameNum, gIn_Spec );
    #endif

    // History clamping
    TYPE m1 = 0;
    TYPE m2 = 0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 pos = threadId + int2( dx, dy );

            TYPE s = s_FastHistory[ pos.y ][ pos.x ];
            m1 += s;
            m2 += s * s;
        }
    }

    float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );
    m1 *= invSum;
    m2 *= invSum;

    TYPE sigma = GetStdDev( m1, m2 );
    TYPE signalMin = m1 - 0.5 * gSigmaScale * sigma;
    TYPE signalMax = m1 + 0.5 * gSigmaScale * sigma;

    TYPE signalCenter = s_FastHistory[ threadId.y + BORDER ][ threadId.x + BORDER ];
    signalMin = min( signalMin, signalCenter );
    signalMax = max( signalMax, signalCenter );

    #if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
        float diffClamped = clamp( diff, signalMin.x, signalMax.x );
        float specClamped = clamp( spec, signalMin.y, signalMax.y );
    #elif( defined REBLUR_DIFFUSE )
        float diffClamped = clamp( diff, signalMin, signalMax );
    #else
        float specClamped = clamp( spec, signalMin, signalMax );
    #endif

    #if( defined REBLUR_DIFFUSE )
        diff = lerp( diffClamped, diff, GetFastHistoryFactor( diffInternalData.y, gDiffMaxFastAccumulatedFrameNum ) );
    #endif

    #if( defined REBLUR_SPECULAR )
        float maxFastAccumSpeedRoughnessAdjusted = gSpecMaxFastAccumulatedFrameNum * GetSpecMagicCurve( roughness );
        spec = lerp( specClamped, spec, GetFastHistoryFactor( specInternalData.y, maxFastAccumSpeedRoughnessAdjusted ) );
    #endif

    // Output
    #if( defined REBLUR_DIFFUSE )
        gOut_Diff[ pixelPos ] = float2( diff, scaledViewZ );
    #endif

    #if( defined REBLUR_SPECULAR )
        gOut_Spec[ pixelPos ] = float2( spec, scaledViewZ );
    #endif
}
