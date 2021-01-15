/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "MemoryD3D12.h"
#include "DeviceD3D12.h"

using namespace nri;

extern D3D12_HEAP_TYPE GetHeapType(MemoryType memoryType);
extern D3D12_HEAP_FLAGS GetHeapFlags(MemoryType memoryType);
extern bool RequiresDedicatedAllocation(MemoryType memoryType);

Result MemoryD3D12::Create(const MemoryType memoryType, uint64_t size)
{
    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.SizeInBytes = size;
    heapDesc.Properties.Type = GetHeapType(memoryType);
    heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapDesc.Properties.CreationNodeMask = NRI_TEMP_NODE_MASK;
    heapDesc.Properties.VisibleNodeMask = NRI_TEMP_NODE_MASK;
    heapDesc.Alignment = 0;
    heapDesc.Flags = size ? GetHeapFlags(memoryType) : D3D12_HEAP_FLAG_NONE;

    if (!::RequiresDedicatedAllocation(memoryType))
    {
        HRESULT hr = ((ID3D12Device*)m_Device)->CreateHeap(&heapDesc, IID_PPV_ARGS(&m_Heap));
        if (FAILED(hr))
        {
            REPORT_ERROR(m_Device.GetLog(), "ID3D12Device::CreateHeap() failed, error code: 0x%X.", hr);
            return Result::FAILURE;
        }
    }

    m_HeapDesc = heapDesc;

    return Result::SUCCESS;
}

Result MemoryD3D12::Create(const MemoryD3D12Desc& memoryDesc)
{
    m_Heap = (ID3D12Heap*)memoryDesc.d3d12Heap;
    m_HeapDesc = m_Heap->GetDesc();

    return Result::SUCCESS;
}

void MemoryD3D12::SetDebugName(const char* name)
{
    SET_D3D_DEBUG_OBJECT_NAME(m_Heap, name);
}
