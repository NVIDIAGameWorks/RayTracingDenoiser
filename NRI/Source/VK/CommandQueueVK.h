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

    struct CommandQueueVK
    {
        CommandQueueVK(DeviceVK& device);
        CommandQueueVK(DeviceVK& device, VkQueue queue, uint32_t familyIndex, CommandQueueType type);

        operator VkQueue() const;
        DeviceVK& GetDevice() const;
        Result Create(const CommandQueueVulkanDesc& commandQueueDesc);
        uint32_t GetFamilyIndex() const;
        CommandQueueType GetType() const;

        void SetDebugName(const char* name);
        void Submit(const WorkSubmissionDesc& workSubmissionDesc, DeviceSemaphore* deviceSemaphore);
        void Wait(DeviceSemaphore& deviceSemaphore);

        Result ChangeResourceStates(const TransitionBarrierDesc& transitionBarriers);
        Result UploadData(const TextureUploadDesc* textureUploadDescs, uint32_t textureUploadDescNum, 
            const BufferUploadDesc* bufferUploadDescs, uint32_t bufferUploadDescNum);
        Result WaitForIdle();

    private:
        VkQueue m_Handle = VK_NULL_HANDLE;
        uint32_t m_FamilyIndex = (uint32_t)-1;
        CommandQueueType m_Type = (CommandQueueType)-1;
        DeviceVK& m_Device;
    };

    inline CommandQueueVK::CommandQueueVK(DeviceVK& device) :
        m_Device(device)
    {
    }

    inline CommandQueueVK::CommandQueueVK(DeviceVK& device, VkQueue queue, uint32_t familyIndex, CommandQueueType type) :
        m_Handle(queue),
        m_FamilyIndex(familyIndex),
        m_Type(type),
        m_Device(device)
    {
    }

    inline CommandQueueVK::operator VkQueue() const
    {
        return m_Handle;
    }

    inline DeviceVK& CommandQueueVK::GetDevice() const
    {
        return m_Device;
    }

    inline uint32_t CommandQueueVK::GetFamilyIndex() const
    {
        return m_FamilyIndex;
    }

    inline CommandQueueType CommandQueueVK::GetType() const
    {
        return m_Type;
    }
}
