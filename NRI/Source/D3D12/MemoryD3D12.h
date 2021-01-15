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
    struct DeviceD3D12;

    struct MemoryD3D12
    {
        MemoryD3D12(DeviceD3D12& device);
        ~MemoryD3D12();

        operator ID3D12Heap*() const;

        DeviceD3D12& GetDevice() const;

        Result Create(const MemoryType memoryType, uint64_t size);
        Result Create(const MemoryD3D12Desc& memoryDesc);
        bool RequiresDedicatedAllocation() const;
        const D3D12_HEAP_DESC& GetHeapDesc() const;

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);

    private:
        DeviceD3D12& m_Device;
        ComPtr<ID3D12Heap> m_Heap;
        D3D12_HEAP_DESC m_HeapDesc = {};
    };

    inline MemoryD3D12::MemoryD3D12(DeviceD3D12& device)
        : m_Device(device)
    {}

    inline MemoryD3D12::~MemoryD3D12()
    {}

    inline MemoryD3D12::operator ID3D12Heap*() const
    {
        return m_Heap.GetInterface();
    }

    inline const D3D12_HEAP_DESC& MemoryD3D12::GetHeapDesc() const
    {
        return m_HeapDesc;
    }

    inline bool MemoryD3D12::RequiresDedicatedAllocation() const
    {
        return m_Heap.GetInterface() ? false : true;
    }

    inline DeviceD3D12& MemoryD3D12::GetDevice() const
    {
        return m_Device;
    }
}
