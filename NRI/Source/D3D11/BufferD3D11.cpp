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
#include "BufferD3D11.h"

#include "MemoryD3D11.h"
#include "QueryPoolD3D11.h"
#include "TextureD3D11.h"

using namespace nri;

BufferD3D11::BufferD3D11(DeviceD3D11& device, const VersionedContext& immediateContext, const BufferDesc& bufferDesc) :
    m_ImmediateContext(immediateContext),
    m_Desc(bufferDesc),
    m_Device(device)
{
}

BufferD3D11::BufferD3D11(DeviceD3D11& device, const VersionedContext& immediateContext) :
    m_ImmediateContext(immediateContext),
    m_Device(device)
{
}

BufferD3D11::~BufferD3D11()
{
    if (m_ReadbackTexture)
        Deallocate(m_Device.GetStdAllocator(), m_ReadbackTexture);
}

Result BufferD3D11::Create(const VersionedDevice& device, const MemoryD3D11& memory)
{
    MemoryLocation memoryLocation = memory.GetType();

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (uint32_t)m_Desc.size;
    desc.StructureByteStride = m_Desc.structureStride;

    if (m_Desc.structureStride)
        desc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    if (m_Desc.usageMask & BufferUsageBits::ARGUMENT_BUFFER)
        desc.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;

    if (memoryLocation == MemoryLocation::HOST_UPLOAD)
    {
        if (m_Desc.usageMask == BufferUsageBits::NONE)
        {
            m_Type = BufferType::UPLOAD;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        }
        else
        {
            m_Type = BufferType::DYNAMIC;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
    }
    else if (memoryLocation == MemoryLocation::HOST_READBACK)
    {
        m_Type = BufferType::READBACK;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    }
    else
    {
        m_Type = BufferType::DEVICE;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;
    }

    if (m_Desc.usageMask & BufferUsageBits::VERTEX_BUFFER)
        desc.BindFlags |= D3D11_BIND_VERTEX_BUFFER;

    if (m_Desc.usageMask & BufferUsageBits::INDEX_BUFFER)
        desc.BindFlags |= D3D11_BIND_INDEX_BUFFER;

    if (m_Desc.usageMask & BufferUsageBits::CONSTANT_BUFFER)
        desc.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;

    if (m_Desc.usageMask & BufferUsageBits::SHADER_RESOURCE)
        desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    if (m_Desc.usageMask & BufferUsageBits::SHADER_RESOURCE_STORAGE)
        desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_Buffer);
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreateBuffer() - FAILED!");

    uint32_t priority = memory.GetResidencyPriority(m_Desc.size);
    if (priority != 0)
        m_Buffer->SetEvictionPriority(priority);

    return Result::SUCCESS;
}

Result BufferD3D11::Create(const BufferD3D11Desc& bufferDesc)
{
    ID3D11Resource* resource = (ID3D11Resource*)bufferDesc.d3d11Resource;
    if (!resource)
        return Result::INVALID_ARGUMENT;

    D3D11_RESOURCE_DIMENSION type;
    resource->GetType(&type);

    if (type != D3D11_RESOURCE_DIMENSION_BUFFER)
        return Result::INVALID_ARGUMENT;

    ID3D11Buffer* buffer = (ID3D11Buffer*)resource;
    m_Buffer = buffer;

    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);

    m_Desc.size = desc.ByteWidth;
    m_Desc.structureStride = desc.StructureByteStride;

    if (desc.BindFlags & D3D11_BIND_VERTEX_BUFFER)
        m_Desc.usageMask |= BufferUsageBits::VERTEX_BUFFER;

    if (desc.BindFlags & D3D11_BIND_INDEX_BUFFER)
        m_Desc.usageMask |= BufferUsageBits::INDEX_BUFFER;

    if (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER)
        m_Desc.usageMask |= BufferUsageBits::CONSTANT_BUFFER;

    if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
        m_Desc.usageMask |= BufferUsageBits::SHADER_RESOURCE;

    if (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
        m_Desc.usageMask |= BufferUsageBits::SHADER_RESOURCE_STORAGE;

    if (desc.MiscFlags & D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS)
        m_Desc.usageMask |= BufferUsageBits::ARGUMENT_BUFFER;

    if (desc.Usage == D3D11_USAGE_STAGING)
        m_Type = desc.CPUAccessFlags == (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE) ? BufferType::UPLOAD : BufferType::READBACK;
    else if (desc.Usage == D3D11_USAGE_DYNAMIC)
        m_Type = BufferType::DYNAMIC;
    else
        m_Type = BufferType::DEVICE;

    return Result::SUCCESS;
}

void* BufferD3D11::Map(MapType mapType, uint64_t offset)
{
    CriticalSection criticalSection(m_ImmediateContext);

    FinalizeQueries();
    FinalizeReadback();

    D3D11_MAP map = D3D11_MAP_READ;
    if (m_Type == BufferType::DYNAMIC)
        map = D3D11_MAP_WRITE_NO_OVERWRITE;
    else if (m_Type == BufferType::UPLOAD && mapType == MapType::DEFAULT)
        map = D3D11_MAP_WRITE;

    D3D11_MAPPED_SUBRESOURCE mappedData = {};
    HRESULT hr = m_ImmediateContext->Map(m_Buffer, 0, map, 0, &mappedData);
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D11DeviceContext::Map() - FAILED!");
        return nullptr;
    }

    uint8_t* ptr = (uint8_t*)mappedData.pData;

    return ptr + offset;
}

void BufferD3D11::FinalizeQueries()
{
    if (!m_QueryRange.pool)
        return;

    D3D11_MAPPED_SUBRESOURCE mappedData = {};
    HRESULT hr = m_ImmediateContext->Map(m_Buffer, 0, D3D11_MAP_WRITE, 0, &mappedData);
    if (SUCCEEDED(hr))
    {
        uint8_t* ptr = (uint8_t*)mappedData.pData;
        ptr += m_QueryRange.bufferOffset;

        m_QueryRange.pool->GetData(ptr, m_ImmediateContext, m_QueryRange.offset, m_QueryRange.num);

        m_ImmediateContext->Unmap(m_Buffer, 0);
    }
    else
        REPORT_ERROR(m_Device.GetLog(), "ID3D11DeviceContext::Map() - FAILED!");

    m_QueryRange.pool = nullptr;
}

void BufferD3D11::FinalizeReadback()
{
    if (!m_IsReadbackDataChanged)
        return;

    m_IsReadbackDataChanged = false;

    D3D11_MAPPED_SUBRESOURCE srcData = {};
    HRESULT hr = m_ImmediateContext->Map(*m_ReadbackTexture, 0, D3D11_MAP_READ, 0, &srcData);
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D11DeviceContext::Map() - FAILED!");
        return;
    }

    D3D11_MAPPED_SUBRESOURCE dstData = {};
    hr = m_ImmediateContext->Map(m_Buffer, 0, D3D11_MAP_WRITE, 0, &dstData);
    if (FAILED(hr))
    {
        m_ImmediateContext->Unmap(*m_ReadbackTexture, 0);
        REPORT_ERROR(m_Device.GetLog(), "ID3D11DeviceContext::Map() - FAILED!");
        return;
    }

    const uint32_t d = m_ReadbackTexture->GetDesc().size[2];
    const uint32_t h = m_ReadbackTexture->GetDesc().size[1];
    const uint8_t* src = (uint8_t*)srcData.pData;
    uint8_t* dst = (uint8_t*)dstData.pData;
    for (uint32_t i = 0; i < d; i++)
    {
        for (uint32_t j = 0; j < h; j++)
        {
            const uint8_t* s = src + j * srcData.RowPitch;
            uint8_t* dstLocal = dst + j * m_ReadbackDataLayoutDesc.rowPitch;
            memcpy(dstLocal, s, srcData.RowPitch);
        }
        src += srcData.DepthPitch;
        dst += m_ReadbackDataLayoutDesc.slicePitch;
    }


    m_ImmediateContext->Unmap(m_Buffer, 0);
    m_ImmediateContext->Unmap(*m_ReadbackTexture, 0);
}

TextureD3D11& BufferD3D11::RecreateReadbackTexture(const VersionedDevice& device, const TextureD3D11& srcTexture, const TextureRegionDesc& srcRegionDesc, const TextureDataLayoutDesc& readbackDataLayoutDesc)
{
    bool isChanged = true;
    if (m_ReadbackTexture)
    {
        const TextureDesc& curr = m_ReadbackTexture->GetDesc();
        isChanged = curr.format != srcTexture.GetDesc().format ||
            curr.size[0] != srcRegionDesc.size[0] ||
            curr.size[1] != srcRegionDesc.size[1] ||
            curr.size[2] != srcRegionDesc.size[2];
    }

    if (isChanged)
    {
        TextureDesc textureDesc = {};
        textureDesc.mipNum = 1;
        textureDesc.sampleNum = 1;
        textureDesc.arraySize = 1;
        textureDesc.format = srcTexture.GetDesc().format;
        textureDesc.size[0] = srcRegionDesc.size[0];
        textureDesc.size[1] = srcRegionDesc.size[1];
        textureDesc.size[2] = srcRegionDesc.size[2];

        textureDesc.type = TextureType::TEXTURE_2D;
        if (srcRegionDesc.size[2] > 1)
            textureDesc.type = TextureType::TEXTURE_3D;
        else if (srcRegionDesc.size[1] == 1)
            textureDesc.type = TextureType::TEXTURE_1D;

        if (m_ReadbackTexture)
            Deallocate(m_Device.GetStdAllocator(), m_ReadbackTexture);

        m_ReadbackTexture = Allocate<TextureD3D11>(m_Device.GetStdAllocator(), m_Device, textureDesc);
        m_ReadbackTexture->Create(device, nullptr);
    }

    m_IsReadbackDataChanged = true;
    m_ReadbackDataLayoutDesc = readbackDataLayoutDesc;

    return *m_ReadbackTexture;
}

inline void BufferD3D11::SetDebugName(const char* name)
{
    SetName(m_Buffer, name);
}

inline void BufferD3D11::GetMemoryInfo(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const
{
    const bool isConstantBuffer = (m_Desc.usageMask & BufferUsageBits::CONSTANT_BUFFER) == (uint32_t)BufferUsageBits::CONSTANT_BUFFER;

    uint32_t alignment = 65536;
    if (isConstantBuffer)
        alignment = 256;
    else if (m_Desc.size <= 4096)
        alignment = 4096;

    uint64_t size = GetAlignedSize(m_Desc.size, alignment);

    memoryDesc.type = (MemoryType)memoryLocation;
    memoryDesc.size = size;
    memoryDesc.alignment = alignment;
    memoryDesc.mustBeDedicated = false;
}

void* BufferD3D11::Map(uint64_t offset, uint64_t size)
{
    return Map(MapType::DEFAULT, offset);
}

void BufferD3D11::Unmap()
{
    CriticalSection criticalSection(m_ImmediateContext);

    m_ImmediateContext->Unmap(m_Buffer, 0);
}

#include "BufferD3D11.hpp"
