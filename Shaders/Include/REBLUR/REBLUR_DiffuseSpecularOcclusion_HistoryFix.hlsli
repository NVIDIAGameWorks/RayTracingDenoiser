/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    float scaledViewZ;
    #if( defined REBLUR_DIFFUSE )
        float2 diffData = gOut_Diff[ pixelPos ];
        scaledViewZ = diffData.y;
    #endif

    #if( defined REBLUR_SPECULAR )
        float2 specData = gOut_Spec[ pixelPos ];
        scaledViewZ = specData.y;
    #endif

    // Internal data
    float unused;
    uint bits;
    float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], unused, bits );
    float2 diffInternalData = internalData.xy;
    float2 specInternalData = internalData.zw;

    bool isFixNeeded = false;
    #if( defined REBLUR_DIFFUSE )
        float diffMipLevel = GetMipLevel( 1.0 ) * gHistoryFixStrength;
        diffMipLevel = max( diffMipLevel - diffInternalData.y, 0.0 );
        diffMipLevel = floor( diffMipLevel );
        isFixNeeded = isFixNeeded || diffMipLevel != 0;
    #endif

    #if( defined REBLUR_SPECULAR )
        float specMipLevel = float( bits & 7 ) * gHistoryFixStrength;
        specMipLevel = max( specMipLevel - specInternalData.y, 0.0 );
        specMipLevel = floor( specMipLevel );
        isFixNeeded = isFixNeeded || specMipLevel != 0;
    #endif

    // Early out
    [branch]
    if( scaledViewZ / NRD_FP16_VIEWZ_SCALE > gDenoisingRange || REBLUR_USE_HISTORY_FIX == 0 )
        return;

    // History reconstruction // TODO: materialID support? migrate to 5x5 filter?
    #if( defined REBLUR_DIFFUSE )
        ReconstructHistory( diffData.x, diffData.y, diffInternalData.y, diffMipLevel, pixelPos, pixelUv, gOut_Diff, gIn_Diff );
    #endif

    #if( defined REBLUR_SPECULAR )
        ReconstructHistory( specData.x, specData.y, specInternalData.y, specMipLevel, pixelPos, pixelUv, gOut_Spec, gIn_Spec );
    #endif
}
