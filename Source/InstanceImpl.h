/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "NRD.h"

#define MATH_NAMESPACE
#include "MathLib/MathLib.h"

typedef nrd::MemoryAllocatorInterface MemoryAllocatorInterface;
#include "StdAllocator.h"

#include "Timer.h"

#define _NRD_STRINGIFY(s) #s
#define NRD_STRINGIFY(s) _NRD_STRINGIFY(s)

#ifdef NRD_EMBEDS_DXBC_SHADERS
    #define GET_DXBC_SHADER_DESC(shaderName) {g_##shaderName##_cs_dxbc, GetCountOf(g_##shaderName##_cs_dxbc)}
#else
    #define GET_DXBC_SHADER_DESC(shaderName) {}
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #define GET_DXIL_SHADER_DESC(shaderName) {g_##shaderName##_cs_dxil, GetCountOf(g_## shaderName##_cs_dxil)}
#else
    #define GET_DXIL_SHADER_DESC(shaderName) {}
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #define GET_SPIRV_SHADER_DESC(shaderName) {g_##shaderName##_cs_spirv, GetCountOf(g_##shaderName##_cs_spirv)}
#else
    #define GET_SPIRV_SHADER_DESC(shaderName) {}
#endif

#define AddDispatch(shaderName, constantNum, numThreads, downsampleFactor) \
    AddComputeDispatchDesc(numThreads, downsampleFactor, constantNum, 1, #shaderName ".cs", GET_DXBC_SHADER_DESC(shaderName), GET_DXIL_SHADER_DESC(shaderName), GET_SPIRV_SHADER_DESC(shaderName))

#define AddDispatchRepeated(shaderName, constantNum, numThreads, downsampleFactor, repeatNum) \
    AddComputeDispatchDesc(numThreads, downsampleFactor, constantNum, repeatNum, #shaderName ".cs", GET_DXBC_SHADER_DESC(shaderName), GET_DXIL_SHADER_DESC(shaderName), GET_SPIRV_SHADER_DESC(shaderName))

#define PushPass(passName) \
    _PushPass(NRD_STRINGIFY(DENOISER_NAME) " - " passName)

// TODO: rework is needed, but still better than copy-pasting
#define NRD_DECLARE_DIMS \
    uint16_t screenW = denoiserData.desc.renderWidth; \
    uint16_t screenH = denoiserData.desc.renderHeight; \
    [[maybe_unused]] uint16_t rectW = uint16_t(screenW * m_CommonSettings.resolutionScale[0] + 0.5f); \
    [[maybe_unused]] uint16_t rectH = uint16_t(screenH * m_CommonSettings.resolutionScale[1] + 0.5f); \
    [[maybe_unused]] uint16_t rectWprev = uint16_t(screenW * m_CommonSettings.resolutionScalePrev[0] + 0.5f); \
    [[maybe_unused]] uint16_t rectHprev = uint16_t(screenH * m_CommonSettings.resolutionScalePrev[1] + 0.5f)

namespace nrd
{
    constexpr uint16_t PERMANENT_POOL_START = 1000;
    constexpr uint16_t TRANSIENT_POOL_START = 2000;
    constexpr size_t CONSTANT_DATA_SIZE = 2 * 1024 * 2014;

    constexpr uint16_t USE_MAX_DIMS = 0xFFFF;
    constexpr uint16_t IGNORE_RS = 0xFFFE;

    inline uint16_t DivideUp(uint32_t x, uint16_t y)
    { return uint16_t((x + y - 1) / y); }

    template <class T>
    inline uint16_t AsUint(T x)
    { return (uint16_t)x; }

    union Constant
    {
        float f;
        uint32_t ui;
    };

    union Settings
    {
        // Add settings here
        ReblurSettings reblur;
        SigmaSettings sigma;
        RelaxDiffuseSettings diffuseRelax;
        RelaxSpecularSettings specularRelax;
        RelaxDiffuseSpecularSettings diffuseSpecularRelax;
        ReferenceSettings reference;
        SpecularReflectionMvSettings specularReflectionMv;
        SpecularDeltaMvSettings specularDeltaMv;
    };

    struct DenoiserData
    {
        DenoiserDesc desc;
        Settings settings;
        size_t settingsSize;
        size_t dispatchOffset;
        size_t pingPongOffset;
        size_t pingPongNum;
    };

    struct PingPong
    {
        size_t resourceIndex;
        uint16_t indexInPoolToSwapWith;
    };

    struct NumThreads
    {
        inline NumThreads(uint8_t w, uint8_t h) : width(w), height(h)
        {}

        inline NumThreads() : width(0), height(0)
        {}

        uint8_t width;
        uint8_t height;
    };

    struct InternalDispatchDesc
    {
        const char* name;
        const ResourceDesc* resources; // concatenated resources for all "ResourceRangeDesc" descriptions in InstanceDesc::pipelines[ pipelineIndex ]
        uint32_t resourcesNum;
        const uint8_t* constantBufferData;
        uint32_t constantBufferDataSize;
        uint16_t pipelineIndex;
        uint16_t downsampleFactor;
        uint16_t maxRepeatsNum; // mostly for internal use
        NumThreads numThreads;
    };

    struct ClearResource
    {
        Identifier identifier;
        ResourceDesc resource;
        uint32_t w;
        uint32_t h;
        bool isInteger;
    };

    class InstanceImpl
    {
    // Add denoisers here
    public:
        // Reblur
        void Add_ReblurDiffuse(DenoiserData& denoiserData);
        void Add_ReblurDiffuseOcclusion(DenoiserData& denoiserData);
        void Add_ReblurDiffuseSh(DenoiserData& denoiserData);
        void Add_ReblurSpecular(DenoiserData& denoiserData);
        void Add_ReblurSpecularOcclusion(DenoiserData& denoiserData);
        void Add_ReblurSpecularSh(DenoiserData& denoiserData);
        void Add_ReblurDiffuseSpecular(DenoiserData& denoiserData);
        void Add_ReblurDiffuseSpecularOcclusion(DenoiserData& denoiserData);
        void Add_ReblurDiffuseSpecularSh(DenoiserData& denoiserData);
        void Add_ReblurDiffuseDirectionalOcclusion(DenoiserData& denoiserData);

        void Update_Reblur(const DenoiserData& denoiserData);
        void Update_ReblurOcclusion(const DenoiserData& denoiserData);

        void AddSharedConstants_Reblur(const DenoiserData& denoiserData, const ReblurSettings& settings, Constant*& data);

        // Sigma
        void Add_SigmaShadow(DenoiserData& denoiserData);
        void Add_SigmaShadowTranslucency(DenoiserData& denoiserData);

        void Update_SigmaShadow(const DenoiserData& denoiserData);

        void AddSharedConstants_Sigma(const DenoiserData& denoiserData, const SigmaSettings& settings, Constant*& data);

        // Relax
        void Add_RelaxDiffuse(DenoiserData& denoiserData);
        void Add_RelaxDiffuseSh(DenoiserData& denoiserData);
        void Add_RelaxSpecular(DenoiserData& denoiserData);
        void Add_RelaxSpecularSh(DenoiserData& denoiserData);
        void Add_RelaxDiffuseSpecular(DenoiserData& denoiserData);
        void Add_RelaxDiffuseSpecularSh(DenoiserData& denoiserData);

        void Update_RelaxDiffuse(const DenoiserData& denoiserData);
        void Update_RelaxDiffuseSh(const DenoiserData& denoiserData);
        void Update_RelaxSpecular(const DenoiserData& denoiserData);
        void Update_RelaxSpecularSh(const DenoiserData& denoiserData);
        void Update_RelaxDiffuseSpecular(const DenoiserData& denoiserData);
        void Update_RelaxDiffuseSpecularSh(const DenoiserData& denoiserData);

        void AddSharedConstants_Relax(const DenoiserData& denoiserData, Constant*& data, Denoiser denoiser);

        // Other
        void Add_Reference(DenoiserData& denoiserData);
        void Update_Reference(const DenoiserData& denoiserData);

        void Add_SpecularReflectionMv(DenoiserData& denoiserData);
        void Update_SpecularReflectionMv(const DenoiserData& denoiserData);

        void Add_SpecularDeltaMv(DenoiserData& denoiserData);
        void Update_SpecularDeltaMv(const DenoiserData& denoiserData);

    // Internal
    public:
        inline InstanceImpl(const StdAllocator<uint8_t>& stdAllocator) :
            m_StdAllocator(stdAllocator)
            , m_DenoiserData(GetStdAllocator())
            , m_PermanentPool(GetStdAllocator())
            , m_TransientPool(GetStdAllocator())
            , m_Resources(GetStdAllocator())
            , m_ClearResources(GetStdAllocator())
            , m_PingPongs(GetStdAllocator())
            , m_ResourceRanges(GetStdAllocator())
            , m_Pipelines(GetStdAllocator())
            , m_Dispatches(GetStdAllocator())
            , m_ActiveDispatches(GetStdAllocator())
            , m_IndexRemap(GetStdAllocator())
        {
            m_ConstantData = m_StdAllocator.allocate(CONSTANT_DATA_SIZE);
            m_DenoiserData.reserve(8);
            m_PermanentPool.reserve(32);
            m_TransientPool.reserve(32);
            m_Resources.reserve(128);
            m_ClearResources.reserve(32);
            m_PingPongs.reserve(32);
            m_ResourceRanges.reserve(64);
            m_Pipelines.reserve(32);
            m_Dispatches.reserve(32);
            m_ActiveDispatches.reserve(32);
        }

        ~InstanceImpl()
        { m_StdAllocator.deallocate(m_ConstantData, 0); }

        inline const InstanceDesc& GetDesc() const
        { return m_Desc; }

        inline StdAllocator<uint8_t>& GetStdAllocator()
        { return m_StdAllocator; }

        Result Create(const InstanceCreationDesc& instanceCreationDesc);
        Result SetCommonSettings(const CommonSettings& commonSettings);
        Result SetDenoiserSettings(Identifier identifier, const void* denoiserSettings);
        Result GetComputeDispatches(const Identifier* identifiers, uint32_t identifiersNum, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescsNum);

    private:
        void AddComputeDispatchDesc
        (
            NumThreads numThreads,
            uint16_t downsampleFactor,
            uint32_t constantBufferDataSize,
            uint32_t maxRepeatNum,
            const char* shaderFileName,
            const ComputeShaderDesc& dxbc,
            const ComputeShaderDesc& dxil,
            const ComputeShaderDesc& spirv
        );

        void PrepareDesc();
        void UpdatePingPong(const DenoiserData& denoiserData);
        void PushTexture(DescriptorType descriptorType, uint16_t localIndex, uint16_t mipOffset, uint16_t mipNum, uint16_t indexToSwapWith = uint16_t(-1));

    // Available in denoiser implementations
    private:
        void AddTextureToTransientPool(const TextureDesc& textureDesc);
        Constant* PushDispatch(const DenoiserData& denoiserData, uint32_t localIndex);

        inline void AddTextureToPermanentPool(const TextureDesc& textureDesc)
        { m_PermanentPool.push_back(textureDesc); }

        inline void SetSharedConstants(uint32_t num4x4, uint32_t num4, uint32_t num2, uint32_t num1)
        {
            m_SharedConstantNum = 16 * num4x4 + 4 * num4 + 2 * num2 + 1 * num1;
            assert( m_SharedConstantNum % 4 == 0 );
        }

        inline uint32_t SumConstants(uint32_t num4x4, uint32_t num4, uint32_t num2, uint32_t num1, bool addShared = true)
        { return ( 16 * num4x4 + 4 * num4 + 2 * num2 + 1 * num1 + ( addShared ? m_SharedConstantNum : 0 ) ) * sizeof(uint32_t); }

        inline void PushInput(uint16_t indexInPool, uint16_t mipOffset = 0, uint16_t mipNum = 1, uint16_t indexToSwapWith = uint16_t(-1))
        { PushTexture(DescriptorType::TEXTURE, indexInPool, mipOffset, mipNum, indexToSwapWith); }

        inline void PushOutput(uint16_t indexInPool, uint16_t mipOffset = 0, uint16_t mipNum = 1, uint16_t indexToSwapWith = uint16_t(-1))
        { PushTexture(DescriptorType::STORAGE_TEXTURE, indexInPool, mipOffset, mipNum, indexToSwapWith); }

        inline void _PushPass(const char* name)
        {
            m_PassName = name;
            m_ResourceOffset = m_Resources.size();
        }

        inline void ValidateConstants(const Constant* lastConstant) const
        {
            const DispatchDesc& dispatchDesc = m_ActiveDispatches.back();

            [[maybe_unused]] size_t num = size_t(lastConstant - (const Constant*)dispatchDesc.constantBufferData);
            [[maybe_unused]] size_t bytes = num * sizeof(uint32_t);
            assert( bytes == dispatchDesc.constantBufferDataSize );
        }

    private:
        StdAllocator<uint8_t> m_StdAllocator;
        Vector<DenoiserData> m_DenoiserData;
        Vector<TextureDesc> m_PermanentPool;
        Vector<TextureDesc> m_TransientPool;
        Vector<ResourceDesc> m_Resources;
        Vector<ClearResource> m_ClearResources;
        Vector<PingPong> m_PingPongs;
        Vector<ResourceRangeDesc> m_ResourceRanges;
        Vector<PipelineDesc> m_Pipelines;
        Vector<InternalDispatchDesc> m_Dispatches;
        Vector<DispatchDesc> m_ActiveDispatches;
        Vector<uint16_t> m_IndexRemap;
        Timer m_Timer;
        ml::sFastRand m_FastRandState = {};
        InstanceDesc m_Desc = {};
        CommonSettings m_CommonSettings = {};
        ml::float4x4 m_ViewToClip = ml::float4x4::Identity();
        ml::float4x4 m_ViewToClipPrev = ml::float4x4::Identity();
        ml::float4x4 m_ClipToView = ml::float4x4::Identity();
        ml::float4x4 m_ClipToViewPrev = ml::float4x4::Identity();
        ml::float4x4 m_WorldToView = ml::float4x4::Identity();
        ml::float4x4 m_WorldToViewPrev = ml::float4x4::Identity();
        ml::float4x4 m_ViewToWorld = ml::float4x4::Identity();
        ml::float4x4 m_ViewToWorldPrev = ml::float4x4::Identity();
        ml::float4x4 m_WorldToClip = ml::float4x4::Identity();
        ml::float4x4 m_WorldToClipPrev = ml::float4x4::Identity();
        ml::float4x4 m_ClipToWorld = ml::float4x4::Identity();
        ml::float4x4 m_ClipToWorldPrev = ml::float4x4::Identity();
        ml::float4x4 m_WorldPrevToWorld = ml::float4x4::Identity();
        ml::float4 m_Rotator_PrePass = ml::float4::Zero();
        ml::float4 m_Rotator_Blur = ml::float4::Zero();
        ml::float4 m_Rotator_PostBlur = ml::float4::Zero();
        ml::float4 m_Frustum = ml::float4::Zero();
        ml::float4 m_FrustumPrev = ml::float4::Zero();
        ml::float3 m_CameraDelta = ml::float3::Zero();
        ml::float3 m_ViewDirection = ml::float3::Zero();
        ml::float3 m_ViewDirectionPrev = ml::float3::Zero();
        const char* m_PassName = nullptr;
        uint8_t* m_ConstantData = nullptr;
        size_t m_ConstantDataOffset = 0;
        size_t m_ResourceOffset = 0;
        size_t m_DispatchClearIndex[2] = {};
        float m_IsOrtho = 0.0f;
        float m_CheckerboardResolveAccumSpeed = 0.0f;
        float m_JitterDelta = 0.0f;
        float m_TimeDelta = 0.0f;
        float m_FrameRateScale = 0.0f;
        float m_ProjectY = 0.0f;
        uint32_t m_SharedConstantNum = 0;
        uint32_t m_AccumulatedFrameNum = 0;
        uint16_t m_TransientPoolOffset = 0;
        uint16_t m_PermanentPoolOffset = 0;
        bool m_IsFirstUse = true;
    };

    inline void AddFloat4x4(Constant*& dst, const ml::float4x4& x)
    {
        memcpy(dst, &x, sizeof(ml::float4x4));
        dst += 16;
    }

    inline void AddFloat4(Constant*& dst, const ml::float4& x)
    {
        memcpy(dst, &x, sizeof(ml::float4));
        dst += 4;
    }

    inline void AddFloat2(Constant*& dst, float x, float y)
    {
        dst->f = x;
        dst++;

        dst->f = y;
        dst++;
    }

    inline void AddFloat(Constant*& dst, float x)
    {
        dst->f = x;
        dst++;
    }

    inline void AddUint(Constant*& dst, uint32_t x)
    {
        dst->ui = x;
        dst++;
    }

    inline void AddUint2(Constant*& dst, uint32_t x, uint32_t y)
    {
        dst->ui = x;
        dst++;

        dst->ui = y;
        dst++;
    }
}

// IMPORTANT: needed only for DXBC produced by ShaderMake without "--useAPI"
#undef BYTE
#define BYTE uint8_t
