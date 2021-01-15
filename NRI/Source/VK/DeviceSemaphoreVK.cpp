/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "DeviceSemaphoreVK.h"
#include "DeviceVK.h"

using namespace nri;

DeviceSemaphoreVK::~DeviceSemaphoreVK()
{
    const auto& vk = m_Device.GetDispatchTable();
    if (m_Handle != VK_NULL_HANDLE && m_OwnsNativeObjects)
        vk.DestroyFence(m_Device, m_Handle, m_Device.GetAllocationCallbacks());
}

Result DeviceSemaphoreVK::Create(bool signaled)
{
    m_OwnsNativeObjects = true;

    const VkFenceCreateInfo fenceInfo = {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        nullptr,
        signaled ? VK_FENCE_CREATE_SIGNALED_BIT : (VkFenceCreateFlags)0
    };

    const auto& vk = m_Device.GetDispatchTable();
    const VkResult result = vk.CreateFence(m_Device, &fenceInfo, m_Device.GetAllocationCallbacks(), &m_Handle);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't create a semaphore: vkCreateFence returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

Result DeviceSemaphoreVK::Create(void* vkFence)
{
    m_OwnsNativeObjects = false;
    m_Handle = (VkFence)vkFence;

    return Result::SUCCESS;
}

void DeviceSemaphoreVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_FENCE, m_Handle, name);
}

#include "DeviceSemaphoreVK.hpp"