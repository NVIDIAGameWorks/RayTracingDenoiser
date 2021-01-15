/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedD3D11.h"
#include "QueryPoolD3D11.h"

using namespace nri;

QueryPoolD3D11::QueryPoolD3D11(DeviceD3D11& device) :
    m_Device(device),
    m_Pool(device.GetStdAllocator())
{
}

QueryPoolD3D11::~QueryPoolD3D11()
{
}

Result QueryPoolD3D11::Create(const VersionedDevice& device, const QueryPoolDesc& queryPoolDesc)
{
    m_Pool.reserve(queryPoolDesc.capacity);
    m_Type = queryPoolDesc.queryType;

    D3D11_QUERY_DESC queryDesc = {};
    queryDesc.Query = D3D11_QUERY_TIMESTAMP;
    if (m_Type == QueryType::OCCLUSION)
        queryDesc.Query = D3D11_QUERY_OCCLUSION;
    else if (m_Type == QueryType::PIPELINE_STATISTICS)
        queryDesc.Query = D3D11_QUERY_PIPELINE_STATISTICS;

    for (uint32_t i = 0; i < queryPoolDesc.capacity; i++)
    {
        ID3D11Query* query = nullptr;
        HRESULT hr = device->CreateQuery(&queryDesc, &query);
        RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreateQuery() - FAILED!");

        m_Pool.push_back(query);
    }

    return Result::SUCCESS;
}

void QueryPoolD3D11::BeginQuery(const VersionedContext& context, uint32_t offset)
{
    ID3D11Query* query = m_Pool[offset];

    context->Begin(query);
}

void QueryPoolD3D11::EndQuery(const VersionedContext& context, uint32_t offset)
{
    ID3D11Query* query = m_Pool[offset];

    context->End(query);
}

void QueryPoolD3D11::GetData(uint8_t* dstMemory, const VersionedContext& immediateContext, uint32_t offset, uint32_t num) const
{
    uint32_t dataSize = GetQuerySize();

    num += offset;

    for (uint32_t i = offset; i < num; i++)
    {
        ID3D11Query* query = m_Pool[i];
        immediateContext->GetData(query, dstMemory, dataSize, 0);

        dstMemory += dataSize;
    }
}

inline void QueryPoolD3D11::SetDebugName(const char* name)
{
}

inline uint32_t QueryPoolD3D11::GetQuerySize() const
{
    if (m_Type == QueryType::PIPELINE_STATISTICS)
        return sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS);

    return sizeof(uint64_t);
}

#include "QueryPoolD3D11.hpp"
