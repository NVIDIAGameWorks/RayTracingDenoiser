/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "QueryPoolVK.h"
#include "DeviceVK.h"

using namespace nri;

QueryPoolVK::~QueryPoolVK()
{
    const auto& vk = m_Device.GetDispatchTable();

    if (!m_OwnsNativeObjects)
        return;

    for (uint32_t i = 0; i < GetCountOf(m_Handles); i++)
    {
        if (m_Handles[i] != VK_NULL_HANDLE)
            vk.DestroyQueryPool(m_Device, m_Handles[i], m_Device.GetAllocationCallbacks());
    }
}

Result QueryPoolVK::Create(const QueryPoolDesc& queryPoolDesc)
{
    m_OwnsNativeObjects = true;
    m_Type = GetQueryType(queryPoolDesc.queryType);

    const VkQueryPoolCreateInfo poolInfo = {
        VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        nullptr,
        (VkQueryPoolCreateFlags)0,
        m_Type,
        queryPoolDesc.capacity,
        GetQueryPipelineStatisticsFlags(queryPoolDesc.pipelineStatsMask)
    };

    const auto& vk = m_Device.GetDispatchTable();

    const uint32_t physicalDeviceMask = (queryPoolDesc.physicalDeviceMask == WHOLE_DEVICE_GROUP) ? 0xff : queryPoolDesc.physicalDeviceMask;

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
        {
            const VkResult result = vk.CreateQueryPool(m_Device, &poolInfo, m_Device.GetAllocationCallbacks(), &m_Handles[i]);

            RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                "Can't create a query pool: vkCreateQueryPool returned %d.", (int32_t)result);
        }
    }

    m_Stride = GetQuerySize();

    return Result::SUCCESS;
}

Result QueryPoolVK::Create(const QueryPoolVulkanDesc& queryPoolDesc)
{
    m_OwnsNativeObjects = false;
    m_Type = (VkQueryType)queryPoolDesc.vkQueryType;

    const VkQueryPool handle = (VkQueryPool)queryPoolDesc.vkQueryPool;
    const uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(queryPoolDesc.physicalDeviceMask);

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
            m_Handles[i] = handle;
    }

    return Result::SUCCESS;
}

void QueryPoolVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToDeviceGroupObject(VK_OBJECT_TYPE_QUERY_POOL, (void**)m_Handles.data(), name);
}

uint32_t QueryPoolVK::GetQuerySize() const
{
    const uint32_t highestBitInPipelineStatsBits = 11;

    switch (m_Type)
    {
    case VK_QUERY_TYPE_TIMESTAMP:
        return sizeof(uint64_t);
    case VK_QUERY_TYPE_OCCLUSION:
        return sizeof(uint64_t);
    case VK_QUERY_TYPE_PIPELINE_STATISTICS:
        return highestBitInPipelineStatsBits * sizeof(uint64_t);
    }

    return 0;
}

#include "QueryPoolVK.hpp"