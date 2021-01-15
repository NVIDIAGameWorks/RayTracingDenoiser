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

    struct QueryPoolD3D12
    {
        QueryPoolD3D12(DeviceD3D12& device);
        ~QueryPoolD3D12();

        operator ID3D12QueryHeap*() const;

        DeviceD3D12& GetDevice() const;

        Result Create(const QueryPoolDesc& queryPoolDesc);
        D3D12_QUERY_TYPE GetType() const;

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);
        uint32_t GetQuerySize() const;

    private:
        DeviceD3D12& m_Device;
        D3D12_QUERY_TYPE m_QueryType = D3D12_QUERY_TYPE(-1);
        ComPtr<ID3D12QueryHeap> m_QueryHeap;
    };

    inline QueryPoolD3D12::QueryPoolD3D12(DeviceD3D12& device)
        : m_Device(device)
    {}

    inline QueryPoolD3D12::~QueryPoolD3D12()
    {}

    inline QueryPoolD3D12::operator ID3D12QueryHeap*() const
    {
        return m_QueryHeap.GetInterface();
    }

    inline D3D12_QUERY_TYPE QueryPoolD3D12::GetType() const
    {
        return m_QueryType;
    }

    inline DeviceD3D12& QueryPoolD3D12::GetDevice() const
    {
        return m_Device;
    }
}
