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
#include "MemoryD3D11.h"

using namespace nri;

MemoryD3D11::MemoryD3D11(DeviceD3D11& device, MemoryType memoryType) :
    m_Location((MemoryLocation)memoryType),
    m_Device(device)
{
}

uint32_t MemoryD3D11::GetResidencyPriority(uint64_t size) const
{
    uint32_t high = 0;

    if (m_ResidencyPriority == MemoryResidencyPriority::MINIMUM)
        high = DXGI_RESOURCE_PRIORITY_MINIMUM;
    else if (m_ResidencyPriority == MemoryResidencyPriority::LOW)
        high = DXGI_RESOURCE_PRIORITY_LOW;
    else if (m_ResidencyPriority == MemoryResidencyPriority::NORMAL)
        high = DXGI_RESOURCE_PRIORITY_NORMAL;
    else if (m_ResidencyPriority == MemoryResidencyPriority::HIGH)
        high = DXGI_RESOURCE_PRIORITY_HIGH;
    else if (m_ResidencyPriority == MemoryResidencyPriority::MAXIMUM)
        high = DXGI_RESOURCE_PRIORITY_MAXIMUM;

    uint32_t low = high ? (uint32_t)(size / (1024 * 1024 * 10)) : 0;
    if (low > 0xFFFF)
        low = 0xFFFF;

    high |= low;

    return high;
}

void MemoryD3D11::SetDebugName(const char* name)
{
}

#include "MemoryD3D11.hpp"
