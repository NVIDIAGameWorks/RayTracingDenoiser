/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float2 s_Diff[ BUFFER_Y ][ BUFFER_X ];
groupshared float2 s_Spec[ BUFFER_Y ][ BUFFER_X ];

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint2 groupPos : SV_GroupId, uint threadIndex : SV_GroupIndex )
{
    // Anti-firefly // TODO: needed for hit distance?

    // Mipmap generation
    uint2 DIMS = uint2( GROUP_X, GROUP_Y );
    uint2 localPos = uint2( threadIndex % DIMS.x, threadIndex / DIMS.x );

    #if( defined REBLUR_DIFFUSE )
        float2 diff = gIn_Diff[ pixelPos ];
        s_Diff[ localPos.y ][ localPos.x ] = diff;
    #endif

    #if( defined REBLUR_SPECULAR )
        float2 spec = gIn_Spec[ pixelPos ];
        s_Spec[ localPos.y ][ localPos.x ] = spec;
    #endif

    GroupMemoryBarrierWithGroupSync( );

    DIMS >>= 1;
    if( threadIndex < DIMS.x * DIMS.y )
    {
        uint2 localPos = uint2( threadIndex % DIMS.x, threadIndex / DIMS.x );
        uint2 globalPos = groupPos * DIMS + localPos;

        uint2 id = localPos << 1;
        uint2 id1 = id + 1;

        #if( defined REBLUR_DIFFUSE )
            float2 d00 = s_Diff[ id.y ][ id.x ];
            float2 d10 = s_Diff[ id.y ][ id1.x ];
            float2 d01 = s_Diff[ id1.y ][ id.x ];
            float2 d11 = s_Diff[ id1.y ][ id1.x ];

            float4 wd = float4( abs( float4( d00.y, d10.y, d01.y, d11.y ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE );
            float sumd = dot( wd, 1.0 );
            wd *= rcp( sumd + 1e-7 );

            float2 d = d00 * wd.x + d10 * wd.y + d01 * wd.z + d11 * wd.w;
            d.y = sumd == 0.0 ? NRD_FP16_MAX : d.y;

            s_Diff[ id.y ][ id.x ] = d;
            gOut_Diff_x2[ globalPos ] = d;
        #endif

        #if( defined REBLUR_SPECULAR )
            float2 s00 = s_Spec[ id.y ][ id.x ];
            float2 s10 = s_Spec[ id.y ][ id1.x ];
            float2 s01 = s_Spec[ id1.y ][ id.x ];
            float2 s11 = s_Spec[ id1.y ][ id1.x ];

            float4 ws = float4( abs( float4( s00.y, s10.y, s01.y, s11.y ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE );
            float sums = dot( ws, 1.0 );
            ws *= rcp( sums + 1e-7 );

            float2 s = s00 * ws.x + s10 * ws.y + s01 * ws.z + s11 * ws.w;
            s.y = sums == 0.0 ? NRD_FP16_MAX : s.y;

            s_Spec[ id.y ][ id.x ] = s;
            gOut_Spec_x2[ globalPos ] = s;
        #endif
    }

    GroupMemoryBarrierWithGroupSync( );

    DIMS >>= 1;
    if( threadIndex < DIMS.x * DIMS.y )
    {
        uint2 localPos = uint2( threadIndex % DIMS.x, threadIndex / DIMS.x );
        uint2 globalPos = groupPos * DIMS + localPos;

        uint2 id = localPos << 2;
        uint2 id1 = id + 3;

        #if( defined REBLUR_DIFFUSE )
            float2 d00 = s_Diff[ id.y ][ id.x ];
            float2 d10 = s_Diff[ id.y ][ id1.x ];
            float2 d01 = s_Diff[ id1.y ][ id.x ];
            float2 d11 = s_Diff[ id1.y ][ id1.x ];

            float4 wd = float4( abs( float4( d00.y, d10.y, d01.y, d11.y ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE );
            float sumd = dot( wd, 1.0 );
            wd *= rcp( sumd + 1e-7 );

            float2 d = d00 * wd.x + d10 * wd.y + d01 * wd.z + d11 * wd.w;
            d.y = sumd == 0.0 ? NRD_FP16_MAX : d.y;

            s_Diff[ id.y ][ id.x ] = d;
            gOut_Diff_x4[ globalPos ] = d;
        #endif

        #if( defined REBLUR_SPECULAR )
            float2 s00 = s_Spec[ id.y ][ id.x ];
            float2 s10 = s_Spec[ id.y ][ id1.x ];
            float2 s01 = s_Spec[ id1.y ][ id.x ];
            float2 s11 = s_Spec[ id1.y ][ id1.x ];

            float4 ws = float4( abs( float4( s00.y, s10.y, s01.y, s11.y ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE );
            float sums = dot( ws, 1.0 );
            ws *= rcp( sums + 1e-7 );

            float2 s = s00 * ws.x + s10 * ws.y + s01 * ws.z + s11 * ws.w;
            s.y = sums == 0.0 ? NRD_FP16_MAX : s.y;

            s_Spec[ id.y ][ id.x ] = s;
            gOut_Spec_x4[ globalPos ] = s;
        #endif
    }

    GroupMemoryBarrierWithGroupSync( );

    DIMS >>= 1;
    if( threadIndex < DIMS.x * DIMS.y )
    {
        uint2 localPos = uint2( threadIndex % DIMS.x, threadIndex / DIMS.x );
        uint2 globalPos = groupPos * DIMS + localPos;

        uint2 id = localPos << 3;
        uint2 id1 = id + 7;

        #if( defined REBLUR_DIFFUSE )
            float2 d00 = s_Diff[ id.y ][ id.x ];
            float2 d10 = s_Diff[ id.y ][ id1.x ];
            float2 d01 = s_Diff[ id1.y ][ id.x ];
            float2 d11 = s_Diff[ id1.y ][ id1.x ];

            float4 wd = float4( abs( float4( d00.y, d10.y, d01.y, d11.y ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE );
            float sumd = dot( wd, 1.0 );
            wd *= rcp( sumd + 1e-7 );

            float2 d = d00 * wd.x + d10 * wd.y + d01 * wd.z + d11 * wd.w;
            d.y = sumd == 0.0 ? NRD_FP16_MAX : d.y;

            // no write to SMEM
            gOut_Diff_x8[ globalPos ] = d;
        #endif

        #if( defined REBLUR_SPECULAR )
            float2 s00 = s_Spec[ id.y ][ id.x ];
            float2 s10 = s_Spec[ id.y ][ id1.x ];
            float2 s01 = s_Spec[ id1.y ][ id.x ];
            float2 s11 = s_Spec[ id1.y ][ id1.x ];

            float4 ws = float4( abs( float4( s00.y, s10.y, s01.y, s11.y ) ) < gDenoisingRange * NRD_FP16_VIEWZ_SCALE );
            float sums = dot( ws, 1.0 );
            ws *= rcp( sums + 1e-7 );

            float2 s = s00 * ws.x + s10 * ws.y + s01 * ws.z + s11 * ws.w;
            s.y = sums == 0.0 ? NRD_FP16_MAX : s.y;

            // no write to SMEM
            gOut_Spec_x8[ globalPos ] = s;
        #endif
    }
}
