/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "DescriptorVK.h"
#include "TextureVK.h"
#include "FrameBufferVK.h"
#include "DeviceVK.h"

using namespace nri;

FrameBufferVK::~FrameBufferVK()
{
    const auto& vk = m_Device.GetDispatchTable();

    for (uint32_t i = 0; i < m_Handles.size(); i++)
    {
        if (m_Handles[i] != VK_NULL_HANDLE)
            vk.DestroyFramebuffer(m_Device, m_Handles[i], m_Device.GetAllocationCallbacks());
    }

    if (m_RenderPass != VK_NULL_HANDLE)
        vk.DestroyRenderPass(m_Device, m_RenderPass, m_Device.GetAllocationCallbacks());
    if (m_RenderPassWithClear != VK_NULL_HANDLE)
        vk.DestroyRenderPass(m_Device, m_RenderPassWithClear, m_Device.GetAllocationCallbacks());
}

void FrameBufferVK::FillDescriptionsAndFormats(const FrameBufferDesc& frameBufferDesc, VkAttachmentDescription* descriptions, VkFormat* formats)
{
    const bool clearColor = frameBufferDesc.colorClearValues != nullptr;
    const bool clearDepth = frameBufferDesc.depthStencilClearValue != nullptr;

    for (uint32_t i = 0; i < frameBufferDesc.colorAttachmentNum; i++)
    {
        const DescriptorVK& descriptorImpl = *(const DescriptorVK*)frameBufferDesc.colorAttachments[i];
        const TextureVK& textureImpl = descriptorImpl.GetTexture();

        formats[i] = descriptorImpl.GetFormat();

        descriptions[i] = {
            (VkAttachmentDescriptionFlags)0,
            descriptorImpl.GetFormat(),
            textureImpl.GetSampleCount(),
            clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            VK_ATTACHMENT_STORE_OP_STORE,
            clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
    }

    if (frameBufferDesc.depthStencilAttachment != nullptr)
    {
        const DescriptorVK& descriptorImpl = *(const DescriptorVK*)frameBufferDesc.depthStencilAttachment;
        const TextureVK& textureImpl = descriptorImpl.GetTexture();

        descriptions[frameBufferDesc.colorAttachmentNum] = {
            (VkAttachmentDescriptionFlags)0,
            descriptorImpl.GetFormat(),
            textureImpl.GetSampleCount(),
            clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            VK_ATTACHMENT_STORE_OP_STORE,
            clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
    }
}

Result FrameBufferVK::SaveClearColors(const FrameBufferDesc& frameBufferDesc)
{
    if (frameBufferDesc.colorClearValues)
        memcpy(m_ClearValues.data(), frameBufferDesc.colorClearValues, frameBufferDesc.colorAttachmentNum * sizeof(ClearValueDesc));

    if (frameBufferDesc.depthStencilClearValue)
        m_ClearValues[frameBufferDesc.colorAttachmentNum] = *frameBufferDesc.depthStencilClearValue;

    return Result::SUCCESS;
}

Result FrameBufferVK::CreateRenderPass(const FrameBufferDesc& frameBufferDesc)
{
    VkAttachmentDescription* descriptions = STACK_ALLOC(VkAttachmentDescription, m_AttachmentNum);
    VkFormat* formats = STACK_ALLOC(VkFormat, m_AttachmentNum);
    FillDescriptionsAndFormats(frameBufferDesc, descriptions, formats);

    VkAttachmentReference* references = STACK_ALLOC(VkAttachmentReference, frameBufferDesc.colorAttachmentNum);

    for (uint32_t i = 0; i < frameBufferDesc.colorAttachmentNum; i++)
        references[i] = { i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    const VkAttachmentReference depth = {
        frameBufferDesc.colorAttachmentNum,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    const VkSubpassDescription subpass = {
        (VkSubpassDescriptionFlags)0,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0,
        nullptr,
        frameBufferDesc.colorAttachmentNum,
        references,
        nullptr,
        frameBufferDesc.depthStencilAttachment ? &depth : nullptr,
        0,
        nullptr
    };

    const VkRenderPassCreateInfo renderPassInfo = {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        (VkRenderPassCreateFlags)0,
        m_AttachmentNum,
        descriptions,
        1,
        &subpass,
        0,
        nullptr
    };

    const auto& vk = m_Device.GetDispatchTable();
    VkResult result = vk.CreateRenderPass(m_Device, &renderPassInfo, m_Device.GetAllocationCallbacks(), &m_RenderPassWithClear);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't create a render pass: vkCreateRenderPass returned %d.", (int32_t)result);

    for (uint32_t i = 0; i < m_AttachmentNum; i++)
    {
        descriptions[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        descriptions[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    }

    result = vk.CreateRenderPass(m_Device, &renderPassInfo, m_Device.GetAllocationCallbacks(), &m_RenderPass);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't create a render pass: vkCreateRenderPass returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

Result FrameBufferVK::Create(const FrameBufferDesc& frameBufferDesc)
{
    VkExtent3D extent = {};
    if (frameBufferDesc.colorAttachmentNum > 0)
        extent = ((DescriptorVK*)frameBufferDesc.colorAttachments[0])->GetExtent();
    else if (frameBufferDesc.depthStencilAttachment != nullptr)
        extent = ((DescriptorVK*)frameBufferDesc.depthStencilAttachment)->GetExtent();

    m_RenderArea = { {}, { extent.width, extent.height } };
    m_AttachmentNum = frameBufferDesc.colorAttachmentNum + (frameBufferDesc.depthStencilAttachment ? 1 : 0);

    Result result = SaveClearColors(frameBufferDesc);
    if (result != Result::SUCCESS)
        return result;

    result = CreateRenderPass(frameBufferDesc);
    if (result != Result::SUCCESS)
        return result;

    std::array<VkImageView, ATTACHMENT_MAX_NUM> imageViews;

    VkFramebufferCreateInfo framebufferInfo = {
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        nullptr,
        (VkFramebufferCreateFlags)0,
        m_RenderPass,
        m_AttachmentNum,
        imageViews.data(),
        m_RenderArea.extent.width,
        m_RenderArea.extent.height,
        1
    };

    const auto& vk = m_Device.GetDispatchTable();

    const uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(frameBufferDesc.physicalDeviceMask);

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
        {
            for (uint32_t j = 0; j < frameBufferDesc.colorAttachmentNum; j++)
            {
                const DescriptorVK& descriptorImpl = *(DescriptorVK*)frameBufferDesc.colorAttachments[j];
                imageViews[j] = descriptorImpl.GetImageView(i);
            }

            if (frameBufferDesc.depthStencilAttachment != nullptr)
            {
                const DescriptorVK& descriptorImpl = *(DescriptorVK*)frameBufferDesc.depthStencilAttachment;
                imageViews[frameBufferDesc.colorAttachmentNum] = descriptorImpl.GetImageView(i);
            }

            const VkResult res = vk.CreateFramebuffer(m_Device, &framebufferInfo, m_Device.GetAllocationCallbacks(), &m_Handles[i]);
            RETURN_ON_FAILURE(m_Device.GetLog(), res == VK_SUCCESS, GetReturnCode(res),
                "Can't create a frame buffer: vkCreateFramebuffer returned %d.", (int32_t)res);
        }
    }

    return Result::SUCCESS;
}

inline void FrameBufferVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToDeviceGroupObject(VK_OBJECT_TYPE_FRAMEBUFFER, (void**)m_Handles.data(), name);
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_RENDER_PASS, m_RenderPass, name);
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_RENDER_PASS, m_RenderPassWithClear, name);
}

#include "FrameBufferVK.hpp"