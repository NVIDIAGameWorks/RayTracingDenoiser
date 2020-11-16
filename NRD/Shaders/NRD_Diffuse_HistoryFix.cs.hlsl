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
NRI_RESOURCE( Texture2D<float2>, gIn_InternalData, t, 0, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_DiffA, t, 1, 0 );  // mips 1-4, mip = 0 actually samples from mip#1!
NRI_RESOURCE( Texture2D<float4>, gIn_DiffB, t, 2, 0 );  // mips 1-4, mip = 0 actually samples from mip#1!

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_DiffA, u, 0, 0 );
NRI_RESOURCE( RWTexture2D<float4>, gOut_DiffB, u, 1, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Debug
    #if( SHOW_MIPS != 0 )
    {
        int realMipLevel = int( gDebug * MIP_NUM );
        int mipLevel = realMipLevel - 1;
        if( realMipLevel == 0 )
            return;

        float2 mipSize = float2( gScreenSizei >> realMipLevel );
        float2 mipUv = pixelUv * gScreenSize / ( mipSize * float( 1 << realMipLevel ) );

        float4 originalB = gOut_DiffB[ pixelPos ];
        float scaledViewZ = originalB.w;

        #if( SHOW_MIPS == 1 )
            float4 finalA = gIn_DiffA.SampleLevel( gLinearClamp, mipUv, mipLevel );
            float4 finalB = gIn_DiffB.SampleLevel( gLinearClamp, mipUv, mipLevel );
        #else
            STL::Filtering::Bilinear filter = STL::Filtering::GetBilinearFilter( mipUv, mipSize );
            float4 bilinearWeights = STL::Filtering::GetBilinearCustomWeights( filter, 1.0 );
            float2 mipUvFootprint00 = ( filter.origin + 0.5 ) / mipSize;

            float4 a00 = gIn_DiffA.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel );
            float4 a10 = gIn_DiffA.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 0 ) );
            float4 a01 = gIn_DiffA.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 0, 1 ) );
            float4 a11 = gIn_DiffA.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 1 ) );

            float4 b00 = gIn_DiffB.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel );
            float4 b10 = gIn_DiffB.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 0 ) );
            float4 b01 = gIn_DiffB.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 0, 1 ) );
            float4 b11 = gIn_DiffB.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 1 ) );

            float4 z = float4( b00.w, b10.w, b01.w, b11.w );
            float4 bilateralWeights = GetBilateralWeight( z, scaledViewZ );
            float4 w = bilinearWeights * bilateralWeights;

            float4 finalA = STL::Filtering::ApplyBilinearCustomWeights( a00, a10, a01, a11, w );
            float4 finalB = STL::Filtering::ApplyBilinearCustomWeights( b00, b10, b01, b11, w );
        #endif

        finalB.w = scaledViewZ;

        gOut_DiffA[ pixelPos ] = finalA;
        gOut_DiffB[ pixelPos ] = finalB;

        return;
    }
    #endif

    float3 diffInternalData = UnpackDiffInternalData( gIn_InternalData[ pixelPos ] );
    float diffNormAccumSpeed = saturate( diffInternalData.x * STL::Math::PositiveRcp( diffInternalData.y * HISTORY_FIX_FRAME_NUM_PERCENTAGE ) ); // .x instead of .z in specular to get free upsampling if maxAccumulatedFrameNum < 5 (it doesn't work for specular)
    float diffRealMipLevelf = GetMipLevel( diffNormAccumSpeed, diffInternalData.z );
    uint diffRealMipLevel = uint( diffRealMipLevelf );

    [branch]
    if( diffRealMipLevel == 0 || USE_HISTORY_FIX == 0 )
        return;

    float4 originalB = gOut_DiffB[ pixelPos ];
    float scaledViewZ = originalB.w;
    float4 blurryA = 0;
    float4 blurryB = 0;
    float sum = 0;

#if( USE_BILATERAL_WEIGHT_CUTOFF_FOR_DIFF == 1 )
    while( sum == 0.0 && diffRealMipLevel != 0 )
    {
#endif
        uint mipLevel = diffRealMipLevel - 1;
        float2 mipSize = float2( gScreenSizei >> diffRealMipLevel );
        float2 invMipSize = 1.0 / mipSize;
        float2 mipUv = pixelUv * gScreenSize / ( mipSize * float( 1 << diffRealMipLevel ) );

        STL::Filtering::Bilinear filter = STL::Filtering::GetBilinearFilter( mipUv, mipSize );
        float4 bilinearWeights = STL::Filtering::GetBilinearCustomWeights( filter, 1.0 );
        float2 mipUvFootprint00 = saturate( ( filter.origin + 0.5 ) * invMipSize );

        [unroll]
        for( int i = 0; i < 2; i++ )
        {
            [unroll]
            for( int j = 0; j < 2; j++ )
            {
                const float2 offset = float2( i, j ) * 2.0 - 1.0;
                const float2 uv = saturate( mipUvFootprint00 + offset * invMipSize );

                float4 a00 = gIn_DiffA.SampleLevel( gNearestClamp, uv, mipLevel );
                float4 a10 = gIn_DiffA.SampleLevel( gNearestClamp, uv, mipLevel, int2( 1, 0 ) );
                float4 a01 = gIn_DiffA.SampleLevel( gNearestClamp, uv, mipLevel, int2( 0, 1 ) );
                float4 a11 = gIn_DiffA.SampleLevel( gNearestClamp, uv, mipLevel, int2( 1, 1 ) );

                float4 b00 = gIn_DiffB.SampleLevel( gNearestClamp, uv, mipLevel );
                float4 b10 = gIn_DiffB.SampleLevel( gNearestClamp, uv, mipLevel, int2( 1, 0 ) );
                float4 b01 = gIn_DiffB.SampleLevel( gNearestClamp, uv, mipLevel, int2( 0, 1 ) );
                float4 b11 = gIn_DiffB.SampleLevel( gNearestClamp, uv, mipLevel, int2( 1, 1 ) );

                #if( USE_BILATERAL_WEIGHT_CUTOFF_FOR_DIFF == 1 )
                    float cutoff = BILATERAL_WEIGHT_CUTOFF; // TODO: slope scale is needed, but no normals here...
                #else
                    float cutoff = 99999.0;
                #endif

                float4 z = float4( b00.w, b10.w, b01.w, b11.w );
                float4 bilateralWeights = GetBilateralWeight( z, scaledViewZ, cutoff );
                float4 w = bilinearWeights * bilateralWeights;

                blurryA += STL::Filtering::ApplyBilinearCustomWeights( a00, a10, a01, a11, w, false );
                blurryB += STL::Filtering::ApplyBilinearCustomWeights( b00, b10, b01, b11, w, false );

                sum += dot( w, 1.0 );
            }
        }

#if( USE_BILATERAL_WEIGHT_CUTOFF_FOR_DIFF == 1 )
        diffRealMipLevel--;
    }
#endif

    float invSum = STL::Math::PositiveRcp( sum );
    blurryA *= invSum;
    blurryB *= invSum;

    #if( USE_MIX_WITH_ORIGINAL == 1 )
        float4 originalA = gOut_DiffA[ pixelPos ];
        blurryA = lerp( blurryA, originalA, diffNormAccumSpeed );
        blurryB.xyz = lerp( blurryB.xyz, originalB.xyz, diffNormAccumSpeed );
    #endif

#if( USE_BILATERAL_WEIGHT_CUTOFF_FOR_DIFF == 1 )
    [branch]
    if( sum != 0 )
#endif
    {
        gOut_DiffA[ pixelPos ] = blurryA;
        gOut_DiffB[ pixelPos ] = float4( blurryB.xyz, scaledViewZ );
    }
}
