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
#include "BufferVal.h"
#include "TextureVal.h"
#include "AccelerationStructureVal.h"
#include "MemoryVal.h"

using namespace nri;

MemoryVal::MemoryVal(DeviceVal& device, Memory& memory, uint64_t size, MemoryLocation memoryLocation) :
    DeviceObjectVal(device, memory),
    m_Size(size),
    m_MemoryLocation(memoryLocation)
{
}

MemoryVal::MemoryVal(DeviceVal& device, Memory& memory, const MemoryD3D12Desc& memoryD3D12Desc) :
    DeviceObjectVal(device, memory)
{
    m_Size = GetMemorySizeD3D12(memoryD3D12Desc);
}

bool MemoryVal::HasBoundResources()
{
    ExclusiveScope lockScope(m_Lock);
    return !m_Buffers.empty() || !m_Textures.empty() || !m_AccelerationStructures.empty();
}

void MemoryVal::ReportBoundResources()
{
    ExclusiveScope lockScope(m_Lock);

    for (size_t i = 0; i < m_Buffers.size(); i++)
    {
        BufferVal& buffer = *m_Buffers[i];
        REPORT_ERROR(m_Device.GetLog(), "Buffer (%p '%s') is still bound to the memory.",
            &buffer, buffer.GetDebugName().c_str());
    }

    for (size_t i = 0; i < m_Textures.size(); i++)
    {
        TextureVal& texture = *m_Textures[i];
        REPORT_ERROR(m_Device.GetLog(), "Texture (%p '%s') is still bound to the memory.",
            &texture, texture.GetDebugName().c_str());
    }

    for (size_t i = 0; i < m_AccelerationStructures.size(); i++)
    {
        AccelerationStructureVal& accelerationStructure = *m_AccelerationStructures[i];
        REPORT_ERROR(m_Device.GetLog(), "AccelerationStructure (%p '%s') is still bound to the memory.",
            &accelerationStructure, accelerationStructure.GetDebugName().c_str());
    }
}

void MemoryVal::BindBuffer(BufferVal& buffer)
{
    ExclusiveScope lockScope(m_Lock);
    m_Buffers.push_back(&buffer);
    buffer.SetBoundToMemory(*this);
}

void MemoryVal::BindTexture(TextureVal& texture)
{
    ExclusiveScope lockScope(m_Lock);
    m_Textures.push_back(&texture);
    texture.SetBoundToMemory(*this);
}

void MemoryVal::BindAccelerationStructure(AccelerationStructureVal& accelerationStructure)
{
    ExclusiveScope lockScope(m_Lock);
    m_AccelerationStructures.push_back(&accelerationStructure);
    accelerationStructure.SetBoundToMemory(*this);
}

void MemoryVal::UnbindBuffer(BufferVal& buffer)
{
    ExclusiveScope lockScope(m_Lock);

    const auto it = std::find(m_Buffers.begin(), m_Buffers.end(), &buffer);
    if (it == m_Buffers.end())
    {
        REPORT_ERROR(m_Device.GetLog(), "Unexpected error: Can't find the buffer in the list of bound resources.");
        return;
    }

    m_Buffers.erase(it);
}

void MemoryVal::UnbindTexture(TextureVal& texture)
{
    ExclusiveScope lockScope(m_Lock);

    const auto it = std::find(m_Textures.begin(), m_Textures.end(), &texture);
    if (it == m_Textures.end())
    {
        REPORT_ERROR(m_Device.GetLog(), "Unexpected error: Can't find the texture in the list of bound resources.");
        return;
    }

    m_Textures.erase(it);
}

void MemoryVal::UnbindAccelerationStructure(AccelerationStructureVal& accelerationStructure)
{
    ExclusiveScope lockScope(m_Lock);

    const auto it = std::find(m_AccelerationStructures.begin(), m_AccelerationStructures.end(), &accelerationStructure);
    if (it == m_AccelerationStructures.end())
    {
        REPORT_ERROR(m_Device.GetLog(), "Unexpected error: Can't find the acceleration structure in the list of bound resources.");
        return;
    }

    m_AccelerationStructures.erase(it);
}

void MemoryVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetMemoryDebugName(m_ImplObject, name);
}

#include "MemoryVal.hpp"
