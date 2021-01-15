/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <d3d11_4.h>

#include "DeviceBase.h"

#define NULL_TEXTURE_REGION_DESC 0xFFFF

enum class BufferType
{
    DEVICE,
    DYNAMIC,
    READBACK,
    UPLOAD
};

enum class MapType
{
    DEFAULT,
    READ
};

enum class DynamicState
{
    SET_ONLY,
    BIND_AND_SET
};

enum class DescriptorTypeDX11 : uint8_t
{
    // don't change order
    NO_SHADER_VISIBLE,
    RESOURCE,
    SAMPLER,
    STORAGE,
    // must be last!
    CONSTANT,
    DYNAMIC_CONSTANT
};

struct FormatInfo
{
    DXGI_FORMAT typeless;
    DXGI_FORMAT typed;
    uint32_t stride;
    bool isInteger;
};

const FormatInfo& GetFormatInfo(nri::Format format);
D3D11_PRIMITIVE_TOPOLOGY GetD3D11TopologyFromTopology(nri::Topology topology, uint32_t patchPoints);
D3D11_CULL_MODE GetD3D11CullModeFromCullMode(nri::CullMode cullMode);
D3D11_COMPARISON_FUNC GetD3D11ComparisonFuncFromCompareFunc(nri::CompareFunc compareFunc);
D3D11_STENCIL_OP GetD3D11StencilOpFromStencilFunc(nri::StencilFunc stencilFunc);
D3D11_BLEND_OP GetD3D11BlendOpFromBlendFunc(nri::BlendFunc blendFunc);
D3D11_BLEND GetD3D11BlendFromBlendFactor(nri::BlendFactor blendFactor);
D3D11_LOGIC_OP GetD3D11LogicOpFromLogicFunc(nri::LogicFunc logicalFunc);

struct AGSContext;
struct DX11Extensions;
struct IDXGISwapChain4;

struct VersionedDevice
{
    ~VersionedDevice()
    {}

    inline ID3D11Device5* operator->() const
    {
        return ptr;
    }

    ComPtr<ID3D11Device5> ptr;
    const DX11Extensions* ext = nullptr;
    bool isDeferredContextsEmulated = false;
    uint8_t version = 0;
};

struct VersionedContext
{
    ~VersionedContext()
    {}

    inline ID3D11DeviceContext4* operator->() const
    {
        return ptr;
    }

    inline void EnterCriticalSection() const
    {
        if (multiThread)
            multiThread->Enter();
        else
            ::EnterCriticalSection(criticalSection);
    }

    inline void LeaveCriticalSection() const
    {
        if (multiThread)
            multiThread->Leave();
        else
            ::LeaveCriticalSection(criticalSection);
    }

    ComPtr<ID3D11DeviceContext4> ptr;
    ComPtr<ID3D11Multithread> multiThread;
    const DX11Extensions* ext = nullptr;
    CRITICAL_SECTION* criticalSection;
    uint8_t version = 0;
};

struct VersionedSwapchain
{
    ~VersionedSwapchain()
    {}

    inline IDXGISwapChain4* operator->() const
    {
        return ptr;
    }

    ComPtr<IDXGISwapChain4> ptr;
    uint8_t version = 0;
};

struct CriticalSection
{
    CriticalSection(const VersionedContext& context) :
        m_Context(context)
    {
        m_Context.EnterCriticalSection();
    }

    ~CriticalSection()
    {
        m_Context.LeaveCriticalSection();
    }

    const VersionedContext& m_Context;
};

struct DX11Extensions
{
    ~DX11Extensions();

    inline bool IsAvailable() const
    {
        return isAvailableNVAPI || isAvailableAGS;
    }

    void Create(const Log& log, nri::Vendor vendor, AGSContext* context);
    void BeginUAVOverlap(const VersionedContext& context) const;
    void EndUAVOverlap(const VersionedContext& context) const;
    void WaitForDrain(const VersionedContext& context, nri::BarrierDependency dependency) const;
    void SetDepthBounds(const VersionedContext& context, float minBound, float maxBound) const;
    void MultiDrawIndirect(const VersionedContext& context, ID3D11Buffer* buffer, uint64_t offset, uint32_t drawNum, uint32_t stride) const;
    void MultiDrawIndexedIndirect(const VersionedContext& context, ID3D11Buffer* buffer, uint64_t offset, uint32_t drawNum, uint32_t stride) const;

    AGSContext* agsContext = nullptr;
    bool isAvailableNVAPI = false;
    bool isAvailableAGS = false;
    const Log* log = nullptr;
};

struct SubresourceInfo
{
    const void* resource;
    uint64_t data;

    inline void Initialize(const void* tex, uint16_t mipOffset, uint16_t mipNum, uint16_t arrayOffset, uint16_t arraySize)
    {
        resource = tex;
        data = (uint64_t(arraySize) << 48) | (uint64_t(arrayOffset) << 32) | (uint64_t(mipNum) << 16) | uint64_t(mipOffset);
    }

    inline void Initialize(const void* buf)
    {
        resource = buf;
        data = 0;
    }

    friend bool operator==(const SubresourceInfo& a, const SubresourceInfo& b)
    {
        return a.resource == b.resource && a.data == b.data;
    }
};

struct BindingState
{
    std::array<SubresourceInfo, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources;

    std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> storages;
    uint32_t storageStartSlot;
    uint32_t storageEndSlot;

    inline void ResetStorages()
    {
        memset(&storages, 0, sizeof(storages));
        storageStartSlot = uint32_t(-1);
        storageEndSlot = 0;
    }

    inline uint32_t UpdateStartEndStorageSlots(uint32_t baseSlot, uint32_t descriptorNum)
    {
        storageStartSlot = std::min(storageStartSlot, baseSlot);
        storageEndSlot = std::max(storageEndSlot, baseSlot + descriptorNum);

        return storageEndSlot - storageStartSlot;
    }

    inline void ResetResources()
    {
        memset(&resources, 0, sizeof(resources));
    }

    inline void UnbindSubresource(const VersionedContext& context, const SubresourceInfo& subresourceInfo) const
    {
        constexpr ID3D11ShaderResourceView* nullDescriptor = nullptr;

        // TODO: suboptimal implementation:
        // - if a resource not present 128 iterations will be taken
        // - store max register to reduce interations number required
        // - store visibility to unbind only necessary state
        // - std::map can be used for fast searches, but... it will slow down BindDescriptorSets what is undesirable
        // - can be skipped if debug layer is not used

        for (uint32_t slot = 0; slot < (uint32_t)resources.size(); slot++)
        {
            if (resources[slot] == subresourceInfo)
            {
                context->VSSetShaderResources(slot, 1, &nullDescriptor);
                context->HSSetShaderResources(slot, 1, &nullDescriptor);
                context->DSSetShaderResources(slot, 1, &nullDescriptor);
                context->GSSetShaderResources(slot, 1, &nullDescriptor);
                context->PSSetShaderResources(slot, 1, &nullDescriptor);
                context->CSSetShaderResources(slot, 1, &nullDescriptor);

                return;
            }
        }
    }
};

namespace nri
{
    struct CommandBufferHelper
    {
        virtual ~CommandBufferHelper() {}
        virtual Result Create(ID3D11DeviceContext* precreatedContext) = 0;
        virtual void Submit(const VersionedContext& context) = 0;
        virtual StdAllocator<uint8_t>& GetStdAllocator() const = 0;
    };
}

template<typename T> void SetName(const ComPtr<T>& obj, const char* name)
{
    if (obj)
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, (uint32_t)std::strlen(name), name);
}

static inline uint64_t ComputeHash(const void* key, uint32_t len)
{
    const uint8_t* p = (uint8_t*)key;
    uint64_t result = 14695981039346656037ull;
    while( len-- )
        result = (result ^ (*p++)) * 1099511628211ull;

    return result;
}

struct SamplePositionsState
{
    std::array<nri::SamplePosition, 16> positions;
    uint64_t positionHash;
    uint32_t positionNum;

    inline void Reset()
    {
        memset(&positions, 0, sizeof(positions));
        positionNum = 0;
        positionHash = 0;
    }

    inline void Set(const nri::SamplePosition* samplePositions, uint32_t samplePositionNum)
    {
        const uint32_t size = sizeof(nri::SamplePosition) * samplePositionNum;

        memcpy(&positions, samplePositions, size);
        positionHash = ComputeHash(samplePositions, size);
        positionNum = samplePositionNum;
    }
};

#include "DeviceD3D11.h"
