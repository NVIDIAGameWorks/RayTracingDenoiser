/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "CommandAllocatorVK.h"
#include "CommandBufferVK.h"
#include "CommandQueueVK.h"
#include "DeviceVK.h"

using namespace nri;

CommandAllocatorVK::~CommandAllocatorVK()
{
    const auto& vk = m_Device.GetDispatchTable();
    if (m_Handle != VK_NULL_HANDLE && m_OwnsNativeObjects)
        vk.DestroyCommandPool(m_Device, m_Handle, m_Device.GetAllocationCallbacks());
}

Result CommandAllocatorVK::Create(const CommandQueue& commandQueue, uint32_t physicalDeviceMask)
{
    m_OwnsNativeObjects = true;
    const CommandQueueVK& commandQueueImpl = (CommandQueueVK&)commandQueue;

    m_Type = commandQueueImpl.GetType();

    const VkCommandPoolCreateInfo info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        commandQueueImpl.GetFamilyIndex()
    };

    const auto& vk = m_Device.GetDispatchTable();
    const VkResult result = vk.CreateCommandPool(m_Device, &info, m_Device.GetAllocationCallbacks(), &m_Handle);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't create a command pool: vkCreateCommandPool returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

Result CommandAllocatorVK::Create(const CommandAllocatorVulkanDesc& commandAllocatorDesc)
{
    m_OwnsNativeObjects = false;
    m_Handle = (VkCommandPool)commandAllocatorDesc.vkCommandPool;
    m_Type = commandAllocatorDesc.commandQueueType;
    return Result::SUCCESS;
}

void CommandAllocatorVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_COMMAND_POOL, m_Handle, name);
}

Result CommandAllocatorVK::CreateCommandBuffer(CommandBuffer*& commandBuffer)
{
    const auto& vk = m_Device.GetDispatchTable();

    const VkCommandBufferAllocateInfo info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        m_Handle,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1
    };

    VkCommandBuffer commandBufferHandle = VK_NULL_HANDLE;
    const VkResult result = vk.AllocateCommandBuffers(m_Device, &info, &commandBufferHandle);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't create the command buffer: vkAllocateCommandBuffers returned %d.", (int32_t)result);

    CommandBufferVK* commandBufferImpl = Allocate<CommandBufferVK>(m_Device.GetStdAllocator(), m_Device);
    commandBufferImpl->Create(m_Handle, commandBufferHandle, m_Type);

    commandBuffer = (CommandBuffer*)commandBufferImpl;

    return Result::SUCCESS;
}

void CommandAllocatorVK::Reset()
{
    const auto& vk = m_Device.GetDispatchTable();
    const VkResult result = vk.ResetCommandPool(m_Device, m_Handle, (VkCommandPoolResetFlags)0);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, ReturnVoid(),
        "Can't reset a command pool. vkResetCommandPool returned %d.", (int32_t)result);
}

#include "CommandAllocatorVK.hpp"