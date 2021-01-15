/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

struct ID3D11Device;
struct ID3D11Resource;
struct ID3D11DeviceContext;

namespace nri
{
    struct DeviceCreationD3D11Desc
    {
        ID3D11Device* d3d11Device;
        void* agsContextAssociatedWithDevice;
        CallbackInterface callbackInterface;
        MemoryAllocatorInterface memoryAllocatorInterface;
        bool enableNRIValidation;
        bool enableAPIValidation;
    };

    struct CommandBufferD3D11Desc
    {
        ID3D11DeviceContext* d3d11DeviceContext;
    };

    struct BufferD3D11Desc
    {
        ID3D11Resource* d3d11Resource;
    };

    struct TextureD3D11Desc
    {
        ID3D11Resource* d3d11Resource;
    };

    NRI_API Result NRI_CALL CreateDeviceFromD3D11Device(const DeviceCreationD3D11Desc& deviceDesc, Device*& device);
    NRI_API Format NRI_CALL GetFormatDXGI(uint32_t dxgiFormat);

    struct WrapperD3D11Interface
    {
        Result (NRI_CALL *CreateCommandBufferD3D11)(Device& device, const CommandBufferD3D11Desc& commandBufferDesc, CommandBuffer*& commandBuffer);
        Result (NRI_CALL *CreateBufferD3D11)(Device& device, const BufferD3D11Desc& bufferDesc, Buffer*& buffer);
        Result (NRI_CALL *CreateTextureD3D11)(Device& device, const TextureD3D11Desc& textureDesc, Texture*& texture);

        ID3D11Device* (NRI_CALL *GetDeviceD3D11)(const Device& device);
        ID3D11Resource* (NRI_CALL *GetBufferD3D11)(const Buffer& buffer);
        ID3D11Resource* (NRI_CALL *GetTextureD3D11)(const Texture& texture);
        ID3D11DeviceContext* (NRI_CALL *GetCommandBufferD3D11)(const CommandBuffer& commandBuffer);
    };
}
