/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedVal.h"
#include "QueryPoolVal.h"

using namespace nri;

QueryPoolVal::QueryPoolVal(DeviceVal& device, QueryPool& queryPool, QueryType queryType, uint32_t queryNum) :
    DeviceObjectVal(device, queryPool),
    m_QueryType(queryType),
    m_DeviceState(device.GetStdAllocator())
{
    m_QueryNum = queryNum;

    if (queryNum != 0)
    {
        const size_t batchNum = std::max(queryNum >> 6, 1u);
        m_DeviceState.resize(batchNum, 0);
    }
}

void QueryPoolVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetQueryPoolDebugName(m_ImplObject, name);
}

uint32_t QueryPoolVal::GetQuerySize() const
{
    return m_CoreAPI.GetQuerySize(m_ImplObject);
}

void QueryPoolVal::ResetQueries(uint32_t offset, uint32_t number)
{
    for (uint32_t i = 0; i < number; i++)
        SetQueryState(offset + i, false);
}

#include "QueryPoolVal.hpp"
