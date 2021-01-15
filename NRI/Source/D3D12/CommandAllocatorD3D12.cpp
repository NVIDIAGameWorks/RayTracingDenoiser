/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "CommandAllocatorD3D12.h"
#include "DeviceD3D12.h"
#include "CommandQueueD3D12.h"
#include "CommandBufferD3D12.h"

using namespace nri;

Result CommandAllocatorD3D12::Create(const CommandQueue& commandQueue)
{
    const CommandQueueD3D12& commandQueueD3D12 = (CommandQueueD3D12&)commandQueue;
    m_CommandListType = commandQueueD3D12.GetType();
    HRESULT hr = ((ID3D12Device*)m_Device)->CreateCommandAllocator(m_CommandListType, IID_PPV_ARGS(&m_CommandAllocator));
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12Device::CreateCommandAllocator() failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    return Result::SUCCESS;
}

inline void CommandAllocatorD3D12::SetDebugName(const char* name)
{
    SET_D3D_DEBUG_OBJECT_NAME(m_CommandAllocator, name);
}

inline Result CommandAllocatorD3D12::CreateCommandBuffer(CommandBuffer*& commandBuffer)
{
    CommandBufferD3D12* commandBufferD3D12 = Allocate<CommandBufferD3D12>(m_Device.GetStdAllocator(), m_Device);
    const Result result = commandBufferD3D12->Create(m_CommandListType, m_CommandAllocator);

    if (result == Result::SUCCESS)
    {
        commandBuffer = (CommandBuffer*)commandBufferD3D12;
        return Result::SUCCESS;
    }

    Deallocate(m_Device.GetStdAllocator(), commandBufferD3D12);

    return result;
}

inline void CommandAllocatorD3D12::Reset()
{
    m_CommandAllocator->Reset();
}

#include "CommandAllocatorD3D12.hpp"
