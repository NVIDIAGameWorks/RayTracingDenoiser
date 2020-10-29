/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nri
{
    struct DeviceD3D11;
    struct MemoryD3D11;

    struct TextureD3D11
    {
        TextureD3D11();
        TextureD3D11(DeviceD3D11& device, const TextureDesc& textureDesc);
        ~TextureD3D11();

        inline DeviceD3D11& GetDevice() const
        { return *m_Device; }

        inline operator ID3D11Resource*() const
        { return m_Texture; }

        inline operator ID3D11Texture1D*() const
        { return (ID3D11Texture1D*)m_Texture.GetInterface(); }

        inline operator ID3D11Texture2D*() const
        { return (ID3D11Texture2D*)m_Texture.GetInterface(); }

        inline operator ID3D11Texture3D*() const
        { return (ID3D11Texture3D*)m_Texture.GetInterface(); }

        inline const TextureDesc& GetDesc() const
        { return m_Desc; }

        inline uint32_t GetSubresourceIndex(const TextureRegionDesc& regionDesc) const
        { return regionDesc.mipOffset + regionDesc.arrayOffset * m_Desc.mipNum; }

        inline uint16_t GetSize(uint32_t dimension, uint32_t mipOffset = 0) const
        {
            const uint16_t size = (uint16_t)GetAlignedSize( std::max(m_Desc.size[dimension] >> mipOffset, 1), GetTexelBlockWidth(m_Desc.format) );
            return (dimension <= (uint32_t)m_Desc.type) ? size : 1;
        }

        Result Create(const VersionedDevice& device, const MemoryD3D11* memory);
        Result Create(DeviceD3D11& device, const TextureD3D11Desc& textureDesc);

        uint32_t GetMipmappedSize(uint32_t w = 0, uint32_t h = 0, uint32_t d = 0, uint32_t mipOffset = 0, uint32_t mipNum = 0) const;

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);
        void GetMemoryInfo(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const;

    private:
        ComPtr<ID3D11Resource> m_Texture;
        TextureDesc m_Desc = {};
        DeviceD3D11* m_Device = nullptr;
    };
}
