/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct ID3D12Heap;
struct ID3D12GraphicsCommandList;
struct ID3D12CommandAllocator;
struct IDXGIAdapter;

namespace nri
{
    struct DeviceCreationD3D12Desc
    {
        ID3D12Device* d3d12Device;
        ID3D12CommandQueue* d3d12GraphicsQueue;
        ID3D12CommandQueue* d3d12ComputeQueue;
        ID3D12CommandQueue* d3d12CopyQueue;
        IDXGIAdapter* d3d12PhysicalAdapter;
        CallbackInterface callbackInterface;
        MemoryAllocatorInterface memoryAllocatorInterface;
        bool enableNRIValidation;
        bool enableAPIValidation;
    };

    struct CommandBufferD3D12Desc
    {
        ID3D12GraphicsCommandList* d3d12CommandList;
        ID3D12CommandAllocator* d3d12CommandAllocator;
    };

    struct BufferD3D12Desc
    {
        ID3D12Resource* d3d12Resource;
        uint32_t structureStride;
    };

    struct TextureD3D12Desc
    {
        ID3D12Resource* d3d12Resource;
    };

    struct MemoryD3D12Desc
    {
        ID3D12Heap* d3d12Heap;
    };

    NRI_API Result NRI_CALL CreateDeviceFromD3D12Device(const DeviceCreationD3D12Desc& deviceDesc, Device*& device);
    NRI_API Format NRI_CALL GetFormatDXGI(uint32_t dxgiFormat);

    struct WrapperD3D12Interface
    {
        Result (NRI_CALL *CreateCommandBufferD3D12)(Device& device, const CommandBufferD3D12Desc& commandBufferDesc, CommandBuffer*& commandBuffer);
        Result (NRI_CALL *CreateBufferD3D12)(Device& device, const BufferD3D12Desc& bufferDesc, Buffer*& buffer);
        Result (NRI_CALL *CreateTextureD3D12)(Device& device, const TextureD3D12Desc& textureDesc, Texture*& texture);
        Result (NRI_CALL *CreateMemoryD3D12)(Device& device, const MemoryD3D12Desc& memoryDesc, Memory*& memory);

        ID3D12Device* (NRI_CALL *GetDeviceD3D12)(const Device& device);
        ID3D12Resource* (NRI_CALL *GetBufferD3D12)(const Buffer& buffer);
        ID3D12Resource* (NRI_CALL *GetTextureD3D12)(const Texture& texture);
        ID3D12GraphicsCommandList* (NRI_CALL *GetCommandBufferD3D12)(const CommandBuffer& commandBuffer);
    };

}