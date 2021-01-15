/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "SharedD3D12.h"

struct ID3D12Resource;

namespace nri
{
    struct DeviceD3D12;
    struct MemoryD3D12;

    struct TextureD3D12
    {
        TextureD3D12(DeviceD3D12& device);
        ~TextureD3D12();

        operator ID3D12Resource*() const;

        DeviceD3D12& GetDevice() const;

        Result Create(const TextureDesc& textureDesc);
        Result Create(const TextureD3D12Desc& textureDesc);
        void Initialize(ID3D12Resource* resource);
        Result BindMemory(const MemoryD3D12* memory, uint64_t offset);
        const D3D12_RESOURCE_DESC& GetTextureDesc() const;
        uint32_t GetSubresourceIndex(uint32_t arrayOffset, uint32_t mipOffset) const;
        uint16_t GetSize(uint32_t dim, uint32_t mipOffset = 0) const;

        //================================================================================================================
        // NRI
        //================================================================================================================
        void SetDebugName(const char* name);
        void GetMemoryInfo(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const;

    private:
        DeviceD3D12& m_Device;
        bool m_IsWrappedObject = false;
        D3D12_RESOURCE_DESC m_TextureDesc = {};
        ComPtr<ID3D12Resource> m_Texture;
        Format m_Format = Format::UNKNOWN;
    };

    inline TextureD3D12::TextureD3D12(DeviceD3D12& device)
        : m_Device(device)
    {}

    inline TextureD3D12::~TextureD3D12()
    {}

    inline TextureD3D12::operator ID3D12Resource*() const
    {
        return m_Texture.GetInterface();
    }

    inline const D3D12_RESOURCE_DESC& TextureD3D12::GetTextureDesc() const
    {
        return m_TextureDesc;
    }

    inline DeviceD3D12& TextureD3D12::GetDevice() const
    {
        return m_Device;
    }

    inline uint16_t TextureD3D12::GetSize(uint32_t dimension, uint32_t mipOffset) const
    {
        uint16_t size = dimension == 1 ? (uint16_t)m_TextureDesc.Height : (uint16_t)m_TextureDesc.Width;
        if (dimension == 2)
            size = (uint16_t)m_TextureDesc.DepthOrArraySize;

        size = (uint16_t)GetAlignedSize( std::max(size >> mipOffset, 1), GetTexelBlockWidth(m_Format) );

        const uint32_t resourceDimension = m_TextureDesc.Dimension - D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        return (dimension <= resourceDimension) ? size : 1;
    }
}
