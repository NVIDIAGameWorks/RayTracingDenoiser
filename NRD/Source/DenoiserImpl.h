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

#define _MemoryAllocatorInterface nrd::MemoryAllocatorInterface
#include "StdAllocator/StdAllocator.h"

#include "Timer/Timer.h"

namespace nrd
{
    constexpr uint32_t PERMANENT_POOL_START = 1000;
    constexpr uint32_t TRANSIENT_POOL_START = 2000;

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
        SvgfSettings svgf;
    };

    struct MethodData
    {
        MethodDesc desc;
        Settings settings;
        size_t settingsSize;
        size_t dispatchOffset;
        size_t dispatchNum;
        size_t constantOffset;
        size_t constantNum;
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

    class DenoiserImpl
    {
    // Add methods here
    public:
        size_t AddMethod_ReblurDiffuse(uint16_t w, uint16_t h);
        void UpdateMethod_ReblurDiffuse(const MethodData& methodData);

        size_t AddMethod_ReblurSpecular(uint16_t w, uint16_t h);
        void UpdateMethod_ReblurSpecular(const MethodData& methodData);

        size_t AddMethod_ReblurDiffuseSpecular(uint16_t w, uint16_t h);
        void UpdateMethod_ReblurDiffuseSpecular(const MethodData& methodData);

        size_t AddMethod_SigmaShadow(uint16_t w, uint16_t h);
        void UpdateMethod_SigmaShadow(const MethodData& methodData);

        size_t AddMethod_SigmaTranslucentShadow(uint16_t w, uint16_t h);
        void UpdateMethod_SigmaTranslucentShadow(const MethodData& methodData);

        size_t AddMethod_RelaxDiffuseSpecular(uint16_t w, uint16_t h);
        void UpdateMethod_RelaxDiffuseSpecular(const MethodData& methodData);

        size_t AddMethod_Svgf(uint16_t w, uint16_t h);
        void UpdateMethod_Svgf(const MethodData& methodData);

    // Internal
    public:
        inline DenoiserImpl(const StdAllocator<uint8_t>& stdAllocator) :
            m_StdAllocator(stdAllocator),
            m_MethodData(GetStdAllocator()),
            m_PermanentPool(GetStdAllocator()),
            m_TransientPool(GetStdAllocator()),
            m_Resources(GetStdAllocator()),
            m_Constants(GetStdAllocator()),
            m_PingPongs(GetStdAllocator()),
            m_DescriptorRanges(GetStdAllocator()),
            m_Pipelines(GetStdAllocator()),
            m_Dispatches(GetStdAllocator()),
            m_ActiveDispatches(GetStdAllocator()),
            m_ActiveDispatchIndices(GetStdAllocator())
        {
            // Reserve ~16Kb of memory to prevent "burst of tiny allocations"
            m_MethodData.reserve(8);
            m_PermanentPool.reserve(32);
            m_TransientPool.reserve(32);
            m_Resources.reserve(128);
            m_Constants.reserve(1024);
            m_PingPongs.reserve(32);
            m_DescriptorRanges.reserve(64);
            m_Pipelines.reserve(32);
            m_Dispatches.reserve(32);
            m_ActiveDispatches.reserve(32);
            m_ActiveDispatchIndices.reserve(32);
        }

        ~DenoiserImpl()
        {}

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
        void AddComputeDispatchDesc(DispatchDesc& computeDispatchDesc, const char* entryPointName, const ComputeShader& dxbc, const ComputeShader& dxil, const ComputeShader& spirv, uint32_t width, uint32_t height, uint32_t ctaWidth = 16, uint32_t ctaHeight = 16);
        void UpdatePingPong(const MethodData& methodData);
        void UpdateCommonSettings(const CommonSettings& commonSettings);
        void PushTexture(DescriptorType descriptorType, uint16_t index, uint16_t mipOffset, uint16_t mipNum, uint16_t indexToSwapWith = uint16_t(-1));

    // Available in methods
    private:
        void AddNrdSharedConstants(const MethodData& methodData, float planeDistSensitivity, Constant*& data);

        constexpr uint32_t GetSharedConstantsNum() const
        { return 32; } // must be a multiply of 4

        constexpr uint32_t SumConstants(uint32_t num4x4, uint32_t num4 = 0, uint32_t num2 = 0, uint32_t num1 = 0, bool addSharedConstants = true)
        { return ( 16 * num4x4 + 4 * num4 + 2 * num2 + 1 * num1 + (addSharedConstants ? GetSharedConstantsNum() : 0) ) * sizeof(uint32_t); }

        inline void PushInput(uint16_t index, uint16_t mipOffset = 0, uint16_t mipNum = 1, uint16_t indexToSwapWith = uint16_t(-1))
        { PushTexture(DescriptorType::TEXTURE, index, mipOffset, mipNum, indexToSwapWith); }

        void PushOutput(uint16_t index, uint16_t mipOffset = 0, uint16_t mipNum = 1, uint16_t indexToSwapWith = uint16_t(-1))
        { PushTexture(DescriptorType::STORAGE_TEXTURE, index, mipOffset, mipNum, indexToSwapWith); }

        inline Constant* PushDispatch(const MethodData& methodData, uint32_t localIndex)
        {
            size_t index = methodData.dispatchOffset + localIndex;
            m_ActiveDispatchIndices.push_back(index);

            return (Constant*)m_Dispatches[index].constantBufferData;
        }

        inline void PushPass(const char* name)
        {
            m_PassName = name;
            m_ResourceOffset = m_Resources.size();
        }

        inline void ValidateConstants(const Constant* lastConstant) const
        {
            const auto& lastDispatch = m_ActiveDispatchIndices.back();
            const DispatchDesc& dispatchDesc = m_Dispatches[lastDispatch];

            size_t num = size_t(lastConstant - (const Constant*)dispatchDesc.constantBufferData);
            assert( num == dispatchDesc.constantBufferDataSize / sizeof(uint32_t) );
        }

    private:
        StdAllocator<uint8_t> m_StdAllocator;
        Vector<MethodData> m_MethodData;
        Vector<TextureDesc> m_PermanentPool;
        Vector<TextureDesc> m_TransientPool;
        Vector<Resource> m_Resources;
        Vector<Constant> m_Constants;
        Vector<PingPong> m_PingPongs;
        Vector<DescriptorRangeDesc> m_DescriptorRanges;
        Vector<PipelineDesc> m_Pipelines;
        Vector<DispatchDesc> m_Dispatches;
        Vector<DispatchDesc> m_ActiveDispatches;
        Vector<size_t> m_ActiveDispatchIndices;
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
        float2 m_JitterPrev = float2(0.0f);
        const char* m_PassName = nullptr;
        size_t m_ResourceOffset = 0;
        float m_IsOrtho = 0.0f;
        float m_IsOrthoPrev = 0.0f;
        float m_CheckerboardResolveAccumSpeed = 0.0f;
        float m_JitterDelta = 0.0f;
        uint32_t m_ProjectionFlags = 0;
        uint16_t m_TransientPoolOffset = 0;
        uint16_t m_PermanentPoolOffset = 0;
        float m_ProjectY = 0.0f; // TODO: NRD assumes that there are no checkerboard "tricks" in Y direction, so no a separate m_ProjectX
        bool m_EnableValidation = false;
    };

    // TODO: allocate aligned memory for m_Constants, and use "typecast & assign operator" instead! Don't use resize()

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

#define AddDispatchWithExplicitCTASize(desc, shaderName, width, height, ctaWidth, ctaHeight) \
    { \
        ComputeShader dxbc = {g_##shaderName##_cs_dxbc, GetCountOf(g_ ## shaderName ## _cs_dxbc)}; \
        ComputeShader dxil = {g_##shaderName##_cs_dxil, GetCountOf(g_ ## shaderName ## _cs_dxil)}; \
        ComputeShader spirv = {g_##shaderName##_cs_spirv, GetCountOf(g_ ## shaderName ## _cs_spirv)}; \
        AddComputeDispatchDesc(desc, "main\0" #shaderName ".cs", dxbc, dxil, spirv, width, height, ctaWidth, ctaHeight); \
    }

#define AddDispatch(desc, shaderName, width, height) \
    { \
        ComputeShader dxbc = {g_##shaderName##_cs_dxbc, GetCountOf(g_ ## shaderName ## _cs_dxbc)}; \
        ComputeShader dxil = {g_##shaderName##_cs_dxil, GetCountOf(g_ ## shaderName ## _cs_dxil)}; \
        ComputeShader spirv = {g_##shaderName##_cs_spirv, GetCountOf(g_ ## shaderName ## _cs_spirv)}; \
        AddComputeDispatchDesc(desc, "main\0" #shaderName ".cs", dxbc, dxil, spirv, width, height); \
    }

inline uint32_t DivideUp(uint32_t x, uint32_t y)
{ return (x + y - 1) / y; }

template <class T>
inline uint16_t AsUint(T x)
{ return (uint16_t)x; }
