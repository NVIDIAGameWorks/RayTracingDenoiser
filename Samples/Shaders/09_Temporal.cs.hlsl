/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "09_Resources.hlsl"

NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 0, 1);
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 1, 1 );
NRI_RESOURCE( Texture2D<float3>, gIn_Color, t, 2, 1 );
NRI_RESOURCE( Texture2D<float3>, gIn_History, t, 3, 1 );

NRI_RESOURCE( RWTexture2D<float3>, gOut_History, u, 4, 1 );
NRI_RESOURCE( RWTexture2D<unorm float3>, gOut_Color, u, 5, 1 );

#define BORDER 1
#define GROUP_X 16
#define GROUP_Y 16
#define BUFFER_X ( GROUP_X + BORDER * 2 )
#define BUFFER_Y ( GROUP_Y + BORDER * 2 )
#define RENAMED_GROUP_Y ( ( GROUP_X * GROUP_Y ) / BUFFER_X )

groupshared float3 s_Color[ BUFFER_Y ][ BUFFER_X ];

float3 BicubicFilterNoCorners( Texture2D<float3> tex, SamplerState samp, float2 samplePos, float2 invTextureSize, compiletime const float sharpness )
{
    #define RGB1(uvx, uvy) float4( tex.SampleLevel( samp, float2( uvx, uvy ), 0 ).xyz, 1.0 )

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
    float4 color = RGB1( tc2.x, tc0.y ) * ( wl2.x * w0.y  ) +
                   RGB1( tc0.x, tc2.y ) * ( w0.x  * wl2.y ) +
                   RGB1( tc2.x, tc2.y ) * ( wl2.x * wl2.y ) +
                   RGB1( tc3.x, tc2.y ) * ( w3.x  * wl2.y ) +
                   RGB1( tc2.x, tc3.y ) * ( wl2.x * w3.y  );

    return color.xyz * STL::Math::PositiveRcp( color.w );

    #undef RGB1
}

void Preload( int2 sharedId, int2 globalId )
{
    float3 color = gIn_Color[ globalId ];
    color = STL::Color::LinearToYCoCg( color.xyz );

    s_Color[ sharedId.y ][ sharedId.x ] = color;
}

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
    if ( newId.y < RENAMED_GROUP_Y )
        Preload( newId, groupBase + newId );

    newId.y += RENAMED_GROUP_Y;

    if ( newId.y < BUFFER_Y )
        Preload( newId, groupBase + newId );

    GroupMemoryBarrierWithGroupSync( );

    // Calculate color distribution
    float3 colorMoment1 = 0;
    float3 colorMoment2 = 0;
    float3 thisPixelColor = 0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 pos = threadId + int2( dx, dy );
            float3 color = s_Color[ pos.y ][ pos.x ];

            if ( dx == BORDER && dy == BORDER )
                thisPixelColor = color;

            colorMoment1 += color;
            colorMoment2 += color * color;
        }
    }

    colorMoment1 /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 );
    colorMoment2 /= ( BORDER * 2 + 1 ) * ( BORDER * 2 + 1 );

    float3 colorVariance = colorMoment2 - colorMoment1 * colorMoment1;
    float3 colorSigma = sqrt( abs( colorVariance ) );

    // Position
    float viewZ = gIn_ViewZ[ pixelPos ];
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gCameraFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * ( gWorldSpaceMotion ? 1.0 : gInvScreenSize.xyy );
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, gWorldSpaceMotion );
    bool isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // History weight
    float2 motion = pixelUvPrev - pixelUv;
    float motionLength = length( motion );
    float historyWeight = lerp( TAA_MAX_HISTORY_WEIGHT, TAA_MIN_HISTORY_WEIGHT, saturate( motionLength / TAA_MOTION_MAX_REUSE ) );
    historyWeight *= float( gMipBias != 0.0 && isInScreen );

    // Mix with history
    float2 pixelPosPrev = saturate( pixelUvPrev ) * gScreenSize;
    float3 history = BicubicFilterNoCorners( gIn_History, gLinearSampler, pixelPosPrev, gInvScreenSize, TAA_HISTORY_SHARPNESS ) - float2( 0.0, 0.5 ).xyy; // 10_10_10_2_UNORM
    float3 colorMin = colorMoment1 - colorSigma;
    float3 colorMax = colorMoment1 + colorSigma;
    float3 historyClamped = clamp( history, colorMin, colorMax );

    float3 newHistory = lerp( thisPixelColor, historyClamped, historyWeight );
    float3 result = STL::Color::YCoCgToLinear( newHistory );

    // Dithering
    STL::Rng::Initialize( pixelPos, gFrameIndex + 567 );
    float2 rnd = STL::Rng::GetFloat2( );
    float amplitude = lerp( 0.2, 0.005, STL::Math::Sqrt01( newHistory.x ) );
    float2 dither = 1.0 + ( rnd - 0.5 ) * amplitude;
    result *= dither.x;
    newHistory *= dither.y;

    // Split screen - noisy input / denoised output
    float3 resultNoAA = STL::Color::YCoCgToLinear( thisPixelColor );
    result = pixelUv.x < gSeparator ? resultNoAA : result;

    // Split screen - vertical line
    float verticalLine = saturate( 1.0 - abs( pixelUv.x - gSeparator ) * gScreenSize.x / 3.5 );
    verticalLine = saturate( verticalLine / 0.5 );

    const float3 nvColor = float3( 118.0, 185.0, 0.0 ) / 255.0;
    result = lerp( result, nvColor * verticalLine, verticalLine * float( gSeparator != 0.0 ) );

    // Output
    gOut_Color[ pixelPos ] = result;
    gOut_History[ pixelPos ] = newHistory + float2( 0.0, 0.5 ).xyy; // 10_10_10_2_UNORM
}