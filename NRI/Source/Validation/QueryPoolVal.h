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
    struct QueryPoolVal : public DeviceObjectVal<QueryPool>
    {
        QueryPoolVal(DeviceVal& device, QueryPool& queryPool, QueryType queryType, uint32_t queryNum);

        void SetDebugName(const char* name);
        uint32_t GetQuerySize() const;

        uint32_t GetQueryNum() const;
        QueryType GetQueryType() const;
        bool IsImported() const;
        bool SetQueryState(uint32_t offset, bool state);
        void ResetQueries(uint32_t offset, uint32_t number);

    private:
        Vector<uint64_t> m_DeviceState;
        uint32_t m_QueryNum;
        QueryType m_QueryType;
    };

    inline uint32_t QueryPoolVal::GetQueryNum() const
    {
        return m_QueryNum;
    }

    inline QueryType QueryPoolVal::GetQueryType() const
    {
        return m_QueryType;
    }

    inline bool QueryPoolVal::IsImported() const
    {
        return m_QueryNum == 0;
    }

    inline bool QueryPoolVal::SetQueryState(uint32_t offset, bool state)
    {
        const size_t batchIndex = offset >> 6;
        const uint64_t batchValue = m_DeviceState[batchIndex];
        const size_t bitIndex = 1ull << (offset & 63);
        const uint64_t maskBitValue = ~bitIndex;
        const uint64_t bitValue = state ? bitIndex : 0;
        m_DeviceState[batchIndex] = (batchValue & maskBitValue) | bitValue;
        return batchValue & bitIndex;
    }
}
