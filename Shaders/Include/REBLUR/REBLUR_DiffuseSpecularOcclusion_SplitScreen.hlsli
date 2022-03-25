/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

[numthreads( GROUP_X, GROUP_Y, 1)]
NRD_EXPORT void NRD_CS_MAIN( uint2 pixelPos : SV_DispatchThreadId)
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    uint2 pixelPosUser = gRectOrigin + pixelPos;

    if( pixelUv.x > gSplitScreen )
        return;

    float viewZ = gIn_ViewZ[ pixelPosUser ];
    uint2 checkerboardPos = pixelPos;

    #if( defined REBLUR_DIFFUSE )
        checkerboardPos.x = pixelPos.x >> ( gDiffCheckerboard != 2 ? 1 : 0 );
        float diffResult = gIn_Diff[ gRectOrigin + checkerboardPos ];
        gOut_Diff[ pixelPos ] = diffResult * float( viewZ < gDenoisingRange );
    #endif

    #if( defined REBLUR_SPECULAR )
        checkerboardPos.x = pixelPos.x >> ( gSpecCheckerboard != 2 ? 1 : 0 );
        float specResult = gIn_Spec[ gRectOrigin + checkerboardPos ];
        gOut_Spec[ pixelPos ] = specResult * float( viewZ < gDenoisingRange );
    #endif
}
