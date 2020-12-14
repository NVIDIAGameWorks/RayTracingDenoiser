/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "BindingBridge.hlsl"

NRI_RESOURCE( cbuffer, globalConstants, b, 0, 0 )
{
    float4x4 gViewToClip;
    float4 gFrustum;
    float2 gInvScreenSize;
    float2 gScreenSize;
    float gMetersToUnits;
    float gIsOrtho;
    float gUnproject;
    float gDebug;
    float gInf;
    float gReference;
    uint gFrameIndex;
    float gFramerateScale;

    float4x4 gWorldToClipPrev;
    float4x4 gViewToWorld;
    float2 gMotionVectorScale;
};

#include "NRD_Common.hlsl"

// Inputs
NRI_RESOURCE( Texture2D<float>, gIn_ViewZ, t, 0, 0 );
NRI_RESOURCE( Texture2D<float3>, gIn_ObjectMotion, t, 1, 0 );
NRI_RESOURCE( Texture2D<SHADOW_TYPE>, gIn_Shadow_Translucency, t, 2, 0 );
NRI_RESOURCE( Texture2D<SHADOW_TYPE>, gIn_History, t, 3, 0 );

// Outputs
NRI_RESOURCE( RWTexture2D<SHADOW_TYPE>, gOut_Shadow_Translucency, u, 0, 0 );

groupshared SHADOW_TYPE s_Data[ BUFFER_Y ][ BUFFER_X ];

void Preload( int2 sharedId, int2 globalId )
{
    float viewZ = gIn_ViewZ[ globalId ];
    SHADOW_TYPE s = gIn_Shadow_Translucency[ globalId ]; // no unpacking - temporal stabilization is done in perceptual gamma space

    uint p = STL::Packing::Rg16fToUint( float2( s.x, viewZ * NRD_FP16_VIEWZ_SCALE ) );

    #ifdef TRANSLUCENT_SHADOW
        s_Data[ sharedId.y ][ sharedId.x ] = float4( asfloat( p ), s.yzw );
    #else
        s_Data[ sharedId.y ][ sharedId.x ] = asfloat( p );
    #endif
}

[numthreads( GROUP_X, GROUP_Y, 1 )]
void main( int2 threadId : SV_GroupThreadId, int2 pixelPos : SV_DispatchThreadId, uint threadIndex : SV_GroupIndex )
{
    float2 pixelUv = ( float2( pixelPos ) + 0.5 ) * gInvScreenSize;

    PRELOAD_INTO_SMEM;

    // Position
    float viewZ = gIn_ViewZ[ pixelPos ];
    float3 Xv = STL::Geometry::ReconstructViewPosition( pixelUv, gFrustum, viewZ, gIsOrtho );
    float3 X = STL::Geometry::AffineTransform( gViewToWorld, Xv );

    // Early out
    [branch]
    if( abs( viewZ ) > abs( gInf ) )
    {
        #if( SHADOW_BLACK_OUT_INF_PIXELS == 1 )
            gOut_Shadow_Translucency[ pixelPos ] = 0;
        #endif
        return;
    }

    // Local variance
    float sum = 0.0;
    SHADOW_TYPE m1 = 0;
    SHADOW_TYPE m2 = 0;
    SHADOW_TYPE input = 0;

    [unroll]
    for( int dy = 0; dy <= BORDER * 2; dy++ )
    {
        [unroll]
        for( int dx = 0; dx <= BORDER * 2; dx++ )
        {
            int2 pos = threadId + int2( dx, dy );
            SHADOW_TYPE p = s_Data[ pos.y ][ pos.x ];

            SHADOW_TYPE s;
            float2 t = STL::Packing::UintToRg16f( asuint( p.x ) );
            s.x = t.x;
            #ifdef TRANSLUCENT_SHADOW
                s.yzw = p.yzw;
            #endif
            float z = t.y / NRD_FP16_VIEWZ_SCALE;

            float w = 1.0;
            if( dx == BORDER && dy == BORDER )
                input = s;
            else
                w = GetBilateralWeight( z, viewZ );

            m1 += s * w;
            m2 += s * s * w;
            sum += w;
        }
    }

    float invSum = STL::Math::PositiveRcp( sum );
    m1 *= invSum;
    m2 *= invSum;

    SHADOW_TYPE sigma = GetVariance( m1, m2 );

    // Compute previous pixel position
    float3 motionVector = gIn_ObjectMotion[ pixelPos ] * gMotionVectorScale.xyy;
    float2 pixelUvPrev = STL::Geometry::GetPrevUvFromMotion( pixelUv, X, gWorldToClipPrev, motionVector, IsWorldSpaceMotion() );
    float isInScreen = float( all( saturate( pixelUvPrev ) == pixelUvPrev ) );

    // Sample history
    SHADOW_TYPE history = gIn_History.SampleLevel( gLinearClamp, pixelUvPrev, 0.0 );

    // Clamp history
    float2 a = m1.xx;
    float2 b = history.xx;

    #ifdef TRANSLUCENT_SHADOW
        a.y = STL::Color::Luminance( m1.yzw );
        b.y = STL::Color::Luminance( history.yzw );
    #endif

    float2 ratio = abs( a - b ) / ( min( a, b ) + 0.05 );
    float2 ratioNorm = ratio / ( 1.0 + ratio );
    float2 scale = 1.0 + SHADOW_MAX_SIGMA_SCALE * ( 1.0 - STL::Math::Sqrt01( ratioNorm ) );

    #ifdef TRANSLUCENT_SHADOW
        sigma *= scale.xyyy;
    #else
        sigma *= scale.x;
    #endif

    SHADOW_TYPE inputMin = m1 - sigma;
    SHADOW_TYPE inputMax = m1 + sigma;
    SHADOW_TYPE historyClamped = clamp( history, inputMin, inputMax );

    // History weight
    float motionLength = length( pixelUvPrev - pixelUv );
    float2 historyWeight = 0.95 * lerp( 1.0, 0.7, ratioNorm );
    historyWeight = lerp( historyWeight, 0.1, saturate( motionLength / TS_MOTION_MAX_REUSE ) );
    historyWeight *= isInScreen;
    historyWeight *= float( gFrameIndex != 0 );

    // Combine with current frame
    SHADOW_TYPE result;
    result.x = lerp( input.x, historyClamped.x, historyWeight.x );

    #ifdef TRANSLUCENT_SHADOW
        result.yzw = lerp( input.yzw, historyClamped.yzw, historyWeight.y );
    #endif

    // Output
    gOut_Shadow_Translucency[ pixelPos ] = result;
}
