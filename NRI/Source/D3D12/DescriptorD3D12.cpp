/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "DescriptorD3D12.h"
#include "BufferD3D12.h"
#include "TextureD3D12.h"
#include "AccelerationStructureD3D12.h"

using namespace nri;

extern DXGI_FORMAT GetFormat(Format format);
extern bool IsFloatingPointFormat(Format format);
extern D3D12_FILTER GetFilterIsotropic(Filter mip, Filter magnification, Filter minification, FilterExt filterExt, bool useComparison);
extern D3D12_FILTER GetFilterAnisotropic(FilterExt filterExt, bool useComparison);
extern D3D12_TEXTURE_ADDRESS_MODE GetAddressMode(AddressMode addressMode);
extern D3D12_COMPARISON_FUNC GetComparisonFunc(CompareFunc compareFunc);
extern DXGI_FORMAT GetShaderFormatForDepth(DXGI_FORMAT format);

Result DescriptorD3D12::Create(const BufferViewDesc& bufferViewDesc)
{
    const BufferD3D12& buffer = *((BufferD3D12*)bufferViewDesc.buffer);
    DXGI_FORMAT format = GetFormat(bufferViewDesc.format);
    uint64_t size = bufferViewDesc.size == WHOLE_SIZE ? buffer.GetByteSize() : bufferViewDesc.size;
    uint32_t elementSize = GetTexelBlockSize(bufferViewDesc.format);
    uint64_t elementOffset;
    uint32_t elementNum;


    uint32_t structureStride = buffer.GetStructureStride();
    if (structureStride) // structured buffer
    {
        elementOffset = bufferViewDesc.offset / structureStride;
        elementNum = (uint32_t)(size / structureStride);
    }
    else
    {
        elementOffset = bufferViewDesc.offset / elementSize;
        elementNum = (uint32_t)(size / elementSize);
    }

    switch (bufferViewDesc.viewType)
    {
    case BufferViewType::CONSTANT:
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC desc;
        desc.BufferLocation = buffer.GetPointerGPU() + bufferViewDesc.offset;
        desc.SizeInBytes = (uint32_t)size;

        return CreateConstantBufferView(desc);
    }
    case BufferViewType::SHADER_RESOURCE:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.FirstElement = elementOffset;
        desc.Buffer.NumElements = elementNum;
        desc.Buffer.StructureByteStride = structureStride;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        return CreateShaderResourceView(buffer, desc);
    }
    case BufferViewType::SHADER_RESOURCE_STORAGE:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = elementOffset;
        desc.Buffer.NumElements = elementNum;
        desc.Buffer.StructureByteStride = structureStride;
        desc.Buffer.CounterOffsetInBytes = 0;
        desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        return CreateUnorderedAccessView(buffer, desc, bufferViewDesc.format);
    }
    }

    return Result::FAILURE;
}

Result DescriptorD3D12::Create(const Texture1DViewDesc& textureViewDesc)
{
    const TextureD3D12& texture = (TextureD3D12&)*textureViewDesc.texture;
    DXGI_FORMAT format = GetFormat(textureViewDesc.format);

    const D3D12_RESOURCE_DESC& textureDesc = texture.GetTextureDesc();
    uint32_t remainingMipLevels = textureViewDesc.mipNum == REMAINING_MIP_LEVELS ? (textureDesc.MipLevels - textureViewDesc.mipOffset) : textureViewDesc.mipNum;
    uint32_t remainingArrayLayers = textureViewDesc.arraySize == REMAINING_ARRAY_LAYERS ? (textureDesc.DepthOrArraySize - textureViewDesc.arrayOffset) : textureViewDesc.arraySize;

    switch (textureViewDesc.viewType)
    {
    case Texture1DViewType::SHADER_RESOURCE_1D:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture1D.MostDetailedMip = textureViewDesc.mipOffset;
        desc.Texture1D.MipLevels = remainingMipLevels;
        desc.Texture1D.ResourceMinLODClamp = 0;

        return CreateShaderResourceView(texture, desc);
    }
    case Texture1DViewType::SHADER_RESOURCE_1D_ARRAY:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture1DArray.MostDetailedMip = textureViewDesc.mipOffset;
        desc.Texture1DArray.MipLevels = remainingMipLevels;
        desc.Texture1DArray.FirstArraySlice = textureViewDesc.arrayOffset;
        desc.Texture1DArray.ArraySize = remainingArrayLayers;
        desc.Texture1DArray.ResourceMinLODClamp = 0;

        return CreateShaderResourceView(texture, desc);
    }
    case Texture1DViewType::SHADER_RESOURCE_STORAGE_1D:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
        desc.Texture1D.MipSlice = textureViewDesc.mipOffset;

        return CreateUnorderedAccessView(texture, desc, textureViewDesc.format);
    }
    case Texture1DViewType::SHADER_RESOURCE_STORAGE_1D_ARRAY:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
        desc.Texture1DArray.MipSlice = textureViewDesc.mipOffset;
        desc.Texture1DArray.FirstArraySlice = textureViewDesc.arrayOffset;
        desc.Texture1DArray.ArraySize = remainingArrayLayers;

        return CreateUnorderedAccessView(texture, desc, textureViewDesc.format);
    }
    case Texture1DViewType::COLOR_ATTACHMENT:
    {
        D3D12_RENDER_TARGET_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
        desc.Texture1DArray.MipSlice = textureViewDesc.mipOffset;
        desc.Texture1DArray.FirstArraySlice = textureViewDesc.arrayOffset;
        desc.Texture1DArray.ArraySize = remainingArrayLayers;

        return CreateRenderTargetView(texture, desc);
    }
    case Texture1DViewType::DEPTH_STENCIL_ATTACHMENT:
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
        desc.Flags = D3D12_DSV_FLAG_NONE;
        desc.Texture1DArray.MipSlice = textureViewDesc.mipOffset;
        desc.Texture1DArray.FirstArraySlice = textureViewDesc.arrayOffset;
        desc.Texture1DArray.ArraySize = remainingArrayLayers;

        if (textureViewDesc.flags & ResourceViewBits::READONLY_DEPTH)
            desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        if (textureViewDesc.flags & ResourceViewBits::READONLY_STENCIL)
            desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;

        return CreateDepthStencilView(texture, desc);
    }
    }

    return Result::FAILURE;
}

Result DescriptorD3D12::Create(const Texture2DViewDesc& textureViewDesc)
{
    const TextureD3D12& texture = (TextureD3D12&)*textureViewDesc.texture;
    DXGI_FORMAT format = GetFormat(textureViewDesc.format);
    bool isMultisampled = texture.GetTextureDesc().SampleDesc.Count > 1 ? true : false;

    const D3D12_RESOURCE_DESC& textureDesc = texture.GetTextureDesc();
    uint32_t remainingMipLevels = textureViewDesc.mipNum == REMAINING_MIP_LEVELS ? (textureDesc.MipLevels - textureViewDesc.mipOffset) : textureViewDesc.mipNum;
    uint32_t remainingArrayLayers = textureViewDesc.arraySize == REMAINING_ARRAY_LAYERS ? (textureDesc.DepthOrArraySize - textureViewDesc.arrayOffset) : textureViewDesc.arraySize;

    switch (textureViewDesc.viewType)
    {
    case Texture2DViewType::SHADER_RESOURCE_2D:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = GetShaderFormatForDepth(format);
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (isMultisampled)
        {
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        }
        else
        {
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MostDetailedMip = textureViewDesc.mipOffset;
            desc.Texture2D.MipLevels = remainingMipLevels;
            desc.Texture2D.PlaneSlice = 0;
            desc.Texture2D.ResourceMinLODClamp = 0;
        }

        return CreateShaderResourceView(texture, desc);
    }
    case Texture2DViewType::SHADER_RESOURCE_2D_ARRAY:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = format;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (isMultisampled)
        {
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
            desc.Texture2DMSArray.FirstArraySlice = textureViewDesc.arrayOffset;
            desc.Texture2DMSArray.ArraySize = remainingArrayLayers;
        }
        else
        {
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MostDetailedMip = textureViewDesc.mipOffset;
            desc.Texture2DArray.MipLevels = remainingMipLevels;
            desc.Texture2DArray.FirstArraySlice = textureViewDesc.arrayOffset;
            desc.Texture2DArray.ArraySize = remainingArrayLayers;
            desc.Texture2D.PlaneSlice = 0;
            desc.Texture2DArray.ResourceMinLODClamp = 0;
        }

        return CreateShaderResourceView(texture, desc);
    }
    case Texture2DViewType::SHADER_RESOURCE_CUBE:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = format;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MostDetailedMip = textureViewDesc.mipOffset;
        desc.TextureCube.MipLevels = remainingMipLevels;

        return CreateShaderResourceView(texture, desc);
    }
    case Texture2DViewType::SHADER_RESOURCE_CUBE_ARRAY:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = format;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        desc.TextureCubeArray.MostDetailedMip = textureViewDesc.mipOffset;
        desc.TextureCubeArray.MipLevels = remainingMipLevels;
        desc.TextureCubeArray.First2DArrayFace = textureViewDesc.arrayOffset;
        desc.TextureCubeArray.NumCubes = remainingArrayLayers / 6;

        return CreateShaderResourceView(texture, desc);
    }
    case Texture2DViewType::SHADER_RESOURCE_STORAGE_2D:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = textureViewDesc.mipOffset;
        desc.Texture2D.PlaneSlice = 0;

        return CreateUnorderedAccessView(texture, desc, textureViewDesc.format);
    }
    case Texture2DViewType::SHADER_RESOURCE_STORAGE_2D_ARRAY:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice = textureViewDesc.mipOffset;
        desc.Texture2DArray.FirstArraySlice = textureViewDesc.arrayOffset;
        desc.Texture2DArray.ArraySize = remainingArrayLayers;
        desc.Texture2DArray.PlaneSlice = 0;

        return CreateUnorderedAccessView(texture, desc, textureViewDesc.format);
    }
    case Texture2DViewType::COLOR_ATTACHMENT:
    {
        D3D12_RENDER_TARGET_VIEW_DESC desc = {};
        desc.Format = format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice = textureViewDesc.mipOffset;
        desc.Texture2DArray.FirstArraySlice = textureViewDesc.arrayOffset;
        desc.Texture2DArray.ArraySize = remainingArrayLayers;
        desc.Texture2DArray.PlaneSlice = 0;

        return CreateRenderTargetView(texture, desc);
    }
    case Texture2DViewType::DEPTH_STENCIL_ATTACHMENT:
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
        desc.Format = format;
        desc.Flags = D3D12_DSV_FLAG_NONE;
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice = textureViewDesc.mipOffset;
        desc.Texture2DArray.FirstArraySlice = textureViewDesc.arrayOffset;
        desc.Texture2DArray.ArraySize = remainingArrayLayers;

        if (textureViewDesc.flags & ResourceViewBits::READONLY_DEPTH)
            desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        if (textureViewDesc.flags & ResourceViewBits::READONLY_STENCIL)
            desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;

        return CreateDepthStencilView(texture, desc);
    }
    }

    return Result::FAILURE;
}

Result DescriptorD3D12::Create(const Texture3DViewDesc& textureViewDesc)
{
    const TextureD3D12& texture = (TextureD3D12&)*textureViewDesc.texture;
    DXGI_FORMAT format = GetFormat(textureViewDesc.format);

    const D3D12_RESOURCE_DESC& textureDesc = texture.GetTextureDesc();
    uint32_t remainingMipLevels = textureViewDesc.mipNum == REMAINING_MIP_LEVELS ? (textureDesc.MipLevels - textureViewDesc.mipOffset) : textureViewDesc.mipNum;

    switch (textureViewDesc.viewType)
    {
    case Texture3DViewType::SHADER_RESOURCE_3D:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture3D.MostDetailedMip = textureViewDesc.mipOffset;
        desc.Texture3D.MipLevels = remainingMipLevels;
        desc.Texture3D.ResourceMinLODClamp = 0;

        return CreateShaderResourceView(texture, desc);
    }
    case Texture3DViewType::SHADER_RESOURCE_STORAGE_3D:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = textureViewDesc.mipOffset;
        desc.Texture3D.FirstWSlice = textureViewDesc.sliceOffset;
        desc.Texture3D.WSize = textureViewDesc.sliceNum;

        return CreateUnorderedAccessView(texture, desc, textureViewDesc.format);
    }
    case Texture3DViewType::COLOR_ATTACHMENT:
    {
        D3D12_RENDER_TARGET_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = textureViewDesc.mipOffset;
        desc.Texture3D.FirstWSlice = textureViewDesc.sliceOffset;
        desc.Texture3D.WSize = textureViewDesc.sliceNum;

        return CreateRenderTargetView(texture, desc);
    }
    }

    return Result::FAILURE;
}

#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
Result DescriptorD3D12::Create(const AccelerationStructure& accelerationStructure)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.RaytracingAccelerationStructure.Location = ((AccelerationStructureD3D12&)accelerationStructure).GetHandle();

    return CreateShaderResourceView(nullptr, desc);
}
#endif

Result DescriptorD3D12::Create(const SamplerDesc& samplerDesc)
{
    bool useAnisotropy = samplerDesc.anisotropy > 1 ? true : false;
    bool useComparison = samplerDesc.compareFunc != CompareFunc::NONE;

    D3D12_SAMPLER_DESC desc = {};
    desc.Filter = useAnisotropy ?
        GetFilterAnisotropic(samplerDesc.filterExt, useComparison) :
        GetFilterIsotropic(samplerDesc.mip, samplerDesc.magnification, samplerDesc.minification, samplerDesc.filterExt, useComparison);
    desc.AddressU = GetAddressMode(samplerDesc.addressModes.u);
    desc.AddressV = GetAddressMode(samplerDesc.addressModes.v);
    desc.AddressW = GetAddressMode(samplerDesc.addressModes.w);
    desc.MipLODBias = samplerDesc.mipBias;
    desc.MaxAnisotropy = samplerDesc.anisotropy;
    desc.ComparisonFunc = GetComparisonFunc(samplerDesc.compareFunc);
    desc.MinLOD = samplerDesc.mipMin;
    desc.MaxLOD = samplerDesc.mipMax;

    if (samplerDesc.borderColor == BorderColor::FLOAT_OPAQUE_BLACK || samplerDesc.borderColor == BorderColor::INT_OPAQUE_BLACK)
    {
        desc.BorderColor[3] = 1.0f;
    }
    else if (samplerDesc.borderColor == BorderColor::FLOAT_OPAQUE_WHITE || samplerDesc.borderColor == BorderColor::INT_OPAQUE_WHITE)
    {
        desc.BorderColor[0] = 1.0f;
        desc.BorderColor[1] = 1.0f;
        desc.BorderColor[2] = 1.0f;
        desc.BorderColor[3] = 1.0f;
    }

    DescriptorHandle descriptorHandle;
    Result result = m_Device.GetDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, descriptorHandle);
    if (result != Result::SUCCESS)
        return result;

    m_DescriptorHandle = descriptorHandle;

    ((ID3D12Device*)m_Device)->CreateSampler(&desc, { m_Device.GetDescriptorPointerCPU(descriptorHandle) });

    return Result::SUCCESS;
}

Result DescriptorD3D12::CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc)
{
    Result result = m_Device.GetDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_DescriptorHandle);
    if (result != Result::SUCCESS)
        return result;

    ((ID3D12Device*)m_Device)->CreateConstantBufferView(&desc, { m_Device.GetDescriptorPointerCPU(m_DescriptorHandle) });
    m_BufferLocation = desc.BufferLocation;

    return Result::SUCCESS;
}

Result DescriptorD3D12::CreateShaderResourceView(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
{
    Result result = m_Device.GetDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_DescriptorHandle);
    if (result != Result::SUCCESS)
        return result;

    ((ID3D12Device*)m_Device)->CreateShaderResourceView(resource, &desc, { m_Device.GetDescriptorPointerCPU(m_DescriptorHandle) });
    m_Resource = resource;

    return Result::SUCCESS;
}

Result DescriptorD3D12::CreateUnorderedAccessView(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc, Format format)
{
    Result result = m_Device.GetDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_DescriptorHandle);
    if (result != Result::SUCCESS)
        return result;

    ((ID3D12Device*)m_Device)->CreateUnorderedAccessView(resource, nullptr, &desc, { m_Device.GetDescriptorPointerCPU(m_DescriptorHandle) });
    m_Resource = resource;
    m_IsFloatingPointFormatUAV = IsFloatingPointFormat(format);

    return Result::SUCCESS;
}

Result DescriptorD3D12::CreateRenderTargetView(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& desc)
{
    Result result = m_Device.GetDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, m_DescriptorHandle);
    if (result != Result::SUCCESS)
        return result;

    ((ID3D12Device*)m_Device)->CreateRenderTargetView(resource, &desc, { m_Device.GetDescriptorPointerCPU(m_DescriptorHandle) });
    m_Resource = resource;

    return Result::SUCCESS;
}

Result DescriptorD3D12::CreateDepthStencilView(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& desc)
{
    Result result = m_Device.GetDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, m_DescriptorHandle);
    if (result != Result::SUCCESS)
        return result;

    ((ID3D12Device*)m_Device)->CreateDepthStencilView(resource, &desc, { m_Device.GetDescriptorPointerCPU(m_DescriptorHandle) });
    m_Resource = resource;

    return Result::SUCCESS;
}

void DescriptorD3D12::SetDebugName(const char* name)
{}
