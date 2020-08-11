/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nri
{
    struct QueryPoolVal : public DeviceObjectVal<QueryPool>
    {
        QueryPoolVal(DeviceVal& device, QueryPool& queryPool, QueryType queryType);

        void SetDebugName(const char* name);
        uint32_t GetQuerySize() const;
        QueryType GetQueryType() const;

    private:
        QueryType m_QueryType;
    };
}
