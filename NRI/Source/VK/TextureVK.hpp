/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#pragma region [  CoreInterface  ]

static void NRI_CALL SetTextureDebugName(Texture& texture, const char* name)
{
    ((TextureVK&)texture).SetDebugName(name);
}

static void NRI_CALL GetTextureMemoryInfo(const Texture& texture, MemoryLocation memoryLocation, MemoryDesc& memoryDesc)
{
    ((TextureVK&)texture).GetMemoryInfo(memoryLocation, memoryDesc);
}

void FillFunctionTableTextureVK(CoreInterface& coreInterface)
{
    coreInterface.SetTextureDebugName = SetTextureDebugName;
    coreInterface.GetTextureMemoryInfo = GetTextureMemoryInfo;
}

#pragma endregion

#pragma region [  WrapperVKInterface  ]

static void NRI_CALL GetTextureVK(const Texture& texture, uint32_t physicalDeviceIndex, TextureVulkanDesc& textureVulkanDesc)
{
    return ((TextureVK&)texture).GetTextureVK(physicalDeviceIndex, textureVulkanDesc);
}

void FillFunctionTableTextureVK(WrapperVKInterface& wrapperVKInterface)
{
    wrapperVKInterface.GetTextureVK = GetTextureVK;
}

#pragma endregion