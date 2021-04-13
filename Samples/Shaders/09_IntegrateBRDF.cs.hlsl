/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"
#include "STL.hlsl"

NRI_RESOURCE( RWTexture2D<float2>, gOutput, u, 0, 0 );

[numthreads( 16, 16, 1 )]
void main( uint2 pixelPos : SV_DispatchThreadId )
{
    const uint SAMPLE_NUM = 16384;
    const float invTexSize = 1.0 / 256.0;

    float NoV = float( pixelPos.x + 0.5 ) * invTexSize;
    float roughness = float( pixelPos.y + 0.5 ) * invTexSize;

    float3 V;
    V.x = STL::Math::Sqrt01( 1.0 - NoV * NoV );
    V.y = 0.0;
    V.z = NoV;

    STL::Rng::Initialize( pixelPos, 12389517 );

    float2 GG = 0.0;
    [loop]
    for( uint i = 0; i < SAMPLE_NUM; i++ )
    {
        float2 rnd = STL::Rng::GetFloat2( );

        // Specular
        {
            float3 H = STL::ImportanceSampling::VNDF::GetRay( rnd, roughness, V );
            float3 L = reflect( -V, H );

            float NoL = saturate( L.z );

            GG.x += STL::BRDF::GeometryTerm_Smith( roughness, NoL );
        }

        // Diffuse
        {
            float3 L = STL::ImportanceSampling::Cosine::GetRay( rnd );
            float3 H = normalize( V + L );

            float NoL = saturate( L.z );
            float VoH = saturate( dot( V, H ) );

            float F = STL::BRDF::Pow5( VoH );
            float Kdiff = STL::BRDF::DiffuseTerm( roughness, NoL, NoV, VoH );

            // NoL gets canceled by PDF
            GG.y += ( 1.0 - F ) * Kdiff * STL::ImportanceSampling::Cosine::GetInversePDF( );
        }
    }

    GG /= SAMPLE_NUM;

    gOutput[ pixelPos ] = GG;
}
