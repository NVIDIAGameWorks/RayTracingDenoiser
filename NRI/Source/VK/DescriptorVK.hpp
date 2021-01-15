/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#pragma region [  CoreInterface  ]

static void NRI_CALL SetDescriptorDebugName(Descriptor& descriptor, const char* name)
{
    ((DescriptorVK&)descriptor).SetDebugName(name);
}

void FillFunctionTableDescriptorVK(CoreInterface& coreInterface)
{
    coreInterface.SetDescriptorDebugName = SetDescriptorDebugName;
}

#pragma endregion

#pragma region [  WrapperVKInterface  ]

static VkImageView NRI_CALL GetTextureDescriptorVK(const Descriptor& descriptor, uint32_t physicalDeviceIndex, VkImageSubresourceRange& subresource)
{
    return ((DescriptorVK&)descriptor).GetTextureDescriptorVK(physicalDeviceIndex, subresource);
}

static VkBufferView NRI_CALL GetBufferDescriptorVK(const Descriptor& descriptor, uint32_t physicalDeviceIndex)
{
    return ((DescriptorVK&)descriptor).GetBufferDescriptorVK(physicalDeviceIndex);
}

void FillFunctionTableDescriptorVK(WrapperVKInterface& wrapperVKInterface)
{
    wrapperVKInterface.GetTextureDescriptorVK = GetTextureDescriptorVK;
    wrapperVKInterface.GetBufferDescriptorVK = GetBufferDescriptorVK;
}

#pragma endregion