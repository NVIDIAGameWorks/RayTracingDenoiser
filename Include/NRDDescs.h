/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#define NRD_DESCS_VERSION_MAJOR 4
#define NRD_DESCS_VERSION_MINOR 2

static_assert (NRD_VERSION_MAJOR == NRD_DESCS_VERSION_MAJOR && NRD_VERSION_MINOR == NRD_DESCS_VERSION_MINOR, "Please, update all NRD SDK files");

namespace nrd
{
    typedef uint32_t Identifier;

    struct Instance;

    enum class Result : uint32_t
    {
        SUCCESS,
        FAILURE,
        INVALID_ARGUMENT,
        UNSUPPORTED,
        NON_UNIQUE_IDENTIFIER,

        MAX_NUM
    };

    enum class Denoiser : uint32_t
    {
        // =============================================================================================================================
        // REBLUR
        // =============================================================================================================================

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_RADIANCE_HITDIST,
        // OPTIONAL INPUTS - IN_DIFF_CONFIDENCE
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST
        REBLUR_DIFFUSE,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_HITDIST,
        // OUTPUTS - OUT_DIFF_HITDIST
        REBLUR_DIFFUSE_OCCLUSION,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_SH0, IN_DIFF_SH1
        // OPTIONAL INPUTS - IN_DIFF_CONFIDENCE
        // OUTPUTS - OUT_DIFF_SH0, OUT_DIFF_SH1
        REBLUR_DIFFUSE_SH,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_RADIANCE_HITDIST,
        // OPTIONAL INPUTS - IN_SPEC_DIRECTION_PDF, IN_SPEC_CONFIDENCE
        // OUTPUTS - OUT_SPEC_RADIANCE_HITDIST
        REBLUR_SPECULAR,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_HITDIST,
        // OUTPUTS - OUT_SPEC_HITDIST
        REBLUR_SPECULAR_OCCLUSION,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_SH0, IN_SPEC_SH1
        // OPTIONAL INPUTS - IN_SPEC_CONFIDENCE
        // OUTPUTS - OUT_SPEC_SH0, OUT_SPEC_SH1
        REBLUR_SPECULAR_SH,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_RADIANCE_HITDIST, IN_SPEC_RADIANCE_HITDIST,
        // OPTIONAL INPUTS - IN_DIFF_CONFIDENCE,  IN_SPEC_CONFIDENCE
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST, OUT_SPEC_RADIANCE_HITDIST
        REBLUR_DIFFUSE_SPECULAR,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_HITDIST, IN_SPEC_HITDIST,
        // OUTPUTS - OUT_DIFF_HITDIST, OUT_SPEC_HITDIST
        REBLUR_DIFFUSE_SPECULAR_OCCLUSION,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_SH0, IN_DIFF_SH1, IN_SPEC_SH0, IN_SPEC_SH1
        // OPTIONAL INPUTS - IN_DIFF_CONFIDENCE,  IN_SPEC_CONFIDENCE
        // OUTPUTS - OUT_DIFF_SH0, OUT_DIFF_SH1, OUT_SPEC_SH0, OUT_SPEC_SH1
        REBLUR_DIFFUSE_SPECULAR_SH,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_DIRECTION_HITDIST,
        // OPTIONAL INPUTS - IN_DIFF_CONFIDENCE
        // OUTPUTS - OUT_DIFF_DIRECTION_HITDIST
        REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION,

        // =============================================================================================================================
        // SIGMA
        // =============================================================================================================================

        // INPUTS - IN_NORMAL_ROUGHNESS, IN_SHADOWDATA, OUT_SHADOW_TRANSLUCENCY (used as history)
        // OUTPUTS - OUT_SHADOW_TRANSLUCENCY
        SIGMA_SHADOW,

        // INPUTS - IN_NORMAL_ROUGHNESS, IN_SHADOWDATA, IN_SHADOW_TRANSLUCENCY, OUT_SHADOW_TRANSLUCENCY (used as history)
        // OUTPUTS - OUT_SHADOW_TRANSLUCENCY
        SIGMA_SHADOW_TRANSLUCENCY,

        // =============================================================================================================================
        // RELAX
        // =============================================================================================================================

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_RADIANCE_HITDIST
        // OPTIONAL INPUTS - IN_DIFF_CONFIDENCE
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST
        RELAX_DIFFUSE,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_SH0, IN_DIFF_SH1
        // OPTIONAL INPUTS - IN_DIFF_CONFIDENCE
        // OUTPUTS - OUT_DIFF_SH0, OUT_DIFF_SH1
        RELAX_DIFFUSE_SH,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_RADIANCE_HITDIST
        // OPTIONAL INPUTS - IN_SPEC_CONFIDENCE
        // OUTPUTS - OUT_SPEC_RADIANCE_HITDIST
        RELAX_SPECULAR,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_SH0, IN_SPEC_SH1
        // OPTIONAL INPUTS - IN_SPEC_CONFIDENCE
        // OUTPUTS - OUT_SPEC_SH0, OUT_SPEC_SH1
        RELAX_SPECULAR_SH,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_RADIANCE_HITDIST, IN_SPEC_RADIANCE_HITDIST
        // OPTIONAL INPUTS - IN_DIFF_CONFIDENCE,  IN_SPEC_CONFIDENCE
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST, OUT_SPEC_RADIANCE_HITDIST
        RELAX_DIFFUSE_SPECULAR,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_SH0, IN_DIFF_SH1, IN_SPEC_SH0, IN_SPEC_SH1
        // OPTIONAL INPUTS - IN_DIFF_CONFIDENCE,  IN_SPEC_CONFIDENCE
        // OUTPUTS - OUT_DIFF_SH0, OUT_DIFF_SH1, OUT_SPEC_SH0, OUT_SPEC_SH1
        RELAX_DIFFUSE_SPECULAR_SH,

        // =============================================================================================================================
        // REFERENCE
        // =============================================================================================================================

        // INPUTS - IN_RADIANCE
        // OUTPUTS - OUT_RADIANCE
        REFERENCE,

        // =============================================================================================================================
        // MOTION VECTORS
        // =============================================================================================================================

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_HITDIST
        // OUTPUTS - OUT_REFLECTION_MV
        SPECULAR_REFLECTION_MV,

        // INPUTS - IN_MV, IN_DELTA_PRIMARY_POS, IN_DELTA_SECONDARY_POS
        // OUTPUT - OUT_DELTA_MV
        SPECULAR_DELTA_MV,

        MAX_NUM
    };

    // See NRD.hlsli for more details
    enum class ResourceType : uint32_t
    {
        //=============================================================================================================================
        // COMMON INPUTS
        //=============================================================================================================================

        // 3D world-space motion (RGBA16f+) or 2D screen-space motion (RG16f+), MVs must be non-jittered, MV = previous - current
        IN_MV,

        // Data must match encoding in "NRD_FrontEnd_PackNormalAndRoughness" and "NRD_FrontEnd_UnpackNormalAndRoughness" (RGBA8+)
        IN_NORMAL_ROUGHNESS,

        // Linear view depth for primary rays (R16f+)
        IN_VIEWZ,

        //=============================================================================================================================
        // INPUTS
        //=============================================================================================================================

        // Noisy radiance and hit distance (RGBA16f+)
        //      REBLUR: use "REBLUR_FrontEnd_PackRadianceAndNormHitDist" for encoding
        //      RELAX: use "RELAX_FrontEnd_PackRadianceAndHitDist" for encoding
        IN_DIFF_RADIANCE_HITDIST,
        IN_SPEC_RADIANCE_HITDIST,

        // Noisy hit distance (R8+)
        //      REBLUR: use "REBLUR_FrontEnd_GetNormHitDist" for encoding
        IN_DIFF_HITDIST,
        IN_SPEC_HITDIST,

        // Noisy bent normal and normalized hit distance (RGBA8+)
        //      REBLUR: use "REBLUR_FrontEnd_PackDirectionalOcclusion" for encoding
        IN_DIFF_DIRECTION_HITDIST,

        // Noisy SH data (2x RGBA16f+)
        //      REBLUR: use "REBLUR_FrontEnd_PackSh" for encoding
        //      RELAX: use "RELAX_FrontEnd_PackSh" for encoding
        IN_DIFF_SH0,
        IN_DIFF_SH1,
        IN_SPEC_SH0,
        IN_SPEC_SH1,

        // (Optional) User-provided history confidence in range 0-1, i.e. antilag (R8+)
        // Used only if "CommonSettings::isHistoryConfidenceAvailable = true"
        IN_DIFF_CONFIDENCE,
        IN_SPEC_CONFIDENCE,

        // (Optional) User-provided disocclusion threshold selector in range 0-1 (R8+)
        // Disocclusion threshold is mixed between "disocclusionThreshold" and "disocclusionThresholdAlternate"
        // Used only if "CommonSettings::isDisocclusionThresholdMixAvailable = true"
        IN_DISOCCLUSION_THRESHOLD_MIX,

        // (Optional) Base color (can be decoupled to diffuse and specular albedo based on metalness) and metalness (RGBA8+)
        // Used only if "CommonSettings::isBaseColorMetalnessAvailable = true"
        IN_BASECOLOR_METALNESS,

        // Noisy shadow data and optional translucency (RG16f+ and RGBA8+ for optional translucency)
        //      SIGMA: use "SIGMA_FrontEnd_PackShadow" for encoding
        IN_SHADOWDATA,
        IN_SHADOW_TRANSLUCENCY,

        // Noisy signal (R8+)
        IN_RADIANCE,

        // Primary and secondary world-space positions (RGBA16f+)
        IN_DELTA_PRIMARY_POS,
        IN_DELTA_SECONDARY_POS,

        //=============================================================================================================================
        // OUTPUTS
        //=============================================================================================================================

        // IMPORTANT: These textures can be potentially used as history buffers!
        // IMPORTANT: Most of denoisers do not write into output pixels outside of "CommonSettings::denoisingRange"!

        // Denoised radiance and hit distance
        //      REBLUR: use "REBLUR_BackEnd_UnpackRadianceAndNormHitDist" for decoding (RGBA16f+)
        //      RELAX: use "RELAX_BackEnd_UnpackRadiance" for decoding (R11G11B10f+)
        OUT_DIFF_RADIANCE_HITDIST,
        OUT_SPEC_RADIANCE_HITDIST,

        // Denoised SH data
        //      REBLUR: use "REBLUR_BackEnd_UnpackSh" for decoding (2x RGBA16f+)
        //      RELAX: use "RELAX_BackEnd_UnpackSh" for decoding (2x RGBA16f+)
        OUT_DIFF_SH0,
        OUT_DIFF_SH1,
        OUT_SPEC_SH0,
        OUT_SPEC_SH1,

        // Denoised normalized hit distance (R8+)
        OUT_DIFF_HITDIST,
        OUT_SPEC_HITDIST,

        // Denoised bent normal and normalized hit distance (RGBA8+)
        //      REBLUR: use "REBLUR_BackEnd_UnpackDirectionalOcclusion" for decoding
        OUT_DIFF_DIRECTION_HITDIST,

        // Denoised shadow and optional transcluceny (R8+ or RGBA8+)
        //      SIGMA: use "SIGMA_BackEnd_UnpackShadow" for decoding
        OUT_SHADOW_TRANSLUCENCY,

        // Denoised signal
        OUT_RADIANCE,

        // 2D screen-space specular motion (RG16f+), MV = previous - current
        OUT_REFLECTION_MV,

        // 2D screen-space refraction motion (RG16f+), MV = previous - current
        OUT_DELTA_MV,

        // (Optional) Debug output (RGBA8+), .w = transparency
        // Used if "CommonSettings::enableValidation = true"
        OUT_VALIDATION,

        //=============================================================================================================================
        // POOLS
        //=============================================================================================================================

        // Can be reused after denoising
        TRANSIENT_POOL,

        // Dedicated to NRD, can't be reused
        PERMANENT_POOL,

        MAX_NUM,
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
        // read-only, SRV
        TEXTURE,

        // read-write, UAV
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

    // NRD_NORMAL_ENCODING variants
    enum class NormalEncoding : uint8_t
    {
        // Worst IQ on curved (not bumpy) surfaces
        RGBA8_UNORM,
        RGBA8_SNORM,

        // Moderate IQ on curved (not bumpy) surfaces, but offers optional materialID support (normals are oct-packed)
        R10_G10_B10_A2_UNORM,

        // Best IQ on curved (not bumpy) surfaces
        RGBA16_UNORM,
        RGBA16_SNORM, // can be used with FP formats

        MAX_NUM
    };

    // NRD_ROUGHNESS_ENCODING variants
    enum class RoughnessEncoding : uint8_t
    {
        // Alpha (m)
        SQ_LINEAR,

        // Linear roughness (best choice)
        LINEAR,

        // Sqrt(linear roughness)
        SQRT_LINEAR,

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
        uint32_t storageTextureAndBufferOffset;
    };

    struct LibraryDesc
    {
        SPIRVBindingOffsets spirvBindingOffsets;
        const Denoiser* supportedDenoisers;
        uint32_t supportedDenoisersNum;
        uint8_t versionMajor;
        uint8_t versionMinor;
        uint8_t versionBuild;
        NormalEncoding normalEncoding;
        RoughnessEncoding roughnessEncoding;
    };

    struct DenoiserDesc
    {
        Identifier identifier;
        Denoiser denoiser;
        uint16_t renderWidth;
        uint16_t renderHeight;
    };

    struct InstanceCreationDesc
    {
        MemoryAllocatorInterface memoryAllocatorInterface;
        const DenoiserDesc* denoisers;
        uint32_t denoisersNum;
    };

    struct TextureDesc
    {
        Format format;
        uint16_t width;
        uint16_t height;
        uint16_t mipNum;
    };

    struct ResourceDesc
    {
        DescriptorType stateNeeded;
        ResourceType type;
        uint16_t indexInPool;
        uint16_t mipOffset;
        uint16_t mipNum;
    };

    struct ResourceRangeDesc
    {
        DescriptorType descriptorType;
        uint32_t baseRegisterIndex;
        uint32_t descriptorsNum;
    };

    struct ComputeShaderDesc
    {
        const void* bytecode;
        uint64_t size;
    };

    struct PipelineDesc
    {
        ComputeShaderDesc computeShaderDXBC;
        ComputeShaderDesc computeShaderDXIL;
        ComputeShaderDesc computeShaderSPIRV;
        const char* shaderFileName;
        const char* shaderEntryPointName;
        const ResourceRangeDesc* resourceRanges;
        uint32_t resourceRangesNum;
        bool hasConstantData;
    };

    struct DescriptorPoolDesc
    {
        uint32_t setsMaxNum;
        uint32_t constantBuffersMaxNum;
        uint32_t samplersMaxNum;
        uint32_t texturesMaxNum;
        uint32_t storageTexturesMaxNum;
    };

    struct InstanceDesc
    {
        // Constant buffer (shared)
        uint32_t constantBufferMaxDataSize;
        uint32_t constantBufferSpaceIndex;
        uint32_t constantBufferRegisterIndex;

        // Samplers (shared)
        const Sampler* samplers;
        uint32_t samplersNum;
        uint32_t samplersSpaceIndex;
        uint32_t samplersBaseRegisterIndex;

        // Pipelines
        // - if "PipelineDesc::hasConstantData = true" a pipeline has a constant buffer with the shared description
        // - if "samplers" are used as static/immutable samplers, "DescriptorPoolDesc::samplerMaxNum" is not needed (it counts samplers across all dispatches)
        const PipelineDesc* pipelines;
        uint32_t pipelinesNum;
        uint32_t resourcesSpaceIndex;

        // Textures
        const TextureDesc* permanentPool;
        uint32_t permanentPoolSize;
        const TextureDesc* transientPool;
        uint32_t transientPoolSize;

        // Limits
        DescriptorPoolDesc descriptorPoolDesc;
    };

    struct DispatchDesc
    {
        const char* name;
        const ResourceDesc* resources; // concatenated resources for all "resourceRanges" descriptions in DenoiserDesc::pipelines[ pipelineIndex ]
        uint32_t resourcesNum;
        const uint8_t* constantBufferData;
        uint32_t constantBufferDataSize;
        uint16_t pipelineIndex;
        uint16_t gridWidth;
        uint16_t gridHeight;
    };
}
