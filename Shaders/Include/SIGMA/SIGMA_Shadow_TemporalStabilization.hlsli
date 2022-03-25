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

    float2 data = gIn_Hit_ViewZ[ globalPos ];
    data.y = abs( data.y ) / NRD_FP16_VIEWZ_SCALE;

    s_Data[ sharedPos.y ][ sharedPos.x ] = data;

    SIGMA_TYPE s = gIn_Shadow_Translucency[ globalPos ];
    s = UnpackShadow( s );

    s_Shadow_Translucency[ sharedPos.y ][ sharedPos.x ] = s;
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
NRD_EXPORT void NRD_CS_MAIN( int2 threadPos : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    uint2 pixelPosUser = pixelPos + gRectOrigin;
    float2 pixelUv = ( float2( pixelPos ) + 0.5 ) * gInvRectSize;

    PRELOAD_INTO_SMEM;

    // Center data
    int2 smemPos = threadPos + BORDER;
    float2 centerData = s_Data[ smemPos.y ][ smemPos.x ];
    float centerHitDist = centerData.x;
    float centerSignNoL = float( centerData.x != 0.0 );
    float viewZ = centerData.y;

    // Early out
    [branch]
    if( viewZ > gDenoisingRange || ( centerHitDist == 0.0 && SIGMA_SHOW_TILES == 0 ) )
    {
        gOut_Shadow_Translucency[ pixelPos ] = PackShadow( s_Shadow_Translucency[ smemPos.y ][ smemPos.x ] );
        return;
    }

    // Local variance
    float sum = 0.0;
    SIGMA_TYPE m1 = 0;
    SIGMA_TYPE m2 = 0;
    SIGMA_TYPE input = 0;

    float viewZnearest = viewZ;
    int2 offseti = int2( BORDER, BORDER );

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 pos = threadPos + int2( dx, dy );
            float2 data = s_Data[ pos.y ][ pos.x ];

            SIGMA_TYPE s = s_Shadow_Translucency[ pos.y ][ pos.x ];
            float signNoL = float( data.x != 0.0 );
            float z = data.y;

            float w = 1.0;
            if( dx == BORDER && dy == BORDER )
                input = s;
            else
            {
                w = GetBilateralWeight( z, viewZ );
                w *= saturate( 1.0 - abs( centerSignNoL - signNoL ) );

                int2 t1 = int2( dx, dy ) - BORDER;
                if( ( abs( t1.x ) + abs( t1.y ) == 1 ) && z < viewZnearest )
                {
                    viewZnearest = z;
                    offseti = int2( dx, dy );
                }
            }

            m1 += s * w;
            m2 += s * s * w;
            sum += w;
        }
    }

    float invSum = STL::Math::PositiveRcp( sum );
    m1 *= invSum;
    m2 *= invSum;

    SIGMA_TYPE sigma = GetStdDev( m1, m2 );

    // Compute previous pixel position
    offseti -= BORDER;
    float2 offset = float2( offseti ) * gInvRectSize;
    float3 Xvnearest = STL::Geometry::ReconstructViewPosition( pixelUv + offset, gFrustum, viewZnearest, gOrthoMode );
    float3 Xnearest = STL::Geometry::AffineTransform( gViewToWorld, Xvnearest );
    float3 motionVector = gIn_ObjectMotion[ pixelPosUser + offseti ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv + offset, Xnearest, gWorldToClipPrev, motionVector, gIsWorldSpaceMotionEnabled );
    pixelUvPrev -= offset;

    float isInScreen = IsInScreen( pixelUvPrev );

    // Clamp UV to prevent sampling from "invalid" regions
    pixelUvPrev = clamp( pixelUvPrev, 1.5 / gRectSizePrev, 1.0 - 1.5 / gRectSizePrev );

    // Sample history
    SIGMA_TYPE history = BicubicFilterNoCorners( gIn_History, gLinearClamp, pixelUvPrev * gRectSizePrev, gInvScreenSize, SIGMA_USE_CATROM );
    history = UnpackShadow( history );

    // Clamp history
    float2 a = m1.xx;
    float2 b = history.xx;

    #ifdef SIGMA_TRANSLUCENT
        a.y = STL::Color::Luminance( m1.yzw );
        b.y = STL::Color::Luminance( history.yzw );
    #endif

    float2 ratio = abs( a - b ) / ( min( a, b ) + 0.05 );
    float2 ratioNorm = ratio / ( 1.0 + ratio );
    float2 scale = 1.0 + SIGMA_MAX_SIGMA_SCALE * ( 1.0 - STL::Math::Sqrt01( ratioNorm ) );

    #ifdef SIGMA_TRANSLUCENT
        sigma *= scale.xyyy;
    #else
        sigma *= scale.x;
    #endif

    SIGMA_TYPE inputMin = m1 - sigma;
    SIGMA_TYPE inputMax = m1 + sigma;
    SIGMA_TYPE historyClamped = clamp( history, inputMin, inputMax );

    // History weight
    float motionLength = length( pixelUvPrev - pixelUv );
    float2 historyWeight = 0.95 * lerp( 1.0, 0.7, ratioNorm );
    historyWeight = lerp( historyWeight, 0.1, saturate( motionLength / SIGMA_TS_MOTION_MAX_REUSE ) );
    historyWeight *= isInScreen;
    historyWeight *= float( gResetHistory == 0 );

    // Reduce history in regions with hard shadows
    float worldRadius = centerHitDist * gBlurRadiusScale;
    float unprojectZ = PixelRadiusToWorld( gUnproject, gOrthoMode, 1.0, viewZ );
    float pixelRadius = worldRadius * STL::Math::PositiveRcp( unprojectZ );
    historyWeight *= STL::Math::LinearStep( 0.0, 3.0, pixelRadius );

    // Combine with current frame
    SIGMA_TYPE result;
    result.x = lerp( input.x, historyClamped.x, historyWeight.x );

    #ifdef SIGMA_TRANSLUCENT
        result.yzw = lerp( input.yzw, historyClamped.yzw, historyWeight.y );
    #endif

    // Reference
    #if( SIGMA_REFERENCE == 1 )
        result = lerp( input, history, 0.95 * isInScreen );
    #endif

    result = Sanitize( result, input );

    // Debug
    #if( SIGMA_SHOW_TILES == 1 )
        float tileValue = gIn_Tiles[ pixelPos >> 4 ];
        tileValue = float( tileValue != 0.0 ); // optional, just to show fully discarded tiles

        #ifdef SIGMA_TRANSLUCENT
            result = lerp( float4( 0, 0, 1, 0 ), result, tileValue );
        #else
            result = tileValue;
        #endif

        // Show grid (works badly with TAA)
        result *= all( ( pixelPos & 15 ) != 0 );
    #endif

    // Output
    gOut_Shadow_Translucency[ pixelPos ] = PackShadow( result );
}
