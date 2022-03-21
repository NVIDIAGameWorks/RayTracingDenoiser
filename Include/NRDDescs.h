/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#define NRD_DESCS_VERSION_MAJOR 3
#define NRD_DESCS_VERSION_MINOR 0

static_assert (NRD_VERSION_MAJOR == NRD_DESCS_VERSION_MAJOR && NRD_VERSION_MINOR == NRD_DESCS_VERSION_MINOR, "Please, update all NRD SDK files");

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

    // DenoiserName_SignalType
    enum class Method : uint32_t
    {
        // =============================================================================================================================
        // REBLUR
        // =============================================================================================================================

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_RADIANCE_HITDIST,
        // OPTIONAL INPUTS - IN_DIFF_DIRECTION_PDF, IN_DIFF_CONFIDENCE
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST
        REBLUR_DIFFUSE,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_HITDIST,
        // OUTPUTS - OUT_DIFF_HITDIST
        REBLUR_DIFFUSE_OCCLUSION,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_RADIANCE_HITDIST,
        // OPTIONAL INPUTS - IN_SPEC_DIRECTION_PDF, IN_SPEC_CONFIDENCE
        // OUTPUTS - OUT_SPEC_RADIANCE_HITDIST
        REBLUR_SPECULAR,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_HITDIST,
        // OUTPUTS - OUT_SPEC_HITDIST
        REBLUR_SPECULAR_OCCLUSION,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_RADIANCE_HITDIST, IN_SPEC_RADIANCE_HITDIST,
        // OPTIONAL INPUTS - IN_DIFF_DIRECTION_PDF, IN_SPEC_DIRECTION_PDF, IN_DIFF_CONFIDENCE,  IN_SPEC_CONFIDENCE
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST, OUT_SPEC_RADIANCE_HITDIST
        REBLUR_DIFFUSE_SPECULAR,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_HITDIST, IN_SPEC_HITDIST,
        // OUTPUTS - OUT_DIFF_HITDIST, OUT_SPEC_HITDIST
        REBLUR_DIFFUSE_SPECULAR_OCCLUSION,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_DIRECTION_HITDIST,
        // OPTIONAL INPUTS - IN_DIFF_DIRECTION_PDF, IN_DIFF_CONFIDENCE
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
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST
        RELAX_DIFFUSE,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_SPEC_RADIANCE_HITDIST
        // OUTPUTS - OUT_SPEC_RADIANCE_HITDIST
        RELAX_SPECULAR,

        // INPUTS - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_DIFF_RADIANCE_HITDIST, IN_SPEC_RADIANCE_HITDIST
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST, OUT_SPEC_RADIANCE_HITDIST
        RELAX_DIFFUSE_SPECULAR,

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
        // OUTPUTS - OUT_SPEC_REFLECTION_MV
        SPEC_REFLECTION_MV,

        // INPUTS - IN_MV, IN_DELTA_PRIMARY_POSW, IN_DELTA_SECONDARY_POSW
        // OUTPUT - OUT_DELTA_MV
        DELTA_OPTIMIZATION_MV,

        MAX_NUM
    };

    // See NRD.hlsli for more details
    enum class ResourceType : uint32_t
    {
        //=============================================================================================================================
        // INPUTS
        //=============================================================================================================================

        // 3D world space motion (RGBA16f+) or 2D screen space motion (RG16f+), MVs must be non-jittered, MV = previous - current
        IN_MV,

        // Data must match encoding / decoding in "NRD_FrontEnd_PackNormalAndRoughness" / "NRD_FrontEnd_UnpackNormalAndRoughness" (RGBA8+ or R10G10B10A2+)
        IN_NORMAL_ROUGHNESS,

        // Linear view depth for primary rays (R16f+)
        IN_VIEWZ,

        // RELAX: Data must be packed using "RELAX_FrontEnd_PackRadianceAndHitDist"
        // REBLUR: Data must be packed using "REBLUR_FrontEnd_PackRadianceAndHitDist" (and "REBLUR_FrontEnd_GetNormHitDist" for hit distance) (RGBA16f+)
        //   Noisy and weighted sum of hit distances along a path (using NRD_GetCorrectedHitDist), excluding primary hit distance.
        //   REBLUR works with normalized hit distances. It's recommended to use REBLUR_FrontEnd_GetNormHitDist for hit distance normalization.
        //   Normalization parameters should be passed into NRD as HitDistanceParameters for diffuse and specular separately for internal
        //   hit distance denormalization.
        IN_DIFF_RADIANCE_HITDIST,
        IN_SPEC_RADIANCE_HITDIST,

        // REBLUR: Data must be packed using "REBLUR_FrontEnd_GetNormHitDist" (R8+)
        IN_DIFF_HITDIST,
        IN_SPEC_HITDIST,

        // REBLUR: Data must be packed using "REBLUR_FrontEnd_PackDirectionAndHitDist" (RGBA8+)
        IN_DIFF_DIRECTION_HITDIST,

        // (Optional) Data must be packed using "NRD_FrontEnd_PackDirectionAndPdf" (RGBA8+)
        //   These inputs are needed only if PrePassMode::ADVANCED is used. The data can be averaged or weighted in case of many RPP. Encoding / decoding
        //   can be changed in "NRD_FrontEnd_PackDirectionAndPdf" and "NRD_FrontEnd_UnpackDirectionAndPdf" functions.
        IN_DIFF_DIRECTION_PDF,
        IN_SPEC_DIRECTION_PDF,

        // (Optional) User-provided history confidence in range 0-1, i.e. antilag (R8+)
        //   Used only if "CommonSettings::isHistoryConfidenceInputsAvailable = true".
        IN_DIFF_CONFIDENCE,
        IN_SPEC_CONFIDENCE,

        // SIGMA: Data must be packed using "SIGMA_FrontEnd_PackShadow" (3-args) (RG16+)
        IN_SHADOWDATA,

        // SIGMA: Data must be packed using "SIGMA_FrontEnd_PackShadow" (5-args) (RGBA8+)
        IN_SHADOW_TRANSLUCENCY,

        // REFERENCE: input
        IN_RADIANCE,

        // DELTA_OPTIMIZATION_MV: input
        IN_DELTA_PRIMARY_POSW,
        IN_DELTA_SECONDARY_POSW,

        //=============================================================================================================================
        // OUTPUTS
        //=============================================================================================================================

        // IMPORTANT: These textures can potentially be used as history buffers

        // REBLUR: Data must be unpacked using "REBLUR_BackEnd_UnpackRadianceAndHitDist"
        //   .xyz - radiance, .w - normalized hit distance (RGBA16f+)
        // RELAX: Data must be unpacked using "RELAX_BackEnd_UnpackRadianceAndHitDist"
        //   .xyz - radiance (R11G11B10f+)
        OUT_DIFF_RADIANCE_HITDIST,
        OUT_SPEC_RADIANCE_HITDIST,

        // REBLUR: .x - normalized hit distance (R8+)
        OUT_DIFF_HITDIST,
        OUT_SPEC_HITDIST,

        // REBLUR: .xyz - direction, .w - normalized hit distance (RGBA8+)
        OUT_DIFF_DIRECTION_HITDIST,

        // SIGMA: Data must be unpacked using "SIGMA_BackEnd_UnpackShadow"
        //   .x - shadow, .yzw - translucency (RGBA8+)
        //   .x - shadow (R8+)
        //   Usage:
        //      shadowData = SIGMA_BackEnd_UnpackShadow( shadowData );
        //      float3 finalShadowCommon = lerp( shadowData.yzw, 1.0, shadowData.x ); // or
        //      float3 finalShadowExotic = shadowData.yzw * shadowData.x; // or
        //      float3 finalShadowMoreExotic = shadowData.yzw;
        OUT_SHADOW_TRANSLUCENCY,

        // REFERENCE: output
        OUT_RADIANCE,

        // SPEC_REFLECTION_MV: .xy - 2D screen space virtual motion (RG16f+), MV = previous - current
        OUT_SPEC_REFLECTION_MV,

        // DELTA_OPTIMIZATION_MV: .xy - 2D screen space motion (RG16f+), MV = previous - current
        OUT_DELTA_MV,

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
        uint32_t storageTextureAndBufferOffset;
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
        Format format;
        uint16_t width;
        uint16_t height;
        uint16_t mipNum;
    };

    /*
    Requested descriptor variants:
      - shader read:
        - a descriptor for all mips
        - a descriptor for first mip only
        - a descriptor for some mips with a specific offset
      - shader write:
        - a descriptor for each mip
    */
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
        const char* shaderFileName; // optional, useful for white-box integration or shaders hot reloading
        const char* shaderEntryPointName;
        const DescriptorRangeDesc* descriptorRanges;
        uint32_t descriptorRangeNum;

        // if "true" all constant buffers share same "ConstantBufferDesc" description
        // if "false" this pipeline doesn't have a constant buffer
        bool hasConstantData;
    };

    struct DescriptorSetDesc
    {
        uint32_t setMaxNum;
        uint32_t constantBufferMaxNum;
        uint32_t staticSamplerMaxNum;
        uint32_t textureMaxNum;
        uint32_t storageTextureMaxNum;
        uint32_t descriptorRangeMaxNumPerPipeline;
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
        uint16_t pipelineIndex;
        uint16_t gridWidth;
        uint16_t gridHeight;
    };
}
