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
    enum class ResourceType
    {
        NONE,
        BUFFER,
        TEXTURE,
        SAMPLER,
        ACCELERATION_STRUCTURE
    };

    enum class ResourceViewType
    {
        NONE,
        COLOR_ATTACHMENT,
        DEPTH_STENCIL_ATTACHMENT,
        SHADER_RESOURCE,
        SHADER_RESOURCE_STORAGE,
        CONSTANT_BUFFER_VIEW
    };

    struct DescriptorVal : public DeviceObjectVal<Descriptor>
    {
        DescriptorVal(DeviceVal& device, Descriptor& descriptor, ResourceType resourceType);
        DescriptorVal(DeviceVal& device, Descriptor& descriptor, const BufferViewDesc& bufferViewDesc);
        DescriptorVal(DeviceVal& device, Descriptor& descriptor, const Texture1DViewDesc& textureViewDesc);
        DescriptorVal(DeviceVal& device, Descriptor& descriptor, const Texture2DViewDesc& textureViewDesc);
        DescriptorVal(DeviceVal& device, Descriptor& descriptor, const Texture3DViewDesc& textureViewDesc);
        DescriptorVal(DeviceVal& device, Descriptor& descriptor, const SamplerDesc& samplerDesc);

        void SetDebugName(const char* name);
        VkBufferView GetBufferDescriptorVK(uint32_t physicalDeviceIndex) const;
        VkImageView GetTextureDescriptorVK(uint32_t physicalDeviceIndex, VkImageSubresourceRange& subresourceRange) const;

        bool IsBufferView() const;
        bool IsTextureView() const;
        bool IsSampler() const;
        bool IsAccelerationStructure() const;

        bool IsConstantBufferView() const;
        bool IsShaderResource() const;
        bool IsShaderResourceStorage() const;
        bool IsColorAttachment() const;
        bool IsDepthStencilAttachment() const;

    private:
        ResourceType m_ResourceType = ResourceType::NONE;
        ResourceViewType m_ResourceViewType = ResourceViewType::NONE;
    };

    inline bool DescriptorVal::IsBufferView() const
    {
        return m_ResourceType == ResourceType::BUFFER;
    }

    inline bool DescriptorVal::IsTextureView() const
    {
        return m_ResourceType == ResourceType::TEXTURE;
    }

    inline bool DescriptorVal::IsSampler() const
    {
        return m_ResourceType == ResourceType::SAMPLER;
    }

    inline bool DescriptorVal::IsAccelerationStructure() const
    {
        return m_ResourceType == ResourceType::ACCELERATION_STRUCTURE;
    }

    inline bool DescriptorVal::IsConstantBufferView() const
    {
        return m_ResourceType == ResourceType::BUFFER && m_ResourceViewType == ResourceViewType::CONSTANT_BUFFER_VIEW;
    }

    inline bool DescriptorVal::IsShaderResource() const
    {
        const bool isResourceTypeValid = m_ResourceType != ResourceType::NONE && m_ResourceType != ResourceType::SAMPLER;
        return isResourceTypeValid && m_ResourceViewType == ResourceViewType::SHADER_RESOURCE;
    }

    inline bool DescriptorVal::IsShaderResourceStorage() const
    {
        const bool isResourceTypeValid = m_ResourceType != ResourceType::NONE && m_ResourceType != ResourceType::SAMPLER;
        return isResourceTypeValid && m_ResourceViewType == ResourceViewType::SHADER_RESOURCE_STORAGE;
    }

    inline bool DescriptorVal::IsColorAttachment() const
    {
        return IsTextureView() && m_ResourceViewType == ResourceViewType::COLOR_ATTACHMENT;
    }

    inline bool DescriptorVal::IsDepthStencilAttachment() const
    {
        return IsTextureView() && m_ResourceViewType == ResourceViewType::DEPTH_STENCIL_ATTACHMENT;
    }
}
