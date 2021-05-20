/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "QueueSemaphoreVK.h"
#include "DeviceVK.h"

using namespace nri;

QueueSemaphoreVK::~QueueSemaphoreVK()
{
    const auto& vk = m_Device.GetDispatchTable();
    if (m_Handle != VK_NULL_HANDLE && m_OwnsNativeObjects)
        vk.DestroySemaphore(m_Device, m_Handle, m_Device.GetAllocationCallbacks());
}

Result QueueSemaphoreVK::Create()
{
    m_OwnsNativeObjects = true;

    const VkSemaphoreCreateInfo semaphoreInfo = {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        nullptr,
        (VkSemaphoreCreateFlags)0
    };

    const auto& vk = m_Device.GetDispatchTable();
    const VkResult result = vk.CreateSemaphore(m_Device, &semaphoreInfo, m_Device.GetAllocationCallbacks(), &m_Handle);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't create a semaphore: vkCreateSemaphore returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

Result QueueSemaphoreVK::Create(void* vkSemaphore)
{
    m_OwnsNativeObjects = false;
    m_Handle = (VkSemaphore)vkSemaphore;

    return Result::SUCCESS;
}

inline void QueueSemaphoreVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_SEMAPHORE, m_Handle, name);
}

#include "QueueSemaphoreVK.hpp"