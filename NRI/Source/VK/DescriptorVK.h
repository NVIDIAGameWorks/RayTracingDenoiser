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
    struct TextureVK;

    enum class DescriptorTypeVK
    {
        NONE = 0,
        BUFFER_VIEW,
        IMAGE_VIEW,
        SAMPLER,
        ACCELERATION_STRUCTURE
    };

    struct DescriptorBufferDesc
    {
        std::array<VkBuffer, PHYSICAL_DEVICE_GROUP_MAX_SIZE> handles;
        uint64_t offset;
        uint64_t size;
    };

    struct DescriptorTextureDesc
    {
        std::array<VkImage, PHYSICAL_DEVICE_GROUP_MAX_SIZE> handles;
        const TextureVK* texture;
        VkImageLayout imageLayout;
        uint32_t imageMipOffset;
        uint32_t imageMipNum;
        uint32_t imageArrayOffset;
        uint32_t imageArraySize;
        VkImageAspectFlags imageAspectFlags;
    };

    struct DescriptorVK
    {
        DescriptorVK(DeviceVK& device);
        ~DescriptorVK();

        DeviceVK& GetDevice() const;

        VkBufferView GetBufferView(uint32_t physicalDeviceIndex) const;
        VkImageView GetImageView(uint32_t physicalDeviceIndex) const;
        const VkSampler& GetSampler() const;
        VkAccelerationStructureNV GetAccelerationStructure(uint32_t physicalDeviceIndex) const;
        VkBuffer GetBuffer(uint32_t physicalDeviceIndex) const;
        VkImage GetImage(uint32_t physicalDeviceIndex) const;
        void GetBufferInfo(uint32_t physicalDeviceIndex, VkDescriptorBufferInfo& info) const;
        const TextureVK& GetTexture() const;

        DescriptorTypeVK GetType() const;
        VkExtent3D GetExtent() const;
        VkFormat GetFormat() const;
        void GetImageSubresourceRange(VkImageSubresourceRange& range) const;
        VkImageLayout GetImageLayout() const;

        template<typename T>
        Result CreateTextureView(const T& textureViewDesc);

        Result Create(const BufferViewDesc& bufferViewDesc);
        Result Create(const Texture1DViewDesc& textureViewDesc);
        Result Create(const Texture2DViewDesc& textureViewDesc);
        Result Create(const Texture3DViewDesc& textureViewDesc);
        Result Create(const SamplerDesc& samplerDesc);
        Result Create(const VkAccelerationStructureNV* accelerationStructures, uint32_t physicalDeviceMask);

        void SetDebugName(const char* name);
        VkBufferView GetBufferDescriptorVK(uint32_t physicalDeviceIndex) const;
        VkImageView GetTextureDescriptorVK(uint32_t physicalDeviceIndex, VkImageSubresourceRange& subresourceRange) const;

    private:
        union
        {
            std::array<VkBufferView, PHYSICAL_DEVICE_GROUP_MAX_SIZE> m_BufferViews;
            std::array<VkImageView, PHYSICAL_DEVICE_GROUP_MAX_SIZE> m_ImageViews;
            std::array<VkAccelerationStructureNV, PHYSICAL_DEVICE_GROUP_MAX_SIZE> m_AccelerationStructures;
            VkSampler m_Sampler;
        };
        union
        {
            DescriptorBufferDesc m_BufferDesc;
            DescriptorTextureDesc m_TextureDesc;
        };
        DescriptorTypeVK m_Type = DescriptorTypeVK::NONE;
        VkFormat m_Format = VK_FORMAT_UNDEFINED;
        VkExtent3D m_Extent = {};
        DeviceVK& m_Device;
    };

    inline DescriptorVK::DescriptorVK(DeviceVK& device) :
        m_Device(device)
    {
        m_BufferViews.fill(VK_NULL_HANDLE);
        m_TextureDesc = {};
    }

    inline VkBufferView DescriptorVK::GetBufferView(uint32_t physicalDeviceIndex) const
    {
        return m_BufferViews[physicalDeviceIndex];
    }

    inline VkImageView DescriptorVK::GetImageView(uint32_t physicalDeviceIndex) const
    {
        return m_ImageViews[physicalDeviceIndex];
    }

    inline const VkSampler& DescriptorVK::GetSampler() const
    {
        return m_Sampler;
    }

    inline VkAccelerationStructureNV DescriptorVK::GetAccelerationStructure(uint32_t physicalDeviceIndex) const
    {
        return m_AccelerationStructures[physicalDeviceIndex];
    }

    inline VkBuffer DescriptorVK::GetBuffer(uint32_t physicalDeviceIndex) const
    {
        return m_BufferDesc.handles[physicalDeviceIndex];
    }

    inline VkImage DescriptorVK::GetImage(uint32_t physicalDeviceIndex) const
    {
        return m_TextureDesc.handles[physicalDeviceIndex];
    }

    inline void DescriptorVK::GetBufferInfo(uint32_t physicalDeviceIndex, VkDescriptorBufferInfo& info) const
    {
        info.buffer = m_BufferDesc.handles[physicalDeviceIndex];
        info.offset = m_BufferDesc.offset;
        info.range = m_BufferDesc.size;
    }

    inline const TextureVK& DescriptorVK::GetTexture() const
    {
        return *m_TextureDesc.texture;
    }

    inline DeviceVK& DescriptorVK::GetDevice() const
    {
        return m_Device;
    }

    inline DescriptorTypeVK DescriptorVK::GetType() const
    {
        return m_Type;
    }

    inline VkExtent3D DescriptorVK::GetExtent() const
    {
        return m_Extent;
    }

    inline VkFormat DescriptorVK::GetFormat() const
    {
        return m_Format;
    }

    inline void DescriptorVK::GetImageSubresourceRange(VkImageSubresourceRange& range) const
    {
        range.aspectMask = m_TextureDesc.imageAspectFlags;
        range.baseMipLevel = m_TextureDesc.imageMipOffset;
        range.levelCount = m_TextureDesc.imageMipNum;
        range.baseArrayLayer = m_TextureDesc.imageArrayOffset;
        range.layerCount = m_TextureDesc.imageArraySize;
    }

    inline VkImageLayout DescriptorVK::GetImageLayout() const
    {
        return m_TextureDesc.imageLayout;
    }
}