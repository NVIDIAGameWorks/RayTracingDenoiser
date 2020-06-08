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
#include "CommandQueueVal.h"

#include "CommandBufferVal.h"
#include "DeviceSemaphoreVal.h"
#include "QueueSemaphoreVal.h"

using namespace nri;

CommandQueueVal::CommandQueueVal(DeviceVal& device, CommandQueue& commandQueue) :
    DeviceObjectVal(device, commandQueue)
{
}

void CommandQueueVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetCommandQueueDebugName(m_ImplObject, name);
}

void CommandQueueVal::Submit(const WorkSubmissionDesc& workSubmissionDesc, DeviceSemaphore* deviceSemaphore)
{
    auto workSubmissionDescImpl = workSubmissionDesc;
    workSubmissionDescImpl.commandBuffers = STACK_ALLOC(CommandBuffer*, workSubmissionDesc.commandBufferNum);
    for (uint32_t i = 0; i < workSubmissionDesc.commandBufferNum; i++)
        ((CommandBuffer**)workSubmissionDescImpl.commandBuffers)[i] = NRI_GET_IMPL(CommandBuffer, workSubmissionDesc.commandBuffers[i]);
    workSubmissionDescImpl.wait = STACK_ALLOC(QueueSemaphore*, workSubmissionDesc.waitNum);
    for (uint32_t i = 0; i < workSubmissionDesc.waitNum; i++)
        ((QueueSemaphore**)workSubmissionDescImpl.wait)[i] = NRI_GET_IMPL(QueueSemaphore, workSubmissionDesc.wait[i]);
    workSubmissionDescImpl.signal = STACK_ALLOC(QueueSemaphore*, workSubmissionDesc.signalNum);
    for (uint32_t i = 0; i < workSubmissionDesc.signalNum; i++)
        ((QueueSemaphore**)workSubmissionDescImpl.signal)[i] = NRI_GET_IMPL(QueueSemaphore, workSubmissionDesc.signal[i]);

    DeviceSemaphore* deviceSemaphoreImpl = nullptr;
    if (deviceSemaphore)
        deviceSemaphoreImpl = NRI_GET_IMPL(DeviceSemaphore, deviceSemaphore);

    for (uint32_t i = 0; i < workSubmissionDesc.waitNum; i++)
    {
        QueueSemaphoreVal* semaphore = (QueueSemaphoreVal*)workSubmissionDesc.wait[i];
        semaphore->Wait();
    }

    m_CoreAPI.SubmitQueueWork(m_ImplObject, workSubmissionDescImpl, deviceSemaphoreImpl);

    for (uint32_t i = 0; i < workSubmissionDesc.signalNum; i++)
    {
        QueueSemaphoreVal* semaphore = (QueueSemaphoreVal*)workSubmissionDesc.signal[i];
        semaphore->Signal();
    }

    if (deviceSemaphore)
        ((DeviceSemaphoreVal*)deviceSemaphore)->Signal();
}

void CommandQueueVal::Wait(DeviceSemaphore& deviceSemaphore)
{
    ((DeviceSemaphoreVal&)deviceSemaphore).Wait();
    DeviceSemaphore* deviceSemaphoreImpl = NRI_GET_IMPL(DeviceSemaphore, &deviceSemaphore);

    m_CoreAPI.WaitForSemaphore(m_ImplObject, *deviceSemaphoreImpl);
}

#include "CommandQueueVal.hpp"
