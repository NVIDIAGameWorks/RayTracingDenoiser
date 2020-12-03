/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    float4x4 gViewToClip;
    float4 gFrustum;
    float2 gInvScreenSize;
    float2 gScreenSize;
    float gMetersToUnits;
    float gIsOrtho;
    float gUnproject;
    float gDebug;
    float gInf;
    float gReference;
    uint gFrameIndex;
    uint gWorldSpaceMotion;

    uint2 gScreenSizei;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 0, 0 );
NRI_RESOURCE( Texture2D<uint>, gIn_InternalData, t, 1, 0 );
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 2, 0 ); // mips 0-4
NRI_RESOURCE( Texture2D<float4>, gIn_Diff, t, 3, 0 );  // mips 1-4, mip = 0 actually samples from mip#1!
NRI_RESOURCE( Texture2D<float4>, gIn_Spec, t, 4, 0 ); // mips 1-4, mip = 0 actually samples from mip#1!

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_Diff, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_Spec, u, 1, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;
    float scaledViewZ = gIn_ScaledViewZ[ pixelPos ];

    // Debug
    #if( SHOW_MIPS != 0 )
    {
        int realMipLevel = int( gDebug * MIP_NUM );
        int mipLevel = realMipLevel - 1;
        if( realMipLevel == 0 )
            return;

        float2 mipSize = float2( gScreenSizei >> realMipLevel );
        float2 mipUv = pixelUv * gScreenSize / ( mipSize * float( 1 << realMipLevel ) );

        #if( SHOW_MIPS == 1 )
            float4 diff = gIn_Diff.SampleLevel( gLinearClamp, mipUv, mipLevel );
            float4 spec = gIn_Spec.SampleLevel( gLinearClamp, mipUv, mipLevel );
        #else
            STL::Filtering::Bilinear filter = STL::Filtering::GetBilinearFilter( mipUv, mipSize );
            float4 bilinearWeights = STL::Filtering::GetBilinearCustomWeights( filter, 1.0 );
            float2 mipUvFootprint00 = ( filter.origin + 0.5 ) / mipSize;

            float4 z;
            z.x = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, realMipLevel );
            z.y = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, realMipLevel, int2( 1, 0 ) );
            z.z = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, realMipLevel, int2( 0, 1 ) );
            z.w = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, realMipLevel, int2( 1, 1 ) );

            float4 d00 = gIn_Diff.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel );
            float4 d10 = gIn_Diff.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 0 ) );
            float4 d01 = gIn_Diff.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 0, 1 ) );
            float4 d11 = gIn_Diff.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 1 ) );

            float4 s00 = gIn_Spec.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel );
            float4 s10 = gIn_Spec.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 0 ) );
            float4 s01 = gIn_Spec.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 0, 1 ) );
            float4 s11 = gIn_Spec.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 1 ) );

            float4 bilateralWeights = GetBilateralWeight( z, scaledViewZ );
            float4 w = bilinearWeights * bilateralWeights;

            float4 diff = STL::Filtering::ApplyBilinearCustomWeights( d00, d10, d01, d11, w );
            float4 spec = STL::Filtering::ApplyBilinearCustomWeights( s00, s10, s01, s11, w );
        #endif

        gOut_Diff[ pixelPos ] = diff;
        gOut_Spec[ pixelPos ] = spec;

        return;
    }
    #endif

    float roughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] ).w;
    float2x3 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ pixelPos ], roughness );

    float3 diffInternalData = internalData[ 0 ];
    float diffNormAccumSpeed = saturate( diffInternalData.x * STL::Math::PositiveRcp( diffInternalData.y * HISTORY_FIX_FRAME_NUM_PERCENTAGE ) ); // .x instead of .z in specular to get free upsampling if maxAccumulatedFrameNum < 5 (it doesn't work for specular)
    float diffRealMipLevelf = GetMipLevel( diffNormAccumSpeed, diffInternalData.z );
    uint diffRealMipLevel = uint( diffRealMipLevelf );

    float3 specInternalData = internalData[ 1 ];
    float specNormAccumSpeed = saturate( specInternalData.z * STL::Math::PositiveRcp( specInternalData.y * HISTORY_FIX_FRAME_NUM_PERCENTAGE ) ); // .x instead of .z can't be used here due to adaptive number of accumulated frames
    float specRealMipLevelf = GetMipLevel( specNormAccumSpeed, specInternalData.z, roughness );
    uint specRealMipLevel = uint( specRealMipLevelf );

    // Diffuse
    [branch]
    if( diffRealMipLevel != 0 && USE_HISTORY_FIX != 0 )
    {
        float sum;
        float4 blurry = ReconstructHistory( diffRealMipLevel, gScreenSizei, pixelUv, scaledViewZ, gIn_ScaledViewZ, gIn_Diff, sum );

        #if( USE_MIX_WITH_ORIGINAL == 1 )
            float4 original = gOut_Diff[ pixelPos ];
            blurry = lerp( blurry, original, diffNormAccumSpeed );
        #endif

        #if( USE_WEIGHT_CUTOFF_FOR_HISTORY_FIX == 1 )
            [branch]
            if( sum != 0 )
        #endif
                gOut_Diff[ pixelPos ] = blurry;
    }

    // Specular
    [branch]
    if( specRealMipLevel != 0 && USE_HISTORY_FIX != 0 )
    {
        float sum;
        float4 blurry = ReconstructHistory( specRealMipLevel, gScreenSizei, pixelUv, scaledViewZ, gIn_ScaledViewZ, gIn_Spec, sum );

        #if( USE_MIX_WITH_ORIGINAL == 1 )
            float4 original = gOut_Spec[ pixelPos ];
            blurry = lerp( blurry, original, specNormAccumSpeed );
        #endif

        #if( USE_WEIGHT_CUTOFF_FOR_HISTORY_FIX == 1 )
            [branch]
            if( sum != 0 )
        #endif
                gOut_Spec[ pixelPos ] = blurry;
    }
}
