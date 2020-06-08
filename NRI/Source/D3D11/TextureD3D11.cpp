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
#include "TextureD3D11.h"

#include "MemoryD3D11.h"

using namespace nri;

static uint16_t GetMaxMipNum(uint16_t w, uint16_t h, uint16_t d)
{
    uint16_t mipNum = 1;

    while (w > 1 || h > 1 || d > 1)
    {
        if (w > 1)
            w >>= 1;

        if (h > 1)
            h >>= 1;

        if (d > 1)
            d >>= 1;

        mipNum++;
    }

    return mipNum;
}

TextureD3D11::TextureD3D11() :
    m_Device(nullptr)
{
}

TextureD3D11::TextureD3D11(DeviceD3D11& device, const TextureDesc& textureDesc) :
    m_Desc(textureDesc),
    m_Device(&device)
{
    uint16_t mipNum = GetMaxMipNum(m_Desc.size[0], m_Desc.size[1], m_Desc.size[2]);
    m_Desc.mipNum = std::min(m_Desc.mipNum, mipNum);
}

TextureD3D11::~TextureD3D11()
{
}

Result TextureD3D11::Create(const VersionedDevice& device, const MemoryD3D11* memory)
{
    HRESULT hr = E_INVALIDARG;
    const FormatInfo& formatInfo = GetFormatInfo(m_Desc.format);
    uint32_t bindFlags = 0;

    if (m_Desc.usageMask & TextureUsageBits::SHADER_RESOURCE)
        bindFlags |= D3D11_BIND_SHADER_RESOURCE;

    if (m_Desc.usageMask & TextureUsageBits::SHADER_RESOURCE_STORAGE)
        bindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    if (m_Desc.usageMask & TextureUsageBits::COLOR_ATTACHMENT)
        bindFlags |= D3D11_BIND_RENDER_TARGET;

    if (m_Desc.usageMask & TextureUsageBits::DEPTH_STENCIL_ATTACHMENT)
        bindFlags |= D3D11_BIND_DEPTH_STENCIL;

    uint32_t cpuAccessFlags = D3D11_CPU_ACCESS_READ;
    D3D11_USAGE usage = D3D11_USAGE_STAGING;
    if (memory)
    {
        usage = memory->GetType() == MemoryLocation::HOST_UPLOAD ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
        cpuAccessFlags = 0;
    }

    if (m_Desc.type == TextureType::TEXTURE_1D)
    {
        D3D11_TEXTURE1D_DESC desc = {};

        desc.Width = m_Desc.size[0];
        desc.MipLevels = m_Desc.mipNum;
        desc.ArraySize = m_Desc.arraySize;
        desc.Format = formatInfo.typeless;
        desc.Usage = usage;
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = cpuAccessFlags;

        hr = device->CreateTexture1D(&desc, nullptr, (ID3D11Texture1D**)&m_Texture);
    }
    else if (m_Desc.type == TextureType::TEXTURE_3D)
    {
        D3D11_TEXTURE3D_DESC desc = {};

        desc.Width = m_Desc.size[0];
        desc.Height = m_Desc.size[1];
        desc.Depth = m_Desc.size[2];
        desc.MipLevels = m_Desc.mipNum;
        desc.Format = formatInfo.typeless;
        desc.Usage = usage;
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = cpuAccessFlags;

        hr = device->CreateTexture3D(&desc, nullptr, (ID3D11Texture3D**)&m_Texture);
    }
    else
    {
        D3D11_TEXTURE2D_DESC desc = {};

        desc.Width = m_Desc.size[0];
        desc.Height = m_Desc.size[1];
        desc.MipLevels = m_Desc.mipNum;
        desc.ArraySize = m_Desc.arraySize;
        desc.Format = formatInfo.typeless;
        desc.SampleDesc.Count = m_Desc.sampleNum;
        desc.Usage = usage;
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = cpuAccessFlags;

        if (m_Desc.sampleNum == 1 && desc.Width == desc.Height && (m_Desc.arraySize % 6 == 0))
            desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

        hr = device->CreateTexture2D(&desc, nullptr, (ID3D11Texture2D**)&m_Texture);
    }

    RETURN_ON_BAD_HRESULT(m_Device->GetLog(), hr, "Can't create texture!");

    uint64_t size = GetMipmappedSize();
    uint32_t priority = memory ? memory->GetResidencyPriority(size) : 0;
    if (priority != 0)
        m_Texture->SetEvictionPriority(priority);

    return Result::SUCCESS;
}

Result TextureD3D11::Create(DeviceD3D11& device, const TextureD3D11Desc& textureDesc)
{
    m_Device = &device;

    ID3D11Resource* resource = (ID3D11Resource*)textureDesc.d3d11Resource;
    if (!resource)
        return Result::INVALID_ARGUMENT;

    D3D11_RESOURCE_DIMENSION type;
    resource->GetType(&type);

    if ((uint32_t)type < D3D11_RESOURCE_DIMENSION_TEXTURE1D)
        return Result::INVALID_ARGUMENT;

    uint32_t bindFlags = 0;
    if (type == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
    {
        ID3D11Texture1D* texture = (ID3D11Texture1D*)resource;
        D3D11_TEXTURE1D_DESC desc = {};
        texture->GetDesc(&desc);

        m_Desc.size[0] = desc.Width;
        m_Desc.size[1] = 1;
        m_Desc.size[2] = 1;
        m_Desc.mipNum = desc.MipLevels;
        m_Desc.arraySize = desc.ArraySize;
        m_Desc.sampleNum = 1;
        m_Desc.type = TextureType::TEXTURE_1D;
        m_Desc.format = GetFormat(desc.Format);

        bindFlags = desc.BindFlags;
    }
    else if (type == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        ID3D11Texture2D* texture = (ID3D11Texture2D*)resource;
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);

        m_Desc.size[0] = desc.Width;
        m_Desc.size[1] = desc.Height;
        m_Desc.size[2] = 1;
        m_Desc.mipNum = desc.MipLevels;
        m_Desc.arraySize = desc.ArraySize;
        m_Desc.sampleNum = desc.SampleDesc.Count;
        m_Desc.type = TextureType::TEXTURE_2D;
        m_Desc.format = GetFormat(desc.Format);

        bindFlags = desc.BindFlags;
    }
    else if (type == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
    {
        ID3D11Texture3D* texture = (ID3D11Texture3D*)resource;
        D3D11_TEXTURE3D_DESC desc = {};
        texture->GetDesc(&desc);

        m_Desc.size[0] = desc.Width;
        m_Desc.size[1] = desc.Height;
        m_Desc.size[2] = desc.Depth;
        m_Desc.mipNum = desc.MipLevels;
        m_Desc.arraySize = 1;
        m_Desc.sampleNum = 1;
        m_Desc.type = TextureType::TEXTURE_3D;
        m_Desc.format = GetFormat(desc.Format);

        bindFlags = desc.BindFlags;
    }

    m_Desc.usageMask = TextureUsageBits::NONE;
    if (bindFlags & D3D11_BIND_RENDER_TARGET)
        m_Desc.usageMask |= TextureUsageBits::COLOR_ATTACHMENT;
    if (bindFlags & D3D11_BIND_DEPTH_STENCIL)
        m_Desc.usageMask |= TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;
    if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
        m_Desc.usageMask |= TextureUsageBits::SHADER_RESOURCE;
    if (bindFlags & D3D11_BIND_UNORDERED_ACCESS)
        m_Desc.usageMask |= TextureUsageBits::SHADER_RESOURCE_STORAGE;

    m_Texture = resource;

    return Result::SUCCESS;
}

uint32_t TextureD3D11::GetMipmappedSize(uint32_t w, uint32_t h, uint32_t d, uint32_t mipOffset, uint32_t mipNum) const
{
    if (!mipNum)
        mipNum = m_Desc.mipNum;

    bool isCompressed = m_Desc.format >= Format::BC1_RGBA_UNORM && m_Desc.format <= Format::BC7_RGBA_SRGB;
    bool isCustom = w || h || d;

    if (!w)
        w = GetSize(0, mipOffset);

    if (!h)
        h = GetSize(1, mipOffset);

    if (!d)
        d = GetSize(2, mipOffset);

    uint32_t size = 0;

    while (mipNum)
    {
        if (isCompressed)
            size += ((w + 3) >> 2) * ((h + 3) >> 2) * d;
        else
            size += w * h * d;

        if (w == 1 && h == 1 && d == 1)
            break;

        if (d > 1)
            d >>= 1;

        if (w > 1)
            w >>= 1;

        if (h > 1)
            h >>= 1;

        mipNum--;
    }

    const FormatInfo& formatInfo = GetFormatInfo(m_Desc.format);
    size *= formatInfo.stride;

    if (!isCustom)
    {
        size *= m_Desc.sampleNum;
        size *= m_Desc.arraySize;
    }

    return size;
}

inline void TextureD3D11::SetDebugName(const char* name)
{
    SetName(m_Texture, name);
}

inline void TextureD3D11::GetMemoryInfo(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const
{
    bool isMultisampled = m_Desc.sampleNum > 1;
    uint32_t size = GetMipmappedSize();

    uint32_t alignment = 65536;
    if (isMultisampled)
        alignment = 4194304;
    else if (size <= 65536)
        alignment = 65536;

    size = GetAlignedSize(size, alignment);

    memoryDesc.type = (MemoryType)memoryLocation;
    memoryDesc.size = size;
    memoryDesc.alignment = alignment;
    memoryDesc.mustBeDedicated = false;
}

#include "TextureD3D11.hpp"

static_assert((uint32_t)nri::TextureType::TEXTURE_1D == 0u, "TextureD3D11::GetSize() depends on nri::TextureType");
static_assert((uint32_t)nri::TextureType::TEXTURE_2D == 1u, "TextureD3D11::GetSize() depends on nri::TextureType");
static_assert((uint32_t)nri::TextureType::TEXTURE_3D == 2u, "TextureD3D11::GetSize() depends on nri::TextureType");