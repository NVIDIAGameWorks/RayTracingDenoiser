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
#include "CommandQueueVal.h"

#include "CommandBufferVal.h"
#include "DeviceSemaphoreVal.h"
#include "QueueSemaphoreVal.h"
#include "TextureVal.h"
#include "BufferVal.h"

using namespace nri;

static bool ValidateTransitionBarrierDesc(DeviceVal& device, uint32_t i, const BufferTransitionBarrierDesc& bufferTransitionBarrierDesc);
static bool ValidateTransitionBarrierDesc(DeviceVal& device, uint32_t i, const TextureTransitionBarrierDesc& textureTransitionBarrierDesc);
static bool ValidateTextureUploadDesc(DeviceVal& device, uint32_t i, const TextureUploadDesc& textureUploadDesc);
static bool ValidateBufferUploadDesc(DeviceVal& device, uint32_t i, const BufferUploadDesc& bufferUploadDesc);

CommandQueueVal::CommandQueueVal(DeviceVal& device, CommandQueue& commandQueue) :
    DeviceObjectVal(device, commandQueue),
    m_HelperAPI(device.GetHelperInterface())
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

Result CommandQueueVal::ChangeResourceStates(const TransitionBarrierDesc& transitionBarriers)
{
    auto* bufferTransitionBarriers = STACK_ALLOC(BufferTransitionBarrierDesc, transitionBarriers.bufferNum);
    auto* textureTransitionBarriers = STACK_ALLOC(TextureTransitionBarrierDesc, transitionBarriers.textureNum);

    for (uint32_t i = 0; i < transitionBarriers.bufferNum; i++)
    {
        if (!ValidateTransitionBarrierDesc(m_Device, i, transitionBarriers.buffers[i]))
            return Result::INVALID_ARGUMENT;

        const BufferVal& bufferVal = *(BufferVal*)transitionBarriers.buffers[i].buffer;

        bufferTransitionBarriers[i] = transitionBarriers.buffers[i];
        bufferTransitionBarriers[i].buffer = &bufferVal.GetImpl();
    }

    for (uint32_t i = 0; i < transitionBarriers.textureNum; i++)
    {
        if (!ValidateTransitionBarrierDesc(m_Device, i, transitionBarriers.textures[i]))
            return Result::INVALID_ARGUMENT;

        const TextureVal& textureVal = *(TextureVal*)transitionBarriers.textures[i].texture;

        textureTransitionBarriers[i] = transitionBarriers.textures[i];
        textureTransitionBarriers[i].texture = &textureVal.GetImpl();
    }

    TransitionBarrierDesc transitionBarriersImpl = transitionBarriers;
    transitionBarriersImpl.buffers = bufferTransitionBarriers;
    transitionBarriersImpl.textures = textureTransitionBarriers;

    return m_HelperAPI.ChangeResourceStates(m_ImplObject, transitionBarriersImpl);
}

Result CommandQueueVal::UploadData(const TextureUploadDesc* textureUploadDescs, uint32_t textureUploadDescNum, 
    const BufferUploadDesc* bufferUploadDescs, uint32_t bufferUploadDescNum)
{
    RETURN_ON_FAILURE(m_Device.GetLog(), textureUploadDescNum == 0 || textureUploadDescs != nullptr, Result::INVALID_ARGUMENT,
        "Can't upload data: 'textureUploadDescs' is invalid.");

    RETURN_ON_FAILURE(m_Device.GetLog(), bufferUploadDescNum == 0 || bufferUploadDescs != nullptr, Result::INVALID_ARGUMENT,
        "Can't upload data: 'bufferUploadDescs' is invalid.");

    TextureUploadDesc* textureUploadDescsImpl = STACK_ALLOC(TextureUploadDesc, textureUploadDescNum);

    for (uint32_t i = 0; i < textureUploadDescNum; i++)
    {
        if (!ValidateTextureUploadDesc(m_Device, i, textureUploadDescs[i]))
            return Result::INVALID_ARGUMENT;

        const TextureVal* textureVal = (TextureVal*)textureUploadDescs[i].texture;

        textureUploadDescsImpl[i] = textureUploadDescs[i];
        textureUploadDescsImpl[i].texture = &textureVal->GetImpl();
    }

    BufferUploadDesc* bufferUploadDescsImpl = STACK_ALLOC(BufferUploadDesc, bufferUploadDescNum);

    for (uint32_t i = 0; i < bufferUploadDescNum; i++)
    {
        if (!ValidateBufferUploadDesc(m_Device, i, bufferUploadDescs[i]))
            return Result::INVALID_ARGUMENT;

        const BufferVal* bufferVal = (BufferVal*)bufferUploadDescs[i].buffer;

        bufferUploadDescsImpl[i] = bufferUploadDescs[i];
        bufferUploadDescsImpl[i].buffer = &bufferVal->GetImpl();
    }

    return m_HelperAPI.UploadData(m_ImplObject, textureUploadDescsImpl, textureUploadDescNum, bufferUploadDescsImpl, bufferUploadDescNum);
}

Result CommandQueueVal::WaitForIdle()
{
    return m_HelperAPI.WaitForIdle(m_ImplObject);
}

static bool ValidateTransitionBarrierDesc(DeviceVal& device, uint32_t i, const BufferTransitionBarrierDesc& bufferTransitionBarrierDesc)
{
    RETURN_ON_FAILURE(device.GetLog(), bufferTransitionBarrierDesc.buffer != nullptr, false,
        "Can't change resource state: 'transitionBarriers.buffers[%u].buffer' is invalid.", i);

    const BufferVal& bufferVal = *(BufferVal*)bufferTransitionBarrierDesc.buffer;

    RETURN_ON_FAILURE(device.GetLog(), bufferVal.IsBoundToMemory(), false,
        "Can't change resource state: 'transitionBarriers.buffers[%u].buffer' is not bound to memory.", i);

    const BufferUsageBits usageMask = bufferVal.GetUsageMask();

    RETURN_ON_FAILURE(device.GetLog(), IsAccessMaskSupported(usageMask, bufferTransitionBarrierDesc.prevAccess), false,
        "Can't change resource state: 'transitionBarriers.buffers[%u].prevAccess' is not supported by usageMask of the buffer.", i);

    RETURN_ON_FAILURE(device.GetLog(), IsAccessMaskSupported(usageMask, bufferTransitionBarrierDesc.nextAccess), false,
        "Can't change resource state: 'transitionBarriers.buffers[%u].nextAccess' is not supported by usageMask of the buffer.", i);

    return true;
}

static bool ValidateTransitionBarrierDesc(DeviceVal& device, uint32_t i, const TextureTransitionBarrierDesc& textureTransitionBarrierDesc)
{
    RETURN_ON_FAILURE(device.GetLog(), textureTransitionBarrierDesc.texture != nullptr, false,
        "Can't change resource state: 'transitionBarriers.textures[%u].texture' is invalid.", i);

    const TextureVal& textureVal = *(TextureVal*)textureTransitionBarrierDesc.texture;

    RETURN_ON_FAILURE(device.GetLog(), textureVal.IsBoundToMemory(), false,
        "Can't change resource state: 'transitionBarriers.textures[%u].texture' is not bound to memory.", i);

    const TextureUsageBits usageMask = textureVal.GetDesc().usageMask;

    RETURN_ON_FAILURE(device.GetLog(), IsAccessMaskSupported(usageMask, textureTransitionBarrierDesc.prevAccess), false,
        "Can't change resource state: 'transitionBarriers.textures[%u].prevAccess' is not supported by usageMask of the texture.", i);

    RETURN_ON_FAILURE(device.GetLog(), IsAccessMaskSupported(usageMask, textureTransitionBarrierDesc.nextAccess), false,
        "Can't change resource state: 'transitionBarriers.textures[%u].nextAccess' is not supported by usageMask of the texture.", i);

    RETURN_ON_FAILURE(device.GetLog(), IsTextureLayoutSupported(usageMask, textureTransitionBarrierDesc.prevLayout), false,
        "Can't change resource state: 'transitionBarriers.textures[%u].prevLayout' is not supported by usageMask of the texture.", i);

    RETURN_ON_FAILURE(device.GetLog(), IsTextureLayoutSupported(usageMask, textureTransitionBarrierDesc.nextLayout), false,
        "Can't change resource state: 'transitionBarriers.textures[%u].nextLayout' is not supported by usageMask of the texture.", i);

    return true;
}

static bool ValidateTextureUploadDesc(DeviceVal& device, uint32_t i, const TextureUploadDesc& textureUploadDesc)
{
    const uint32_t subresourceNum = textureUploadDesc.arraySize * textureUploadDesc.mipNum;

    RETURN_ON_FAILURE(device.GetLog(), textureUploadDesc.texture != nullptr, false,
        "Can't upload data: 'textureUploadDescs[%u].texture' is invalid.", i);

    if (subresourceNum == 0)
    {
        REPORT_WARNING(device.GetLog(), "No data to upload: the number of subresources in 'textureUploadDescs[%u]' is 0.", i);
        return true;
    }

    RETURN_ON_FAILURE(device.GetLog(), textureUploadDesc.subresources != nullptr, false,
        "Can't upload data: 'textureUploadDescs[%u].subresources' is invalid.", i);

    const TextureVal& textureVal = *(TextureVal*)textureUploadDesc.texture;

    RETURN_ON_FAILURE(device.GetLog(), textureUploadDesc.mipNum <= textureVal.GetDesc().mipNum, false,
        "Can't upload data: 'textureUploadDescs[%u].mipNum' is invalid.", i);

    RETURN_ON_FAILURE(device.GetLog(), textureUploadDesc.arraySize <= textureVal.GetDesc().arraySize, false,
        "Can't upload data: 'textureUploadDescs[%u].arraySize' is invalid.", i);

    RETURN_ON_FAILURE(device.GetLog(), textureUploadDesc.nextLayout < TextureLayout::MAX_NUM, false,
        "Can't upload data: 'textureUploadDescs[%u].nextLayout' is invalid.", i);

    RETURN_ON_FAILURE(device.GetLog(), textureVal.IsBoundToMemory(), false,
        "Can't upload data: 'textureUploadDescs[%u].texture' is not bound to memory.", i);

    for (uint32_t j = 0; j < subresourceNum; j++)
    {
        const TextureSubresourceUploadDesc& subresource = textureUploadDesc.subresources[j];

        if (subresource.sliceNum == 0)
        {
            REPORT_WARNING(device.GetLog(), "No data to upload: the number of subresources in "
                "'textureUploadDescs[%u].subresources[%u].sliceNum' is 0.", i, j);
            continue;
        }

        RETURN_ON_FAILURE(device.GetLog(), subresource.slices != nullptr, false,
            "Can't upload data: 'textureUploadDescs[%u].subresources[%u].slices' is invalid.", i, j);

        RETURN_ON_FAILURE(device.GetLog(), subresource.rowPitch != 0, false,
            "Can't upload data: 'textureUploadDescs[%u].subresources[%u].rowPitch' is 0.", i, j);

        RETURN_ON_FAILURE(device.GetLog(), subresource.slicePitch != 0, false,
            "Can't upload data: 'textureUploadDescs[%u].subresources[%u].slicePitch' is 0.", i, j);
    }

    return true;
}

static bool ValidateBufferUploadDesc(DeviceVal& device, uint32_t i, const BufferUploadDesc& bufferUploadDesc)
{
    RETURN_ON_FAILURE(device.GetLog(), bufferUploadDesc.buffer != nullptr, false,
        "Can't upload data: 'bufferUploadDescs[%u].buffer' is invalid.", i);

    if (bufferUploadDesc.dataSize == 0)
    {
        REPORT_WARNING(device.GetLog(), "No data to upload: 'bufferUploadDescs[%u].dataSize' is 0.", i);
        return true;
    }

    RETURN_ON_FAILURE(device.GetLog(), bufferUploadDesc.data != nullptr, false,
        "Can't upload data: 'bufferUploadDescs[%u].data' is invalid.", i);

    const BufferVal& bufferVal = *(BufferVal*)bufferUploadDesc.buffer;

    const size_t rangeEnd = bufferUploadDesc.bufferOffset + bufferUploadDesc.dataSize;

    RETURN_ON_FAILURE(device.GetLog(), rangeEnd <= bufferVal.GetDesc().size, false,
        "Can't upload data: 'bufferUploadDescs[%u].bufferOffset + bufferUploadDescs[%u].dataSize' is out of bounds.", i, i);

    RETURN_ON_FAILURE(device.GetLog(), bufferVal.IsBoundToMemory(), false,
        "Can't upload data: 'bufferUploadDescs[%u].buffer' is not bound to memory.", i);

    return true;
}

#include "CommandQueueVal.hpp"
