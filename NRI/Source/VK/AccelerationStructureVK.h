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

    struct AccelerationStructureVK
    {
        AccelerationStructureVK(DeviceVK& device);
        ~AccelerationStructureVK();

        VkAccelerationStructureNV GetHandle(uint32_t physicalDeviceIndex) const;
        DeviceVK& GetDevice() const;

        Result Create(const AccelerationStructureDesc& accelerationStructureDesc);
        Result RetrieveNativeHandle();
        uint32_t GetCreationFlags() const; 

        void SetDebugName(const char* name);
        void GetMemoryInfo(MemoryDesc& memoryDesc) const;
        uint64_t GetUpdateScratchBufferSize() const;
        uint64_t GetBuildScratchBufferSize() const;
        uint64_t GetNativeHandle(uint32_t physicalDeviceIndex) const;
        Result CreateDescriptor(uint32_t physicalDeviceMask, Descriptor*& descriptor) const;

    private:
        void GetMemoryInfo(VkAccelerationStructureMemoryRequirementsTypeNV type, VkMemoryRequirements2& requirements) const;

        VkAccelerationStructureNV m_Handles[PHYSICAL_DEVICE_GROUP_MAX_SIZE] = {};
        uint64_t m_NativeHandles[PHYSICAL_DEVICE_GROUP_MAX_SIZE] = {};
        DeviceVK& m_Device;
        uint32_t m_CreationFlags = 0;
        bool m_OwnsNativeObjects = false;
    };

    inline AccelerationStructureVK::AccelerationStructureVK(DeviceVK& device) :
        m_Device(device)
    {
    }

    inline VkAccelerationStructureNV AccelerationStructureVK::GetHandle(uint32_t physicalDeviceIndex) const
    {
        return m_Handles[physicalDeviceIndex];
    }

    inline DeviceVK& AccelerationStructureVK::GetDevice() const
    {
        return m_Device;
    }

    inline uint32_t AccelerationStructureVK::GetCreationFlags() const
    {
        return m_CreationFlags;
    }

    inline uint64_t AccelerationStructureVK::GetNativeHandle(uint32_t physicalDeviceIndex) const
    {
        return m_NativeHandles[physicalDeviceIndex];
    }
}