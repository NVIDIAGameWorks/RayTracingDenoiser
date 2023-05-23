/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float2 s_Data[ BUFFER_Y ][ BUFFER_X ];
groupshared SIGMA_TYPE s_Shadow_Translucency[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSize - 1.0 );
    #ifdef SIGMA_FIRST_PASS
        globalPos += gRectOrigin;
    #endif

    float2 data = gIn_Hit_ViewZ[ globalPos ];
    data.y = abs( data.y ) / NRD_FP16_VIEWZ_SCALE;

    s_Data[ sharedPos.y ][ sharedPos.x ] = data;

    SIGMA_TYPE s;
    #if( !defined SIGMA_FIRST_PASS || defined SIGMA_TRANSLUCENT )
        s = gIn_Shadow_Translucency[ globalPos ];
    #else
        s = float( data.x == NRD_FP16_MAX );
    #endif

    #ifndef SIGMA_FIRST_PASS
        s = UnpackShadowSpecial( s );
    #endif

    s_Shadow_Translucency[ sharedPos.y ][ sharedPos.x ] = s;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = gRectOrigin + pixelPos;
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    // Preload
    float isSky = gIn_Tiles[ pixelPos >> 4 ].y;
    PRELOAD_INTO_SMEM_WITH_TILE_CHECK;

    // Tile-based early out
    if( isSky != 0.0 )
        return;

    // Center data
    int2 smemPos = threadPos + BORDER;
    float2 centerData = s_Data[ smemPos.y ][ smemPos.x ];
    float centerHitDist = centerData.x;
    float centerSignNoL = float( centerData.x != 0.0 );
    float viewZ = centerData.y;

    // Early out
    if( viewZ > gDenoisingRange )
        return;

    #ifdef SIGMA_FIRST_PASS
        // Copy history
        gOut_History[ pixelPos ] = gIn_History[ pixelPos ];

        float tileValue = TextureCubic( gIn_Tiles, pixelUv * gResolutionScale );
        tileValue *= all( pixelPos < gRectSize ); // due to USE_MAX_DIMS
    #else
        float tileValue = 1.0;
    #endif

    // Early out
    if( centerHitDist == 0.0 || tileValue == 0.0 )
    {
        gOut_Shadow_Translucency[ pixelPos ] = PackShadow( s_Shadow_Translucency[ smemPos.y ][ smemPos.x ] );
        gOut_Hit_ViewZ[ pixelPos ] = float2( 0.0, viewZ * NRD_FP16_VIEWZ_SCALE );

        return;
    }

    // Reference
    #if( SIGMA_REFERENCE == 1 )
        gOut_Shadow_Translucency[ pixelPos ] = PackShadow( s_Shadow_Translucency[ smemPos.y ][ smemPos.x ] );
        gOut_Hit_ViewZ[ pixelPos ] = float2( centerHitDist * centerSignNoL, viewZ * NRD_FP16_VIEWZ_SCALE );

        return;
    #endif

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );

    // Normal
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ pixelPosUser ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );

    // Estimate average distance to occluder
    float sum = 0;
    float hitDist = 0;
    SIGMA_TYPE result = 0;

    [unroll]
    for( j = 0; j <= BORDER * 2; j++ )
    {
        [unroll]
        for( i = 0; i <= BORDER * 2; i++ )
        {
            int2 pos = threadPos + int2( i, j );
            float2 data = s_Data[ pos.y ][ pos.x ];

            SIGMA_TYPE s = s_Shadow_Translucency[ pos.y ][ pos.x ];
            float h = data.x;
            float signNoL = float( data.x != 0.0 );
            float z = data.y;

            float w = 1.0;
            if( !( i == BORDER && j == BORDER ) )
            {
                w = GetBilateralWeight( z, viewZ );
                w *= saturate( 1.0 - abs( centerSignNoL - signNoL ) );
            }

            result += s * w;
            hitDist += h * float( s.x != 1.0 ) * w;
            sum += w;
        }
    }

    float invSum = 1.0 / sum;
    result *= invSum;
    hitDist *= invSum;

    // Blur radius
    float innerShadowRadiusScale = lerp( 0.5, 1.0, result.x );
    float outerShadowRadiusScale = 1.0; // TODO: find a way to improve penumbra
    float worldRadius = hitDist * innerShadowRadiusScale * outerShadowRadiusScale * gBlurRadiusScale;
    worldRadius *= tileValue;

    float unprojectZ = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
    float pixelRadius = worldRadius * STL::Math::PositiveRcp( unprojectZ );
    pixelRadius = min( pixelRadius, SIGMA_MAX_PIXEL_RADIUS );
    worldRadius = pixelRadius * unprojectZ;

    float centerWeight = STL::Math::LinearStep( 0.9, 1.0, result.x );
    worldRadius += SIGMA_PENUMBRA_FIX_BLUR_RADIUS_ADDON * lerp( saturate( pixelRadius / 1.5 ), 1.0, centerWeight ) * unprojectZ * result.x;

    // Tangent basis
    float3x3 mWorldToLocal = STL::Geometry::GetBasis( Nv );
    float3 Tv = mWorldToLocal[ 0 ] * worldRadius;
    float3 Bv = mWorldToLocal[ 1 ] * worldRadius;

    // Random rotation
    float4 rotator = GetBlurKernelRotation( NRD_PIXEL, pixelPos, gRotator, gFrameIndex );

    // Denoising
    sum = 1.0;

    float frustumSize = PixelRadiusToWorld( gUnproject, gOrthoMode, min( gRectSize.x, gRectSize.y ), viewZ );
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumSize, Xv, Nv, 1.0 );

    [unroll]
    for( uint n = 0; n < SIGMA_POISSON_SAMPLE_NUM; n++ )
    {
        // Sample coordinates
        float3 offset = SIGMA_POISSON_SAMPLES[ n ];
        float2 uv = GetKernelSampleCoordinates( gViewToClip, offset, Xv, Tv, Bv, rotator );

        // Fetch data
        float2 uvScaled = uv * gResolutionScale;
        #ifdef SIGMA_FIRST_PASS
            uvScaled += gRectOffset;
        #endif

        float2 data = gIn_Hit_ViewZ.SampleLevel( gNearestClamp, uvScaled, 0 );
        float h = data.x;
        float signNoL = float( data.x != 0.0 );
        float z = abs( data.y ) / NRD_FP16_VIEWZ_SCALE;

        SIGMA_TYPE s;
        #if( !defined SIGMA_FIRST_PASS || defined SIGMA_TRANSLUCENT )
            s = gIn_Shadow_Translucency.SampleLevel( gNearestClamp, uvScaled, 0 );
        #else
            s = float( h == NRD_FP16_MAX );
        #endif

        #ifndef SIGMA_FIRST_PASS
            s = UnpackShadowSpecial( s );
        #endif

        // Sample weight
        float3 samplePos = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gOrthoMode );
        float w = GetGeometryWeight( geometryWeightParams, Nv, samplePos );
        w *= saturate( 1.0 - abs( centerSignNoL - signNoL ) );
        w *= IsInScreen( uv );

        // Weight for outer shadow (to avoid blurring of ~umbra)
        w *= lerp( 1.0, s.x, centerWeight );

        result += s * w;
        hitDist += h * float( s.x != 1.0 ) * w;
        sum += w;
    }

    invSum = 1.0 / sum;
    result *= invSum;
    hitDist *= invSum;

    hitDist = max( hitDist * tileValue, SIGMA_MIN_DISTANCE );

    // Output
    gOut_Shadow_Translucency[ pixelPos ] = PackShadow( result );
    gOut_Hit_ViewZ[ pixelPos ] = float2( hitDist * centerSignNoL, viewZ * NRD_FP16_VIEWZ_SCALE );
}
