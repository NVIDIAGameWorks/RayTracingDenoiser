/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "QueryPoolD3D12.h"
#include "DeviceD3D12.h"

using namespace nri;

constexpr extern D3D12_QUERY_TYPE GetQueryType(QueryType queryType);
constexpr extern D3D12_QUERY_HEAP_TYPE GetQueryHeapType(QueryType queryType);
extern uint32_t GetQueryElementSize(D3D12_QUERY_TYPE queryType);

Result QueryPoolD3D12::Create(const QueryPoolDesc& queryPoolDesc)
{
    D3D12_QUERY_HEAP_DESC desc;
    desc.Type = GetQueryHeapType(queryPoolDesc.queryType);
    desc.Count = queryPoolDesc.capacity;
    desc.NodeMask = NRI_TEMP_NODE_MASK;

    HRESULT hr = ((ID3D12Device*)m_Device)->CreateQueryHeap(&desc, IID_PPV_ARGS(&m_QueryHeap));
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12Device::CreateQueryHeap() failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    m_QueryType = GetQueryType(queryPoolDesc.queryType);

    return Result::SUCCESS;
}

inline void QueryPoolD3D12::SetDebugName(const char* name)
{
    SET_D3D_DEBUG_OBJECT_NAME(m_QueryHeap, name);
}

inline uint32_t QueryPoolD3D12::GetQuerySize() const
{
    return GetQueryElementSize(m_QueryType);
}

#include "QueryPoolD3D12.hpp"
