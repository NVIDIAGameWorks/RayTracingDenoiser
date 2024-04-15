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
    globalPos = clamp( globalPos, 0, gRectSizeMinusOne );

    float2 data = gIn_Hit_ViewZ[ globalPos ];
    data.y = abs( data.y ) / NRD_FP16_VIEWZ_SCALE;

    s_Data[ sharedPos.y ][ sharedPos.x ] = data;

    SIGMA_TYPE s;
    #if( !defined SIGMA_FIRST_PASS || defined SIGMA_TRANSLUCENT )
        s = gIn_Shadow_Translucency[ globalPos ];
    #else
        s = IsLit( data.x );
    #endif

    #ifndef SIGMA_FIRST_PASS
        s = SIGMA_BackEnd_UnpackShadow( s );
    #endif

    s_Shadow_Translucency[ sharedPos.y ][ sharedPos.x ] = s;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    // Preload
    float isSky = gIn_Tiles[ pixelPos >> 4 ].y;
    PRELOAD_INTO_SMEM_WITH_TILE_CHECK;

    // Tile-based early out
    if( isSky != 0.0 || any( pixelPos > gRectSizeMinusOne ) )
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

    // Copy history
    #ifdef SIGMA_FIRST_PASS
        if( gStabilizationStrength != 0 )
            gOut_History[ pixelPos ] = gIn_History[ pixelPos ];
    #endif

    // Tile-based early out ( potentially )
    float2 pixelUv = float2( pixelPos + 0.5 ) * gRectSizeInv;
    float tileValue = TextureCubic( gIn_Tiles, pixelUv * gResolutionScale );
    #ifdef SIGMA_FIRST_PASS
        tileValue *= all( pixelPos < gRectSize ); // due to USE_MAX_DIMS
    #endif

    if( ( tileValue == 0.0 && NRD_USE_TILE_CHECK ) || centerHitDist == 0.0 )
    {
        gOut_Hit_ViewZ[ pixelPos ] = float2( 0.0, viewZ * NRD_FP16_VIEWZ_SCALE );
        gOut_Shadow_Translucency[ pixelPos ] = PackShadow( s_Shadow_Translucency[ smemPos.y ][ smemPos.x ] );

        return;
    }

    // Position
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );

    // Normal
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ WithRectOrigin( pixelPos ) ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = STL::Geometry::RotateVector( gWorldToView, N );

    // Parameters
    float frustumSize = PixelRadiusToWorld( gUnproject, gOrthoMode, min( gRectSize.x, gRectSize.y ), viewZ ); // TODO: use GetFrustumSize
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumSize, Xv, Nv, 1.0 );

    // Estimate average distance to occluder
    float2 sum = 0;
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
            float h = data.x;
            float signNoL = float( data.x != 0.0 );
            float z = data.y;

            float w = 1.0;
            if( !( i == BORDER && j == BORDER ) )
            {
                float2 uv = pixelUv + float2( i - BORDER, j - BORDER ) * gRectSizeInv;
                float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gOrthoMode );
                float NoX = dot( Nv, Xvs );

                w = ComputeWeight( NoX, geometryWeightParams.x, geometryWeightParams.y );
                w *= GetGaussianWeight( length( float2( i - BORDER, j - BORDER ) / BORDER ) );
                w *= float( z < gDenoisingRange );
                w *= float( centerSignNoL == signNoL );
            }

            SIGMA_TYPE s = s_Shadow_Translucency[ pos.y ][ pos.x ];
            s = Denanify( w, s );

            float2 ww = w;
            ww.y *= float( s.x != 1.0 ); // TODO: what if s.x == 1.0, but h < NRD_FP16_MAX?
            ww.y *= 1.0 / ( 1.0 + h * SIGMA_PENUMBRA_WEIGHT_SCALE ); // prefer smaller penumbra

            result += s * ww.x;
            hitDist += h * ww.y;
            sum += ww;
        }
    }

    result /= sum.x;
    hitDist /= max( sum.y, NRD_EPS ); // yes, without patching

    float invHitDist = 1.0 / max( hitDist, NRD_EPS );

    // Blur radius
    float unprojectZ = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
    float worldRadius = GetKernelRadiusInPixels( hitDist, unprojectZ ) * unprojectZ;
    worldRadius *= tileValue; // helps to prevent blurring "inside" umbra
    worldRadius /= SIGMA_SPATIAL_PASSES_NUM;

    // Tangent basis with anisotropy
    float3x3 mWorldToLocal = STL::Geometry::GetBasis( Nv );
    float3 Tv = mWorldToLocal[ 0 ];
    float3 Bv = mWorldToLocal[ 1 ];

    float3 t = cross( gLightDirectionView.xyz, Nv ); // TODO: add support for other light types to bring proper anisotropic filtering
    if( length( t ) > 0.001 )
    {
        Tv = normalize( t );
        Bv = cross( Tv, Nv );

        float cosa = abs( dot( Nv, gLightDirectionView.xyz ) );
        float skewFactor = lerp( 0.25, 1.0, cosa );

        //Tv *= skewFactor; // TODO: needed?
        Bv /= skewFactor;
    }

    Tv *= worldRadius;
    Bv *= worldRadius;

    // Random rotation
    float4 rotator = GetBlurKernelRotation( SIGMA_ROTATOR_MODE, pixelPos, gRotator, gFrameIndex );

    // Denoising
    sum.x = 1.0;
    sum.y = float( sum.y != 0.0 );

    [unroll]
    for( uint n = 0; n < SIGMA_POISSON_SAMPLE_NUM; n++ )
    {
        // Sample coordinates
        float3 offset = SIGMA_POISSON_SAMPLES[ n ];
        float2 uv = GetKernelSampleCoordinates( gViewToClip, offset, Xv, Tv, Bv, rotator );

        // Snap to the pixel center!
        uv = ( floor( uv * gRectSize ) + 0.5 ) * gRectSizeInv;

        // Texture coordinates
        float2 uvScaled = ClampUvToViewport( uv );

        // Fetch data
        float2 data = gIn_Hit_ViewZ.SampleLevel( gNearestClamp, uvScaled, 0 );
        float h = data.x;
        float signNoL = float( data.x != 0.0 );
        float z = abs( data.y ) / NRD_FP16_VIEWZ_SCALE;

        // Sample weight
        float3 Xvs = STL::Geometry::ReconstructViewPosition( uv, gFrustum, z, gOrthoMode );
        float NoX = dot( Nv, Xvs );

        float w = IsInScreenNearest( uv );
        w *= GetGaussianWeight( offset.z );
        w *= ComputeWeight( NoX, geometryWeightParams.x, geometryWeightParams.y );
        w *= float( z < gDenoisingRange );
        w *= float( centerSignNoL == signNoL );

        // Avoid umbra leaking inside wide penumbra
        float t = saturate( h * invHitDist );
        w *= STL::Math::LinearStep( 0.0, 0.1, t );

        // Fetch shadow
        SIGMA_TYPE s;
        #if( !defined SIGMA_FIRST_PASS || defined SIGMA_TRANSLUCENT )
            s = gIn_Shadow_Translucency.SampleLevel( gNearestClamp, uvScaled, 0 );
        #else
            s = IsLit( h );
        #endif
        s = Denanify( w, s );

        #ifndef SIGMA_FIRST_PASS
            s = SIGMA_BackEnd_UnpackShadow( s );
        #endif

        // Accumulate
        float2 ww = w;
        ww.y *= float( s.x != 1.0 ); // TODO: what if s.x == 1.0, but h < NRD_FP16_MAX?
        ww.y *= 1.0 / ( 1.0 + h * SIGMA_PENUMBRA_WEIGHT_SCALE ); // prefer smaller penumbra

        result += s * ww.x;
        hitDist += h * ww.y;
        sum += ww;
    }

    result /= sum.x;
    hitDist = sum.y == 0.0 ? centerHitDist : hitDist / sum.y;

    // Output
    #ifndef SIGMA_FIRST_PASS
        if( gStabilizationStrength != 0 )
    #endif
            gOut_Hit_ViewZ[ pixelPos ] = float2( hitDist, viewZ * NRD_FP16_VIEWZ_SCALE );

    gOut_Shadow_Translucency[ pixelPos ] = PackShadow( result );
}
