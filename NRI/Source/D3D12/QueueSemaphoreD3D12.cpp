/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "QueueSemaphoreD3D12.h"
#include "DeviceD3D12.h"

using namespace nri;

Result QueueSemaphoreD3D12::Create()
{
    HRESULT hr = ((ID3D12Device*)m_Device)->CreateFence(m_SignalValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12Device::CreateFence() failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    m_SignalValue++;

    return Result::SUCCESS;
}

void QueueSemaphoreD3D12::Signal(ID3D12CommandQueue* commandQueue)
{
    commandQueue->Signal(m_Fence, m_SignalValue);
}

void QueueSemaphoreD3D12::Wait(ID3D12CommandQueue* commandQueue)
{
    commandQueue->Wait(m_Fence, m_SignalValue);
    m_SignalValue++;
}

void QueueSemaphoreD3D12::SetDebugName(const char* name)
{
    SET_D3D_DEBUG_OBJECT_NAME(m_Fence, name);
}
