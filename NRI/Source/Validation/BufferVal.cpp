/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "DeviceBase.h"
#include "DeviceVal.h"
#include "SharedVal.h"
#include "MemoryVal.h"
#include "BufferVal.h"

using namespace nri;

BufferVal::BufferVal(DeviceVal& device, Buffer& buffer, const BufferDesc& bufferDesc) :
    DeviceObjectVal(device, buffer),
    m_BufferDesc(bufferDesc)
{
}

BufferVal::BufferVal(DeviceVal& device, Buffer& buffer, const BufferD3D11Desc& bufferD3D11Desc) :
    DeviceObjectVal(device, buffer)
{
    GetBufferDescD3D11(bufferD3D11Desc, m_BufferDesc);
}

BufferVal::BufferVal(DeviceVal& device, Buffer& buffer, const BufferD3D12Desc& bufferD3D12Desc) :
    DeviceObjectVal(device, buffer)
{
    GetBufferDescD3D12(bufferD3D12Desc, m_BufferDesc);
}

BufferVal::BufferVal(DeviceVal& device, Buffer& buffer, const BufferVulkanDesc& bufferVulkanDesc) :
    DeviceObjectVal(device, buffer)
{
    m_BufferDesc = {};
    m_BufferDesc.physicalDeviceMask = bufferVulkanDesc.physicalDeviceMask;
    m_BufferDesc.size = bufferVulkanDesc.bufferSize;

    static_assert(sizeof(BufferUsageBits) == sizeof(uint16_t), "unexpected BufferUsageBits sizeof");
    m_BufferDesc.usageMask = (BufferUsageBits)0xffff;
}

BufferVal::~BufferVal()
{
    if (m_Memory != nullptr)
        m_Memory->UnbindBuffer(*this);
}

void BufferVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetBufferDebugName(m_ImplObject, name);
}

void BufferVal::GetMemoryInfo(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const
{
    m_CoreAPI.GetBufferMemoryInfo(m_ImplObject, memoryLocation, memoryDesc);
    m_Device.RegisterMemoryType(memoryDesc.type, memoryLocation);
}

void* BufferVal::Map(uint64_t offset, uint64_t size)
{
    RETURN_ON_FAILURE(m_Device.GetLog(), m_Memory != nullptr, nullptr,
        "Can't map Buffer: Buffer is not bound to memory.");

    RETURN_ON_FAILURE(m_Device.GetLog(), !m_IsMapped, nullptr,
        "Can't map Buffer: the buffer is already mapped.");

    m_IsMapped = true;

    return m_CoreAPI.MapBuffer(m_ImplObject, offset, size);
}

void BufferVal::Unmap()
{
    RETURN_ON_FAILURE(m_Device.GetLog(), m_IsMapped, ReturnVoid(),
        "Can't unmap Buffer: the buffer is not mapped.");

    m_IsMapped = false;

    m_CoreAPI.UnmapBuffer(m_ImplObject);
}

ID3D11Resource* BufferVal::GetBufferD3D11() const
{
    return m_Device.GetWrapperD3D11Interface().GetBufferD3D11(m_ImplObject);
}

ID3D12Resource* BufferVal::GetBufferD3D12() const
{
    return m_Device.GetWrapperD3D12Interface().GetBufferD3D12(m_ImplObject);
}

#include "BufferVal.hpp"
