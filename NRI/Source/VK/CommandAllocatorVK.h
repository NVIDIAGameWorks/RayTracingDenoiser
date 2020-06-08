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

    struct CommandAllocatorVK
    {
        CommandAllocatorVK(DeviceVK& device);
        ~CommandAllocatorVK();

        operator VkCommandPool() const;
        DeviceVK& GetDevice() const;

        Result Create(const CommandQueue& commandQueue, uint32_t physicalDeviceMask);
        Result Create(const CommandAllocatorVulkanDesc& commandAllocatorDesc);

        void SetDebugName(const char* name);
        Result CreateCommandBuffer(CommandBuffer*& commandBuffer);
        void Reset();

    private:
        VkCommandPool m_Handle = VK_NULL_HANDLE;
        CommandQueueType m_Type = (CommandQueueType)0;
        DeviceVK& m_Device;
        bool m_OwnsNativeObjects = false;
    };

    inline CommandAllocatorVK::CommandAllocatorVK(DeviceVK& device) :
        m_Device(device)
    {
    }

    inline CommandAllocatorVK::operator VkCommandPool() const
    {
        return m_Handle;
    }

    inline DeviceVK& CommandAllocatorVK::GetDevice() const
    {
        return m_Device;
    }
}
