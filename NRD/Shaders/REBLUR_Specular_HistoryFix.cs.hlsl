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
    REBLUR_SPEC_SHARED_CB_DATA;

    float4x4 gWorldToView;
    float4 gRotator;
    float gSpecMaxFastAccumulatedFrameNum;
    float gSpecFastHistoryClampingColorBoxSigmaScale;
    uint gSpecAntiFirefly;
};

#if( REBLUR_USE_5X5_HISTORY_CLAMPING == 1 )
    #define NRD_USE_BORDER_2
#endif
#include "NRD_Common.hlsl"

#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_InternalData, t, 1, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 2, 0 ); // mips 0+
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 3, 0 ); // mips 1+, mip = 0 actually samples from mip#1!
NRI_RESOURCE( Texture2D<float4>, gIn_Fast_Spec, t, 4, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 0, 0 );

groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    s_Spec[ sharedId.y ][ sharedId.x ] = gIn_Fast_Spec[ globalId ];
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
    float roughness = normalAndRoughness.w;

    float2 specInternalData = UnpackSpecInternalData( gIn_InternalData[ pixelPos ], roughness );
    float4 spec = ReconstructHistorySpec( specInternalData, pixelPos, scaledViewZ, N, gRotator, gWorldToView, gIn_ScaledViewZ, gIn_Spec, roughness, gIn_Normal_Roughness );

    // History clamping
    #if( REBLUR_USE_FAST_HISTORY == 1 )
        float4 specM1 = 0;
        float4 specM2 = 0;
        float4 specMaxInput = -NRD_INF;
        float4 specMinInput = NRD_INF;

        [unroll]
        for( int dy = 0; dy <= BORDER * 2; dy++ )
        {
            [unroll]
            for( int dx = 0; dx <= BORDER * 2; dx++ )
            {
                int2 pos = threadId + int2( dx, dy );

                float4 s = s_Spec[ pos.y ][ pos.x ];
                specM1 += s;
                specM2 += s * s;

                if( dx != BORDER || dy != BORDER )
                {
                    specMaxInput = max( specMaxInput, s );
                    specMinInput = min( specMinInput, s );
                }
            }
        }

        float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );

        #if( REBLUR_USE_ANTI_FIREFLY == 1 )
            [flatten]
            if( gSpecAntiFirefly != 0 )
                spec = clamp( spec, specMinInput, specMaxInput );
        #endif

        specM1 *= invSum;
        specM2 *= invSum;
        float4 specSigma = GetStdDev( specM1, specM2 );
        float4 specMin = specM1 - gSpecFastHistoryClampingColorBoxSigmaScale * specSigma;
        float4 specMax = specM1 + gSpecFastHistoryClampingColorBoxSigmaScale * specSigma;
        float4 specCenter = s_Spec[ threadId.y + BORDER ][ threadId.x + BORDER ];
        specMin = min( specMin, specCenter );
        specMax = max( specMax, specCenter );
        float4 specClamped = clamp( spec, specMin, specMax );

        [flatten]
        if( gSpecMaxFastAccumulatedFrameNum < gSpecMaxAccumulatedFrameNum )
            spec = lerp( specClamped, spec, specInternalData.x );
    #endif

    gOut_Spec[ pixelPos ] = CompressRadianceAndNormHitDist( spec.xyz, spec.w, viewZ, gSpecHitDistParams, roughness );
}
