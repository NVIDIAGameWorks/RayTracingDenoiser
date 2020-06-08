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

    struct QueryPoolVK
    {
        QueryPoolVK(DeviceVK& device);
        ~QueryPoolVK();

        VkQueryPool GetHandle(uint32_t physicalDeviceIndex) const;
        DeviceVK& GetDevice() const;
        uint32_t GetStride() const;
        VkQueryType GetType() const;

        Result Create(const QueryPoolDesc& queryPoolDesc);
        Result Create(const QueryPoolVulkanDesc& queryPoolDesc);

        void SetDebugName(const char* name);
        uint32_t GetQuerySize() const;

    private:
        VkQueryPool m_Handles[PHYSICAL_DEVICE_GROUP_MAX_SIZE] = {};
        uint32_t m_Stride = 0;
        VkQueryType m_Type = (VkQueryType)0;
        DeviceVK& m_Device;
        bool m_OwnsNativeObjects = false;
    };

    inline QueryPoolVK::QueryPoolVK(DeviceVK& device) :
        m_Device(device)
    {
    }

    inline VkQueryPool QueryPoolVK::GetHandle(uint32_t physicalDeviceIndex) const
    {
        return m_Handles[physicalDeviceIndex];
    }

    inline DeviceVK& QueryPoolVK::GetDevice() const
    {
        return m_Device;
    }

    inline uint32_t QueryPoolVK::GetStride() const
    {
        return m_Stride;
    }

    inline VkQueryType QueryPoolVK::GetType() const
    {
        return m_Type;
    }
}