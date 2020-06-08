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
    struct DescriptorSetVK;

    struct DescriptorPoolVK
    {
        DescriptorPoolVK(DeviceVK& device);
        ~DescriptorPoolVK();

        operator VkDescriptorPool() const;
        DeviceVK& GetDevice() const;

        Result Create(const DescriptorPoolDesc& descriptorPoolDesc);
        Result Create(void* vkDescriptorPool);

        void SetDebugName(const char* name);

        Result AllocateDescriptorSets(const PipelineLayout& pipelineLayout, uint32_t setIndex, DescriptorSet** const descriptorSets,
            uint32_t numberOfCopies, uint32_t physicalDeviceMask, uint32_t variableDescriptorNum);

        void Reset();

    private:
        VkDescriptorPool m_Handle = VK_NULL_HANDLE;
        Vector<DescriptorSetVK*> m_AllocatedSets;
        uint32_t m_UsedSets = 0;
        DeviceVK& m_Device;
        bool m_OwnsNativeObjects = false;
    };

    inline DescriptorPoolVK::operator VkDescriptorPool() const
    {
        return m_Handle;
    }

    inline DeviceVK& DescriptorPoolVK::GetDevice() const
    {
        return m_Device;
    }
}