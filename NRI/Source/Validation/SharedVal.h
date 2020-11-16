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

    constexpr bool IsAccessMaskSupported(BufferUsageBits usageMask, AccessBits accessMask)
    {
        BufferUsageBits requiredUsageMask = BufferUsageBits::NONE;

        if (accessMask & AccessBits::VERTEX_BUFFER)
            requiredUsageMask |= BufferUsageBits::VERTEX_BUFFER;

        if (accessMask & AccessBits::INDEX_BUFFER)
            requiredUsageMask |= BufferUsageBits::INDEX_BUFFER;

        if (accessMask & AccessBits::CONSTANT_BUFFER)
            requiredUsageMask |= BufferUsageBits::CONSTANT_BUFFER;

        if (accessMask & AccessBits::ARGUMENT_BUFFER)
            requiredUsageMask |= BufferUsageBits::ARGUMENT_BUFFER;

        if (accessMask & AccessBits::SHADER_RESOURCE)
            requiredUsageMask |= BufferUsageBits::SHADER_RESOURCE;

        if (accessMask & AccessBits::SHADER_RESOURCE_STORAGE)
            requiredUsageMask |= BufferUsageBits::SHADER_RESOURCE_STORAGE;

        if (accessMask & AccessBits::COLOR_ATTACHMENT)
            return false;

        if (accessMask & AccessBits::DEPTH_STENCIL_WRITE)
            return false;

        if (accessMask & AccessBits::DEPTH_STENCIL_READ)
            return false;

        if (accessMask & AccessBits::ACCELERATION_STRUCTURE_READ)
            return false;

        if (accessMask & AccessBits::ACCELERATION_STRUCTURE_WRITE)
            return false;

        return (uint32_t)(requiredUsageMask & usageMask) == (uint32_t)requiredUsageMask;
    }

    constexpr bool IsAccessMaskSupported(TextureUsageBits usageMask, AccessBits accessMask)
    {
        TextureUsageBits requiredUsageMask = TextureUsageBits::NONE;

        if (accessMask & AccessBits::VERTEX_BUFFER)
            return false;

        if (accessMask & AccessBits::INDEX_BUFFER)
            return false;

        if (accessMask & AccessBits::CONSTANT_BUFFER)
            return false;

        if (accessMask & AccessBits::ARGUMENT_BUFFER)
            return false;

        if (accessMask & AccessBits::SHADER_RESOURCE)
            requiredUsageMask |= TextureUsageBits::SHADER_RESOURCE;

        if (accessMask & AccessBits::SHADER_RESOURCE_STORAGE)
            requiredUsageMask |= TextureUsageBits::SHADER_RESOURCE_STORAGE;

        if (accessMask & AccessBits::COLOR_ATTACHMENT)
            requiredUsageMask |= TextureUsageBits::COLOR_ATTACHMENT;

        if (accessMask & AccessBits::DEPTH_STENCIL_WRITE)
            requiredUsageMask |= TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;

        if (accessMask & AccessBits::DEPTH_STENCIL_READ)
            requiredUsageMask |= TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;

        if (accessMask & AccessBits::ACCELERATION_STRUCTURE_READ)
            return false;

        if (accessMask & AccessBits::ACCELERATION_STRUCTURE_WRITE)
            return false;

        return (uint32_t)(requiredUsageMask & usageMask) == (uint32_t)requiredUsageMask;
    }

    constexpr std::array<TextureUsageBits, (size_t)TextureLayout::MAX_NUM> TEXTURE_USAGE_FOR_TEXTURE_LAYOUT_TABLE = {
        TextureUsageBits::NONE, // GENERAL
        TextureUsageBits::COLOR_ATTACHMENT, // COLOR_ATTACHMENT
        TextureUsageBits::DEPTH_STENCIL_ATTACHMENT, // DEPTH_STENCIL
        TextureUsageBits::DEPTH_STENCIL_ATTACHMENT, // DEPTH_STENCIL_READONLY
        TextureUsageBits::DEPTH_STENCIL_ATTACHMENT, // DEPTH_READONLY
        TextureUsageBits::DEPTH_STENCIL_ATTACHMENT, // STENCIL_READONLY
        TextureUsageBits::SHADER_RESOURCE, // SHADER_RESOURCE
        TextureUsageBits::NONE, // PRESENT
        TextureUsageBits::NONE // UNKNOWN
    };

    constexpr bool IsTextureLayoutSupported(TextureUsageBits usageMask, TextureLayout textureLayout)
    {
        const TextureUsageBits requiredMask = TEXTURE_USAGE_FOR_TEXTURE_LAYOUT_TABLE[(size_t)textureLayout];

        return (uint32_t)(requiredMask & usageMask) == (uint32_t)requiredMask;
    }
}

#define NRI_GET_IMPL(className, object) \
    ((object != nullptr) ? &((className##Val*)object)->GetImpl() : nullptr)