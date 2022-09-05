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

// TODO: move to a C++ / HLSL shared file (most likely with CB layout declarations)
#define NRD_CS_MAIN "main"

#define MATH_NAMESPACE
#include "MathLib/MathLib.h"

typedef nrd::MemoryAllocatorInterface MemoryAllocatorInterface;
#include "StdAllocator.h"

#include "Timer.h"

#define _NRD_STRINGIFY(s) #s
#define NRD_STRINGIFY(s) _NRD_STRINGIFY(s)

#ifdef NRD_USE_PRECOMPILED_SHADERS
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #define AddDispatch(shaderName, constantNum, workgroupDim, downsampleFactor) \
            AddComputeDispatchDesc(workgroupDim, workgroupDim, downsampleFactor, constantNum, 1, #shaderName ".cs", {g_##shaderName##_cs_dxbc, GetCountOf(g_##shaderName##_cs_dxbc)}, {g_##shaderName##_cs_dxil, GetCountOf(g_## shaderName##_cs_dxil)}, {g_##shaderName##_cs_spirv, GetCountOf(g_##shaderName##_cs_spirv)})

        #define AddDispatchRepeated(shaderName, constantNum, workgroupDim, downsampleFactor, repeatNum) \
            AddComputeDispatchDesc(workgroupDim, workgroupDim, downsampleFactor, constantNum, repeatNum, #shaderName ".cs", {g_##shaderName##_cs_dxbc, GetCountOf(g_##shaderName##_cs_dxbc)}, {g_##shaderName##_cs_dxil, GetCountOf(g_##shaderName##_cs_dxil)}, {g_##shaderName##_cs_spirv, GetCountOf(g_##shaderName##_cs_spirv)})
    #else
        #define AddDispatch(shaderName, constantNum, workgroupDim, downsampleFactor) \
            AddComputeDispatchDesc(workgroupDim, workgroupDim, downsampleFactor, constantNum, 1, #shaderName ".cs", {}, {}, {g_##shaderName##_cs_spirv, GetCountOf(g_##shaderName##_cs_spirv)})

        #define AddDispatchRepeated(shaderName, constantNum, workgroupDim, downsampleFactor, repeatNum) \
            AddComputeDispatchDesc(workgroupDim, workgroupDim, downsampleFactor, constantNum, repeatNum, #shaderName ".cs", {}, {}, {g_##shaderName##_cs_spirv, GetCountOf(g_##shaderName##_cs_spirv)})
    #endif
#else
    #define AddDispatch(shaderName, constantNum, workgroupDim, downsampleFactor) \
        AddComputeDispatchDesc(workgroupDim, workgroupDim, downsampleFactor, constantNum, 1, #shaderName ".cs", {}, {}, {})

    #define AddDispatchRepeated(shaderName, constantNum, workgroupDim, downsampleFactor, repeatNum) \
        AddComputeDispatchDesc(workgroupDim, workgroupDim, downsampleFactor, constantNum, repeatNum, #shaderName ".cs", {}, {}, {})
#endif

#define PushPass(passName) _PushPass(NRD_STRINGIFY(METHOD_NAME) " - " passName)

// TODO: rework is needed, but still better than copy-pasting
#define NRD_DECLARE_DIMS \
    uint16_t screenW = methodData.desc.fullResolutionWidth; \
    uint16_t screenH = methodData.desc.fullResolutionHeight; \
    [[maybe_unused]] uint16_t rectW = uint16_t(screenW * m_CommonSettings.resolutionScale[0] + 0.5f); \
    [[maybe_unused]] uint16_t rectH = uint16_t(screenH * m_CommonSettings.resolutionScale[1] + 0.5f); \
    [[maybe_unused]] uint16_t rectWprev = uint16_t(screenW * m_ResolutionScalePrev.x + 0.5f); \
    [[maybe_unused]] uint16_t rectHprev = uint16_t(screenH * m_ResolutionScalePrev.y + 0.5f)

namespace nrd
{
    constexpr uint32_t PERMANENT_POOL_START = 1000;
    constexpr uint32_t TRANSIENT_POOL_START = 2000;
    constexpr size_t CONSTANT_DATA_SIZE = 2 * 1024 * 2014;
    constexpr uint16_t USE_MAX_DIMS = 0xFFFF;

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

    struct MethodData
    {
        MethodDesc desc;
        Settings settings;
        size_t settingsSize;
        size_t dispatchOffset;
        size_t textureOffset;
        size_t pingPongOffset;
        size_t pingPongNum;
    };

    struct PingPong
    {
        size_t textureIndex;
        uint16_t indexInPoolToSwapWith;
    };

    struct InternalDispatchDesc
    {
        const char* name;
        const Resource* resources; // concatenated resources for all "DescriptorRangeDesc" descriptions in DenoiserDesc::pipelines[ pipelineIndex ]
        uint32_t resourceNum;
        const uint8_t* constantBufferData;
        uint32_t constantBufferDataSize;
        uint16_t pipelineIndex;
        uint16_t downsampleFactor;
        uint16_t maxRepeatNum; // mostly for internal use
        uint8_t workgroupDimX;
        uint8_t workgroupDimY;
    };

    class DenoiserImpl
    {
    // Add methods here
    public:
        // Reblur
        size_t AddMethod_ReblurDiffuse(uint16_t w, uint16_t h);
        size_t AddMethod_ReblurDiffuseOcclusion(uint16_t w, uint16_t h);
        size_t AddMethod_ReblurDiffuseSh(uint16_t w, uint16_t h);
        size_t AddMethod_ReblurSpecular(uint16_t w, uint16_t h);
        size_t AddMethod_ReblurSpecularOcclusion(uint16_t w, uint16_t h);
        size_t AddMethod_ReblurSpecularSh(uint16_t w, uint16_t h);
        size_t AddMethod_ReblurDiffuseSpecular(uint16_t w, uint16_t h);
        size_t AddMethod_ReblurDiffuseSpecularOcclusion(uint16_t w, uint16_t h);
        size_t AddMethod_ReblurDiffuseSpecularSh(uint16_t w, uint16_t h);
        size_t AddMethod_ReblurDiffuseDirectionalOcclusion(uint16_t w, uint16_t h);

        void UpdateMethod_Reblur(const MethodData& methodData, bool isDiffuse, bool isSpecular);
        void UpdateMethod_ReblurOcclusion(const MethodData& methodData);

        void AddSharedConstants_Reblur(const MethodData& methodData, const ReblurSettings& settings, Constant*& data);

        // Sigma
        size_t AddMethod_SigmaShadow(uint16_t w, uint16_t h);
        void UpdateMethod_SigmaShadow(const MethodData& methodData);
        void AddSharedConstants_Sigma(const MethodData& methodData, const SigmaSettings& settings, Constant*& data);

        size_t AddMethod_SigmaShadowTranslucency(uint16_t w, uint16_t h);

        // Relax
        void AddSharedConstants_Relax(const MethodData& methodData, Constant*& data, nrd::Method method);

        size_t AddMethod_RelaxDiffuse(uint16_t w, uint16_t h);
        void UpdateMethod_RelaxDiffuse(const MethodData& methodData);

        size_t AddMethod_RelaxSpecular(uint16_t w, uint16_t h);
        void UpdateMethod_RelaxSpecular(const MethodData& methodData);

        size_t AddMethod_RelaxDiffuseSpecular(uint16_t w, uint16_t h);
        void UpdateMethod_RelaxDiffuseSpecular(const MethodData& methodData);

        size_t AddMethod_Reference(uint16_t w, uint16_t h);
        void UpdateMethod_Reference(const MethodData& methodData);

        size_t AddMethod_SpecularReflectionMv();
        void UpdateMethod_SpecularReflectionMv(const MethodData& methodData);

        size_t AddMethod_SpecularDeltaMv(uint16_t w, uint16_t h);
        void UpdateMethod_SpecularDeltaMv(const MethodData& methodData);

    // Internal
    public:
        inline DenoiserImpl(const StdAllocator<uint8_t>& stdAllocator) :
            m_StdAllocator(stdAllocator),
            m_MethodData(GetStdAllocator()),
            m_PermanentPool(GetStdAllocator()),
            m_TransientPool(GetStdAllocator()),
            m_Resources(GetStdAllocator()),
            m_PingPongs(GetStdAllocator()),
            m_DescriptorRanges(GetStdAllocator()),
            m_Pipelines(GetStdAllocator()),
            m_Dispatches(GetStdAllocator()),
            m_ActiveDispatches(GetStdAllocator())
        {
            m_ConstantData = m_StdAllocator.allocate(CONSTANT_DATA_SIZE);
            m_MethodData.reserve(8);
            m_PermanentPool.reserve(32);
            m_TransientPool.reserve(32);
            m_Resources.reserve(128);
            m_PingPongs.reserve(32);
            m_DescriptorRanges.reserve(64);
            m_Pipelines.reserve(32);
            m_Dispatches.reserve(32);
            m_ActiveDispatches.reserve(32);
        }

        ~DenoiserImpl()
        { m_StdAllocator.deallocate(m_ConstantData, 0); }

        inline const DenoiserDesc& GetDesc() const
        { return m_Desc; }

        inline StdAllocator<uint8_t>& GetStdAllocator()
        { return m_StdAllocator; }

        Result Create(const DenoiserCreationDesc& denoiserCreationDesc);
        Result GetComputeDispatches(const CommonSettings& commonSettings, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescNum);
        Result SetMethodSettings(Method method, const void* methodSettings);

    private:
        void AddComputeDispatchDesc
        (
            uint8_t workgroupDimX,
            uint8_t workgroupDimY,
            uint16_t downsampleFactor,
            uint32_t constantBufferDataSize,
            uint32_t maxRepeatNum,
            const char* shaderFileName,
            const ComputeShader& dxbc,
            const ComputeShader& dxil,
            const ComputeShader& spirv
        );

        void Optimize();
        void PrepareDesc();
        void UpdatePingPong(const MethodData& methodData);
        void UpdateCommonSettings(const CommonSettings& commonSettings);
        void PushTexture(DescriptorType descriptorType, uint16_t index, uint16_t mipOffset, uint16_t mipNum, uint16_t indexToSwapWith = uint16_t(-1));

    // Available in methods
    private:
        constexpr void SetSharedConstants(uint32_t num4x4, uint32_t num4, uint32_t num2, uint32_t num1)
        { m_SharedConstantNum = 16 * num4x4 + 4 * num4 + 2 * num2 + 1 * num1; }

        constexpr uint32_t SumConstants(uint32_t num4x4, uint32_t num4, uint32_t num2, uint32_t num1, bool addShared = true)
        { return ( 16 * num4x4 + 4 * num4 + 2 * num2 + 1 * num1 + ( addShared ? m_SharedConstantNum : 0 ) ) * sizeof(uint32_t); }

        inline void PushInput(uint16_t index, uint16_t mipOffset = 0, uint16_t mipNum = 1, uint16_t indexToSwapWith = uint16_t(-1))
        { PushTexture(DescriptorType::TEXTURE, index, mipOffset, mipNum, indexToSwapWith); }

        void PushOutput(uint16_t index, uint16_t mipOffset = 0, uint16_t mipNum = 1, uint16_t indexToSwapWith = uint16_t(-1))
        { PushTexture(DescriptorType::STORAGE_TEXTURE, index, mipOffset, mipNum, indexToSwapWith); }

        inline Constant* PushDispatch(const MethodData& methodData, uint32_t localIndex)
        {
            size_t index = methodData.dispatchOffset + localIndex;
            const InternalDispatchDesc& internalDispatchDesc = m_Dispatches[index];

            // Copy data
            DispatchDesc dispatchDesc = {};
            dispatchDesc.name = internalDispatchDesc.name;
            dispatchDesc.resources = internalDispatchDesc.resources;
            dispatchDesc.resourceNum = internalDispatchDesc.resourceNum;
            dispatchDesc.pipelineIndex = internalDispatchDesc.pipelineIndex;

            // Update constant data
            if (m_ConstantDataOffset + internalDispatchDesc.constantBufferDataSize > CONSTANT_DATA_SIZE)
                m_ConstantDataOffset = 0;
            dispatchDesc.constantBufferData = m_ConstantData + m_ConstantDataOffset;
            dispatchDesc.constantBufferDataSize = internalDispatchDesc.constantBufferDataSize;
            m_ConstantDataOffset += internalDispatchDesc.constantBufferDataSize;

            // Update grid size
            float sx = ml::Max(internalDispatchDesc.downsampleFactor == USE_MAX_DIMS ? m_ResolutionScalePrev.x : 0.0f, m_CommonSettings.resolutionScale[0]);
            float sy = ml::Max(internalDispatchDesc.downsampleFactor == USE_MAX_DIMS ? m_ResolutionScalePrev.y : 0.0f, m_CommonSettings.resolutionScale[1]);
            uint16_t d = internalDispatchDesc.downsampleFactor == USE_MAX_DIMS ? 1 : internalDispatchDesc.downsampleFactor;

            uint16_t w = uint16_t( float(DivideUp(methodData.desc.fullResolutionWidth, d)) * sx + 0.5f );
            uint16_t h = uint16_t( float(DivideUp(methodData.desc.fullResolutionHeight, d)) * sy + 0.5f );

            dispatchDesc.gridWidth = DivideUp(w, internalDispatchDesc.workgroupDimX);
            dispatchDesc.gridHeight = DivideUp(h, internalDispatchDesc.workgroupDimY);

            // Store
            m_ActiveDispatches.push_back(dispatchDesc);

            return (Constant*)dispatchDesc.constantBufferData;
        }

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
        Vector<MethodData> m_MethodData;
        Vector<TextureDesc> m_PermanentPool;
        Vector<TextureDesc> m_TransientPool;
        Vector<Resource> m_Resources;
        Vector<PingPong> m_PingPongs;
        Vector<DescriptorRangeDesc> m_DescriptorRanges;
        Vector<PipelineDesc> m_Pipelines;
        Vector<InternalDispatchDesc> m_Dispatches;
        Vector<DispatchDesc> m_ActiveDispatches;
        Timer m_Timer;
        ml::sFastRand m_FastRandState = {};
        DenoiserDesc m_Desc = {};
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
        ml::float4 m_Rotator_HistoryFix = ml::float4::Zero();
        ml::float4 m_Frustum = ml::float4::Zero();
        ml::float4 m_FrustumPrev = ml::float4::Zero();
        ml::float3 m_CameraDelta = ml::float3::Zero();
        ml::float3 m_ViewDirection = ml::float3::Zero();
        ml::float3 m_ViewDirectionPrev = ml::float3::Zero();
        ml::float2 m_JitterPrev = ml::float2(0.0f);
        ml::float2 m_ResolutionScalePrev = ml::float2(1.0f);
        const char* m_PassName = nullptr;
        uint8_t* m_ConstantData = nullptr;
        size_t m_ConstantDataOffset = 0;
        size_t m_ResourceOffset = 0;
        size_t m_ClearDispatchOffset = 0;
        float m_IsOrtho = 0.0f;
        float m_CheckerboardResolveAccumSpeed = 0.0f;
        float m_JitterDelta = 0.0f;
        float m_TimeDelta = 0.0f;
        float m_FrameRateScale = 0.0f;
        uint32_t m_SharedConstantNum = 0;
        uint32_t m_AccumulatedFrameNum = 0;
        uint16_t m_TransientPoolOffset = 0;
        uint16_t m_PermanentPoolOffset = 0;
        float m_ProjectY = 0.0f; // TODO: NRD assumes that there are no checkerboard "tricks" in Y direction, so no a separate m_ProjectX
        bool m_EnableValidation = false;
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
