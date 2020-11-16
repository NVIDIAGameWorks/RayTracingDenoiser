/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedD3D11.h"
#include "CommandQueueD3D11.h"

#include "CommandBufferD3D11.h"
#include "DeviceSemaphoreD3D11.h"
#include "QueueSemaphoreD3D11.h"

using namespace nri;

CommandQueueD3D11::CommandQueueD3D11(DeviceD3D11& device, const VersionedContext& immediateContext, CommandQueueType type) :
    m_ImmediateContext(immediateContext),
    m_Type(type),
    m_Device(device)
{
}

CommandQueueD3D11::~CommandQueueD3D11()
{
}

inline void CommandQueueD3D11::SetDebugName(const char* name)
{
    SetName(m_ImmediateContext.ptr, name);
}

inline void CommandQueueD3D11::Submit(const WorkSubmissionDesc& workSubmissionDesc, DeviceSemaphore* deviceSemaphore)
{
    for (uint32_t i = 0; i < workSubmissionDesc.waitNum; i++)
    {
        QueueSemaphoreD3D11* semaphore = (QueueSemaphoreD3D11*)workSubmissionDesc.wait[i];
        semaphore->Wait();
    }

    for (uint32_t i = 0; i < workSubmissionDesc.commandBufferNum; i++)
    {
        CommandBufferHelper* commandBuffer = (CommandBufferHelper*)workSubmissionDesc.commandBuffers[i];
        commandBuffer->Submit(m_ImmediateContext);
    }

    for (uint32_t i = 0; i < workSubmissionDesc.signalNum; i++)
    {
        QueueSemaphoreD3D11* semaphore = (QueueSemaphoreD3D11*)workSubmissionDesc.signal[i];
        semaphore->Signal();
    }

    if (deviceSemaphore)
        ((DeviceSemaphoreD3D11*)deviceSemaphore)->Signal(m_ImmediateContext);
}

inline void CommandQueueD3D11::Wait(DeviceSemaphore& deviceSemaphore)
{
    ((DeviceSemaphoreD3D11&)deviceSemaphore).Wait(m_ImmediateContext);
}

inline Result CommandQueueD3D11::ChangeResourceStates(const TransitionBarrierDesc& transitionBarriers)
{
    HelperResourceStateChange resourceStateChange(m_Device.GetCoreInterface(), (Device&)m_Device, (CommandQueue&)*this);

    return resourceStateChange.ChangeStates(transitionBarriers);
}

inline Result CommandQueueD3D11::UploadData(const TextureUploadDesc* textureUploadDescs, uint32_t textureUploadDescNum,
    const BufferUploadDesc* bufferUploadDescs, uint32_t bufferUploadDescNum)
{
    HelperDataUpload helperDataUpload(m_Device.GetCoreInterface(), (Device&)m_Device, m_Device.GetStdAllocator(), (CommandQueue&)*this);

    return helperDataUpload.UploadData(textureUploadDescs, textureUploadDescNum, bufferUploadDescs, bufferUploadDescNum);
}

inline Result CommandQueueD3D11::WaitForIdle()
{
    HelperWaitIdle helperWaitIdle(m_Device.GetCoreInterface(), (Device&)m_Device, (CommandQueue&)*this);

    return helperWaitIdle.WaitIdle();
}

#include "CommandQueueD3D11.hpp"
