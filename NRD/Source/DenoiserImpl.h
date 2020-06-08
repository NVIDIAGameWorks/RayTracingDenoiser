/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.h"

#define NDC_DONT_CARE
#include "MathLib/MathLib.h"

#include "Timer/Timer.h"

#include <vector>

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
        DiffuseSettings diffuse;
        SpecularSettings specular;
        ShadowSettings shadow;
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
        uint32_t indexInPoolToSwapWith;
    };

    class DenoiserImpl
    {
    // Add your methods here
    public:
        size_t AddMethod_Diffuse(uint32_t w, uint32_t h);
        void UpdateMethod_Diffuse(const MethodData& methodData);

        size_t AddMethod_Specular(uint32_t w, uint32_t h);
        void UpdateMethod_Specular(const MethodData& methodData);

        size_t AddMethod_Shadow(uint32_t w, uint32_t h);
        void UpdateMethod_Shadow(const MethodData& methodData);

    // Internal
    public:
        DenoiserImpl()
        {}

        ~DenoiserImpl()
        {}

        inline const DenoiserDesc& GetDesc() const
        { return m_Desc; }

        Result Create(const DenoiserCreationDesc& denoiserCreationDesc);
        Result GetComputeDispatches(const CommonSettings& commonSettings, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescNum);
        Result SetMethodSettings(Method method, const void* methodSettings);

    private:
        void Optimize();
        void PrepareDesc();
        void AddComputeDispatchDesc(DispatchDesc& computeDispatchDesc, const char* entryPointName, const ComputeShader& dxbc, const ComputeShader& dxil, const ComputeShader& spirv, uint32_t width, uint32_t height, uint32_t ctaWidth = 16, uint32_t ctaHeight = 16);
        void UpdatePingPong(const MethodData& methodData);
        void UpdateCommonSettings(const CommonSettings& commonSettings);
        void PushTexture(DescriptorType descriptorType, uint32_t index, uint16_t mipOffset, uint16_t mipNum, uint32_t indexToSwapWith = uint32_t(-1));

    // Available in methods
    private:
        inline void PushInput(uint32_t index, uint16_t mipOffset = 0, uint16_t mipNum = 1, uint32_t indexToSwapWith = uint32_t(-1))
        { PushTexture(DescriptorType::TEXTURE, index, mipOffset, mipNum, indexToSwapWith); }

        void PushOutput(uint32_t index, uint16_t mipOffset = 0, uint16_t mipNum = 1, uint32_t indexToSwapWith = uint32_t(-1))
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
        std::vector<MethodData> m_MethodData;
        std::vector<TextureDesc> m_PermanentPool;
        std::vector<TextureDesc> m_TransientPool;
        std::vector<Resource> m_Resources;
        std::vector<Constant> m_Constants;
        std::vector<PingPong> m_PingPongs;
        std::vector<DescriptorRangeDesc> m_DescriptorRanges;
        std::vector<PipelineDesc> m_Pipelines;
        std::vector<DispatchDesc> m_Dispatches;
        std::vector<DispatchDesc> m_ActiveDispatches;
        std::vector<size_t> m_ActiveDispatchIndices;
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
        float4 m_Frustum;
        float4 m_FrustumPrev;
        float3 m_CameraDelta = {};
        float2 m_WhiteNoiseSinCos = {};
        float2 m_BlueNoiseSinCos = {};
        const char* m_PassName = nullptr;
        size_t m_ResourceOffset = 0;
        float m_IsOrtho = 0.0f;
        float m_IsOrthoPrev = 0.0f;
        uint32_t m_TransientPoolOffset = 0;
        uint32_t m_PermanentPoolOffset = 0;
        float m_Project = 0.0f;
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

    constexpr uint32_t SumConstants(uint32_t num4x4, uint32_t num4 = 0, uint32_t num2 = 0, uint32_t num1 = 0)
    { return (16 * num4x4 + 4 * num4 + 2 * num2 + 1 * num1) * sizeof(uint32_t); }
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

// TODO: use a shared header?
inline uint32_t DivideUp(uint32_t x, uint32_t y)
{ return (x + y - 1) / y; }

template <class T>
inline uint32_t AsUint(T x)
{ return (uint32_t)x; }

template <typename T, uint32_t N> constexpr uint32_t GetCountOf(T const (&)[N])
{ return N; }

template<typename T> constexpr T GetAlignedSize(const T& size, size_t alignment)
{ return T(((size + alignment - 1) / alignment) * alignment); }
