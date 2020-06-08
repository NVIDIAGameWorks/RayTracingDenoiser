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
#include "DeviceSemaphoreVal.h"

using namespace nri;

DeviceSemaphoreVal::DeviceSemaphoreVal(DeviceVal& device, DeviceSemaphore& deviceSemaphore) :
    DeviceObjectVal(device, deviceSemaphore)
{
}

void DeviceSemaphoreVal::Create(bool signaled)
{
    m_Value = signaled ? 1 : 0;
}

void DeviceSemaphoreVal::Signal()
{
    if (!IsUnsignaled())
        REPORT_ERROR(m_Device.GetLog(), "Semaphore is already in signaled state!");

    m_Value++;
}

void DeviceSemaphoreVal::Wait()
{
    if (IsUnsignaled())
        REPORT_ERROR(m_Device.GetLog(), "Semaphore is already in unsignaled state!");

    m_Value++;
}

void DeviceSemaphoreVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetDeviceSemaphoreDebugName(m_ImplObject, name);
}

#include "DeviceSemaphoreVal.hpp"
