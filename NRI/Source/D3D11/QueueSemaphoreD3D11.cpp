/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedD3D11.h"
#include "QueueSemaphoreD3D11.h"

using namespace nri;

QueueSemaphoreD3D11::QueueSemaphoreD3D11(DeviceD3D11& device) :
    m_Device(device)
{
}

QueueSemaphoreD3D11::~QueueSemaphoreD3D11()
{
}

void QueueSemaphoreD3D11::SetDebugName(const char* name)
{
}

#include "QueueSemaphoreD3D11.hpp"
