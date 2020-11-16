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
NRI_RESOURCE( Texture2D<float3>, gIn_InternalData, t, 0, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_Normal_Roughness, t, 1, 0 );
NRI_RESOURCE( Texture2D<float4>, gIn_SpecHit, t, 2, 0 ); // mips 1-4, mip = 0 actually samples from mip#1!
NRI_RESOURCE( Texture2D<float>, gIn_ScaledViewZ, t, 3, 0 ); // mips 0-4

// Outputs
NRI_RESOURCE( RWTexture2D<float4>, gOut_SpecHit, u, 0, 0 );

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvScreenSize;

    // Debug
    #if( SHOW_MIPS != 0 )
    {
        int specRealMipLevel = int( gDebug * MIP_NUM );
        int mipLevel = specRealMipLevel - 1;
        if( specRealMipLevel == 0 )
            return;

        float2 mipSize = float2( gScreenSizei >> specRealMipLevel );
        float2 mipUv = pixelUv * gScreenSize / ( mipSize * float( 1 << specRealMipLevel ) );

        #if( SHOW_MIPS == 1 )
            float4 final = gIn_SpecHit.SampleLevel( gLinearClamp, mipUv, mipLevel );
        #else
            STL::Filtering::Bilinear filter = STL::Filtering::GetBilinearFilter( mipUv, mipSize );
            float4 bilinearWeights = STL::Filtering::GetBilinearCustomWeights( filter, 1.0 );
            float2 mipUvFootprint00 = ( filter.origin + 0.5 ) / mipSize;

            float4 z;
            z.x = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, specRealMipLevel );
            z.y = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, specRealMipLevel, int2( 1, 0 ) );
            z.z = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, specRealMipLevel, int2( 0, 1 ) );
            z.w = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, specRealMipLevel, int2( 1, 1 ) );

            float4 s00 = gIn_SpecHit.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel );
            float4 s10 = gIn_SpecHit.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 0 ) );
            float4 s01 = gIn_SpecHit.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 0, 1 ) );
            float4 s11 = gIn_SpecHit.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 1 ) );

            float scaledViewZ = gIn_ScaledViewZ[ pixelPos ];

            float4 bilateralWeights = GetBilateralWeight( z, scaledViewZ );
            float4 w = bilinearWeights * bilateralWeights;

            float4 final = STL::Filtering::ApplyBilinearCustomWeights( s00, s10, s01, s11, w );
        #endif

        gOut_SpecHit[ pixelPos ] = final;

        return;
    }
    #endif

    float roughness = _NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPos ] ).w;
    float3 specInternalData = UnpackSpecInternalData( gIn_InternalData[ pixelPos ], roughness );
    float specNormAccumSpeed = saturate( specInternalData.z * STL::Math::PositiveRcp( specInternalData.y * HISTORY_FIX_FRAME_NUM_PERCENTAGE ) ); // .x instead of .z can't be used here due to adaptive number of accumulated frames
    float specRealMipLevelf = GetMipLevel( specNormAccumSpeed, specInternalData.z, roughness );
    uint specRealMipLevel = uint( specRealMipLevelf );

    [branch]
    if( specRealMipLevel == 0 || USE_HISTORY_FIX == 0 )
        return;

    float scaledViewZ = gIn_ScaledViewZ[ pixelPos ];
    float4 blurry = 0;
    float sum = 0;

#if( USE_BILATERAL_WEIGHT_CUTOFF_FOR_SPEC == 1 )
    while( sum == 0.0 && specRealMipLevel != 0 )
    {
#endif
        uint mipLevel = specRealMipLevel - 1;
        float2 mipSize = float2( gScreenSizei >> specRealMipLevel );
        float2 invMipSize = 1.0 / mipSize;
        float2 mipUv = pixelUv * gScreenSize / ( mipSize * float( 1 << specRealMipLevel ) );

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

                float4 z;
                z.x = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, specRealMipLevel );
                z.y = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, specRealMipLevel, int2( 1, 0 ) );
                z.z = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, specRealMipLevel, int2( 0, 1 ) );
                z.w = gIn_ScaledViewZ.SampleLevel( gNearestClamp, mipUvFootprint00, specRealMipLevel, int2( 1, 1 ) );

                #if( USE_BILATERAL_WEIGHT_CUTOFF_FOR_SPEC == 1 )
                    float cutoff = BILATERAL_WEIGHT_CUTOFF; // TODO: slope scale is needed, but no normals here...
                #else
                    float cutoff = 99999.0;
                #endif

                float4 bilateralWeights = GetBilateralWeight( z, scaledViewZ, cutoff );
                float4 w = bilinearWeights * bilateralWeights;

                float4 s00 = gIn_SpecHit.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel );
                float4 s10 = gIn_SpecHit.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 0 ) );
                float4 s01 = gIn_SpecHit.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 0, 1 ) );
                float4 s11 = gIn_SpecHit.SampleLevel( gNearestClamp, mipUvFootprint00, mipLevel, int2( 1, 1 ) );

                blurry += STL::Filtering::ApplyBilinearCustomWeights( s00, s10, s01, s11, w, false );

                sum += dot( w, 1.0 );
            }
        }

#if( USE_BILATERAL_WEIGHT_CUTOFF_FOR_SPEC == 1 )
        specRealMipLevel--;
    }
#endif

    blurry *= STL::Math::PositiveRcp( sum );

    #if( USE_MIX_WITH_ORIGINAL == 1 )
        float4 original = gOut_SpecHit[ pixelPos ];
        blurry = lerp( blurry, original, specNormAccumSpeed );
    #endif

#if( USE_BILATERAL_WEIGHT_CUTOFF_FOR_SPEC == 1 )
    [branch]
    if( sum != 0 )
#endif
        gOut_SpecHit[ pixelPos ] = blurry;
}
