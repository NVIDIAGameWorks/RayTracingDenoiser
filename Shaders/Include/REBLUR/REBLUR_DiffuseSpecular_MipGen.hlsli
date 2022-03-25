/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float4 s_Diff[ BUFFER_Y ][ BUFFER_X ];
groupshared float4 s_Spec[ BUFFER_Y ][ BUFFER_X ];

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
    PRELOAD_INTO_SMEM;

    #if( defined REBLUR_DIFFUSE )
        float4 diff = s_Diff[ threadPos.y + BORDER ][ threadPos.x + BORDER ];
    #endif

    #if( defined REBLUR_SPECULAR )
        float4 spec = s_Spec[ threadPos.y + BORDER ][ threadPos.x + BORDER ];

        float roughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ gRectOrigin + pixelPos ] ).w;
    #endif

    // Anti-firefly // TODO: needed for hit distance?
    if( gIsAntiFireflyEnabled )
    {
        float3 diffMaxInput = -NRD_INF;
        float3 diffMinInput = NRD_INF;

        float3 specMaxInput = -NRD_INF;
        float3 specMinInput = NRD_INF;

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
                    diffMaxInput = max( diffMaxInput, d );
                    diffMinInput = min( diffMinInput, d );
                #endif

                #if( defined REBLUR_SPECULAR )
                    float3 s = s_Spec[ pos.y ][ pos.x ].xyz;
                    specMaxInput = max( specMaxInput, s );
                    specMinInput = min( specMinInput, s );
                #endif
            }
        }

        #if( defined REBLUR_DIFFUSE )
            float3 diffClamped = clamp( diff.xyz, diffMinInput, diffMaxInput );
            diff.xyz = diffClamped;
        #endif

        #if( defined REBLUR_SPECULAR )
            float3 specClamped = clamp( spec.xyz, specMinInput, specMaxInput );
            spec.xyz = lerp( spec.xyz, specClamped, GetSpecMagicCurve( roughness ) );
        #endif
    }

    #if( defined REBLUR_DIFFUSE )
        gOut_Diff[ pixelPos ] = diff;
    #endif

    #if( defined REBLUR_SPECULAR )
        gOut_Spec[ pixelPos ] = spec;
    #endif

    // Mipmap generation
    GroupMemoryBarrierWithGroupSync( );

    uint2 DIMS = uint2( GROUP_X, GROUP_Y );
    uint2 localPos = uint2( threadIndex % DIMS.x, threadIndex / DIMS.x );

    #if( defined REBLUR_DIFFUSE )
        s_Diff[ localPos.y ][ localPos.x ] = diff;
    #endif

    #if( defined REBLUR_SPECULAR )
        s_Spec[ localPos.y ][ localPos.x ] = spec;
    #endif

    s_ViewZ[ localPos.y ][ localPos.x ] = gIn_ScaledViewZ[ pixelPos ];

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

        float4 w = float4( abs( float4( z00, z10, z01, z11 ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE );
        float sum = dot( w, 1.0 );
        w *= rcp( sum + 1e-7 );

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

        float z = z00 * w.x + z10 * w.y + z01 * w.z + z11 * w.w;
        z = sum == 0.0 ? NRD_FP16_MAX : z;

        s_ViewZ[ id.y ][ id.x ] = z;
        gOut_ScaledViewZ_x2[ globalPos ] = z;
    }

    GroupMemoryBarrierWithGroupSync( );

    DIMS >>= 1;
    if( threadIndex < DIMS.x * DIMS.y )
    {
        uint2 localPos = uint2( threadIndex % DIMS.x, threadIndex / DIMS.x );
        uint2 globalPos = groupPos * DIMS + localPos;

        uint2 id = localPos << 2;
        uint2 id1 = id + 3;

        float z00 = s_ViewZ[ id.y ][ id.x ];
        float z10 = s_ViewZ[ id.y ][ id1.x ];
        float z01 = s_ViewZ[ id1.y ][ id.x ];
        float z11 = s_ViewZ[ id1.y ][ id1.x ];

        float4 w = float4( abs( float4( z00, z10, z01, z11 ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE );
        float sum = dot( w, 1.0 );
        w *= rcp( sum + 1e-7 );

        #if( defined REBLUR_DIFFUSE )
            float4 d00 = s_Diff[ id.y ][ id.x ];
            float4 d10 = s_Diff[ id.y ][ id1.x ];
            float4 d01 = s_Diff[ id1.y ][ id.x ];
            float4 d11 = s_Diff[ id1.y ][ id1.x ];
            float4 d = d00 * w.x + d10 * w.y + d01 * w.z + d11 * w.w;

            s_Diff[ id.y ][ id.x ] = d;
            gOut_Diff_x4[ globalPos ] = d;
        #endif

        #if( defined REBLUR_SPECULAR )
            float4 s00 = s_Spec[ id.y ][ id.x ];
            float4 s10 = s_Spec[ id.y ][ id1.x ];
            float4 s01 = s_Spec[ id1.y ][ id.x ];
            float4 s11 = s_Spec[ id1.y ][ id1.x ];
            float4 s = s00 * w.x + s10 * w.y + s01 * w.z + s11 * w.w;

            s_Spec[ id.y ][ id.x ] = s;
            gOut_Spec_x4[ globalPos ] = s;
        #endif

        float z = z00 * w.x + z10 * w.y + z01 * w.z + z11 * w.w;
        z = sum == 0.0 ? NRD_FP16_MAX : z;

        s_ViewZ[ id.y ][ id.x ] = z;
        gOut_ScaledViewZ_x4[ globalPos ] = z;
    }

    GroupMemoryBarrierWithGroupSync( );

    DIMS >>= 1;
    if( threadIndex < DIMS.x * DIMS.y )
    {
        uint2 localPos = uint2( threadIndex % DIMS.x, threadIndex / DIMS.x );
        uint2 globalPos = groupPos * DIMS + localPos;

        uint2 id = localPos << 3;
        uint2 id1 = id + 7;

        float z00 = s_ViewZ[ id.y ][ id.x ];
        float z10 = s_ViewZ[ id.y ][ id1.x ];
        float z01 = s_ViewZ[ id1.y ][ id.x ];
        float z11 = s_ViewZ[ id1.y ][ id1.x ];

        float4 w = float4( abs( float4( z00, z10, z01, z11 ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE );
        float sum = dot( w, 1.0 );
        w *= rcp( sum + 1e-7 );

        #if( defined REBLUR_DIFFUSE )
            float4 d00 = s_Diff[ id.y ][ id.x ];
            float4 d10 = s_Diff[ id.y ][ id1.x ];
            float4 d01 = s_Diff[ id1.y ][ id.x ];
            float4 d11 = s_Diff[ id1.y ][ id1.x ];
            float4 d = d00 * w.x + d10 * w.y + d01 * w.z + d11 * w.w;

            // no write to SMEM
            gOut_Diff_x8[ globalPos ] = d;
        #endif

        #if( defined REBLUR_SPECULAR )
            float4 s00 = s_Spec[ id.y ][ id.x ];
            float4 s10 = s_Spec[ id.y ][ id1.x ];
            float4 s01 = s_Spec[ id1.y ][ id.x ];
            float4 s11 = s_Spec[ id1.y ][ id1.x ];
            float4 s = s00 * w.x + s10 * w.y + s01 * w.z + s11 * w.w;

            // no write to SMEM
            gOut_Spec_x8[ globalPos ] = s;
        #endif

        float z = z00 * w.x + z10 * w.y + z01 * w.z + z11 * w.w;
        z = sum == 0.0 ? NRD_FP16_MAX : z;

        // no write to SMEM
        gOut_ScaledViewZ_x8[ globalPos ] = z;
    }
}
