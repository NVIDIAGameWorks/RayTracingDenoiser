/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

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

    struct QueueSemaphoreVK
    {
        QueueSemaphoreVK(DeviceVK& device);
        ~QueueSemaphoreVK();

        operator VkSemaphore() const;
        DeviceVK& GetDevice() const;
        Result Create();
        Result Create(void* vkSemaphore);

        void SetDebugName(const char* name);

    private:
        VkSemaphore m_Handle = VK_NULL_HANDLE;
        DeviceVK& m_Device;
        bool m_OwnsNativeObjects = false;
    };

    inline QueueSemaphoreVK::QueueSemaphoreVK(DeviceVK& device) :
        m_Device(device)
    {
    }

    inline QueueSemaphoreVK::operator VkSemaphore() const
    {
        return m_Handle;
    }

    inline DeviceVK& QueueSemaphoreVK::GetDevice() const
    {
        return m_Device;
    }
}