/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "BufferVK.h"
#include "MemoryVK.h"
#include "CommandQueueVK.h"
#include "DeviceVK.h"

using namespace nri;

BufferVK::~BufferVK()
{
    const auto& vk = m_Device.GetDispatchTable();

    if (!m_OwnsNativeObjects)
        return;

    if (m_Memory != VK_NULL_HANDLE)
    {
        vk.DestroyBuffer(m_Device, m_Handles[0], m_Device.GetAllocationCallbacks());
    }
    else
    {
        for (uint32_t i = 0; i < GetCountOf(m_Handles); i++)
        {
            if (m_Handles[i] != VK_NULL_HANDLE)
                vk.DestroyBuffer(m_Device, m_Handles[i], m_Device.GetAllocationCallbacks());
        }
    }
}

Result BufferVK::Create(const BufferDesc& bufferDesc)
{
    m_OwnsNativeObjects = true;
    m_Size = bufferDesc.size;

    const VkSharingMode sharingMode =
        m_Device.IsConcurrentSharingModeEnabledForBuffers() ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;

    const Vector<uint32_t>& queueIndices = m_Device.GetConcurrentSharingModeQueueIndices();

    VkBufferCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = bufferDesc.size;
    info.usage = GetBufferUsageFlags(bufferDesc.usageMask, bufferDesc.structureStride);
    info.sharingMode = sharingMode;
    info.queueFamilyIndexCount = (uint32_t)queueIndices.size();
    info.pQueueFamilyIndices = queueIndices.data();

    const auto& vk = m_Device.GetDispatchTable();

    const uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(bufferDesc.physicalDeviceMask);

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
        {
            const VkResult result = vk.CreateBuffer(m_Device, &info, m_Device.GetAllocationCallbacks(), &m_Handles[i]);

            RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                "Can't create a buffer: vkCreateBuffer returned %d.", (int32_t)result);
        }
    }

    return Result::SUCCESS;
}

Result BufferVK::Create(const BufferVulkanDesc& bufferDesc)
{
    m_OwnsNativeObjects = false;
    m_Memory = (MemoryVK*)bufferDesc.memory;
    m_MappedMemoryOffset = bufferDesc.memoryOffset;
    m_Size = bufferDesc.bufferSize;

    const VkBuffer handle = (VkBuffer)bufferDesc.vkBuffer;

    uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(bufferDesc.physicalDeviceMask);

    if (m_Memory != nullptr)
        physicalDeviceMask = 0x1;

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
            m_Handles[i] = handle;
    }

    return Result::SUCCESS;
}

void BufferVK::SetHostMemory(MemoryVK& memory, uint64_t memoryOffset)
{
    m_Memory = &memory;
    m_MappedMemoryOffset = memoryOffset;

    const auto& vk = m_Device.GetDispatchTable();

    // No need to keep more than one instance of host buffer
    for (uint32_t i = 1; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if (m_Handles[i] != VK_NULL_HANDLE)
            vk.DestroyBuffer(m_Device, m_Handles[i], m_Device.GetAllocationCallbacks());
        m_Handles[i] = m_Handles[0];
    }
}

inline void BufferVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToDeviceGroupObject(VK_OBJECT_TYPE_BUFFER, (void**)m_Handles.data(), name);
}

void BufferVK::GetMemoryInfo(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const
{
    VkBuffer handle = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize() && handle == VK_NULL_HANDLE; i++)
        handle = m_Handles[i];

    const auto& vk = m_Device.GetDispatchTable();

    VkMemoryDedicatedRequirements dedicatedRequirements = {
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
        nullptr
    };

    VkMemoryRequirements2 requirements = {
        VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        &dedicatedRequirements
    };

    VkBufferMemoryRequirementsInfo2 info = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
        nullptr,
        handle
    };

    vk.GetBufferMemoryRequirements2(m_Device, &info, &requirements);

    memoryDesc.mustBeDedicated = dedicatedRequirements.requiresDedicatedAllocation;
    memoryDesc.alignment = (uint32_t)requirements.memoryRequirements.alignment;
    memoryDesc.size = requirements.memoryRequirements.size;

    MemoryTypeUnpack unpack = {};
    const bool found = m_Device.GetMemoryType(memoryLocation, requirements.memoryRequirements.memoryTypeBits, unpack.info);
    CHECK(m_Device.GetLog(), found, "Can't find suitable memory type: %d", requirements.memoryRequirements.memoryTypeBits);

    unpack.info.isDedicated = dedicatedRequirements.requiresDedicatedAllocation;

    memoryDesc.type = unpack.type;
}

inline void* BufferVK::Map(uint64_t offset, uint64_t size)
{
    CHECK(m_Device.GetLog(), m_Memory != nullptr, "The buffer does not support memory mapping.");

    m_MappedRangeOffset = offset;
    m_MappedRangeSize = size;

    if (size == WHOLE_SIZE)
        size = m_Size;

    return m_Memory->GetMappedMemory(0) + m_MappedMemoryOffset + offset;
}

inline void BufferVK::Unmap()
{
    // TODO: flush the range if the memory is not host coherent
    // if (m_Memory->IsHostCoherent())
    //     m_Memory->FlushMemoryRange(m_MappedMemoryOffset + m_MappedRangeOffset, m_MappedRangeSize);
}

#include "BufferVK.hpp"