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

    struct TextureVK
    {
        TextureVK(DeviceVK& device);
        ~TextureVK();

        DeviceVK& GetDevice() const;
        void Create(VkImage handle, VkImageAspectFlags aspectFlags, VkImageType imageType, const VkExtent3D& extent, Format format);
        Result Create(const TextureDesc& textureDesc);
        Result Create(const TextureVulkanDesc& textureDesc);
        VkImage GetHandle(uint32_t physicalDeviceIndex) const;
        VkImageAspectFlags GetImageAspectFlags() const;
        const VkExtent3D& GetExtent() const;
        uint16_t GetSize(uint32_t dim, uint32_t mipOffset = 0) const;
        uint16_t GetMipNum() const;
        uint16_t GetArraySize() const;
        Format GetFormat() const;
        TextureType GetType() const;
        VkSampleCountFlagBits GetSampleCount() const;
        void ClearHandle();

        void SetDebugName(const char* name);
        void GetMemoryInfo(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const;
        void GetTextureVK(uint32_t physicalDeviceIndex, TextureVulkanDesc& textureVulkanDesc) const;

    private:
        std::array<VkImage, PHYSICAL_DEVICE_GROUP_MAX_SIZE> m_Handles = {};
        VkImageAspectFlags m_ImageAspectFlags = (VkImageAspectFlags)0;
        VkExtent3D m_Extent = {};
        uint16_t m_MipNum = 0;
        uint16_t m_ArraySize = 0;
        Format m_Format = nri::Format::UNKNOWN;
        TextureType m_TextureType = (TextureType)0;
        VkSampleCountFlagBits m_SampleCount = (VkSampleCountFlagBits)0;
        DeviceVK& m_Device;
        bool m_OwnsNativeObjects = false;
    };

    inline TextureVK::TextureVK(DeviceVK& device) :
        m_Device(device)
    {
    }

    inline VkImage TextureVK::GetHandle(uint32_t physicalDeviceIndex) const
    {
        return m_Handles[physicalDeviceIndex];
    }

    inline DeviceVK& TextureVK::GetDevice() const
    {
        return m_Device;
    }

    inline VkImageAspectFlags TextureVK::GetImageAspectFlags() const
    {
        return m_ImageAspectFlags;
    }

    inline uint16_t TextureVK::GetSize(uint32_t dimension, uint32_t mipOffset) const
    {
        uint32_t size = (&m_Extent.width)[dimension];
        size = std::max(size >> mipOffset, 1u);
        return (dimension <= (uint32_t)m_TextureType) ? (uint16_t)size : 1u;
    }

    inline const VkExtent3D& TextureVK::GetExtent() const
    {
        return m_Extent;
    }

    inline uint16_t TextureVK::GetMipNum() const
    {
        return m_MipNum;
    }

    inline uint16_t TextureVK::GetArraySize() const
    {
        return m_ArraySize;
    }

    inline Format TextureVK::GetFormat() const
    {
        return m_Format;
    }

    inline TextureType TextureVK::GetType() const
    {
        return m_TextureType;
    }

    inline VkSampleCountFlagBits TextureVK::GetSampleCount() const
    {
        return m_SampleCount;
    }

    inline void TextureVK::ClearHandle()
    {
        for (uint32_t i = 0; i < GetCountOf(m_Handles); i++)
            m_Handles[i] = VK_NULL_HANDLE;
    }
}