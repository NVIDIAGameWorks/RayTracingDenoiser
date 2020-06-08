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
#include "QueueSemaphoreVal.h"

using namespace nri;

QueueSemaphoreVal::QueueSemaphoreVal(DeviceVal& device, QueueSemaphore& queueSemaphore) :
    DeviceObjectVal(device, queueSemaphore)
{
}

void QueueSemaphoreVal::Signal()
{
    if (m_isSignaled)
        REPORT_ERROR(m_Device.GetLog(), "Can't signal QueueSemaphore: it's already in signaled state.");

    m_isSignaled = true;
}

void QueueSemaphoreVal::Wait()
{
    if (!m_isSignaled)
        REPORT_ERROR(m_Device.GetLog(), "Can't wait for QueueSemaphore: it's already in unsignaled state.");

    m_isSignaled = false;
}

void QueueSemaphoreVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetQueueSemaphoreDebugName(m_ImplObject, name);
}

#include "QueueSemaphoreVal.hpp"
