/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float4 s_Normal_Roughness[ BUFFER_Y ][ BUFFER_X ];
groupshared float3 s_HitDist_ViewZ[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );
    uint2 globalIdUser = gRectOrigin + globalPos;

    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ globalIdUser ] );
    float viewZ = abs( gIn_ViewZ[ globalIdUser ] );

    float2 hitDist = 1.0;
    #if( defined REBLUR_DIFFUSE )
        #ifdef REBLUR_OCCLUSION
            hitDist.x = gIn_Diff[ globalPos ].x;
        #else
            hitDist.x = gIn_Diff[ globalPos ].w;
        #endif
        hitDist.x *= _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, 1.0 );
    #endif

    #if( defined REBLUR_SPECULAR )
        #ifdef REBLUR_OCCLUSION
            hitDist.y = gIn_Spec[ globalPos ].x;
        #else
            hitDist.y = gIn_Spec[ globalPos ].w;
        #endif
        hitDist.y *= _REBLUR_GetHitDistanceNormalization( viewZ, gHitDistParams, normalAndRoughness.w );
    #endif

    s_Normal_Roughness[ sharedPos.y ][ sharedPos.x ] = normalAndRoughness;
    s_HitDist_ViewZ[ sharedPos.y ][ sharedPos.x ] = float3( hitDist, viewZ );
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Early out
    int2 smemPos = threadPos + BORDER;
    float3 center = s_HitDist_ViewZ[ smemPos.y ][ smemPos.x ];

    [branch]
    if( center.z > gDenoisingRange )
        return;

    // Center data
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ] );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    #if( defined REBLUR_DIFFUSE )
        #ifndef REBLUR_OCCLUSION
            float3 diff = gIn_Diff[ pixelPos ].xyz;
        #endif
    #endif

    #if( defined REBLUR_SPECULAR )
        #ifndef REBLUR_OCCLUSION
            float3 spec = gIn_Spec[ pixelPos ].xyz;
        #endif
    #endif

    // Hit distance reconstruction
    float2 sum = 1000.0 * float2( center.xy != 0.0 );
    center.xy *= sum;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 o = int2( dx, dy ) - BORDER;

            #ifdef REBLUR_PERFORMANCE_MODE
                if( ( o.x == 0 && o.y == 0 ) || abs( o.x ) + abs( o.y ) > 1 ) // skip corners
                    continue;
            #else
                if( o.x == 0 && o.y == 0 )
                    continue;
            #endif

            int2 pos = threadPos + int2( dx, dy );
            float4 normalAndRoughness = s_Normal_Roughness[ pos.y ][ pos.x ];
            float3 temp = s_HitDist_ViewZ[ pos.y ][ pos.x ];

            float w = IsInScreen( pixelUv + o * gInvRectSize );
            w *= GetGaussianWeight( length( o ) * 0.5 );
            w *= GetBilateralWeight( temp.z, center.z );
            w *= GetEncodingAwareNormalWeight( normalAndRoughness.xyz, N, STL::Math::Pi( 0.5 ) ); // TODO: use diffuse and specular lobe angle? roughness weight for specular?

            float2 ww = w * float2( temp.xy != 0.0 );

            center.xy += temp.xy * ww;
            sum += ww;
        }
    }

    // Normalize weighted sum
    center.xy /= max( sum, 1e-6 );

    // Return back to normalized hit distances
    center.x /= _REBLUR_GetHitDistanceNormalization( center.z, gHitDistParams, 1.0 );
    center.y /= _REBLUR_GetHitDistanceNormalization( center.z, gHitDistParams, roughness );

    // Output
    #if( defined REBLUR_DIFFUSE )
        #ifdef REBLUR_OCCLUSION
            gOut_Diff[ pixelPos ] = center.x;
        #else
            gOut_Diff[ pixelPos ] = float4( diff, center.x );
        #endif
    #endif

    #if( defined REBLUR_SPECULAR )
        #ifdef REBLUR_OCCLUSION
            gOut_Spec[ pixelPos ] = center.y;
        #else
            gOut_Spec[ pixelPos ] = float4( spec, center.y );
        #endif
    #endif
}
