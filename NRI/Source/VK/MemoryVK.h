/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nri
{
    struct DeviceVK;
    struct BufferVK;
    struct TextureVK;

    struct MemoryVK
    {
        MemoryVK(DeviceVK& device);
        ~MemoryVK();

        VkDeviceMemory GetHandle(uint32_t physicalDeviceIndex) const;
        DeviceVK& GetDevice() const;
        MemoryType GetType() const;
        uint8_t* GetMappedMemory(uint32_t physicalDeviceIndex) const;

        Result Create(uint32_t physicalDeviceMask, const MemoryType memoryType, uint64_t size);
        Result Create(const MemoryVulkanDesc& memoryDesc);
        Result CreateDedicated(BufferVK& buffer, uint32_t physicalDeviceMask);
        Result CreateDedicated(TextureVK& texture, uint32_t physicalDeviceMask);

        void SetDebugName(const char* name);

    private:
        std::array<VkDeviceMemory, PHYSICAL_DEVICE_GROUP_MAX_SIZE> m_Handles = {};
        std::array<uint8_t*, PHYSICAL_DEVICE_GROUP_MAX_SIZE> m_MappedMemory = {};
        MemoryType m_Type = std::numeric_limits<MemoryType>::max();
        DeviceVK& m_Device;
        bool m_OwnsNativeObjects = false;
    };

    inline MemoryVK::MemoryVK(DeviceVK& device) :
        m_Device(device)
    {
    }

    inline VkDeviceMemory MemoryVK::GetHandle(uint32_t physicalDeviceIndex) const
    {
        return m_Handles[physicalDeviceIndex];
    }

    inline DeviceVK& MemoryVK::GetDevice() const
    {
        return m_Device;
    }

    inline MemoryType MemoryVK::GetType() const
    {
        return m_Type;
    }

    inline uint8_t* MemoryVK::GetMappedMemory(uint32_t physicalDeviceIndex) const
    {
        return m_MappedMemory[physicalDeviceIndex];
    }
}