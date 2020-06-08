/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "MemoryVK.h"
#include "BufferVK.h"
#include "TextureVK.h"
#include "DeviceVK.h"

using namespace nri;

MemoryVK::~MemoryVK()
{
    const auto& vk = m_Device.GetDispatchTable();

    if (!m_OwnsNativeObjects)
        return;

    for (uint32_t i = 0; i < GetCountOf(m_Handles); i++)
    {
        if (m_Handles[i] != VK_NULL_HANDLE)
            vk.FreeMemory(m_Device, m_Handles[i], m_Device.GetAllocationCallbacks());
    }
}

Result MemoryVK::Create(uint32_t physicalDeviceMask, const MemoryType memoryType, uint64_t size)
{
    m_OwnsNativeObjects = true;
    m_Type = memoryType;

    const MemoryTypeUnpack unpack = { memoryType };
    const MemoryTypeInfo& memoryTypeInfo = unpack.info;

    const MemoryLocation memoryLocation = (MemoryLocation)memoryTypeInfo.location;

    // Dedicated allocation occurs on memory binding
    if (memoryTypeInfo.isDedicated == 1)
        return Result::SUCCESS;

    VkMemoryAllocateFlagsInfo flagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT;

    VkMemoryAllocateInfo memoryInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memoryInfo.pNext = &flagsInfo;
    memoryInfo.allocationSize = size;
    memoryInfo.memoryTypeIndex = memoryTypeInfo.memoryTypeIndex;

    physicalDeviceMask = GetPhysicalDeviceGroupMask(physicalDeviceMask);

    // No need to allocate more than one instance of host memory
    if (IsHostVisibleMemory(memoryLocation))
    {
        physicalDeviceMask = 0x1;
        memoryInfo.pNext = nullptr;
    }

    const auto& vk = m_Device.GetDispatchTable();

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
        {
            flagsInfo.deviceMask = 1 << i;

            VkResult result = vk.AllocateMemory(m_Device, &memoryInfo, m_Device.GetAllocationCallbacks(), &m_Handles[i]);

            RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                "Can't allocate a memory: vkAllocateMemory returned %d.", (int32_t)result);

            if (IsHostVisibleMemory(memoryLocation))
            {
                result = vk.MapMemory(m_Device, m_Handles[i], 0, size, 0, (void**)&m_MappedMemory[i]);

                RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                    "Can't map the allocated memory: vkMapMemory returned %d.", (int32_t)result);
            }
        }
    }

    return Result::SUCCESS;
}

Result MemoryVK::Create(const MemoryVulkanDesc& memoryDesc)
{
    m_OwnsNativeObjects = false;

    MemoryTypeUnpack unpack = {};
    const bool found = m_Device.GetMemoryType(memoryDesc.memoryTypeIndex, unpack.info);
    CHECK(m_Device.GetLog(), found, "Can't find memory type: %u", memoryDesc.memoryTypeIndex);

    const VkDeviceMemory handle = (VkDeviceMemory)memoryDesc.vkDeviceMemory;
    const uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(memoryDesc.physicalDeviceMask);

    const MemoryTypeInfo& memoryTypeInfo = unpack.info;

    const auto& vk = m_Device.GetDispatchTable();

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
        {
            m_Handles[i] = handle;

            if (IsHostVisibleMemory((MemoryLocation)memoryTypeInfo.location))
            {
                const VkResult result = vk.MapMemory(m_Device, m_Handles[i], 0, memoryDesc.size, 0, (void**)&m_MappedMemory[i]);

                RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                    "Can't map the memory: vkMapMemory returned %d.", (int32_t)result);
            }
        }
    }

    return Result::SUCCESS;
}

Result MemoryVK::CreateDedicated(BufferVK& buffer, uint32_t physicalDeviceMask)
{
    m_OwnsNativeObjects = true;

    RETURN_ON_FAILURE(m_Device.GetLog(), m_Type != std::numeric_limits<MemoryType>::max(), Result::FAILURE,
        "Can't allocate a dedicated memory: memory type is invalid.");

    const MemoryTypeUnpack unpack = { m_Type };
    const MemoryTypeInfo& memoryTypeInfo = unpack.info;

    const MemoryLocation memoryLocation = (MemoryLocation)memoryTypeInfo.location;

    RETURN_ON_FAILURE(m_Device.GetLog(), memoryTypeInfo.isDedicated == 1, Result::FAILURE,
        "Can't allocate a dedicated memory: memory type is not dedicated.");

    MemoryDesc memoryDesc = {};
    buffer.GetMemoryInfo(memoryLocation, memoryDesc);

    VkMemoryAllocateFlagsInfo flagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT;

    VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicatedAllocateInfo.pNext = &flagsInfo;

    VkMemoryAllocateInfo memoryInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memoryInfo.pNext = &dedicatedAllocateInfo;
    memoryInfo.allocationSize = memoryDesc.size;
    memoryInfo.memoryTypeIndex = memoryTypeInfo.memoryTypeIndex;

    // No need to allocate two instances of host memory
    if (IsHostVisibleMemory(memoryLocation))
    {
        physicalDeviceMask = 0x1;
        dedicatedAllocateInfo.pNext = nullptr;
    }

    const auto& vk = m_Device.GetDispatchTable();

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
        {
            dedicatedAllocateInfo.buffer = buffer.GetHandle(i);
            flagsInfo.deviceMask = 1 << i;

            VkResult result = vk.AllocateMemory(m_Device, &memoryInfo, m_Device.GetAllocationCallbacks(), &m_Handles[i]);

            RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                "Can't allocate a dedicated memory: vkAllocateMemory returned %d.", (int32_t)result);

            if (IsHostVisibleMemory(memoryLocation))
            {
                result = vk.MapMemory(m_Device, m_Handles[i], 0, memoryDesc.size, 0, (void**)&m_MappedMemory[i]);

                RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                    "Can't map the allocated memory: vkMapMemory returned %d.", (int32_t)result);
            }
        }
    }

    return Result::SUCCESS;
}

Result MemoryVK::CreateDedicated(TextureVK& texture, uint32_t physicalDeviceMask)
{
    m_OwnsNativeObjects = true;

    RETURN_ON_FAILURE(m_Device.GetLog(), m_Type != std::numeric_limits<MemoryType>::max(), Result::FAILURE,
        "Can't allocate a dedicated memory: invalid memory type.");

    const MemoryTypeUnpack unpack = { m_Type };
    const MemoryTypeInfo& memoryTypeInfo = unpack.info;

    const MemoryLocation memoryLocation = (MemoryLocation)memoryTypeInfo.location;

    RETURN_ON_FAILURE(m_Device.GetLog(), memoryTypeInfo.isDedicated == 1, Result::FAILURE,
        "Can't allocate a dedicated memory: the memory type is not dedicated.");

    MemoryDesc memoryDesc = {};
    texture.GetMemoryInfo((MemoryLocation)memoryTypeInfo.location, memoryDesc);

    VkMemoryAllocateFlagsInfo flagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT;

    VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicatedAllocateInfo.pNext = &flagsInfo;

    VkMemoryAllocateInfo memoryInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memoryInfo.pNext = &dedicatedAllocateInfo;
    memoryInfo.allocationSize = memoryDesc.size;
    memoryInfo.memoryTypeIndex = memoryTypeInfo.memoryTypeIndex;

    // No need to allocate two instances of host memory
    if (IsHostVisibleMemory(memoryLocation))
    {
        physicalDeviceMask = 0x1;
        dedicatedAllocateInfo.pNext = nullptr;
    }

    const auto& vk = m_Device.GetDispatchTable();

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
        {
            dedicatedAllocateInfo.image = texture.GetHandle(i);
            flagsInfo.deviceMask = 1 << i;

            VkResult result = vk.AllocateMemory(m_Device, &memoryInfo, m_Device.GetAllocationCallbacks(), &m_Handles[i]);

            RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                "Can't allocate a dedicated memory: vkAllocateMemory returned %d.", (int32_t)result);

            if (IsHostVisibleMemory(memoryLocation))
            {
                result = vk.MapMemory(m_Device, m_Handles[i], 0, memoryDesc.size, 0, (void**)&m_MappedMemory[i]);

                RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                    "Can't map the allocated memory: vkMapMemory returned %d.", (int32_t)result);
            }
        }
    }

    return Result::SUCCESS;
}

void MemoryVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToDeviceGroupObject(VK_OBJECT_TYPE_DEVICE_MEMORY, (void**)m_Handles, name);
}

#include "MemoryVK.hpp"