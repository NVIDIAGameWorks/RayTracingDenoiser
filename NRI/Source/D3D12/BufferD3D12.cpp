/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "BufferD3D12.h"
#include "DeviceD3D12.h"
#include "MemoryD3D12.h"

using namespace nri;

extern D3D12_RESOURCE_FLAGS GetBufferFlags(BufferUsageBits bufferUsageMask);

Result BufferD3D12::Create(const BufferDesc& bufferDesc)
{
    m_BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    m_BufferDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; // 64KB
    m_BufferDesc.Width = bufferDesc.size;
    m_BufferDesc.Height = 1;
    m_BufferDesc.DepthOrArraySize = 1;
    m_BufferDesc.MipLevels = 1;
    m_BufferDesc.SampleDesc.Count = 1;
    m_BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    m_BufferDesc.Flags = GetBufferFlags(bufferDesc.usageMask);

    m_StructureStride = bufferDesc.structureStride;

    return Result::SUCCESS;
}

Result BufferD3D12::Create(const BufferD3D12Desc& bufferDesc)
{
    m_StructureStride = bufferDesc.structureStride;
    Initialize((ID3D12Resource*)bufferDesc.d3d12Resource);
    return Result::SUCCESS;
}

void BufferD3D12::Initialize(ID3D12Resource* resource)
{
    m_Buffer = resource;
    m_BufferDesc = resource->GetDesc();
}

Result BufferD3D12::BindMemory(const MemoryD3D12* memory, uint64_t offset, bool isAccelerationStructureBuffer)
{
    const D3D12_HEAP_DESC& heapDesc = memory->GetHeapDesc();
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

    if (heapDesc.Properties.Type == D3D12_HEAP_TYPE_UPLOAD)
        initialState |= D3D12_RESOURCE_STATE_GENERIC_READ;
    else if (heapDesc.Properties.Type == D3D12_HEAP_TYPE_READBACK)
        initialState |= D3D12_RESOURCE_STATE_COPY_DEST;

#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
    if (isAccelerationStructureBuffer)
        initialState |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
#endif

    if (memory->RequiresDedicatedAllocation())
    {
        HRESULT hr = ((ID3D12Device*)m_Device)->CreateCommittedResource(
            &heapDesc.Properties,
            D3D12_HEAP_FLAG_NONE,
            &m_BufferDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(&m_Buffer)
        );

        if (FAILED(hr))
        {
            REPORT_ERROR(m_Device.GetLog(), "ID3D12Device::CreateCommittedResource() failed, error code: 0x%X.", hr);
            return Result::FAILURE;
        }
    }
    else
    {
        HRESULT hr = ((ID3D12Device*)m_Device)->CreatePlacedResource(
            *memory,
            offset,
            &m_BufferDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(&m_Buffer)
        );

        if (FAILED(hr))
        {
            REPORT_ERROR(m_Device.GetLog(), "ID3D12Device::CreatePlacedResource() failed, error code: 0x%X.", hr);
            return Result::FAILURE;
        }
    }

    return Result::SUCCESS;
}

inline void BufferD3D12::SetDebugName(const char* name)
{
    SET_D3D_DEBUG_OBJECT_NAME(m_Buffer, name);
}

inline void BufferD3D12::GetMemoryInfo(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const
{
    m_Device.GetMemoryInfo(memoryLocation, m_BufferDesc, memoryDesc);
}

inline void* BufferD3D12::Map(uint64_t offset, uint64_t size)
{
    uint8_t* data = nullptr;

    if (size == WHOLE_SIZE)
        size =  m_BufferDesc.Width;

    D3D12_RANGE range = {offset, offset + size};
    HRESULT hr = m_Buffer->Map(0, &range, (void**)&data);
    if (FAILED(hr))
        REPORT_ERROR(m_Device.GetLog(), "ID3D12Resource::Map() failed, error code: 0x%X.", hr);

    return data + offset;
}

inline void BufferD3D12::Unmap()
{
    m_Buffer->Unmap(0, nullptr);
}

#include "BufferD3D12.hpp"
