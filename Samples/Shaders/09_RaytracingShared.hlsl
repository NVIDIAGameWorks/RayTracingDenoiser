/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

NRI_RESOURCE( RaytracingAccelerationStructure, gWorldTlas, t, 0, 2 );
NRI_RESOURCE( RaytracingAccelerationStructure, gLightTlas, t, 1, 2 );
NRI_RESOURCE( Buffer<uint4>, gIn_PrimitiveData, t, 2, 2 );
NRI_RESOURCE( Buffer<float4>, gIn_InstanceData, t, 3, 2 );
NRI_RESOURCE( Texture2D<float4>, gIn_Textures[], t, 4, 2 );

//====================================================================================================================================

#define FLAG_FIRST_BIT                  20 // this + number of CPU flags must be <= 24
#define INSTANCE_ID_MASK                ( ( 1 << FLAG_FIRST_BIT ) - 1 )

// CPU flags
#define FLAG_OPAQUE_OR_ALPHA_OPAQUE     0x01
#define FLAG_TRANSPARENT                0x02
#define FLAG_EMISSION                   0x04
#define FLAG_FORCED_EMISSION            0x08

// Local flags
#define FLAG_BACKFACE                   0x10
#define FLAG_UNUSED1                    0x20
#define FLAG_UNUSED2                    0x40
#define FLAG_UNUSED3                    0x80

#define FLAGS_ALL                       ( FLAG_OPAQUE_OR_ALPHA_OPAQUE | FLAG_EMISSION | FLAG_FORCED_EMISSION | FLAG_TRANSPARENT )
#define FLAGS_ONLY_EMISSION             ( FLAG_EMISSION | FLAG_FORCED_EMISSION )
#define FLAGS_ONLY_TRANSPARENT          ( FLAG_TRANSPARENT )
#define FLAGS_IGNORE_TRANSPARENT        ( FLAG_OPAQUE_OR_ALPHA_OPAQUE | FLAG_EMISSION | FLAG_FORCED_EMISSION )
#define FLAGS_DEFAULT                   FLAGS_IGNORE_TRANSPARENT

struct IntersectionAttributes
{
    float2 barycentrics;
};

struct Payload
{
    uint pack0;
    uint pack1;
    uint primitiveId;
    float tmin;

    float2 GetMipAndCone( )
    { return asfloat( uint2( pack0, pack1 ) ); }
};

struct UnpackedPayload
{
    float2 mipAndCone;
    float3 barycentrics;
    float tmin;
    uint primitiveId;
    uint instanceIdAndFlags;

    bool IsTransparent()
    { return ( instanceIdAndFlags & ( FLAG_TRANSPARENT << 24 ) ) != 0; }

    bool IsEmissive()
    { return ( instanceIdAndFlags & ( ( FLAG_EMISSION | FLAG_FORCED_EMISSION ) << 24 ) ) != 0; }

    bool IsForcedEmission()
    { return ( instanceIdAndFlags & ( FLAG_FORCED_EMISSION << 24 ) ) != 0; }

    bool IsBackFace()
    { return ( instanceIdAndFlags & ( FLAG_BACKFACE << 24 ) ) != 0; }

    uint GetFlags()
    { return instanceIdAndFlags & 0xFF000000; }

    uint GetInstanceId()
    { return instanceIdAndFlags & 0x00FFFFFF; }
};

Payload InitPayload( float2 mipAndCone )
{
    Payload p = ( Payload ) 0;
    p.tmin = INF;
    p.pack0 = asuint( mipAndCone.x );
    p.pack1 = asuint( mipAndCone.y );

    return p;
}

Payload PackPayload( float tmin, uint instanceIdAndFlags, uint primitiveId, bool isBackFace, float2 barycentrics )
{
    Payload p = ( Payload ) 0;
    p.pack0 = instanceIdAndFlags & INSTANCE_ID_MASK;
    p.pack0 |= ( ( instanceIdAndFlags >> FLAG_FIRST_BIT ) | ( isBackFace ? FLAG_BACKFACE : 0 ) ) << 24;
    p.pack1 = STL::Packing::RgbaToUint( barycentrics.xyyy, 16, 16 );
    p.primitiveId = primitiveId;
    p.tmin = tmin;

    return p;
}

UnpackedPayload UnpackPayload( float tmin, uint instanceIdAndFlags, uint primitiveId, bool isBackFace, float2 barycentrics, float2 mipAndCone )
{
    UnpackedPayload u = ( UnpackedPayload ) 0;
    u.mipAndCone = mipAndCone;
    u.barycentrics.yz = barycentrics;
    u.barycentrics.x = 1.0 - u.barycentrics.y - u.barycentrics.z;
    u.tmin = tmin;
    u.primitiveId = primitiveId;
    u.instanceIdAndFlags = instanceIdAndFlags & INSTANCE_ID_MASK;
    u.instanceIdAndFlags |= ( ( instanceIdAndFlags >> FLAG_FIRST_BIT ) | ( isBackFace ? FLAG_BACKFACE : 0 ) ) << 24;

    return u;
}

UnpackedPayload UnpackPayload( Payload p, float2 mipAndCone )
{
    UnpackedPayload u = ( UnpackedPayload ) 0;
    u.mipAndCone = mipAndCone;
    u.barycentrics.yz = STL::Packing::UintToRgba( p.pack1, 16, 16 ).xy;
    u.barycentrics.x = 1.0 - u.barycentrics.y - u.barycentrics.z;
    u.tmin = p.tmin;
    u.primitiveId = p.primitiveId;
    u.instanceIdAndFlags = p.pack0;

    return u;
}

//====================================================================================================================================

struct GeometryProps
{
    float3 motion;
    float3 X;
    float4 T;
    float3 N;
    float2 uv;
    float mip;
    float viewZ;
    float tmin;
    uint textureOffsetAndFlags;

    float3 GetXWithOffset()
    {
        // Moves the ray origin further from surface to prevent self-intersections. Minimizes the distance for best results ( taken from RT Gems "A Fast and Robust Method for Avoiding Self-Intersection" )
        int3 o = int3( N * 256.0 );
        float3 a = asfloat( asint( X ) + ( X < 0.0 ? -o : o ) );
        float3 b = X + N * ( 1.0 / 65536.0 );

        return abs( X ) < ( 1.0f / 32.0 ) ? b : a;
    }

    bool IsTransparent()
    { return ( textureOffsetAndFlags & ( FLAG_TRANSPARENT << 24 ) ) != 0; }

    bool IsEmissive()
    { return ( textureOffsetAndFlags & ( ( FLAG_EMISSION | FLAG_FORCED_EMISSION ) << 24 ) ) != 0; }

    bool IsForcedEmission()
    { return ( textureOffsetAndFlags & ( FLAG_FORCED_EMISSION << 24 ) ) != 0; }

    bool IsBackFace()
    { return ( textureOffsetAndFlags & ( FLAG_BACKFACE << 24 ) ) != 0; }

    uint GetFlags()
    { return textureOffsetAndFlags & 0xFF000000; }

    uint GetBaseTexture()
    { return ( textureOffsetAndFlags & 0x00FFFFFF ) << 2; } // 4 textures per object

    float3 GetForcedEmissionColor()
    { return ( textureOffsetAndFlags & 0x1 ) ? float3( 1, 0, 0 ) : float3( 0, 1, 0 ); }

    bool IsSky()
    { return viewZ == INF; }
};

GeometryProps GetGeometryProps( UnpackedPayload unpackedPayload, float3 rayOrigin, float3 rayDirection, bool useSimplifiedModel = false )
{
    float3 Xprev;
    GeometryProps props = ( GeometryProps )0;
    props.tmin = unpackedPayload.tmin;

    [branch]
    if( unpackedPayload.tmin == INF )
    {
        props.mip = unpackedPayload.mipAndCone.x + MAX_MIP_LEVEL;
        props.X = rayOrigin + rayDirection * INF;
        props.viewZ = INF;

        Xprev = props.X;
    }
    else
    {
        // Instance
        uint instanceDataOffset = unpackedPayload.GetInstanceId();
        instanceDataOffset *= 6;

        float4 instanceData0 = gIn_InstanceData[ instanceDataOffset ];
        float4 instanceData1 = gIn_InstanceData[ instanceDataOffset + 1 ];
        float4 instanceData2 = gIn_InstanceData[ instanceDataOffset + 2 ];

        float3x4 mWorldToWorldPrev;
        mWorldToWorldPrev[ 0 ] = gIn_InstanceData[ instanceDataOffset + 3 ];
        mWorldToWorldPrev[ 1 ] = gIn_InstanceData[ instanceDataOffset + 4 ];
        mWorldToWorldPrev[ 2 ] = gIn_InstanceData[ instanceDataOffset + 5 ];

        float3x3 mObjectToWorld;
        mObjectToWorld[ 0 ] = instanceData0.xyz;
        mObjectToWorld[ 1 ] = instanceData1.xyz;
        mObjectToWorld[ 2 ] = instanceData2.xyz;

        // Primitive data
        uint primitiveIndex = unpackedPayload.primitiveId;
        primitiveIndex += asuint( instanceData0.w );
        primitiveIndex *= 4;

        uint4 primitiveData0 = gIn_PrimitiveData[ primitiveIndex ];
        uint4 primitiveData1 = gIn_PrimitiveData[ primitiveIndex + 1 ];
        uint4 primitiveData2 = gIn_PrimitiveData[ primitiveIndex + 2 ];
        uint4 primitiveData3 = gIn_PrimitiveData[ primitiveIndex + 3 ];

        float2 uv0 = STL::Packing::UintToRg16f( primitiveData0.x );
        float2 uv1 = STL::Packing::UintToRg16f( primitiveData0.y );
        float2 uv2 = STL::Packing::UintToRg16f( primitiveData0.z );
        float2 nfx_nfy = STL::Packing::UintToRg16f( primitiveData0.w );

        float2 nfz_worldToUvUnits = STL::Packing::UintToRg16f( primitiveData1.x );
        float2 n0x_n0y = STL::Packing::UintToRg16f( primitiveData1.y );
        float2 n0z_n1x = STL::Packing::UintToRg16f( primitiveData1.z );
        float2 n1y_n1z = STL::Packing::UintToRg16f( primitiveData1.w );

        float2 n2x_n2y = STL::Packing::UintToRg16f( primitiveData2.x );
        float2 n2z_t0x = STL::Packing::UintToRg16f( primitiveData2.y );
        float2 t0y_t0z = STL::Packing::UintToRg16f( primitiveData2.z );
        float2 t1x_t1y = STL::Packing::UintToRg16f( primitiveData2.w );

        float2 t1z_t2x = STL::Packing::UintToRg16f( primitiveData3.x );
        float2 t2y_t2z = STL::Packing::UintToRg16f( primitiveData3.y );
        float2 b0s_b1s = STL::Packing::UintToRg16f( primitiveData3.z );
        float b2s = STL::Packing::UintToRg16f( primitiveData3.w ).x;

        if( useSimplifiedModel || USE_SIMPLIFIED_BRDF_MODEL )
        {
            // Material & flags
            props.textureOffsetAndFlags = asuint( instanceData2.w );
            props.textureOffsetAndFlags |= unpackedPayload.GetFlags();

            // Normal
            float3 N = float3( nfx_nfy, nfz_worldToUvUnits.x );
            N = STL::Geometry::RotateVector( mObjectToWorld, N );
            N = normalize( N );
            props.N = unpackedPayload.IsBackFace() ? -N : N;

            // Mip level (just a stub to not overload AHS)
            props.mip = MAX_MIP_LEVEL - 4; // 128x128
        }
        else
        {
            // Barycentrics
            float3 barycentrics = unpackedPayload.barycentrics;

            // Uv
            props.uv = barycentrics.x * uv0 + barycentrics.y * uv1 + barycentrics.z * uv2;

            // Normal
            float3 n0 = float3( n0x_n0y, n0z_n1x.x );
            float3 n1 = float3( n0z_n1x.y, n1y_n1z );
            float3 n2 = float3( n2x_n2y, n2z_t0x.x );

            float3 N = barycentrics.x * n0 + barycentrics.y * n1 + barycentrics.z * n2;
            N = STL::Geometry::RotateVector( mObjectToWorld, N );
            N = normalize( N );
            props.N = unpackedPayload.IsBackFace() ? -N : N;

            // Tangent
            float4 t0 = float4( n2z_t0x.y, t0y_t0z, b0s_b1s.x );
            float4 t1 = float4( t1x_t1y, t1z_t2x.x, b0s_b1s.y );
            float4 t2 = float4( t1z_t2x.y, t2y_t2z, b2s );

            float4 T = barycentrics.x * t0 + barycentrics.y * t1 + barycentrics.z * t2;
            T.w = T.w * 2.0 - 1.0; // binormal sign
            T.xyz = STL::Geometry::RotateVector( mObjectToWorld, T.xyz );
            T.xyz = normalize( T.xyz );
            props.T = T;

            // Texture offset & flags
            props.textureOffsetAndFlags = asuint( instanceData1.w );
            props.textureOffsetAndFlags |= unpackedPayload.GetFlags();

            // Handling object scale embedded into the transformation matrix (assuming uniform scale)
            float invObjectScale = STL::Math::Rsqrt( STL::Math::LengthSquared( instanceData0.xyz ) );

            // Mip level (TODO: doesn't take into account integrated AO / SO - i.e. diffuse = lowest mip, but what if we see the sky through a tiny hole?)
            #if( USE_SIMPLE_MIP_SELECTION == 1 )
                float NoR = abs( dot( rayDirection, props.N ) );

                float a = unpackedPayload.tmin;
                a *= unpackedPayload.mipAndCone.y;
                a *= STL::Math::PositiveRcp( NoR );
                a *= nfz_worldToUvUnits.y * invObjectScale;

                float mip = log2( a );
            #else
                // Basis
                float3 xDir = normalize( cross( props.N, rayDirection ) );
                float3 yDir = cross( rayDirection, xDir );

                // For Y we must avoid hitting a triangle plane behind us
                float NoS = dot( rayDirection, props.N );
                float dYp = NoS + dot( yDir, props.N ) * unpackedPayload.mipAndCone.y;
                float dYn = NoS - dot( yDir, props.N ) * unpackedPayload.mipAndCone.y;

                // Ray differentials
                float3 rayDirectionOffsetY = rayDirection + ( dYn < dYp ? -1.0 : 1.0 ) * yDir * unpackedPayload.mipAndCone.y;
                float3 rayDirectionOffsetX = rayDirection + xDir * unpackedPayload.mipAndCone.y;

                // Finding intersection points ( with the triangle plane ) and corresponding deltas
                float kx = NoS / dot( rayDirectionOffsetX, props.N );
                float ky = NoS / dot( rayDirectionOffsetY, props.N );
                float3 dx = rayDirectionOffsetX * kx - rayDirection;
                float3 dy = rayDirectionOffsetY * ky - rayDirection;

                // Transforming from "world" to "uv" space
                float k = nfz_worldToUvUnits.y * unpackedPayload.tmin * invObjectScale;
                float kSq = k * k;
                float duvdxMulTexSizeSq = dot( dx, dx ) * kSq;
                float duvdyMulTexSizeSq = dot( dy, dy ) * kSq;

                // No anisotropy since it doesn't make a lot of sense here - no real ddx/ddy, SampleLevel is used instead
                // This value doesn't include "textureSize", because log2( x * size ) = log2( x ) + log2( size )
                float mip = STL::Filtering::GetMipmapLevel( duvdxMulTexSizeSq, duvdyMulTexSizeSq, 1.0 );
            #endif

            mip += MAX_MIP_LEVEL;
            mip = max( mip, 0.0 );

            // Propagation
            mip += unpackedPayload.mipAndCone.x;
            props.mip = mip;
        }

        // Position
        props.X = rayOrigin + rayDirection * unpackedPayload.tmin;
        props.viewZ = STL::Geometry::AffineTransform( gWorldToView, props.X ).z;

        Xprev = STL::Geometry::AffineTransform( mWorldToWorldPrev, props.X );
    }

    // Motion (world or screen)
    float4 clip = STL::Geometry::ProjectiveTransform( gWorldToClip, props.X );
    float4 clipPrev = STL::Geometry::ProjectiveTransform( gWorldToClipPrev, Xprev );
    float2 sampleUv = ( clip.xy / clip.w ) * float2( 0.5, -0.5 ) + 0.5;
    float2 sampleUvPrev = ( clipPrev.xy / clipPrev.w ) * float2( 0.5, -0.5 ) + 0.5;
    float2 surfaceMotion = ( sampleUvPrev - sampleUv ) * gScreenSize;
    float3 worldMotion = Xprev - props.X;
    props.motion = gWorldSpaceMotion ? worldMotion : surfaceMotion.xyy;

    return props;
}

//====================================================================================================================================

struct MaterialProps
{
    float3 Lsum; // direct (unshadowed) + emissive
    float3 N;
    float3 baseColor;
    float roughness;
    float metalness;
    bool isEmissive;
};

/*
Returns:
    .x - for visibility (emissive, shadow)
        We must avoid using lower mips because it can lead to significant increase in AHS invocations. Mips lower than 128x128 are skipped!
    .y - for sampling (normals...)
        Negative MIP bias is applied
    .z - for sharp sampling
        Negative MIP bias is applied (can be more negative...)
*/
float3 GetRealMip( uint textureIndex, float mip )
{
    float w, h;
    gIn_Textures[ textureIndex ].GetDimensions( w, h ); // TODO: if I only had it as a constant...

    // Taking into account real dimensions of the current texture
    float mipNum = log2( w );
    float realMip = mip + mipNum - MAX_MIP_LEVEL;

    float3 mips;
    mips.x = min( realMip, mipNum - 7.0 );
    mips.y = realMip + gMipBias * 0.5;
    mips.z = realMip + gMipBias;

    return max( mips, 0.0 ) * gUseMipmapping;
}

MaterialProps GetMaterialProps( GeometryProps geometryProps, float3 rayDirection, bool useSimplifiedModel = false  )
{
    MaterialProps props = ( MaterialProps )0;

    float3 Csky = GetSkyIntensity( rayDirection, gSunDirection, gSunAngularDiameter );
    float3 Csun = GetSkyIntensity( gSunDirection, gSunDirection );

    [branch]
    if( geometryProps.IsSky() )
    {
        props.Lsum = Csky * gExposure;
        props.isEmissive = true;

        return props;
    }

    // Shadow fix
    float NoL = dot( geometryProps.N, gSunDirection );
    float shadow = STL::Math::SmoothStep( -0.03, 0.03, NoL );

    if( useSimplifiedModel || USE_SIMPLIFIED_BRDF_MODEL )
    {
        // Material data
        float3 baseColor = STL::Packing::UintToRgba( geometryProps.textureOffsetAndFlags, 8, 8, 8, 0 ).xyz;
        baseColor = STL::Color::GammaToLinear( baseColor );

        float metalness = 0.0;
        float roughness = 0.0;

        ModifyMaterial( baseColor, metalness, roughness );

        float3 albedo, Rf0;
        STL::BRDF::ConvertDiffuseMetalnessToAlbedoRf0( baseColor, metalness, albedo, Rf0 );

        // Emission
        float3 emissive = geometryProps.IsForcedEmission() ? geometryProps.GetForcedEmissionColor() : baseColor;
        emissive *= gEmissionIntensity * float( geometryProps.IsEmissive() );

        // Direct lighting (no shadow)
        float3 Cimp = lerp( Csky, Csun, STL::Math::SmoothStep( 0.0, 0.2, roughness ) ); // sky importance sampling

        float m = roughness * roughness;
        float3 C = albedo * Csun + Rf0 * m * Cimp;
        float NoL = dot( geometryProps.N, gSunDirection );
        float Kdiff = saturate( NoL ) / STL::Math::Pi( 1.0 );
        float3 Lsum = Kdiff * C;
        Lsum *= shadow;

        props.isEmissive = STL::Color::Luminance( emissive ) != 0.0;
        props.N = geometryProps.N;
        props.baseColor = baseColor;
        props.metalness = metalness;
        props.roughness = roughness;
        props.Lsum = props.isEmissive ? emissive : Lsum;
        props.Lsum *= gExposure;
    }
    else
    {
        uint baseTexture = geometryProps.GetBaseTexture();
        float3 mips = GetRealMip( baseTexture, geometryProps.mip );

        // Material data
        float4 baseColor = gIn_Textures[ baseTexture ].SampleLevel( gLinearMipmapLinearSampler, geometryProps.uv, mips.z );
        baseColor.xyz *= geometryProps.IsTransparent() ? 1.0 : STL::Math::PositiveRcp( baseColor.w ); // Correctly handle BC1 with pre-multiplied alpha
        baseColor.xyz = saturate( baseColor.xyz );

        float3 materialProps = gIn_Textures[ baseTexture + 1 ].SampleLevel( gLinearMipmapLinearSampler, geometryProps.uv, mips.z ).xyz;
        float roughness = materialProps.y;
        float metalness = materialProps.z;

        ModifyMaterial( baseColor.xyz, metalness, roughness );

        // Emission
        float3 emissive = gIn_Textures[ baseTexture + 3 ].SampleLevel( gLinearMipmapLinearSampler, geometryProps.uv, mips.x ).xyz;
        emissive *= ( baseColor.xyz + 0.01 ) / ( max( baseColor.x, max( baseColor.y, baseColor.z ) ) + 0.01 );
        emissive = geometryProps.IsForcedEmission() ? geometryProps.GetForcedEmissionColor() : emissive;
        emissive *= gEmissionIntensity * float( geometryProps.IsEmissive() );

        // Normal
        float2 packedNormal = gIn_Textures[ baseTexture + 2 ].SampleLevel( gLinearMipmapLinearSampler, geometryProps.uv, mips.y ).xy;
        packedNormal = gUseNormalMap ? packedNormal : ( 127.0 / 255.0 );
        float3 N = STL::Geometry::TransformLocalNormal( packedNormal, geometryProps.T, geometryProps.N );

        // Direct lighting (no shadow)
        float3 Lsum = 0;
        if( shadow != 0.0 )
        {
            float3 albedo, Rf0;
            STL::BRDF::ConvertDiffuseMetalnessToAlbedoRf0( baseColor.xyz, metalness, albedo, Rf0 );

            float3 Cdiff, Cspec;
            STL::BRDF::DirectLighting( N, gSunDirection, -rayDirection, Rf0, roughness, Cdiff, Cspec );

            float3 Cimp = lerp( Csky, Csun, STL::Math::SmoothStep( 0.0, 0.2, roughness ) ); // sky importance sampling

            Lsum = Cdiff * albedo * Csun + Cspec * Cimp;
            Lsum *= shadow;
        }

        props.isEmissive = STL::Color::Luminance( emissive ) != 0.0;
        props.N = N;
        props.baseColor = baseColor.xyz;
        props.metalness = metalness;
        props.roughness = roughness;
        props.Lsum = props.isEmissive ? emissive : Lsum;
        props.Lsum *= gExposure;
    }

    return props;
}
