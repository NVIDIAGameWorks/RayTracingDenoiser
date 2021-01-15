/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedVal.h"
#include "DescriptorVal.h"

using namespace nri;

DescriptorVal::DescriptorVal(DeviceVal& device, Descriptor& descriptor, ResourceType resourceType) :
    DeviceObjectVal(device, descriptor),
    m_ResourceType(resourceType)
{
}

DescriptorVal::DescriptorVal(DeviceVal& device, Descriptor& descriptor, const BufferViewDesc& bufferViewDesc) :
    DeviceObjectVal(device, descriptor),
    m_ResourceType(ResourceType::BUFFER)
{
    switch(bufferViewDesc.viewType)
    {
    case BufferViewType::CONSTANT:
        m_ResourceViewType = ResourceViewType::CONSTANT_BUFFER_VIEW;
        break;
    case BufferViewType::SHADER_RESOURCE:
        m_ResourceViewType = ResourceViewType::SHADER_RESOURCE;
        break;
    case BufferViewType::SHADER_RESOURCE_STORAGE:
        m_ResourceViewType = ResourceViewType::SHADER_RESOURCE_STORAGE;
        break;
   }
}

DescriptorVal::DescriptorVal(DeviceVal& device, Descriptor& descriptor, const Texture1DViewDesc& textureViewDesc) :
    DeviceObjectVal(device, descriptor),
    m_ResourceType(ResourceType::TEXTURE)
{
    switch(textureViewDesc.viewType)
    {
    case Texture1DViewType::SHADER_RESOURCE_1D:
    case Texture1DViewType::SHADER_RESOURCE_1D_ARRAY:
        m_ResourceViewType = ResourceViewType::SHADER_RESOURCE;
        break;
    case Texture1DViewType::SHADER_RESOURCE_STORAGE_1D:
    case Texture1DViewType::SHADER_RESOURCE_STORAGE_1D_ARRAY:
        m_ResourceViewType = ResourceViewType::SHADER_RESOURCE_STORAGE;
        break;
    case Texture1DViewType::COLOR_ATTACHMENT:
        m_ResourceViewType = ResourceViewType::COLOR_ATTACHMENT;
         break;
    case Texture1DViewType::DEPTH_STENCIL_ATTACHMENT:
        m_ResourceViewType = ResourceViewType::DEPTH_STENCIL_ATTACHMENT;
         break;
   }
}

DescriptorVal::DescriptorVal(DeviceVal& device, Descriptor& descriptor, const Texture2DViewDesc& textureViewDesc) :
    DeviceObjectVal(device, descriptor),
    m_ResourceType(ResourceType::TEXTURE)
{
    switch(textureViewDesc.viewType)
    {
    case Texture2DViewType::SHADER_RESOURCE_2D:
    case Texture2DViewType::SHADER_RESOURCE_2D_ARRAY:
    case Texture2DViewType::SHADER_RESOURCE_CUBE:
    case Texture2DViewType::SHADER_RESOURCE_CUBE_ARRAY:
        m_ResourceViewType = ResourceViewType::SHADER_RESOURCE;
        break;
    case Texture2DViewType::SHADER_RESOURCE_STORAGE_2D:
    case Texture2DViewType::SHADER_RESOURCE_STORAGE_2D_ARRAY:
        m_ResourceViewType = ResourceViewType::SHADER_RESOURCE_STORAGE;
        break;
    case Texture2DViewType::COLOR_ATTACHMENT:
        m_ResourceViewType = ResourceViewType::COLOR_ATTACHMENT;
         break;
    case Texture2DViewType::DEPTH_STENCIL_ATTACHMENT:
        m_ResourceViewType = ResourceViewType::DEPTH_STENCIL_ATTACHMENT;
         break;
   }
}

DescriptorVal::DescriptorVal(DeviceVal& device, Descriptor& descriptor, const Texture3DViewDesc& textureViewDesc) :
    DeviceObjectVal(device, descriptor),
    m_ResourceType(ResourceType::TEXTURE)
{
    switch(textureViewDesc.viewType)
    {
    case Texture3DViewType::SHADER_RESOURCE_3D:
        m_ResourceViewType = ResourceViewType::SHADER_RESOURCE;
        break;
    case Texture3DViewType::SHADER_RESOURCE_STORAGE_3D:
        m_ResourceViewType = ResourceViewType::SHADER_RESOURCE_STORAGE;
        break;
    case Texture3DViewType::COLOR_ATTACHMENT:
        m_ResourceViewType = ResourceViewType::COLOR_ATTACHMENT;
         break;
   }
}

DescriptorVal::DescriptorVal(DeviceVal& device, Descriptor& descriptor, const SamplerDesc& samplerDesc) :
    DeviceObjectVal(device, descriptor),
    m_ResourceType(ResourceType::SAMPLER)
{
}

void DescriptorVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetDescriptorDebugName(m_ImplObject, name);
}

VkBufferView DescriptorVal::GetBufferDescriptorVK(uint32_t physicalDeviceIndex) const
{
    const WrapperVKInterface& interface = m_Device.GetWrapperVKInterface();

    return interface.GetBufferDescriptorVK(m_ImplObject, physicalDeviceIndex);
}

VkImageView DescriptorVal::GetTextureDescriptorVK(uint32_t physicalDeviceIndex, VkImageSubresourceRange& subresourceRange) const
{
    const WrapperVKInterface& interface = m_Device.GetWrapperVKInterface();

    return interface.GetTextureDescriptorVK(m_ImplObject, physicalDeviceIndex, subresourceRange);
}

#include "DescriptorVal.hpp"
