/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
    #define TYPE float2
#else
    #define TYPE float
#endif

groupshared TYPE s_FrameNum[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );

    float4 internalData = UnpackDiffSpecInternalData( gIn_InternalData[ globalPos ] );
    internalData = saturate( internalData / REBLUR_FIXED_FRAME_NUM );

    #if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
        s_FrameNum[ sharedPos.y ][ sharedPos.x ] = internalData.yw;
    #elif( defined REBLUR_DIFFUSE )
        s_FrameNum[ sharedPos.y ][ sharedPos.x ] = internalData.y;
    #else
        s_FrameNum[ sharedPos.y ][ sharedPos.x ] = internalData.w;
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;
    int2 pixelPosUser = gRectOrigin + pixelPos;

    PRELOAD_INTO_SMEM;

    // ViewZ
    #ifdef REBLUR_OCCLUSION
        float scaledViewZ;
        #if( defined REBLUR_DIFFUSE )
            float2 diffData = gOut_Diff[ pixelPos ];
            scaledViewZ = diffData.y;
        #endif

        #if( defined REBLUR_SPECULAR )
            float2 specData = gOut_Spec[ pixelPos ];
            scaledViewZ = specData.y;
        #endif
    #else
        float scaledViewZ = gIn_ScaledViewZ[ pixelPos ];
    #endif
    float viewZ = scaledViewZ / NRD_FP16_VIEWZ_SCALE;

    // Early out
    int2 smemPos = threadPos + BORDER;
    TYPE frameNum = s_FrameNum[ smemPos.y ][ smemPos.x ];

    if( all( frameNum == 1.0 ) || viewZ > gDenoisingRange )
        return;

    // Normal and roughness
    float materialID;
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ], materialID );
    float3 N = normalAndRoughness.xyz;
    float roughness = normalAndRoughness.w;

    // Smooth internal data // TODO: move this to TA to store smoothed accum speed in "internal data", due SMEM preloading limitations
    // edge pixels will have to clamp to the CTA size, but still:
    // - a corner pixel will get 3 neighbors
    // - an edge pixel will get 5 neighbors
    // - all other will get 8 neighbors
    // - it shouldn't affect IQ and even logic
    TYPE c = frameNum;
    TYPE sum = 1.0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            if( dx == BORDER && dy == BORDER )
                continue;

            int2 pos = threadPos + int2( dx, dy );
            TYPE s = s_FrameNum[ pos.y ][ pos.x ];

            TYPE w = step( c, s );

            //float2 o = float2( dx, dy ) - BORDER;
            //w *= exp2( -0.66 * STL::Math::LengthSquared( o ) );

            frameNum += s * w;
            sum += w;
        }
    }

    frameNum *= rcp( sum );

    // Slope correction
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );
    float3 X = STL::Geometry::RotateVector( gViewToWorld, Xv );
    float3 V = GetViewVector( X );
    float NoV = abs( dot( V, N ) );

    TYPE scale = saturate( 1.0 - frameNum );
    scale *= lerp( 1.0, 0.5, STL::BRDF::Pow5( NoV ) );
    scale *= gHistoryFixStrength;

    #if( defined REBLUR_DIFFUSE && defined REBLUR_SPECULAR )
        float diffScale = scale.x;
        float specScale = scale.y * GetSpecMagicCurve( roughness );
    #elif( defined REBLUR_DIFFUSE )
        float diffScale = scale.x;
    #else
        float specScale = scale.x * GetSpecMagicCurve( roughness );
    #endif

    // History reconstruction // TODO: materialID support?
    float frustumHeight = PixelRadiusToWorld( gUnproject, gOrthoMode, gRectSize.y, viewZ );
    float3 Nv = STL::Geometry::RotateVectorInverse( gViewToWorld, N );
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumHeight, Xv, Nv, 1.0 );

    #if( defined REBLUR_DIFFUSE )
        ReconstructHistory(
            geometryWeightParams, Nv, scaledViewZ, diffScale, pixelPos, pixelUv, gOut_Diff, gIn_Diff
            #ifndef REBLUR_OCCLUSION
                , gIn_ScaledViewZ
            #endif
        );
    #endif

    #if( defined REBLUR_SPECULAR )
        ReconstructHistory(
            geometryWeightParams, Nv, scaledViewZ, specScale, pixelPos, pixelUv, gOut_Spec, gIn_Spec
            #ifndef REBLUR_OCCLUSION
                , gIn_ScaledViewZ
            #endif
        );
    #endif
}
