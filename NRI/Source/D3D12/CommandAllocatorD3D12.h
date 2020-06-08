/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

struct ID3D12CommandAllocator;
enum D3D12_COMMAND_LIST_TYPE;

namespace nri
{
    struct DeviceD3D12;

    struct CommandAllocatorD3D12
    {
        CommandAllocatorD3D12(DeviceD3D12& device);
        ~CommandAllocatorD3D12();

        operator ID3D12CommandAllocator*() const;

        DeviceD3D12& GetDevice() const;

        Result Create(const CommandQueue& commandQueue);

        //================================================================================================================
        // NRI
        //================================================================================================================
        void SetDebugName(const char* name);

        Result CreateCommandBuffer(CommandBuffer*& commandBuffer);
        void Reset();

    private:
        DeviceD3D12& m_Device;
        ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
        D3D12_COMMAND_LIST_TYPE m_CommandListType;
    };

    inline CommandAllocatorD3D12::CommandAllocatorD3D12(DeviceD3D12& device)
        : m_Device(device)
    {}

    inline CommandAllocatorD3D12::~CommandAllocatorD3D12()
    {}

    inline CommandAllocatorD3D12::operator ID3D12CommandAllocator*() const
    {
        return m_CommandAllocator.GetInterface();
    }

    inline DeviceD3D12& CommandAllocatorD3D12::GetDevice() const
    {
        return m_Device;
    }
}
