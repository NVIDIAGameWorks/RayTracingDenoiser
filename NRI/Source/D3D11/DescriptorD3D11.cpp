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
#include "DescriptorD3D11.h"

#include "TextureD3D11.h"
#include "BufferD3D11.h"

using namespace nri;

inline D3D11_TEXTURE_ADDRESS_MODE GetD3D11AdressMode(nri::AddressMode mode)
{
    return (D3D11_TEXTURE_ADDRESS_MODE)(D3D11_TEXTURE_ADDRESS_WRAP + (uint32_t)mode);
}

inline D3D11_FILTER GetFilterIsotropic(Filter mip, Filter magnification, Filter minification, FilterExt filterExt, bool useComparison)
{
   uint32_t combinedMask = mip == Filter::LINEAR ? 0x1 : 0;
   combinedMask |= magnification == Filter::LINEAR ? 0x4 : 0;
   combinedMask |= minification == Filter::LINEAR ? 0x10 : 0;

   if (useComparison)
       combinedMask |= 0x80;
   else if (filterExt == FilterExt::MIN)
       combinedMask |= 0x100;
   else if (filterExt == FilterExt::MAX)
       combinedMask |= 0x180;

   return (D3D11_FILTER)combinedMask;
}

static DXGI_FORMAT GetShaderFormatForDepth(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_D16_UNORM:                return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT:                return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:     return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default: return format;
    }
}

D3D11_FILTER GetFilterAnisotropic(FilterExt filterExt, bool useComparison)
{
   if (filterExt == FilterExt::MIN)
       return D3D11_FILTER_MINIMUM_ANISOTROPIC;
   else if (filterExt == FilterExt::MAX)
       return D3D11_FILTER_MAXIMUM_ANISOTROPIC;

   return useComparison ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
}

DescriptorD3D11::DescriptorD3D11(DeviceD3D11& device) :
    m_Device(device)
{
}

DescriptorD3D11::~DescriptorD3D11()
{
}

Result DescriptorD3D11::Create(const VersionedDevice& device, const Texture1DViewDesc& textureViewDesc)
{
    const TextureD3D11& texture = *(TextureD3D11*)textureViewDesc.texture;
    const FormatInfo& formatInfo = GetFormatInfo(textureViewDesc.format);
    HRESULT hr = E_INVALIDARG;

    const TextureDesc& textureDesc = texture.GetDesc();
    uint32_t remainingMipLevels = textureViewDesc.mipNum == REMAINING_MIP_LEVELS ? (textureDesc.mipNum - textureViewDesc.mipOffset) : textureViewDesc.mipNum;
    uint32_t remainingArrayLayers = textureViewDesc.arraySize == REMAINING_ARRAY_LAYERS ? (textureDesc.arraySize - textureViewDesc.arrayOffset) : textureViewDesc.arraySize;

    switch (textureViewDesc.viewType)
    {
    case Texture1DViewType::SHADER_RESOURCE_1D:
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};

            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
            srv.Texture1D.MostDetailedMip = textureViewDesc.mipOffset;
            srv.Texture1D.MipLevels = remainingMipLevels;
            srv.Format = formatInfo.typed;

            hr = device->CreateShaderResourceView(texture, &srv, (ID3D11ShaderResourceView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::RESOURCE;
        }
        break;
    case Texture1DViewType::SHADER_RESOURCE_1D_ARRAY:
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};

            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
            srv.Texture1DArray.MostDetailedMip = textureViewDesc.mipOffset;
            srv.Texture1DArray.MipLevels = remainingMipLevels;
            srv.Texture1DArray.FirstArraySlice = textureViewDesc.arrayOffset;
            srv.Texture1DArray.ArraySize = remainingArrayLayers;
            srv.Format = formatInfo.typed;

            hr = device->CreateShaderResourceView(texture, &srv, (ID3D11ShaderResourceView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::RESOURCE;
        }
        break;
    case Texture1DViewType::SHADER_RESOURCE_STORAGE_1D:
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};

            uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D;
            uav.Texture1D.MipSlice = textureViewDesc.mipOffset;
            uav.Format = formatInfo.typed;

            hr = device->CreateUnorderedAccessView(texture, &uav, (ID3D11UnorderedAccessView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::STORAGE;
        }
        break;
    case Texture1DViewType::SHADER_RESOURCE_STORAGE_1D_ARRAY:
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};

            uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1DARRAY;
            uav.Texture1DArray.MipSlice = textureViewDesc.mipOffset;
            uav.Texture1DArray.FirstArraySlice = textureViewDesc.arrayOffset;
            uav.Texture1DArray.ArraySize = remainingArrayLayers;
            uav.Format = formatInfo.typed;

            hr = device->CreateUnorderedAccessView(texture, &uav, (ID3D11UnorderedAccessView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::STORAGE;
        }
        break;
    case Texture1DViewType::COLOR_ATTACHMENT:
        {
            D3D11_RENDER_TARGET_VIEW_DESC rtv = {};

            rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
            rtv.Texture1DArray.MipSlice = textureViewDesc.mipOffset;
            rtv.Texture1DArray.FirstArraySlice = textureViewDesc.arrayOffset;
            rtv.Texture1DArray.ArraySize = remainingArrayLayers;
            rtv.Format = formatInfo.typed;

            hr = device->CreateRenderTargetView(texture, &rtv, (ID3D11RenderTargetView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::NO_SHADER_VISIBLE;
        }
        break;
    case Texture1DViewType::DEPTH_STENCIL_ATTACHMENT:
        {
            D3D11_DEPTH_STENCIL_VIEW_DESC dsv = {};

            dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1DARRAY;
            dsv.Texture1DArray.MipSlice = textureViewDesc.mipOffset;
            dsv.Texture1DArray.FirstArraySlice = textureViewDesc.arrayOffset;
            dsv.Texture1DArray.ArraySize = remainingArrayLayers;
            dsv.Format = formatInfo.typed;

            if (textureViewDesc.flags & ResourceViewBits::READONLY_DEPTH)
                dsv.Flags |= D3D11_DSV_READ_ONLY_DEPTH;

            if (textureViewDesc.flags & ResourceViewBits::READONLY_STENCIL)
                dsv.Flags |= D3D11_DSV_READ_ONLY_STENCIL;

            hr = device->CreateDepthStencilView(texture, &dsv, (ID3D11DepthStencilView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::NO_SHADER_VISIBLE;
        }
        break;
    };

    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "Can't create view!");

    m_IsIntegerFormat = formatInfo.isInteger;
    m_SubresourceInfo.Initialize(textureViewDesc.texture, textureViewDesc.mipOffset, textureViewDesc.mipNum, textureViewDesc.arrayOffset, textureViewDesc.arraySize);

    return Result::SUCCESS;
}

Result DescriptorD3D11::Create(const VersionedDevice& device, const Texture2DViewDesc& textureViewDesc)
{
    const TextureD3D11& texture = *(TextureD3D11*)textureViewDesc.texture;
    const TextureDesc& desc = texture.GetDesc();
    const FormatInfo& formatInfo = GetFormatInfo(textureViewDesc.format);
    HRESULT hr = E_INVALIDARG;

    const TextureDesc& textureDesc = texture.GetDesc();
    uint32_t remainingMipLevels = textureViewDesc.mipNum == REMAINING_MIP_LEVELS ? (textureDesc.mipNum - textureViewDesc.mipOffset) : textureViewDesc.mipNum;
    uint32_t remainingArrayLayers = textureViewDesc.arraySize == REMAINING_ARRAY_LAYERS ? (textureDesc.arraySize - textureViewDesc.arrayOffset) : textureViewDesc.arraySize;

    switch (textureViewDesc.viewType)
    {
    case Texture2DViewType::SHADER_RESOURCE_2D:
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};

            if (desc.sampleNum > 1)
                srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
            else
            {
                srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srv.Texture2D.MostDetailedMip = textureViewDesc.mipOffset;
                srv.Texture2D.MipLevels = remainingMipLevels;
            }
            srv.Format = GetShaderFormatForDepth(formatInfo.typed);

            hr = device->CreateShaderResourceView(texture, &srv, (ID3D11ShaderResourceView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::RESOURCE;
        }
        break;
    case Texture2DViewType::SHADER_RESOURCE_2D_ARRAY:
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};

            if (desc.sampleNum > 1)
            {
                srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
                srv.Texture2DMSArray.FirstArraySlice = textureViewDesc.arrayOffset;
                srv.Texture2DMSArray.ArraySize = remainingArrayLayers;
            }
            else
            {
                srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                srv.Texture2DArray.MostDetailedMip = textureViewDesc.mipOffset;
                srv.Texture2DArray.MipLevels = remainingMipLevels;
                srv.Texture2DArray.FirstArraySlice = textureViewDesc.arrayOffset;
                srv.Texture2DArray.ArraySize = remainingArrayLayers;
            }
            srv.Format = GetShaderFormatForDepth(formatInfo.typed);

            hr = device->CreateShaderResourceView(texture, &srv, (ID3D11ShaderResourceView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::RESOURCE;
        }
        break;
    case Texture2DViewType::SHADER_RESOURCE_CUBE:
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};

            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srv.TextureCube.MostDetailedMip = textureViewDesc.mipOffset;
            srv.TextureCube.MipLevels = remainingMipLevels;
            srv.Format = formatInfo.typed;

            hr = device->CreateShaderResourceView(texture, &srv, (ID3D11ShaderResourceView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::RESOURCE;
        }
        break;
    case Texture2DViewType::SHADER_RESOURCE_CUBE_ARRAY:
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};

            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
            srv.TextureCubeArray.MostDetailedMip = textureViewDesc.mipOffset;
            srv.TextureCubeArray.MipLevels = remainingMipLevels;
            srv.TextureCubeArray.First2DArrayFace = textureViewDesc.arrayOffset;
            srv.TextureCubeArray.NumCubes = textureViewDesc.arraySize / 6;
            srv.Format = GetShaderFormatForDepth(formatInfo.typed);

            hr = device->CreateShaderResourceView(texture, &srv, (ID3D11ShaderResourceView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::RESOURCE;
        }
        break;
    case Texture2DViewType::SHADER_RESOURCE_STORAGE_2D:
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};

            uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uav.Texture2D.MipSlice = textureViewDesc.mipOffset;
            uav.Format = GetShaderFormatForDepth(formatInfo.typed);

            hr = device->CreateUnorderedAccessView(texture, &uav, (ID3D11UnorderedAccessView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::STORAGE;
        }
        break;
    case Texture2DViewType::SHADER_RESOURCE_STORAGE_2D_ARRAY:
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};

            uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            uav.Texture2DArray.MipSlice = textureViewDesc.mipOffset;
            uav.Texture2DArray.FirstArraySlice = textureViewDesc.arrayOffset;
            uav.Texture2DArray.ArraySize = remainingArrayLayers;
            uav.Format = GetShaderFormatForDepth(formatInfo.typed);

            hr = device->CreateUnorderedAccessView(texture, &uav, (ID3D11UnorderedAccessView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::STORAGE;
        }
        break;
    case Texture2DViewType::COLOR_ATTACHMENT:
        {
            D3D11_RENDER_TARGET_VIEW_DESC rtv = {};

            if (desc.sampleNum > 1)
            {
                rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
                rtv.Texture2DMSArray.FirstArraySlice = textureViewDesc.arrayOffset;
                rtv.Texture2DMSArray.ArraySize = remainingArrayLayers;
            }
            else
            {
                rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rtv.Texture2DArray.MipSlice = textureViewDesc.mipOffset;
                rtv.Texture2DArray.FirstArraySlice = textureViewDesc.arrayOffset;
                rtv.Texture2DArray.ArraySize = remainingArrayLayers;
            }
            rtv.Format = formatInfo.typed;

            hr = device->CreateRenderTargetView(texture, &rtv, (ID3D11RenderTargetView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::NO_SHADER_VISIBLE;
        }
        break;
    case Texture2DViewType::DEPTH_STENCIL_ATTACHMENT:
        {
            D3D11_DEPTH_STENCIL_VIEW_DESC dsv = {};

            if (desc.sampleNum > 1)
            {
                dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
                dsv.Texture2DMSArray.FirstArraySlice = textureViewDesc.arrayOffset;
                dsv.Texture2DMSArray.ArraySize = remainingArrayLayers;
            }
            else
            {
                dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                dsv.Texture2DArray.MipSlice = textureViewDesc.mipOffset;
                dsv.Texture2DArray.FirstArraySlice = textureViewDesc.arrayOffset;
                dsv.Texture2DArray.ArraySize = remainingArrayLayers;
            }
            dsv.Format = formatInfo.typed;

            if (textureViewDesc.flags & ResourceViewBits::READONLY_DEPTH)
                dsv.Flags |= D3D11_DSV_READ_ONLY_DEPTH;

            if (textureViewDesc.flags & ResourceViewBits::READONLY_STENCIL)
                dsv.Flags |= D3D11_DSV_READ_ONLY_STENCIL;

            hr = device->CreateDepthStencilView(texture, &dsv, (ID3D11DepthStencilView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::NO_SHADER_VISIBLE;
        }
        break;
    };

    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "Can't create view!");

    m_IsIntegerFormat = formatInfo.isInteger;
    m_SubresourceInfo.Initialize(textureViewDesc.texture, textureViewDesc.mipOffset, textureViewDesc.mipNum, textureViewDesc.arrayOffset, textureViewDesc.arraySize);

    return Result::SUCCESS;
}

Result DescriptorD3D11::Create(const VersionedDevice& device, const Texture3DViewDesc& textureViewDesc)
{
    const TextureD3D11& texture = *(TextureD3D11*)textureViewDesc.texture;
    const FormatInfo& formatInfo = GetFormatInfo(textureViewDesc.format);
    HRESULT hr = E_INVALIDARG;

    const TextureDesc& textureDesc = texture.GetDesc();
    uint32_t remainingMipLevels = textureViewDesc.mipNum == REMAINING_MIP_LEVELS ? (textureDesc.mipNum - textureViewDesc.mipOffset) : textureViewDesc.mipNum;

    switch (textureViewDesc.viewType)
    {
    case Texture3DViewType::SHADER_RESOURCE_3D:
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};

            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
            srv.Texture3D.MostDetailedMip = textureViewDesc.mipOffset;
            srv.Texture3D.MipLevels = remainingMipLevels;
            srv.Format = formatInfo.typed;

            hr = device->CreateShaderResourceView(texture, &srv, (ID3D11ShaderResourceView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::RESOURCE;
        }
        break;
    case Texture3DViewType::SHADER_RESOURCE_STORAGE_3D:
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};

            uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
            uav.Texture3D.MipSlice = textureViewDesc.mipOffset;
            uav.Texture3D.FirstWSlice = textureViewDesc.sliceOffset;
            uav.Texture3D.WSize = textureViewDesc.sliceNum;
            uav.Format = formatInfo.typed;

            hr = device->CreateUnorderedAccessView(texture, &uav, (ID3D11UnorderedAccessView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::STORAGE;
        }
        break;
    case Texture3DViewType::COLOR_ATTACHMENT:
        {
            D3D11_RENDER_TARGET_VIEW_DESC rtv = {};

            rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
            rtv.Texture3D.MipSlice = textureViewDesc.mipOffset;
            rtv.Texture3D.FirstWSlice = textureViewDesc.sliceOffset;
            rtv.Texture3D.WSize = textureViewDesc.sliceNum;
            rtv.Format = formatInfo.typed;

            hr = device->CreateRenderTargetView(texture, &rtv, (ID3D11RenderTargetView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::NO_SHADER_VISIBLE;
        }
        break;
    };

    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "Can't create view!");

    m_IsIntegerFormat = formatInfo.isInteger;
    m_SubresourceInfo.Initialize(textureViewDesc.texture, textureViewDesc.mipOffset, textureViewDesc.mipNum, textureViewDesc.sliceOffset, textureViewDesc.sliceNum);

    return Result::SUCCESS;
}

Result DescriptorD3D11::Create(const VersionedDevice& device, const BufferViewDesc& bufferViewDesc)
{
    const BufferD3D11& buffer = *(BufferD3D11*)bufferViewDesc.buffer;
    const BufferDesc& desc = buffer.GetDesc();
    uint64_t size = bufferViewDesc.size == WHOLE_SIZE ? desc.size : bufferViewDesc.size;
    uint32_t stride = desc.structureStride;
    HRESULT hr = E_INVALIDARG;

    Format format = bufferViewDesc.format;
    if (bufferViewDesc.viewType == BufferViewType::CONSTANT)
    {
        format = Format::RGBA32_SFLOAT;

        if (bufferViewDesc.offset != 0 && device.version == 0)
            REPORT_ERROR(m_Device.GetLog(), "Constant buffers with non-zero offsets require 11.1+ feature level!");
    }
    else if (stride)
        format = Format::UNKNOWN;

    const FormatInfo& formatInfo = GetFormatInfo(format);
    if (!stride )
        stride = formatInfo.stride;

    m_ElementOffset = (uint32_t)(bufferViewDesc.offset / stride);
    m_ElementNum = (uint32_t)(size / stride);

    switch (bufferViewDesc.viewType)
    {
    case BufferViewType::CONSTANT:
        {
            m_Descriptor = buffer;
            hr = S_OK;
            m_Type = DescriptorTypeDX11::CONSTANT;
        }
        break;
    case BufferViewType::SHADER_RESOURCE:
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};

            srv.Format = formatInfo.typed;
            srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srv.Buffer.FirstElement = m_ElementOffset;
            srv.Buffer.NumElements = m_ElementNum;

            hr = device->CreateShaderResourceView(buffer, &srv, (ID3D11ShaderResourceView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::RESOURCE;
        }
        break;
    case BufferViewType::SHADER_RESOURCE_STORAGE:
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};

            uav.Format = formatInfo.typed;
            uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uav.Buffer.FirstElement = m_ElementOffset;
            uav.Buffer.NumElements = m_ElementNum;

            hr = device->CreateUnorderedAccessView(buffer, &uav, (ID3D11UnorderedAccessView**)&m_Descriptor);

            m_Type = DescriptorTypeDX11::STORAGE;
        }
        break;
    };

    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "Can't create view!");

    m_IsIntegerFormat = formatInfo.isInteger;
    m_SubresourceInfo.Initialize(bufferViewDesc.buffer);

    return Result::SUCCESS;
}

void DescriptorD3D11::CreateSamplerState(const Log& log, const VersionedDevice& device, const SamplerDesc& samplerDesc, ID3D11SamplerState** samplerState)
{
    bool isAnisotropy = samplerDesc.anisotropy > 1;
    bool isComparison = samplerDesc.compareFunc != CompareFunc::NONE;

    D3D11_SAMPLER_DESC desc = {};
    desc.AddressU = GetD3D11AdressMode(samplerDesc.addressModes.u);
    desc.AddressV = GetD3D11AdressMode(samplerDesc.addressModes.v);
    desc.AddressW = GetD3D11AdressMode(samplerDesc.addressModes.w);
    desc.MipLODBias = samplerDesc.mipBias;
    desc.MaxAnisotropy = samplerDesc.anisotropy;
    desc.ComparisonFunc = GetD3D11ComparisonFuncFromCompareFunc(samplerDesc.compareFunc);
    desc.MinLOD = samplerDesc.mipMin;
    desc.MaxLOD = samplerDesc.mipMax;
    desc.Filter = isAnisotropy ?
       GetFilterAnisotropic(samplerDesc.filterExt, isComparison) :
       GetFilterIsotropic(samplerDesc.mip, samplerDesc.magnification, samplerDesc.minification, samplerDesc.filterExt, isComparison);

    if (samplerDesc.borderColor == BorderColor::FLOAT_OPAQUE_BLACK || samplerDesc.borderColor == BorderColor::INT_OPAQUE_BLACK)
        desc.BorderColor[3] = 1.0f;
    else if (samplerDesc.borderColor == BorderColor::FLOAT_OPAQUE_WHITE || samplerDesc.borderColor == BorderColor::INT_OPAQUE_WHITE)
    {
        desc.BorderColor[0] = 1.0f;
        desc.BorderColor[1] = 1.0f;
        desc.BorderColor[2] = 1.0f;
        desc.BorderColor[3] = 1.0f;
    }

    HRESULT hr = device->CreateSamplerState(&desc, samplerState);
    if (FAILED(hr))
        REPORT_ERROR(log, "ID3D11Device::CreateSamplerState() - FAILED!");
}

Result DescriptorD3D11::Create(const VersionedDevice& device, const SamplerDesc& samplerDesc)
{
    CreateSamplerState(m_Device.GetLog(), device, samplerDesc, (ID3D11SamplerState**)&m_Descriptor);
    m_Type = DescriptorTypeDX11::SAMPLER;

    return m_Descriptor ? Result::SUCCESS : Result::FAILURE;
}

DescriptorD3D11::DescriptorD3D11(DeviceD3D11& device, ID3D11ShaderResourceView* resource) :
    m_Descriptor(resource),
    m_Device(device)
{
    m_Type = DescriptorTypeDX11::RESOURCE;
}

DescriptorD3D11::DescriptorD3D11(DeviceD3D11& device, ID3D11UnorderedAccessView* storage) :
    m_Descriptor(storage),
    m_Device(device)
{
    m_Type = DescriptorTypeDX11::STORAGE;
}

DescriptorD3D11::DescriptorD3D11(DeviceD3D11& device, ID3D11RenderTargetView* randerTarget) :
    m_Descriptor(randerTarget),
    m_Device(device)
{
    m_Type = DescriptorTypeDX11::NO_SHADER_VISIBLE;
}

DescriptorD3D11::DescriptorD3D11(DeviceD3D11& device, ID3D11DepthStencilView* depthStencil) :
    m_Descriptor(depthStencil),
    m_Device(device)
{
    m_Type = DescriptorTypeDX11::NO_SHADER_VISIBLE;
}

DescriptorD3D11::DescriptorD3D11(DeviceD3D11& device, ID3D11Buffer* constantBuffer, uint32_t elementOffset, uint32_t elementNum) :
    m_Descriptor(constantBuffer),
    m_ElementOffset(elementOffset),
    m_ElementNum(elementNum),
    m_Device(device)
{
    m_Type = DescriptorTypeDX11::CONSTANT;
}

DescriptorD3D11::DescriptorD3D11(DeviceD3D11& device, ID3D11SamplerState* sampler) :
    m_Descriptor(sampler),
    m_Device(device)
{
    m_Type = DescriptorTypeDX11::SAMPLER;
}

void DescriptorD3D11::SetDebugName(const char* name)
{
    SetName(m_Descriptor, name);
}

#include "DescriptorD3D11.hpp"
