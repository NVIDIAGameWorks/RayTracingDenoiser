/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once
#include "DeviceBase.h"
#include "DeviceVal.h"

#include <algorithm>

namespace nri
{
    struct DeviceObjectBaseVal
    {
        DeviceObjectBaseVal(DeviceVal& device);

        DeviceVal& GetDevice() const;

    protected:
        DeviceVal& m_Device;
        const CoreInterface& m_CoreAPI;
    };

    inline DeviceVal& DeviceObjectBaseVal::GetDevice() const
    {
        return m_Device;
    }

    template< typename T >
    struct DeviceObjectVal : public DeviceObjectBaseVal
    {
        DeviceObjectVal(DeviceVal& device, T& object);

        T& GetImpl() const;
        const String& GetDebugName() const;

    protected:
        T& m_ImplObject;
        String m_Name;
    };

    template< typename T >
    inline DeviceObjectVal<T>::DeviceObjectVal(DeviceVal& device, T& object) :
        DeviceObjectBaseVal(device),
        m_ImplObject(object),
        m_Name(device.GetStdAllocator())
    {
    }

    template< typename T >
    inline T& DeviceObjectVal<T>::GetImpl() const
    {
        return m_ImplObject;
    }

    template< typename T >
    const String& DeviceObjectVal<T>::GetDebugName() const
    {
        return m_Name;
    }

    template< typename T >
    inline DeviceVal& GetDeviceVal(T& object)
    {
        return ((DeviceObjectBaseVal&)object).GetDevice();
    }

    uint64_t GetMemorySizeD3D12(const MemoryD3D12Desc& memoryD3D12Desc);
    void GetTextureDescD3D12(const TextureD3D12Desc& textureD3D12Desc, TextureDesc& textureDesc);
    void GetBufferDescD3D12(const BufferD3D12Desc& bufferD3D12Desc, BufferDesc& bufferDesc);

    void GetTextureDescD3D11(const TextureD3D11Desc& textureD3D11Desc, TextureDesc& textureDesc);
    void GetBufferDescD3D11(const BufferD3D11Desc& bufferD3D11Desc, BufferDesc& bufferDesc);

    constexpr const char* DESCRIPTOR_TYPE_NAME[] = {
        "SAMPLER",
        "CONSTANT_BUFFER",
        "TEXTURE",
        "STORAGE_TEXTURE",
        "BUFFER",
        "STORAGE_BUFFER",
        "STRUCTURED_BUFFER",
        "STORAGE_STRUCTURED_BUFFER",
        "ACCELERATION_STRUCTURE"
    };
    static_assert(GetCountOf(DESCRIPTOR_TYPE_NAME) == (uint32_t)nri::DescriptorType::MAX_NUM, "descriptor type name array is out of date");

    constexpr const char* GetDescriptorTypeName(nri::DescriptorType descriptorType)
    {
        return DESCRIPTOR_TYPE_NAME[(uint32_t)descriptorType];
    }
}

#define NRI_GET_IMPL(className, object) \
    ((object != nullptr) ? &((className##Val*)object)->GetImpl() : nullptr)