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
struct ID3D12Fence;

namespace nri
{
    struct DeviceD3D12;

    struct DeviceSemaphoreD3D12
    {
        DeviceSemaphoreD3D12(DeviceD3D12& device);
        ~DeviceSemaphoreD3D12();

        operator ID3D12Fence*() const;

        DeviceD3D12& GetDevice() const;

        Result Create(bool signaled);

        void Signal(ID3D12CommandQueue* commandQueue) const;
        void Wait();

        //================================================================================================================
        // NRI
        //================================================================================================================
        void SetDebugName(const char* name);

    private:
        DeviceD3D12& m_Device;
        ComPtr<ID3D12Fence> m_Fence;
        uint64_t m_SignalValue = 0;
    };

    inline DeviceSemaphoreD3D12::DeviceSemaphoreD3D12(DeviceD3D12& device)
        : m_Device(device)
    {}

    inline DeviceSemaphoreD3D12::~DeviceSemaphoreD3D12()
    {}

    inline DeviceSemaphoreD3D12::operator ID3D12Fence*() const
    {
        return m_Fence.GetInterface();
    }

    inline DeviceD3D12& DeviceSemaphoreD3D12::GetDevice() const
    {
        return m_Device;
    }
}
