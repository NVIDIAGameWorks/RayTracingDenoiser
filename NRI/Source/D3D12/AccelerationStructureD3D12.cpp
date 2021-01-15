/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"

#include "AccelerationStructureD3D12.h"
#include "DeviceD3D12.h"
#include "BufferD3D12.h"
#include "MemoryD3D12.h"

#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__

using namespace nri;

extern D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE GetAccelerationStructureType(AccelerationStructureType accelerationStructureType);
extern D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS GetAccelerationStructureBuildFlags(AccelerationStructureBuildBits accelerationStructureBuildFlags);
extern void ConvertGeometryDescs(D3D12_RAYTRACING_GEOMETRY_DESC* geometryDescs, const GeometryObject* geometryObjects, uint32_t geometryObjectNum);
extern DXGI_FORMAT GetFormat(Format format);
extern MemoryType GetMemoryType(D3D12_HEAP_TYPE heapType, D3D12_HEAP_FLAGS heapFlags);

AccelerationStructureD3D12::~AccelerationStructureD3D12()
{
    if (m_Buffer)
        Deallocate(m_Device.GetStdAllocator(), m_Buffer);
}

Result AccelerationStructureD3D12::Create(const AccelerationStructureDesc& accelerationStructureDesc)
{
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS accelerationStructureInputs = {};
    accelerationStructureInputs.Type = GetAccelerationStructureType(accelerationStructureDesc.type);
    accelerationStructureInputs.Flags = GetAccelerationStructureBuildFlags(accelerationStructureDesc.flags);
    accelerationStructureInputs.NumDescs = accelerationStructureDesc.instanceOrGeometryObjectNum;
    accelerationStructureInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; // TODO:

    Vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(accelerationStructureDesc.instanceOrGeometryObjectNum, m_Device.GetStdAllocator());
    if (accelerationStructureInputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL && accelerationStructureDesc.instanceOrGeometryObjectNum)
    {
        ConvertGeometryDescs(&geometryDescs[0], accelerationStructureDesc.geometryObjects, accelerationStructureDesc.instanceOrGeometryObjectNum);
        accelerationStructureInputs.pGeometryDescs = &geometryDescs[0];
    }

    ((ID3D12Device5*)m_Device)->GetRaytracingAccelerationStructurePrebuildInfo(&accelerationStructureInputs, &m_PrebuildInfo);

    BufferDesc bufferDesc = {};
    bufferDesc.size = m_PrebuildInfo.ResultDataMaxSizeInBytes;
    bufferDesc.usageMask = BufferUsageBits::SHADER_RESOURCE_STORAGE;

    m_Buffer = Allocate<BufferD3D12>(m_Device.GetStdAllocator(), m_Device);
    Result result = m_Buffer->Create(bufferDesc);

    return result;
}

void AccelerationStructureD3D12::GetMemoryInfo(MemoryDesc& memoryDesc) const
{
    memoryDesc.size = m_PrebuildInfo.ResultDataMaxSizeInBytes;
    memoryDesc.alignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
    memoryDesc.type = GetMemoryType(D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
    memoryDesc.mustBeDedicated = false;
}

uint64_t AccelerationStructureD3D12::GetUpdateScratchBufferSize() const
{
    return m_PrebuildInfo.UpdateScratchDataSizeInBytes;
}

uint64_t AccelerationStructureD3D12::GetBuildScratchBufferSize() const
{
    return m_PrebuildInfo.ScratchDataSizeInBytes;
}

Result AccelerationStructureD3D12::BindMemory(Memory* memory, uint64_t offset)
{
    Result result = m_Buffer->BindMemory((MemoryD3D12*)memory, offset, true);

    return result;
}

Result AccelerationStructureD3D12::CreateDescriptor(Descriptor*& descriptor) const
{
    const AccelerationStructure& accelerationStructure = (const AccelerationStructure&)*this;
    Result result = m_Device.CreateDescriptor(accelerationStructure, descriptor);

    return result;
}

uint64_t AccelerationStructureD3D12::GetHandle() const
{
    return m_Buffer->GetPointerGPU();
}

inline void AccelerationStructureD3D12::SetDebugName(const char* name)
{}

#include "AccelerationStructureD3D12.hpp"

#endif
