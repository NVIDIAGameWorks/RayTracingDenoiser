/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

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

QueryPoolVal::QueryPoolVal(DeviceVal& device, QueryPool& queryPool) :
    DeviceObjectVal(device, queryPool)
{
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

#include "QueryPoolVal.hpp"
