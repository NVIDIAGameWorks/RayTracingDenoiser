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

    struct FrameBufferVK
    {
        FrameBufferVK(DeviceVK& device);
        ~FrameBufferVK();

        DeviceVK& GetDevice() const;

        VkFramebuffer GetHandle(uint32_t physicalDeviceIndex) const;
        const VkRect2D& GetRenderArea() const;
        VkRenderPass GetRenderPass(RenderPassBeginFlag renderPassBeginFlag) const;
        uint32_t GetAttachmentNum() const;
        void GetClearValues(VkClearValue* values) const;

        Result Create(const FrameBufferDesc& frameBufferDesc);

        void SetDebugName(const char* name);

    private:
        void FillDescriptionsAndFormats(const FrameBufferDesc& frameBufferDesc, VkAttachmentDescription* descriptions, VkFormat* formats);
        Result SaveClearColors(const FrameBufferDesc& frameBufferDesc);
        Result CreateRenderPass(const FrameBufferDesc& frameBufferDesc);

        static constexpr uint32_t ATTACHMENT_MAX_NUM = 8;

        std::array<VkFramebuffer, PHYSICAL_DEVICE_GROUP_MAX_SIZE> m_Handles = {};
        VkRenderPass m_RenderPassWithClear = VK_NULL_HANDLE;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        std::array<ClearValueDesc, ATTACHMENT_MAX_NUM> m_ClearValues = {};
        VkRect2D m_RenderArea = {};
        uint32_t m_AttachmentNum = 0;
        DeviceVK& m_Device;
    };

    inline FrameBufferVK::FrameBufferVK(DeviceVK& device) :
        m_Device(device)
    {
    }

    inline DeviceVK& FrameBufferVK::GetDevice() const
    {
        return m_Device;
    }

    inline VkFramebuffer FrameBufferVK::GetHandle(uint32_t physicalDeviceIndex) const
    {
        return m_Handles[physicalDeviceIndex];
    }

    inline VkRenderPass FrameBufferVK::GetRenderPass(RenderPassBeginFlag renderPassBeginFlag) const
    {
        return (renderPassBeginFlag == RenderPassBeginFlag::SKIP_FRAME_BUFFER_CLEAR) ? m_RenderPass : m_RenderPassWithClear;
    }

    inline const VkRect2D& FrameBufferVK::GetRenderArea() const
    {
        return m_RenderArea;
    }

    inline uint32_t FrameBufferVK::GetAttachmentNum() const
    {
        return m_AttachmentNum;
    }

    inline void FrameBufferVK::GetClearValues(VkClearValue* values) const
    {
        memcpy(values, m_ClearValues.data(), m_AttachmentNum * sizeof(VkClearValue));
    }
}