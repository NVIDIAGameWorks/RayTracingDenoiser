/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "DescriptorVK.h"
#include "TextureVK.h"
#include "BufferVK.h"
#include "DeviceVK.h"

using namespace nri;

DescriptorVK::~DescriptorVK()
{
    const auto& vk = m_Device.GetDispatchTable();

    switch (m_Type)
    {
    case DescriptorTypeVK::NONE:
    case DescriptorTypeVK::ACCELERATION_STRUCTURE:
        break;
    case DescriptorTypeVK::BUFFER_VIEW:
        for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
        {
            if (m_BufferViews[i] != VK_NULL_HANDLE)
                vk.DestroyBufferView(m_Device, m_BufferViews[i], m_Device.GetAllocationCallbacks());
        }
        break;
    case DescriptorTypeVK::IMAGE_VIEW:
        for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
        {
            if (m_ImageViews[i] != VK_NULL_HANDLE)
                vk.DestroyImageView(m_Device, m_ImageViews[i], m_Device.GetAllocationCallbacks());
        }
        break;
    case DescriptorTypeVK::SAMPLER:
        if (m_Sampler != VK_NULL_HANDLE)
            vk.DestroySampler(m_Device, m_Sampler, m_Device.GetAllocationCallbacks());
        break;
    }
}

template<typename T>
void FillTextureDesc(const T& textureViewDesc, DescriptorTextureDesc& descriptorTextureDesc)
{
    const TextureVK& texture = *(const TextureVK*)textureViewDesc.texture;

    descriptorTextureDesc.texture = &texture;
    descriptorTextureDesc.imageAspectFlags = texture.GetImageAspectFlags();
    descriptorTextureDesc.imageMipOffset = textureViewDesc.mipOffset;
    descriptorTextureDesc.imageMipNum = textureViewDesc.mipNum;
    descriptorTextureDesc.imageArrayOffset = textureViewDesc.arrayOffset;
    descriptorTextureDesc.imageArraySize = textureViewDesc.arraySize;
    descriptorTextureDesc.imageLayout = GetImageLayoutForView(textureViewDesc.viewType);
}

template<>
void FillTextureDesc(const Texture3DViewDesc& textureViewDesc, DescriptorTextureDesc& descriptorTextureDesc)
{
    const TextureVK& texture = *(const TextureVK*)textureViewDesc.texture;

    descriptorTextureDesc.texture = &texture;
    descriptorTextureDesc.imageAspectFlags = texture.GetImageAspectFlags();
    descriptorTextureDesc.imageMipOffset = textureViewDesc.mipOffset;
    descriptorTextureDesc.imageMipNum = textureViewDesc.mipNum;
    descriptorTextureDesc.imageArrayOffset = 0;
    descriptorTextureDesc.imageArraySize = 1;
    descriptorTextureDesc.imageLayout = GetImageLayoutForView(textureViewDesc.viewType);
}

template<typename T>
void FillImageSubresourceRange(const T& textureViewDesc, VkImageSubresourceRange& subresourceRange)
{
    const TextureVK& texture = *(const TextureVK*)textureViewDesc.texture;

    subresourceRange = {
        texture.GetImageAspectFlags(),
        textureViewDesc.mipOffset,
        (textureViewDesc.mipNum == REMAINING_MIP_LEVELS) ? VK_REMAINING_MIP_LEVELS : textureViewDesc.mipNum,
        textureViewDesc.arrayOffset,
        (textureViewDesc.arraySize == REMAINING_ARRAY_LAYERS) ? VK_REMAINING_ARRAY_LAYERS : textureViewDesc.arraySize
    };
}

template<>
void FillImageSubresourceRange(const Texture3DViewDesc& textureViewDesc, VkImageSubresourceRange& subresourceRange)
{
    const TextureVK& texture = *(const TextureVK*)textureViewDesc.texture;

    subresourceRange = {
        texture.GetImageAspectFlags(),
        textureViewDesc.mipOffset,
        (textureViewDesc.mipNum == REMAINING_MIP_LEVELS) ? VK_REMAINING_MIP_LEVELS : textureViewDesc.mipNum,
        0,
        1
    };
}

template<typename T>
Result DescriptorVK::CreateTextureView(const T& textureViewDesc)
{
    const TextureVK& texture = *(const TextureVK*)textureViewDesc.texture;

    m_Type = DescriptorTypeVK::IMAGE_VIEW;
    m_Format = ::GetVkImageViewFormat(textureViewDesc.format);
    m_Extent = texture.GetExtent();
    FillTextureDesc(textureViewDesc, m_TextureDesc);

    VkImageSubresourceRange subresource;
    FillImageSubresourceRange(textureViewDesc, subresource);

    VkImageViewCreateInfo info = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        (VkImageViewCreateFlags)0,
        VK_NULL_HANDLE,
        GetImageViewType(textureViewDesc.viewType),
        m_Format,
        VkComponentMapping{},
        subresource
    };

    const auto& vk = m_Device.GetDispatchTable();

    const uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(textureViewDesc.physicalDeviceMask);

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
        {
            m_TextureDesc.handles[i] = texture.GetHandle(i);
            info.image = texture.GetHandle(i);

            const VkResult result = vk.CreateImageView(m_Device, &info, m_Device.GetAllocationCallbacks(), &m_ImageViews[i]);

            RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                "Can't create a texture view: vkCreateImageView returned %d.", (int32_t)result);
        }
    }

    return Result::SUCCESS;
}

Result DescriptorVK::Create(const BufferViewDesc& bufferViewDesc)
{
    const BufferVK& buffer = *(const BufferVK*)bufferViewDesc.buffer;

    m_Type = DescriptorTypeVK::BUFFER_VIEW;
    m_Format = ::GetVkFormat((nri::Format)bufferViewDesc.format);
    m_BufferDesc.offset = bufferViewDesc.offset;
    m_BufferDesc.size = (bufferViewDesc.size == WHOLE_SIZE) ? VK_WHOLE_SIZE : bufferViewDesc.size;

    const uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(bufferViewDesc.physicalDeviceMask);

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
            m_BufferDesc.handles[i] = buffer.GetHandle(i);
    }

    if (bufferViewDesc.format == Format::UNKNOWN)
        return Result::SUCCESS;

    VkBufferViewCreateInfo info = {
        VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
        nullptr,
        (VkBufferViewCreateFlags)0,
        VK_NULL_HANDLE,
        m_Format,
        bufferViewDesc.offset,
        m_BufferDesc.size
    };

    const auto& vk = m_Device.GetDispatchTable();

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
        {
            info.buffer = buffer.GetHandle(i);

            const VkResult result = vk.CreateBufferView(m_Device, &info, m_Device.GetAllocationCallbacks(), &m_BufferViews[i]);

            RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                "Can't create a buffer view: vkCreateBufferView returned %d.", (int32_t)result);
        }
    }

    return Result::SUCCESS;
}

Result DescriptorVK::Create(const Texture1DViewDesc& textureViewDesc)
{
    return CreateTextureView(textureViewDesc);
}

Result DescriptorVK::Create(const Texture2DViewDesc& textureViewDesc)
{
    return CreateTextureView(textureViewDesc);
}

Result DescriptorVK::Create(const Texture3DViewDesc& textureViewDesc)
{
    return CreateTextureView(textureViewDesc);
}

Result DescriptorVK::Create(const SamplerDesc& samplerDesc)
{
    m_Type = DescriptorTypeVK::SAMPLER;

    const VkSamplerCreateInfo samplerInfo = {
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        nullptr,
        (VkSamplerCreateFlags)0,
        GetFilter(samplerDesc.magnification),
        GetFilter(samplerDesc.minification),
        GetSamplerMipmapMode(samplerDesc.minification),
        GetSamplerAddressMode(samplerDesc.addressModes.u),
        GetSamplerAddressMode(samplerDesc.addressModes.v),
        GetSamplerAddressMode(samplerDesc.addressModes.w),
        samplerDesc.mipBias,
        VkBool32(samplerDesc.anisotropy > 1.0f),
        (float)samplerDesc.anisotropy,
        VkBool32(samplerDesc.compareFunc != CompareFunc::NONE),
        GetCompareOp(samplerDesc.compareFunc),
        samplerDesc.mipMin,
        samplerDesc.mipMax,
        VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        VkBool32(samplerDesc.unnormalizedCoordinates)
    };

    const auto& vk = m_Device.GetDispatchTable();
    const VkResult result = vk.CreateSampler(m_Device, &samplerInfo, m_Device.GetAllocationCallbacks(), &m_Sampler);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't create a sampler: vkCreateSampler returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

Result DescriptorVK::Create(const VkAccelerationStructureNV* accelerationStructures, uint32_t physicalDeviceMask)
{
    m_Type = DescriptorTypeVK::ACCELERATION_STRUCTURE;

    physicalDeviceMask = GetPhysicalDeviceGroupMask(physicalDeviceMask);

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
            m_AccelerationStructures[i] = accelerationStructures[i];
    }

    return Result::SUCCESS;
}

void DescriptorVK::SetDebugName(const char* name)
{
    switch (m_Type)
    {
    case DescriptorTypeVK::BUFFER_VIEW:
        m_Device.SetDebugNameToDeviceGroupObject(VK_OBJECT_TYPE_BUFFER_VIEW, (void**)m_BufferViews.data(), name);
        break;

    case DescriptorTypeVK::IMAGE_VIEW:
        m_Device.SetDebugNameToDeviceGroupObject(VK_OBJECT_TYPE_IMAGE_VIEW, (void**)m_ImageViews.data(), name);
        break;

    case DescriptorTypeVK::SAMPLER:
        m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_SAMPLER, m_Sampler, name);
        break;
    }
}

VkBufferView DescriptorVK::GetBufferDescriptorVK(uint32_t physicalDeviceIndex) const
{
    return m_BufferViews[physicalDeviceIndex];
}

VkImageView DescriptorVK::GetTextureDescriptorVK(uint32_t physicalDeviceIndex, VkImageSubresourceRange& subresourceRange) const
{
    GetImageSubresourceRange(subresourceRange);
    return m_ImageViews[physicalDeviceIndex];
}

#include "DescriptorVK.hpp"