/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "CommandQueueD3D12.h"
#include "DeviceD3D12.h"
#include "QueueSemaphoreD3D12.h"
#include "DeviceSemaphoreD3D12.h"
#include "CommandBufferD3D12.h"

using namespace nri;

constexpr extern D3D12_COMMAND_LIST_TYPE GetCommandListType(CommandQueueType commandQueueType);

Result CommandQueueD3D12::Create(CommandQueueType queueType)
{
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
    commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    commandQueueDesc.NodeMask = NRI_TEMP_NODE_MASK;
    commandQueueDesc.Type = GetCommandListType(queueType);

    HRESULT hr = ((ID3D12Device*)m_Device)->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&m_CommandQueue));
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12Device::CreateCommandQueue() failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    m_CommandListType = commandQueueDesc.Type;

    return Result::SUCCESS;
}

Result CommandQueueD3D12::Create(ID3D12CommandQueue* commandQueue)
{
    const D3D12_COMMAND_QUEUE_DESC& commandQueueDesc = m_CommandQueue->GetDesc();

    m_CommandQueue = commandQueue;
    m_CommandListType = commandQueueDesc.Type;

    return Result::SUCCESS;
}

D3D12_COMMAND_LIST_TYPE CommandQueueD3D12::GetType() const
{
    return m_CommandListType;
}

inline void CommandQueueD3D12::SetDebugName(const char* name)
{
    SET_D3D_DEBUG_OBJECT_NAME(m_CommandQueue, name);
}

inline void CommandQueueD3D12::Submit(const WorkSubmissionDesc& workSubmissionDesc, DeviceSemaphore* deviceSemaphore)
{
    for (uint32_t i = 0; i < workSubmissionDesc.waitNum; i++)
        ((QueueSemaphoreD3D12*)workSubmissionDesc.wait[i])->Wait(m_CommandQueue);

    if (workSubmissionDesc.commandBufferNum)
    {
        Vector<ID3D12GraphicsCommandList*> commandLists(m_Device.GetStdAllocator());
        for (uint32_t j = 0; j < workSubmissionDesc.commandBufferNum; j++)
            commandLists.push_back(*((CommandBufferD3D12*)workSubmissionDesc.commandBuffers[j]));

        m_CommandQueue->ExecuteCommandLists((UINT)commandLists.size(), (ID3D12CommandList**)&commandLists[0]);
    }

    for (uint32_t i = 0; i < workSubmissionDesc.signalNum; i++)
        ((QueueSemaphoreD3D12*)workSubmissionDesc.signal[i])->Signal(m_CommandQueue);

    if (deviceSemaphore)
        ((DeviceSemaphoreD3D12*)deviceSemaphore)->Signal(m_CommandQueue);
}

inline void CommandQueueD3D12::Wait(DeviceSemaphore& deviceSemaphore)
{
    ((DeviceSemaphoreD3D12&)deviceSemaphore).Wait();
}

#include "CommandQueueD3D12.hpp"
