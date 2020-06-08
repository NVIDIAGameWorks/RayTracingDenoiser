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
#include "DeviceSemaphoreD3D11.h"

using namespace nri;

DeviceSemaphoreD3D11::DeviceSemaphoreD3D11(DeviceD3D11& device, const VersionedDevice& versionedDevice) :
    m_VersionedDevice(versionedDevice),
    m_Device(device)
{
}

DeviceSemaphoreD3D11::~DeviceSemaphoreD3D11()
{
    if (m_Handle)
        CloseHandle(m_Handle);
}

Result DeviceSemaphoreD3D11::Create(bool signaled)
{
    HRESULT hr = E_INVALIDARG;
    const char* message = "";

    m_IgnoreWait = signaled;

    if (m_VersionedDevice.version >= 5)
    {
        m_Handle = CreateEvent(nullptr, FALSE, 0, nullptr);
        RETURN_ON_FAILURE(m_Device.GetLog(), m_Handle, Result::FAILURE, "CreateEvent() - FAILED!");

        hr = m_VersionedDevice->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
        message = "ID3D11Device5::CreateFence() - FAILED!";
    }
    else if (m_VersionedDevice.version >= 3)
    {
        D3D11_QUERY_DESC1 queryDesc = {};
        queryDesc.Query = D3D11_QUERY_EVENT;
        queryDesc.ContextType = D3D11_CONTEXT_TYPE_ALL;

        hr = m_VersionedDevice->CreateQuery1(&queryDesc, &m_Query);
        message = "ID3D11Device3::CreateQuery1() - FAILED!";
    }
    else
    {
        D3D11_QUERY_DESC queryDesc = {};
        queryDesc.Query = D3D11_QUERY_EVENT;

        hr = m_VersionedDevice->CreateQuery(&queryDesc, (ID3D11Query**)&m_Query);
        message = "ID3D11Device::CreateQuery() - FAILED!";
    }

    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, message);

    return Result::SUCCESS;
}

void DeviceSemaphoreD3D11::Signal(const VersionedContext& immediateContext)
{
    m_IgnoreWait = false;
    m_Value++;

    if (m_Fence)
        immediateContext->Signal(m_Fence, m_Value);
    else
        immediateContext->End(m_Query);
}

void DeviceSemaphoreD3D11::Wait(const VersionedContext& immediateContext)
{
    if (m_IgnoreWait)
        return;

    HRESULT hr;

    if (m_Fence)
    {
        hr = m_Fence->SetEventOnCompletion(m_Value, m_Handle);
        CHECK(m_Device.GetLog(), hr == S_OK, "D3D11Fence::SetEventOnCompletion() - FAILED!");

        WaitForSingleObject(m_Handle, INFINITE);
    }
    else
    {
        while (true)
        {
            hr = immediateContext->GetData(m_Query, nullptr, 0, 0);
            if (hr != S_FALSE)
                break;
            Sleep(0);
        }

        CHECK(m_Device.GetLog(), hr == S_OK, "D3D11DeviceContext::GetData() - FAILED!");
    }

    m_Value++;
}

void DeviceSemaphoreD3D11::SetDebugName(const char* name)
{
    SetName(m_Fence, name);
    SetName(m_Query, name);
}

#include "DeviceSemaphoreD3D11.hpp"
