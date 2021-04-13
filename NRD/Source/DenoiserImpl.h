/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "NRD.h"

#define NDC_DONT_CARE
#include "MathLib/MathLib.h"

typedef nrd::MemoryAllocatorInterface MemoryAllocatorInterface;
#include "StdAllocator/StdAllocator.h"

#include "Timer/Timer.h"

#define AddDispatch(shaderName, constantNum, workgroupDim, downsampleFactor) \
    AddComputeDispatchDesc(workgroupDim, downsampleFactor, constantNum, 1, "main\0" #shaderName ".cs", {g_##shaderName##_cs_dxbc, GetCountOf(g_ ## shaderName ## _cs_dxbc)}, {g_##shaderName##_cs_dxil, GetCountOf(g_ ## shaderName ## _cs_dxil)}, {g_##shaderName##_cs_spirv, GetCountOf(g_ ## shaderName ## _cs_spirv)})

#define AddDispatchRepeated(shaderName, constantNum, workgroupDim, downsampleFactor, repeatNum) \
    AddComputeDispatchDesc(workgroupDim, downsampleFactor, constantNum, repeatNum, "main\0" #shaderName ".cs", {g_##shaderName##_cs_dxbc, GetCountOf(g_ ## shaderName ## _cs_dxbc)}, {g_##shaderName##_cs_dxil, GetCountOf(g_ ## shaderName ## _cs_dxil)}, {g_##shaderName##_cs_spirv, GetCountOf(g_ ## shaderName ## _cs_spirv)})

inline uint16_t DivideUp(uint16_t x, uint16_t y)
{ return (x + y - 1) / y; }

template <class T>
inline uint16_t AsUint(T x)
{ return (uint16_t)x; }

namespace nrd
{
    constexpr uint32_t PERMANENT_POOL_START = 1000;
    constexpr uint32_t TRANSIENT_POOL_START = 2000;
    constexpr size_t CONSTANT_DATA_SIZE = 2 * 1024 * 2014;

    union Constant
    {
        float f;
        uint32_t ui;
    };

    union Settings
    {
        // Add settings here
        ReblurDiffuseSettings diffuse;
        ReblurSpecularSettings specular;
        ReblurDiffuseSpecularSettings diffuseSpecular;
        SigmaShadowSettings shadow;
        RelaxDiffuseSpecularSettings relax;
    };

    struct MethodData
    {
        MethodDesc desc;
        Settings settings;
        size_t settingsSize;
        size_t dispatchOffset;
        size_t dispatchNum;
        size_t textureOffset;
        size_t textureNum;
        size_t permanentPoolNum;
        size_t transientPoolNum;
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
        uint16_t workgroupDim;
        uint16_t downsampleFactor;
        uint16_t maxRepeatNum; // mostly for internal use
    };

    class DenoiserImpl
    {
    // Add methods here
    public:
        size_t AddMethod_ReblurDiffuse(uint16_t w, uint16_t h);
        void UpdateMethod_ReblurDiffuse(const MethodData& methodData);
        void AddSharedConstants_ReblurDiffuse(const MethodData& methodData, const ReblurDiffuseSettings& settings, Constant*& data);

        size_t AddMethod_ReblurSpecular(uint16_t w, uint16_t h);
        void UpdateMethod_ReblurSpecular(const MethodData& methodData);
        void AddSharedConstants_ReblurSpecular(const MethodData& methodData, const ReblurSpecularSettings& settings, Constant*& data);

        size_t AddMethod_ReblurDiffuseSpecular(uint16_t w, uint16_t h);
        void UpdateMethod_ReblurDiffuseSpecular(const MethodData& methodData);
        void AddSharedConstants_ReblurDiffuseSpecular(const MethodData& methodData, const ReblurDiffuseSpecularSettings& settings, Constant*& data);

        size_t AddMethod_SigmaShadow(uint16_t w, uint16_t h);
        void UpdateMethod_SigmaShadow(const MethodData& methodData);
        void AddSharedConstants_SigmaShadow(const MethodData& methodData, const SigmaShadowSettings& settings, Constant*& data);

        size_t AddMethod_SigmaTranslucentShadow(uint16_t w, uint16_t h);
        void UpdateMethod_SigmaTranslucentShadow(const MethodData& methodData);
        void AddSharedConstants_SigmaTranslucentShadow(const MethodData& methodData, const SigmaShadowSettings& settings, Constant*& data);

        size_t AddMethod_RelaxDiffuseSpecular(uint16_t w, uint16_t h);
        void UpdateMethod_RelaxDiffuseSpecular(const MethodData& methodData);

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
        void Optimize();
        void PrepareDesc();
        void AddComputeDispatchDesc(uint16_t workgroupDim, uint16_t downsampleFactor, uint32_t constantBufferDataSize, uint32_t maxRepeatNum, const char* entryPointName, const ComputeShader& dxbc, const ComputeShader& dxil, const ComputeShader& spirv);
        void UpdatePingPong(const MethodData& methodData);
        void UpdateCommonSettings(const CommonSettings& commonSettings);
        void PushTexture(DescriptorType descriptorType, uint16_t index, uint16_t mipOffset, uint16_t mipNum, uint16_t indexToSwapWith = uint16_t(-1));

    // Available in methods
    private:
        constexpr void SetSharedConstants(uint32_t num4x4, uint32_t num4, uint32_t num2, uint32_t num1)
        { m_SharedConstantNum = 16 * num4x4 + 4 * num4 + 2 * num2 + 1 * num1; }

        constexpr uint32_t SumConstants(uint32_t num4x4, uint32_t num4, uint32_t num2, uint32_t num1, bool addShared = true)
        { return ( 16 * num4x4 + 4 * num4 + 2 * num2 + 1 * num1 +( addShared ? m_SharedConstantNum : 0 ) ) * sizeof(uint32_t); }

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
            uint16_t w = uint16_t( float(DivideUp(methodData.desc.fullResolutionWidth, internalDispatchDesc.downsampleFactor)) * m_CommonSettings.resolutionScale + 0.5f );
            uint16_t h = uint16_t( float(DivideUp(methodData.desc.fullResolutionHeight, internalDispatchDesc.downsampleFactor)) * m_CommonSettings.resolutionScale + 0.5f );
            dispatchDesc.gridWidth = DivideUp(w, internalDispatchDesc.workgroupDim);
            dispatchDesc.gridHeight = DivideUp(h, internalDispatchDesc.workgroupDim);

            // Store
            m_ActiveDispatches.push_back(dispatchDesc);

            return (Constant*)dispatchDesc.constantBufferData;
        }

        inline void PushPass(const char* name)
        {
            m_PassName = name;
            m_ResourceOffset = m_Resources.size();
        }

        inline void ValidateConstants(const Constant* lastConstant) const
        {
            const DispatchDesc& dispatchDesc = m_ActiveDispatches.back();

            size_t num = size_t(lastConstant - (const Constant*)dispatchDesc.constantBufferData);
            size_t bytes = num * sizeof(uint32_t);
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
        DenoiserDesc m_Desc = {};
        CommonSettings m_CommonSettings = {};
        float4x4 m_ViewToClip = float4x4::identity;
        float4x4 m_ViewToClipPrev = float4x4::identity;
        float4x4 m_ClipToView = float4x4::identity;
        float4x4 m_ClipToViewPrev = float4x4::identity;
        float4x4 m_WorldToView = float4x4::identity;
        float4x4 m_WorldToViewPrev = float4x4::identity;
        float4x4 m_ViewToWorld = float4x4::identity;
        float4x4 m_ViewToWorldPrev = float4x4::identity;
        float4x4 m_WorldToClip = float4x4::identity;
        float4x4 m_WorldToClipPrev = float4x4::identity;
        float4x4 m_ClipToWorld = float4x4::identity;
        float4x4 m_ClipToWorldPrev = float4x4::identity;
        float4 m_Rotator[3] = {};
        float4 m_Frustum = float4(0.0f);
        float4 m_FrustumPrev = float4(0.0f);
        float3 m_CameraDelta = float3(0.0f);
        float3 m_CameraDeltaSmoothed = float3(0.0f);
        float2 m_JitterPrev = float2(0.0f);
        const char* m_PassName = nullptr;
        uint8_t* m_ConstantData = nullptr;
        size_t m_ConstantDataOffset = 0;
        size_t m_ResourceOffset = 0;
        float m_ResolutionScalePrev = 1.0f;
        float m_IsOrtho = 0.0f;
        float m_CheckerboardResolveAccumSpeed = 0.0f;
        float m_JitterDelta = 0.0f;
        float m_TimeDelta = 0.0f;
        float m_FrameRateScale = 0.0f;
        uint32_t m_SharedConstantNum = 0;
        uint16_t m_TransientPoolOffset = 0;
        uint16_t m_PermanentPoolOffset = 0;
        float m_ProjectY = 0.0f; // TODO: NRD assumes that there are no checkerboard "tricks" in Y direction, so no a separate m_ProjectX
        bool m_EnableValidation = false;
        bool m_IsFirstUse = true;
    };

    inline void AddFloat4x4(Constant*& dst, const float4x4& x)
    {
        memcpy(dst, &x, sizeof(float4x4));
        dst += 16;
    }

    inline void AddFloat4(Constant*& dst, const float4& x)
    {
        memcpy(dst, &x, sizeof(float4));
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
