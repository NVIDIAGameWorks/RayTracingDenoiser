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
        // INPUTS - IN_MOTION_VECTOR, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_A, IN_DIFF_B
        // OUTPUTS - OUT_DIFF_A, OUT_DIFF_B
        DIFFUSE,

        // INPUTS - IN_MOTION_VECTOR, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_HIT
        // OUTPUTS - OUT_SPEC_HIT
        SPECULAR,

        // INPUTS - IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SHADOW
        // OUTPUTS - OUT_SHADOW
        SHADOW,

        MAX_NUM
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

    enum class ResourceType : uint32_t
    {
        // INPUTS ===========================================================================================================================

        // .xyz - 3D world space motion (RGBA16f+) or .xy - 2D screen space motion (RG16f+), MVs must be non-jittered, old = new + MV
        IN_MOTION_VECTOR,

        // .xyz - normal in world space, unpacking "normalize(x * 2 - 1)", .w - artistic roughness (RGBA8+)
        // where "artistic roughness" = sqrt( "mathematical roughness" )
        IN_NORMAL_ROUGHNESS,

        // linear view depth - "+" for LHS, "-" for RHS (R32f)
        IN_VIEWZ,

        // see "NRD_FrontEnd_PackShadow" in NRD.hlsl (RG16f+), INF pixels must be cleared with NRD_INF_SHADOW macro
        IN_SHADOW,

        // see "NRD_FrontEnd_PackDiffuse" in NRD.hlsl (RGBA16f+)
        IN_DIFF_A,

        // see "NRD_FrontEnd_PackDiffuse" in NRD.hlsl (RGBA16f+), INF pixels must be cleared with NRD_INF_DIFF_B macro
        IN_DIFF_B,

        // see "NRD_FrontEnd_PackSpecular" in NRD.hlsl (RGBA16f+)
        IN_SPEC_HIT,

        // OUTPUTS ==========================================================================================================================

        // IMPORTANT: potentially, these textures can be used as history buffers, it's not recommened to reuse them after denoising.

        // .x - shadow (R8+)
        OUT_SHADOW,

        // .xyz - radiance, .w - normalized hit distance (RGBA16f+)
        OUT_DIFF_HIT,

        // .xyz - radiance, .w - normalized hit distance (RGBA16f+)
        OUT_SPEC_HIT,

        // POOLS ============================================================================================================================

        // can be reused after denoising
        TRANSIENT_POOL,

        // dedicated to NRD, can't be reused
        PERMANENT_POOL,

        MAX_NUM
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
        uint32_t fullResolutionWidth;
        uint32_t fullResolutionHeight;
    };

    struct DenoiserCreationDesc
    {
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
        uint32_t width;
        uint32_t height;
        uint16_t mipNum;
    };

    struct Resource
    {
        DescriptorType stateNeeded;
        ResourceType type;
        uint32_t indexInPool;
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
