/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "DeviceSemaphoreVK.h"
#include "QueueSemaphoreVK.h"
#include "CommandBufferVK.h"
#include "CommandQueueVK.h"
#include "DeviceVK.h"

using namespace nri;

Result CommandQueueVK::Create(const CommandQueueVulkanDesc& commandQueueDesc)
{
    m_Handle = (VkQueue)commandQueueDesc.vkQueue;
    m_FamilyIndex = commandQueueDesc.familyIndex;
    m_Type = commandQueueDesc.commandQueueType;

    return Result::SUCCESS;
}

void CommandQueueVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_QUEUE, m_Handle, name);
}

void CommandQueueVK::Submit(const WorkSubmissionDesc& workSubmissionDesc, DeviceSemaphore* deviceSemaphore)
{
    VkCommandBuffer* commandBuffers = STACK_ALLOC(VkCommandBuffer, workSubmissionDesc.commandBufferNum);
    VkSemaphore* waitSemaphores = STACK_ALLOC(VkSemaphore, workSubmissionDesc.waitNum);
    VkPipelineStageFlags* waitStages = STACK_ALLOC(VkPipelineStageFlags, workSubmissionDesc.waitNum);
    VkSemaphore* signalSemaphores = STACK_ALLOC(VkSemaphore, workSubmissionDesc.signalNum);

    VkSubmitInfo submission = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        workSubmissionDesc.waitNum,
        waitSemaphores,
        waitStages,
        workSubmissionDesc.commandBufferNum,
        commandBuffers,
        workSubmissionDesc.signalNum,
        signalSemaphores
    };

    for (uint32_t i = 0; i < workSubmissionDesc.commandBufferNum; i++)
        *(commandBuffers++) = *(CommandBufferVK*)workSubmissionDesc.commandBuffers[i];

    for (uint32_t i = 0; i < workSubmissionDesc.waitNum; i++)
    {
        *(waitSemaphores++) = *(QueueSemaphoreVK*)workSubmissionDesc.wait[i];
        *(waitStages++) = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; // TODO: more optimal stage
    }

    for (uint32_t i = 0; i < workSubmissionDesc.signalNum; i++)
        *(signalSemaphores++) = *(QueueSemaphoreVK*)workSubmissionDesc.signal[i];

    VkFence fence = VK_NULL_HANDLE;
    if (deviceSemaphore != nullptr)
        fence = *(DeviceSemaphoreVK*)deviceSemaphore;

    VkDeviceGroupSubmitInfo deviceGroupInfo;
    if (m_Device.GetPhyiscalDeviceGroupSize() > 1)
    {
        uint32_t* waitSemaphoreDeviceIndices = STACK_ALLOC(uint32_t, workSubmissionDesc.waitNum);
        uint32_t* commandBufferDeviceMasks = STACK_ALLOC(uint32_t, workSubmissionDesc.commandBufferNum);
        uint32_t* signalSemaphoreDeviceIndices = STACK_ALLOC(uint32_t, workSubmissionDesc.signalNum);

        for (uint32_t i = 0; i < workSubmissionDesc.waitNum; i++)
            waitSemaphoreDeviceIndices[i] = workSubmissionDesc.physicalDeviceIndex;

        for (uint32_t i = 0; i < workSubmissionDesc.commandBufferNum; i++)
            commandBufferDeviceMasks[i] = 1u << workSubmissionDesc.physicalDeviceIndex;

        for (uint32_t i = 0; i < workSubmissionDesc.signalNum; i++)
            signalSemaphoreDeviceIndices[i] = workSubmissionDesc.physicalDeviceIndex;

        deviceGroupInfo = {
            VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO,
            nullptr,
            workSubmissionDesc.waitNum,
            waitSemaphoreDeviceIndices,
            workSubmissionDesc.commandBufferNum,
            commandBufferDeviceMasks,
            workSubmissionDesc.signalNum,
            signalSemaphoreDeviceIndices
        };

        submission.pNext = &deviceGroupInfo;
    }

    const auto& vk = m_Device.GetDispatchTable();
    const VkResult result = vk.QueueSubmit(m_Handle, 1, &submission, fence);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, ReturnVoid(),
        "Can't submit work to a command queue: vkQueueSubmit returned %d.", (int32_t)result);
}

void CommandQueueVK::Wait(DeviceSemaphore& deviceSemaphore)
{
    const VkFence fence = *(DeviceSemaphoreVK*)&deviceSemaphore;

    constexpr uint64_t tenSeconds = 10000000000;

    const auto& vk = m_Device.GetDispatchTable();
    VkResult result = vk.WaitForFences(m_Device, 1, &fence, VK_TRUE, tenSeconds);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, ReturnVoid(),
        "Can't wait on a device semaphore: vkWaitForFences returned %d.", (int32_t)result);

    result = vk.ResetFences(m_Device, 1, &fence);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, ReturnVoid(),
        "Can't reset a device semaphore: vkResetFences returned %d.", (int32_t)result);
}

#include "CommandQueueVK.hpp"