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

float4 BicubicFilterNoCorners( Texture2D<float4> tex, SamplerState samp, float2 samplePos, float2 invTextureSize, compiletime const float sharpness )
{
    float2 centerPos = floor( samplePos - 0.5 ) + 0.5;
    float2 f = samplePos - centerPos;
    float2 f2 = f * f;
    float2 f3 = f * f2;
    float2 w0 = -sharpness * f3 + 2.0 * sharpness * f2 - sharpness * f;
    float2 w1 = ( 2.0 - sharpness ) * f3 - ( 3.0 - sharpness ) * f2 + 1.0;
    float2 w2 = -( 2.0 - sharpness ) * f3 + ( 3.0 - 2.0 * sharpness ) * f2 + sharpness * f;
    float2 w3 = sharpness * f3 - sharpness * f2;
    float2 wl2 = w1 + w2;
    float2 tc2 = invTextureSize * ( centerPos + w2 * STL::Math::PositiveRcp( wl2 ) );
    float2 tc0 = invTextureSize * ( centerPos - 1.0 );
    float2 tc3 = invTextureSize * ( centerPos + 2.0 );

    float w = wl2.x * w0.y;
    float4 color = tex.SampleLevel( samp, float2( tc2.x, tc0.y ), 0 ) * w;
    float sum = w;

    w = w0.x  * wl2.y;
    color += tex.SampleLevel( samp, float2( tc0.x, tc2.y ), 0 ) * w;
    sum += w;

    w = wl2.x * wl2.y;
    color += tex.SampleLevel( samp, float2( tc2.x, tc2.y ), 0 ) * w;
    sum += w;

    w = w3.x  * wl2.y;
    color += tex.SampleLevel( samp, float2( tc3.x, tc2.y ), 0 ) * w;
    sum += w;

    w = wl2.x * w3.y;
    color += tex.SampleLevel( samp, float2( tc2.x, tc3.y ), 0 ) * w;
    sum += w;

    color *= STL::Math::PositiveRcp( sum );

    return color;
}

float3 ClipAABB( float3 center, float3 extents, float3 prevSample )
{
    // note: only clips towards aabb center (but fast!)
    float3 d = prevSample - center;
    float3 dn = abs( d * STL::Math::PositiveRcp( extents ) );
    float maxd = max( dn.x, max( dn.y, dn.z ) );
    float3 t = center + d * STL::Math::PositiveRcp( maxd );

    return maxd > 1.0 ? t : prevSample;
}

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
    float2 pixelUv = ( float2( pixelPos ) + 0.5 ) * gInvScreenSize;
    float2 sampleUv = pixelUv + gJitter;

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

    m1 /= 9.0;
    m2 /= 9.0;

    float3 aabbCenter = m1;
    float3 aabbExtents = sqrt( abs( m2 - m1 * m1 ) );

    // Previous pixel position
    offseti -= BORDER;
    float2 offset = float2( offseti ) * gInvScreenSize;
    float3 Xvnearest = STL::Geometry::ReconstructViewPosition( pixelUv + offset, gCameraFrustum, viewZnearest, gIsOrtho );
    float3 Xnearest = STL::Geometry::AffineTransform( gViewToWorld, Xvnearest );
    float3 mvNearest = gIn_ObjectMotion[ pixelPos + offseti ] * ( gWorldSpaceMotion ? 1.0 : gInvScreenSize.xyy );
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv + offset, Xnearest, gWorldToClipPrev, mvNearest, gWorldSpaceMotion );
    pixelUvPrev -= offset;

    // History clamping
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float3 history = BicubicFilterNoCorners( gIn_History, gLinearSampler, pixelPosPrev, gInvScreenSize, TAA_HISTORY_SHARPNESS ).xyz;
    float3 historyClamped = ClipAABB( aabbCenter, aabbExtents, history );

    // History weight
    bool isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );
    float2 motion = pixelUvPrev - pixelUv;
    float motionAmount = saturate( length( motion ) / TAA_MOTION_MAX_REUSE );
    float historyWeight = lerp( TAA_MAX_HISTORY_WEIGHT, TAA_MIN_HISTORY_WEIGHT, motionAmount );
    historyWeight *= float( gMipBias != 0.0 && isInScreen );

    // Dithering
    STL::Rng::Initialize( pixelPos, gFrameIndex );
    float2 rnd = STL::Rng::GetFloat2( );
    float luma = STL::Color::Luminance( aabbCenter, STL_LUMINANCE_BT709 );
    float amplitude = lerp( 0.1, 0.0025, STL::Math::Sqrt01( luma ) );
    float2 dither = 1.0 + ( rnd - 0.5 ) * amplitude;
    historyClamped *= dither.x;

    // Final mix
    float3 result = lerp( input, historyClamped, historyWeight );

    // Split screen - noisy input / denoised output
    result = pixelUv.x < gSeparator ? input : result;

    // Split screen - vertical line
    float verticalLine = saturate( 1.0 - abs( pixelUv.x - gSeparator ) * gScreenSize.x / 3.5 );
    verticalLine = saturate( verticalLine / 0.5 );

    const float3 nvColor = float3( 118.0, 185.0, 0.0 ) / 255.0;
    result = lerp( result, nvColor * verticalLine, verticalLine * float( gSeparator != 0.0 ) );

    // Output
    gOut_History[ pixelPos ] = result;
}
