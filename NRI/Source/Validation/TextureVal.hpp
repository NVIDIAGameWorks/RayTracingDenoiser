/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static void NRI_CALL SetTextureDebugName(Texture& texture, const char* name)
{
    ((TextureVal*)&texture)->SetDebugName(name);
}

static void NRI_CALL GetTextureMemoryInfo(const Texture& texture, MemoryLocation memoryLocation, MemoryDesc& memoryDesc)
{
    ((TextureVal*)&texture)->GetMemoryInfo(memoryLocation, memoryDesc);
}

void FillFunctionTableTextureVal(CoreInterface& coreInterface)
{
    coreInterface.SetTextureDebugName = SetTextureDebugName;
    coreInterface.GetTextureMemoryInfo = GetTextureMemoryInfo;
}

#pragma endregion

#pragma region [  WrapperD3D11Interface  ]

static ID3D11Resource* NRI_CALL GetTextureD3D11(const Texture& texture)
{
    return ((TextureVal*)&texture)->GetTextureD3D11();
}

void FillFunctionTableTextureVal(WrapperD3D11Interface& wrapperD3D11Interface)
{
    wrapperD3D11Interface.GetTextureD3D11 = GetTextureD3D11;
}

#pragma endregion

#pragma region [  WrapperD3D12Interface  ]

static ID3D12Resource* NRI_CALL GetTextureD3D12(const Texture& texture)
{
    return ((TextureVal*)&texture)->GetTextureD3D12();
}

void FillFunctionTableTextureVal(WrapperD3D12Interface& wrapperD3D12Interface)
{
    wrapperD3D12Interface.GetTextureD3D12 = GetTextureD3D12;
}

#pragma endregion

#pragma region [  WrapperVKInterface  ]

static void NRI_CALL GetTextureVK(const Texture& texture, uint32_t physicalDeviceIndex, TextureVulkanDesc& textureVulkanDesc)
{
    return ((TextureVal&)texture).GetTextureVK(physicalDeviceIndex, textureVulkanDesc);
}

void FillFunctionTableTextureVal(WrapperVKInterface& wrapperVKInterface)
{
    wrapperVKInterface.GetTextureVK = GetTextureVK;
}

#pragma endregion