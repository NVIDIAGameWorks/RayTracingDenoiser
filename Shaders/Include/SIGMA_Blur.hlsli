/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

groupshared float2 s_Penumbra_ViewZ[ BUFFER_Y ][ BUFFER_X ];
groupshared SIGMA_TYPE s_Shadow_Translucency[ BUFFER_Y ][ BUFFER_X ];

void Preload( uint2 sharedPos, int2 globalPos )
{
    globalPos = clamp( globalPos, 0, gRectSizeMinusOne );

    float2 data;
    data.x = gIn_Penumbra[ globalPos ];
    data.y = UnpackViewZ( gIn_ViewZ[ WithRectOrigin( globalPos ) ] );

    s_Penumbra_ViewZ[ sharedPos.y ][ sharedPos.x ] = data;

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
    float2 centerData = s_Penumbra_ViewZ[ smemPos.y ][ smemPos.x ];
    float centerPenumbra = centerData.x;
    float centerSignNoL = float( centerPenumbra != 0.0 );
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

    if( ( tileValue == 0.0 && NRD_USE_TILE_CHECK ) || centerPenumbra == 0.0 )
    {
        gOut_Penumbra[ pixelPos ] = centerPenumbra;
        gOut_Shadow_Translucency[ pixelPos ] = PackShadow( s_Shadow_Translucency[ smemPos.y ][ smemPos.x ] );

        return;
    }

    // Position
    float3 Xv = Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gOrthoMode );

    // Normal
    float4 normalAndRoughness = NRD_FrontEnd_UnpackNormalAndRoughness( gIn_Normal_Roughness[ WithRectOrigin( pixelPos ) ] );
    float3 N = normalAndRoughness.xyz;
    float3 Nv = Geometry::RotateVector( gWorldToView, N );

    // Parameters
    float unprojectZ = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
    float frustumSize = GetFrustumSize( gMinRectDimMulUnproject, gOrthoMode, viewZ );
    float2 geometryWeightParams = GetGeometryWeightParams( gPlaneDistSensitivity, frustumSize, Xv, Nv, 1.0 );

    // Estimate penumbra size and filter shadow ( pass 1: dense 3x3 or 5x5 )
    float2 sum = 0;
    float penumbra = 0;
    SIGMA_TYPE result = 0;
    SIGMA_TYPE centerTap;

    [unroll]
    for( j = 0; j <= BORDER * 2; j++ )
    {
        [unroll]
        for( i = 0; i <= BORDER * 2; i++ )
        {
            int2 pos = threadPos + int2( i, j );

            float2 data = s_Penumbra_ViewZ[ pos.y ][ pos.x ];
            float penum = data.x;
            float z = data.y;
            float signNoL = float( penum != 0.0 );

            SIGMA_TYPE s = s_Shadow_Translucency[ pos.y ][ pos.x ];

            float w;
            if( i == BORDER && j == BORDER )
            {
                centerTap = s;
                w = 1.0;
            }
            else
            {
                float2 uv = pixelUv + float2( i - BORDER, j - BORDER ) * gRectSizeInv;
                float3 Xvs = Geometry::ReconstructViewPosition( uv, gFrustum, z, gOrthoMode );
                float NoX = dot( Nv, Xvs );

                w = ComputeWeight( NoX, geometryWeightParams.x, geometryWeightParams.y );
                w *= GetGaussianWeight( length( float2( i - BORDER, j - BORDER ) / BORDER ) );
                w *= float( z < gDenoisingRange );
                w *= float( centerSignNoL == signNoL );

                s = Denanify( w, s );
            }

            float2 ww = w;
            ww.y *= !IsLit( penum );

            float penumInPixels = penum / unprojectZ;
            ww.y /= 1.0 + penumInPixels; // prefer smaller penumbra

            result += s * ww.x;
            penumbra += penum * ww.y;
            sum += ww;
        }
    }

    result /= sum.x;
    sum.x = 1.0;

    penumbra /= max( sum.y, NRD_EPS ); // yes, without patching
    sum.y = float( sum.y != 0.0 );

    // Avoid 1-pixel wide blur if penumbra size < 1 pixel
    float penumbraInPixels = penumbra / unprojectZ;
    float f = Math::LinearStep( 0.75, 1.25, penumbraInPixels );
    result = lerp( centerTap, result, f );

    // Tangent basis with anisotropy
    float3x3 mWorldToLocal = Geometry::GetBasis( Nv );
    float3 Tv = mWorldToLocal[ 0 ];
    float3 Bv = mWorldToLocal[ 1 ];

    float3 t = cross( gLightDirectionView.xyz, Nv ); // TODO: add support for other light types to bring proper anisotropic filtering
    if( length( t ) > 0.001 )
    {
        Tv = normalize( t );
        Bv = cross( Tv, Nv );

        float cosa = abs( dot( Nv, gLightDirectionView.xyz ) );
        float skewFactor = lerp( 0.25, 1.0, cosa );

        //Tv *= skewFactor; // TODO: let's not srink filtering in the other direction
        Bv /= skewFactor;
    }

    // Blur radius
    float worldRadius = GetKernelRadiusInPixels( penumbra, unprojectZ, tileValue ) * unprojectZ;

    Tv *= worldRadius;
    Bv *= worldRadius;

    // Random rotation
    float4 rotator = GetBlurKernelRotation( SIGMA_ROTATOR_MODE, pixelPos, gRotator, gFrameIndex );

    // Estimate penumbra size and filter shadow ( pass 2: sparse 8-taps )
    float invEstimatedPenumbra = 1.0 / max( penumbra, NRD_EPS );

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
        float penum = gIn_Penumbra.SampleLevel( gNearestClamp, uvScaled, 0 );
        float z = UnpackViewZ( gIn_ViewZ.SampleLevel( gNearestClamp, WithRectOffset( uvScaled ), 0 ) );
        float signNoL = float( penum != 0.0 );

        // Sample weight
        float3 Xvs = Geometry::ReconstructViewPosition( uv, gFrustum, z, gOrthoMode );
        float NoX = dot( Nv, Xvs );

        float w = IsInScreenNearest( uv );
        w *= GetGaussianWeight( offset.z );
        w *= ComputeWeight( NoX, geometryWeightParams.x, geometryWeightParams.y );
        w *= float( z < gDenoisingRange );
        w *= float( centerSignNoL == signNoL );

        // Avoid umbra leaking inside wide penumbra
        float t = saturate( penum * invEstimatedPenumbra );
        w *= Math::SmoothStep( 0.0, 1.0, t ); // TODO: it works surprisingly well, keep an eye on it!

        // Fetch shadow
        SIGMA_TYPE s;
        #if( !defined SIGMA_FIRST_PASS || defined SIGMA_TRANSLUCENT )
            s = gIn_Shadow_Translucency.SampleLevel( gNearestClamp, uvScaled, 0 );
        #else
            s = IsLit( penum );
        #endif
        s = Denanify( w, s );

        #ifndef SIGMA_FIRST_PASS
            s = SIGMA_BackEnd_UnpackShadow( s );
        #endif

        // Accumulate
        float2 ww = w;
        ww.y *= !IsLit( penum );

        float penumInPixels = penum / unprojectZ;
        ww.y /= 1.0 + penumInPixels; // prefer smaller penumbra

        result += s * ww.x;
        penumbra += penum * ww.y;
        sum += ww;
    }

    result /= sum.x;
    penumbra = sum.y == 0.0 ? centerPenumbra : penumbra / sum.y;

    // Output
    #ifndef SIGMA_FIRST_PASS
        if( gStabilizationStrength != 0 )
    #endif
            gOut_Penumbra[ pixelPos ] = penumbra;

    gOut_Shadow_Translucency[ pixelPos ] = PackShadow( result );
}
