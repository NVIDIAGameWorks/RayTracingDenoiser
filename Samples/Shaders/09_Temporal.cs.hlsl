/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "09_Resources.hlsl"

NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 0, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_ComposedLighting_ViewZ, t, 1, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_TransparentLighting, t, 2, 1 );
NRI_RESOURCE( Texture2D<float4>, gIn_History, t, 3, 1 );

NRI_RESOURCE( RWTexture2D<float3>, gOut_History, u, 4, 1 );

#define BORDER 1
#define GROUP_X 16
#define GROUP_Y 16
#define BUFFER_X ( GROUP_X + BORDER * 2 )
#define BUFFER_Y ( GROUP_Y + BORDER * 2 )
#define RENAMED_GROUP_Y ( ( GROUP_X * GROUP_Y ) / BUFFER_X )

groupshared float4 s_Data[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    float4 color_viewZ = gIn_ComposedLighting_ViewZ[ globalId ];
    color_viewZ.xyz = ApplyPostLightingComposition( globalId, color_viewZ.xyz, gIn_TransparentLighting );
    color_viewZ.w = abs( color_viewZ.w ) * STL::Math::Sign( gNearZ ) / NRD_FP16_VIEWZ_SCALE;

    s_Data[ sharedId.y ][ sharedId.x ] = color_viewZ;
}

#define MOTION_LENGTH_SCALE 16.0

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = float2( pixelPos + 0.5 ) * gInvRectSize;

    // Rename the 16x16 group into a 18x14 group + some idle threads in the end
    float linearId = ( threadIndex + 0.5 ) / BUFFER_X;
    int2 newId = int2( frac( linearId ) * BUFFER_X, linearId );
    int2 groupBase = pixelPos - threadId - BORDER;

    // Preload into shared memory
    if( newId.y < RENAMED_GROUP_Y )
        Preload( newId, groupBase + newId );

    newId.y += RENAMED_GROUP_Y;

    if( newId.y < BUFFER_Y )
        Preload( newId, groupBase + newId );

    GroupMemoryBarrierWithGroupSync( );

    // Neighborhood
    float3 m1 = 0;
    float3 m2 = 0;
    float3 input = 0;

    float viewZ = s_Data[ threadId.y + BORDER ][threadId.x + BORDER ].w;
    float viewZnearest = viewZ;
    int2 offseti = int2( BORDER, BORDER );

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 t = int2( dx, dy );
            int2 smemPos = threadId + t;
            float4 data = s_Data[ smemPos.y ][ smemPos.x ];

            if( dx == BORDER && dy == BORDER )
                input = data.xyz;
            else
            {
                int2 t1 = t - BORDER;
                if( ( abs( t1.x ) + abs( t1.y ) == 1 ) && abs( data.w ) < abs( viewZnearest ) )
                {
                    viewZnearest = data.w;
                    offseti = t;
                }
            }

            m1 += data.xyz;
            m2 += data.xyz * data.xyz;
        }
    }

    float invSum = 1.0 / ( ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 ) );
    m1 *= invSum;
    m2 *= invSum;

    float3 sigma = sqrt( abs( m2 - m1 * m1 ) );

    // Previous pixel position
    offseti -= BORDER;
    float2 offset = float2( offseti ) * gInvRectSize;
    float3 Xvnearest = STL::Geometry::ReconstructViewPosition( pixelUv + offset, gCameraFrustum, viewZnearest, gIsOrtho );
    float3 Xnearest = STL::Geometry::AffineTransform( gViewToWorld, Xvnearest );
    float3 mvNearest = gIn_ObjectMotion[ pixelPos + offseti ] * ( gWorldSpaceMotion ? 1.0 : gInvRectSize.xyy );
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv + offset, Xnearest, gWorldToClipPrev, mvNearest, gWorldSpaceMotion );
    pixelUvPrev -= offset;

    // History clamping
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gRectSizePrev;
    float3 history = BicubicFilterNoCorners( gIn_History, gLinearSampler, pixelPosPrev, gInvScreenSize, TAA_HISTORY_SHARPNESS ).xyz;
    float3 historyClamped = STL::Color::Clamp( m1.xyzz, sigma.xyzz, history.xyzz ).xyz;

    // History weight
    bool isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );
    float2 pixelMotion = pixelUvPrev - pixelUv;
    float motionAmount = saturate( length( pixelMotion ) / TAA_MOTION_MAX_REUSE );
    float historyWeight = lerp( TAA_MAX_HISTORY_WEIGHT, TAA_MIN_HISTORY_WEIGHT, motionAmount );
    historyWeight *= float( gMipBias != 0.0 && isInScreen );

    // Dithering
    STL::Rng::Initialize( pixelPos, gFrameIndex );
    float2 rnd = STL::Rng::GetFloat2( );
    float luma = STL::Color::Luminance( m1, STL_LUMINANCE_BT709 );
    float amplitude = lerp( 0.1, 0.0025, STL::Math::Sqrt01( luma ) );
    float2 dither = 1.0 + ( rnd - 0.5 ) * amplitude;
    historyClamped *= dither.x;

    // Final mix
    float3 result = lerp( input, historyClamped, historyWeight );

    // Split screen - noisy input / denoised output
    result = pixelUv.x < gSeparator ? input : result;

    // Split screen - vertical line
    float verticalLine = saturate( 1.0 - abs( pixelUv.x - gSeparator ) * gRectSize.x / 3.5 );
    verticalLine = saturate( verticalLine / 0.5 );
    verticalLine *= float( gSeparator != 0.0 );
    verticalLine *= float( gScreenSize.x == gRectSize.x );

    const float3 nvColor = float3( 118.0, 185.0, 0.0 ) / 255.0;
    result = lerp( result, nvColor * verticalLine, verticalLine );

    // Output
    gOut_History[ pixelPos ] = result;
}
