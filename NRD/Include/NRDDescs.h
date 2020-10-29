/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nrd
{
    struct Denoiser;

    enum class Result : uint32_t
    {
        SUCCESS,
        FAILURE,
        INVALID_ARGUMENT,

        MAX_NUM
    };

    enum class Method : uint32_t
    {
        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFFA, IN_DIFFB
        // OUTPUTS - OUT_DIFF_HIT
        NRD_DIFFUSE,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_HIT
        // OUTPUTS - OUT_SPEC_HIT
        NRD_SPECULAR,

        // INPUTS - IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SHADOW, OUT_SHADOW
        // OUTPUTS - OUT_SHADOW
        NRD_SHADOW,

        // INPUTS - IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SHADOW, IN_TRANSLUCENCY, OUT_SHADOW_TRANSLUCENCY
        // OUTPUTS - OUT_SHADOW_TRANSLUCENCY
        NRD_TRANSLUCENT_SHADOW,

        // INPUTS - SVGF_INPUT
        // OUTPUTS - SVGF_OUTPUT
        SVGF,

        MAX_NUM
    };

    enum class ResourceType : uint32_t
    {
        // INPUTS ===========================================================================================================================

        // 3D world space motion (RGBA16f+) or 2D screen space motion (RG16f+), MVs must be non-jittered, old = new + MV
        IN_MV,

        // See "NRD.hlsl/_NRD_FrontEnd_UnpackNormalAndRoughness" (RGBA8+ or R10G10B10A2+ depending on encoding)
        IN_NORMAL_ROUGHNESS,

        // Linear view depth - "+" for LHS, "-" for RHS (R16f+)
        IN_VIEWZ,

        // Data must be packed using "NRD.hlsl/NRD_FrontEnd_PackShadow" (RG16f+). INF pixels must be cleared with NRD_INF_SHADOW macro
        IN_SHADOW,

        // Data must be packed using "NRD.hlsl/NRD_FrontEnd_PackDiffuse" (RGBA16f+). For IN_DIFFB INF pixels must be cleared with NRD_INF_DIFF_B macro
        IN_DIFFA,
        IN_DIFFB,

        // Data must be packed using "NRD.hlsl/NRD_FrontEnd_PackSpecular" (RGBA16f+)
        IN_SPEC_HIT,

        // 3-channel translucency (RGBA8+)
        IN_TRANSLUCENCY,

        // OUTPUTS ==========================================================================================================================

        // IMPORTANT: These textures could potentially be used as history buffers. It's not recommended to reuse them after denoising or clean before denoising.

        // .x - shadow (R8+)
        // Data must be unpacked using "NRD.hlsl/NRD_BackEnd_UnpackShadow"
        OUT_SHADOW,

        // .x - shadow, .yzw - translucency (RGBA8+), assuming that it will be used as: translucent shadow = lerp( .yzw, 1, .x )
        // Data must be unpacked using "NRD.hlsl/NRD_BackEnd_UnpackShadow"
        OUT_SHADOW_TRANSLUCENCY,

        // .xyz - radiance, .w - normalized hit distance (RGBA16f+)
        // Data must be unpacked using "NRD.hlsl/NRD_BackEnd_UnpackDiffuse"
        OUT_DIFF_HIT,

        // .xyz - radiance, .w - normalized hit distance (RGBA16f+)
        // Data must be unpacked using "NRD.hlsl/NRD_BackEnd_UnpackSpecular"
        OUT_SPEC_HIT,

        // POOLS ============================================================================================================================

        // Can be reused after denoising
        TRANSIENT_POOL,

        // Dedicated to NRD, can't be reused
        PERMANENT_POOL,

        MAX_NUM,

        // RENAMINGS ========================================================================================================================

        // .xyz - signal1, .w - signal2
        IN_SVGF = IN_SPEC_HIT,

        // .xyz - signal1, .w - signal2
        OUT_SVGF = OUT_SPEC_HIT,

    };

    enum class Format : uint32_t
    {
        R8_UNORM,
        R8_SNORM,
        R8_UINT,
        R8_SINT,

        RG8_UNORM,
        RG8_SNORM,
        RG8_UINT,
        RG8_SINT,

        RGBA8_UNORM,
        RGBA8_SNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_SRGB,

        R16_UNORM,
        R16_SNORM,
        R16_UINT,
        R16_SINT,
        R16_SFLOAT,

        RG16_UNORM,
        RG16_SNORM,
        RG16_UINT,
        RG16_SINT,
        RG16_SFLOAT,

        RGBA16_UNORM,
        RGBA16_SNORM,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_SFLOAT,

        R32_UINT,
        R32_SINT,
        R32_SFLOAT,

        RG32_UINT,
        RG32_SINT,
        RG32_SFLOAT,

        RGB32_UINT,
        RGB32_SINT,
        RGB32_SFLOAT,

        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_SFLOAT,

        R10_G10_B10_A2_UNORM,
        R10_G10_B10_A2_UINT,
        R11_G11_B10_UFLOAT,
        R9_G9_B9_E5_UFLOAT,

        MAX_NUM
    };

    enum class DescriptorType : uint32_t
    {
        TEXTURE,
        STORAGE_TEXTURE,

        MAX_NUM
    };

    enum class Sampler : uint32_t
    {
        NEAREST_CLAMP,
        NEAREST_MIRRORED_REPEAT,
        LINEAR_CLAMP,
        LINEAR_MIRRORED_REPEAT,

        MAX_NUM
    };

    struct MemoryAllocatorInterface
    {
        void* (*Allocate)(void* userArg, size_t size, size_t alignment);
        void* (*Reallocate)(void* userArg, void* memory, size_t size, size_t alignment);
        void (*Free)(void* userArg, void* memory);
        void* userArg;
    };

    struct SPIRVBindingOffsets
    {
        uint32_t samplerOffset;
        uint32_t textureOffset;
        uint32_t constantBufferOffset;
        uint32_t storageTextureOffset;
    };

    struct LibraryDesc
    {
        SPIRVBindingOffsets spirvBindingOffsets;
        const Method* supportedMethods;
        uint32_t supportedMethodNum;
        uint8_t versionMajor;
        uint8_t versionMinor;
        uint8_t versionBuild;
    };

    struct MethodDesc
    {
        Method method;
        uint16_t fullResolutionWidth;
        uint16_t fullResolutionHeight;
    };

    struct DenoiserCreationDesc
    {
        MemoryAllocatorInterface memoryAllocatorInterface;
        const MethodDesc* requestedMethods;
        uint32_t requestedMethodNum;
        bool enableValidation : 1;
    };

    struct TextureDesc
    {
        // - always texture-read and texture-storage access
        // - potential descriptors:
        //   - shader read:
        //     - a descriptor for all mips
        //     - a descriptor for first mip only
        //     - a descriptor for some mips with a specific offset
        //   - shader write:
        //     - a descriptor for each mip
        Format format;
        uint16_t width;
        uint16_t height;
        uint16_t mipNum;
    };

    struct Resource
    {
        DescriptorType stateNeeded;
        ResourceType type;
        uint16_t indexInPool;
        uint16_t mipOffset;
        uint16_t mipNum;
    };

    struct DescriptorRangeDesc
    {
        DescriptorType descriptorType;
        uint32_t baseRegisterIndex;
        uint32_t descriptorNum;
    };

    struct ComputeShader
    {
        const void* bytecode;
        uint64_t size;
    };

    struct StaticSamplerDesc
    {
        Sampler sampler;
        uint32_t registerIndex;
    };

    struct PipelineDesc
    {
        ComputeShader computeShaderDXBC;
        ComputeShader computeShaderDXIL;
        ComputeShader computeShaderSPIRV;
        const char* shaderEntryPointName;
        const DescriptorRangeDesc* descriptorRanges;
        uint32_t descriptorRangeNum;
    };

    struct DescriptorSetDesc
    {
        uint32_t setNum;
        uint32_t constantBufferNum;
        uint32_t textureNum;
        uint32_t storageTextureNum;
        uint32_t maxDescriptorRangeNumPerPipeline;
    };

    struct ConstantBufferDesc
    {
        uint32_t registerIndex;
        uint32_t maxDataSize;
    };

    struct DenoiserDesc
    {
        const PipelineDesc* pipelines;
        uint32_t pipelineNum;
        const StaticSamplerDesc* staticSamplers;
        uint32_t staticSamplerNum;
        const TextureDesc* permanentPool;
        uint32_t permanentPoolSize;
        const TextureDesc* transientPool;
        uint32_t transientPoolSize;
        ConstantBufferDesc constantBufferDesc;
        DescriptorSetDesc descriptorSetDesc;
    };

    struct DispatchDesc
    {
        const char* name;
        const Resource* resources; // concatenated resources for all "DescriptorRangeDesc" descriptions in DenoiserDesc::pipelines[ pipelineIndex ]
        uint32_t resourceNum;
        const uint8_t* constantBufferData;
        uint32_t constantBufferDataSize;
        uint32_t pipelineIndex;
        uint32_t gridWidth;
        uint32_t gridHeight;
    };
}
