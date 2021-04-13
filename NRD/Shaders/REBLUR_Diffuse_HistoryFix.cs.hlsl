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

    uint2 gScreenSizei;
    uint gDiffAntiFirefly;
};

#if( REBLUR_5X5_HISTORY_CLAMPING == 1 )
    #define NRD_USE_BORDER_2
#endif
#include "NRD_Common.hlsl"

#include "REBLUR_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float2>, gIn_InternalData, t, 0, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 1, 0 ); // mips 0+
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 2, 0 );  // mips 1+, mip = 0 actually samples from mip#1!
NRI_RESOURCE( Texture2D<float4>, gIn_Fast_Diff, t, 3, 0 );

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
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    float scaledViewZ = gIn_ScaledViewZ[ pixelPos ];
    float viewZ = scaledViewZ / NRD_FP16_VIEWZ_SCALE;

    [branch]
    if( viewZ > gInf )
        return;

    // Debug
    #if( REBLUR_DEBUG == REBLUR_SHOW_MIPS )
    {
        int realMipLevel = int( gDebug * REBLUR_MIP_NUM );
        int mipLevel = realMipLevel - 1;
        if( realMipLevel == 0 )
            return;

        float2 mipSize = float2( gScreenSizei >> realMipLevel );
        float2 mipUv = pixelUv * gScreenSize / ( mipSize * float( 1 << realMipLevel ) );

        float2 mipUvScaled = mipUv * gResolutionScale;
        float4 diff = gIn_Diff.SampleLevel( gLinearClamp, mipUvScaled, mipLevel );

        gOut_Diff[ pixelPos ] = diff;

        return;
    }
    #endif

    float2 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ] );
    float diffRealMipLevelf = GetMipLevel( diffInternalData.y );
    uint diffRealMipLevel = uint( diffRealMipLevelf );

    // Local variance
    #if( REBLUR_USE_FAST_HISTORY == 1 )
        float4 diffM1 = 0;
        float4 diffM2 = 0;

        float4 diffMaxInput = -INF;
        float4 diffMinInput = INF;

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

        diffM1 *= invSum;
        diffM2 *= invSum;
        float4 diffSigma = GetStdDev( diffM1, diffM2 );
        float4 diff = gOut_Diff[ pixelPos ];

        #if( REBLUR_USE_ANTI_FIREFLY == 1 )
            [flatten]
            if( gDiffAntiFirefly != 0 )
                diff = clamp( diff, diffMinInput, diffMaxInput );
        #endif
    #endif

    // Diffuse
    [branch]
    if( diffRealMipLevel != 0 && REBLUR_USE_HISTORY_FIX != 0 )
    {
        #if( REBLUR_USE_FAST_HISTORY == 0 )
            float4 diff = gOut_Diff[ pixelPos ];
        #endif

        float4 blurry = ReconstructHistory( diff, diffRealMipLevel, gScreenSizei, pixelUv, scaledViewZ, gIn_ScaledViewZ, gIn_Diff );
        diff = lerp( diff, blurry, diffInternalData.x );

        #if( REBLUR_USE_FAST_HISTORY == 0 )
            gOut_Diff[ pixelPos ] = diff;
        #endif
    }

    #if( REBLUR_USE_FAST_HISTORY == 1 )
        float4 diffClamped = STL::Color::Clamp( diffM1, REBLUR_FAST_HISTORY_SIGMA_AMPLITUDE * diffSigma, diff );
        float diffFactor = saturate( 1.0 - diffRealMipLevelf / REBLUR_MIP_NUM );

        diff = lerp( diff, diffClamped, diffFactor * float( gDiffMaxFastAccumulatedFrameNum < gDiffMaxAccumulatedFrameNum ) );

        gOut_Diff[ pixelPos ] = diff;
    #endif
}
