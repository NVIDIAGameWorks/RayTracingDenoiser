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
#include "CommandAllocatorVal.h"

#include "CommandBufferVal.h"

using namespace nri;

CommandAllocatorVal::CommandAllocatorVal(DeviceVal& device, CommandAllocator& commandAllocator) :
    DeviceObjectVal(device, commandAllocator)
{
}

void CommandAllocatorVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetCommandAllocatorDebugName(m_ImplObject, name);
}

Result CommandAllocatorVal::CreateCommandBuffer(CommandBuffer*& commandBuffer)
{
    CommandBuffer* commandBufferImpl;
    const Result result = m_CoreAPI.CreateCommandBuffer(m_ImplObject, commandBufferImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(m_Device.GetLog(), commandBufferImpl != nullptr, Result::FAILURE, "Implementation failure: 'commandBufferImpl' is NULL!");
        commandBuffer = (CommandBuffer*)Allocate<CommandBufferVal>(m_Device.GetStdAllocator(), m_Device, *commandBufferImpl);
    }

    return result;
}

void CommandAllocatorVal::Reset()
{
    m_CoreAPI.ResetCommandAllocator(m_ImplObject);
}

#include "CommandAllocatorVal.hpp"
