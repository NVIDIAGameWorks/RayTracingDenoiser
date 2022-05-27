/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifdef REBLUR_PERFORMANCE_MODE
    groupshared float4 s_Diff[ GROUP_Y ][ GROUP_X ];
    groupshared float4 s_Spec[ GROUP_Y ][ GROUP_X ];
    groupshared float s_ViewZ[ GROUP_Y ][ GROUP_X ];
#else
    groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];
    groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];
    groupshared float s_ViewZ[ BUFFER_Y ][ BUFFER_X ];
#endif

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );

    #if( defined REBLUR_DIFFUSE )
        s_Diff[ sharedPos.y ][ sharedPos.x ] = gIn_Diff[ globalPos ];
    #endif

    #if( defined REBLUR_SPECULAR )
        s_Spec[ sharedPos.y ][ sharedPos.x ] = gIn_Spec[ globalPos ];
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint2 groupPos : SV_GroupId, uint threadIndex : SV_GroupIndex )
{
    uint2 DIMS = uint2( GROUP_X, GROUP_Y );
    uint2 localPos = uint2( threadIndex % DIMS.x, threadIndex / DIMS.x );

    s_ViewZ[ localPos.y ][ localPos.x ] = gIn_ScaledViewZ[ pixelPos ];

    #ifdef REBLUR_PERFORMANCE_MODE
        #if( defined REBLUR_DIFFUSE )
            s_Diff[ localPos.y ][ localPos.x ] = gOut_Diff[ pixelPos ];
        #endif

        #if( defined REBLUR_SPECULAR )
            s_Spec[ localPos.y ][ localPos.x ] = gOut_Spec[ pixelPos ];
        #endif
    #else
        PRELOAD_INTO_SMEM;

        // Anti-firefly // TODO: needed for hit distance?
        float3 diffMax = -NRD_INF;
        float3 diffMin = NRD_INF;

        float3 specMax = -NRD_INF;
        float3 specMin = NRD_INF;

        [unroll]
        for( int dy = 0; dy <= BORDER * 2; dy++ )
        {
            [unroll]
            for( int dx = 0; dx <= BORDER * 2; dx++ )
            {
                if( dx == BORDER && dy == BORDER )
                    continue;

                int2 pos = threadPos + int2( dx, dy );

                #if( defined REBLUR_DIFFUSE )
                    float3 d = s_Diff[ pos.y ][ pos.x ].xyz;
                    diffMax = max( diffMax, d );
                    diffMin = min( diffMin, d );
                #endif

                #if( defined REBLUR_SPECULAR )
                    float3 s = s_Spec[ pos.y ][ pos.x ].xyz;
                    specMax = max( specMax, s );
                    specMin = min( specMin, s );
                #endif
            }
        }

        #if( defined REBLUR_DIFFUSE )
            float4 diff = s_Diff[ threadPos.y + BORDER ][ threadPos.x + BORDER ];

            float3 diffClamped = clamp( diff.xyz, diffMin, diffMax );
            diff.xyz = diffClamped;

            gOut_Diff[ pixelPos ] = diff;
            s_Diff[ localPos.y ][ localPos.x ] = diff;
        #endif

        #if( defined REBLUR_SPECULAR )
            float4 spec = s_Spec[ threadPos.y + BORDER ][ threadPos.x + BORDER ];

            float3 specClamped = clamp( spec.xyz, specMin, specMax );
            spec.xyz = specClamped;

            gOut_Spec[ pixelPos ] = spec;
            s_Spec[ localPos.y ][ localPos.x ] = spec;
        #endif
    #endif

    // Mipmap generation
    GroupMemoryBarrierWithGroupSync( );

    DIMS >>= 1;
    if( threadIndex < DIMS.x * DIMS.y )
    {
        uint2 localPos = uint2( threadIndex % DIMS.x, threadIndex / DIMS.x );
        uint2 globalPos = groupPos * DIMS + localPos;

        uint2 id = localPos << 1;
        uint2 id1 = id + 1;

        float z00 = s_ViewZ[ id.y ][ id.x ];
        float z10 = s_ViewZ[ id.y ][ id1.x ];
        float z01 = s_ViewZ[ id1.y ][ id.x ];
        float z11 = s_ViewZ[ id1.y ][ id1.x ];

        float4 w = float4( float4( z00, z10, z01, z11 ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE );
        float sum = dot( w, 1.0 );
        w *= rcp( sum + 1e-7 );

        float z = z00 * w.x + z10 * w.y + z01 * w.z + z11 * w.w;
        z = sum == 0.0 ? NRD_FP16_MAX : z;

        s_ViewZ[ id.y ][ id.x ] = z;
        gOut_ScaledViewZ_x2[ globalPos ] = z;

        #if( defined REBLUR_DIFFUSE )
            float4 d00 = s_Diff[ id.y ][ id.x ];
            float4 d10 = s_Diff[ id.y ][ id1.x ];
            float4 d01 = s_Diff[ id1.y ][ id.x ];
            float4 d11 = s_Diff[ id1.y ][ id1.x ];
            float4 d = d00 * w.x + d10 * w.y + d01 * w.z + d11 * w.w;

            s_Diff[ id.y ][ id.x ] = d;
            gOut_Diff_x2[ globalPos ] = d;
        #endif

        #if( defined REBLUR_SPECULAR )
            float4 s00 = s_Spec[ id.y ][ id.x ];
            float4 s10 = s_Spec[ id.y ][ id1.x ];
            float4 s01 = s_Spec[ id1.y ][ id.x ];
            float4 s11 = s_Spec[ id1.y ][ id1.x ];
            float4 s = s00 * w.x + s10 * w.y + s01 * w.z + s11 * w.w;

            s_Spec[ id.y ][ id.x ] = s;
            gOut_Spec_x2[ globalPos ] = s;
        #endif
    }
}
