/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

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

    struct DescriptorD3D11
    {
        DescriptorD3D11(DeviceD3D11& device);
        ~DescriptorD3D11();

        inline DeviceD3D11& GetDevice() const
        { return m_Device; }

        inline operator void*() const
        { return (void*)m_Descriptor.GetInterface(); }

        inline operator ID3D11View*() const
        { return (ID3D11View*)m_Descriptor.GetInterface(); }

        inline operator ID3D11RenderTargetView*() const
        { return (ID3D11RenderTargetView*)m_Descriptor.GetInterface(); }

        inline operator ID3D11DepthStencilView*() const
        { return (ID3D11DepthStencilView*)m_Descriptor.GetInterface(); }

        inline operator ID3D11UnorderedAccessView*() const
        { return (ID3D11UnorderedAccessView*)m_Descriptor.GetInterface(); }

        inline uint32_t GetElementOffset() const
        { return m_ElementOffset; }

        inline uint32_t GetElementNum() const
        { return m_ElementNum; }

        inline uint32_t IsIntegerFormat() const
        { return m_IsIntegerFormat; }

        inline const SubresourceInfo& GetSubresourceInfo() const
        { return m_SubresourceInfo; }

        static void CreateSamplerState(const Log& log, const VersionedDevice& device, const SamplerDesc& samplerDesc, ID3D11SamplerState** samplerState);

        Result Create(const VersionedDevice& device, const Texture1DViewDesc& textureViewDesc);
        Result Create(const VersionedDevice& device, const Texture2DViewDesc& textureViewDesc);
        Result Create(const VersionedDevice& device, const Texture3DViewDesc& textureViewDesc);
        Result Create(const VersionedDevice& device, const BufferViewDesc& bufferViewDesc);
        Result Create(const VersionedDevice& device, const SamplerDesc& samplerDesc);

        //======================================================================================================================
        // Native wrapper
        //======================================================================================================================
        DescriptorD3D11(DeviceD3D11& device, ID3D11ShaderResourceView* resource);
        DescriptorD3D11(DeviceD3D11& device, ID3D11UnorderedAccessView* storage);
        DescriptorD3D11(DeviceD3D11& device, ID3D11RenderTargetView* randerTarget);
        DescriptorD3D11(DeviceD3D11& device, ID3D11DepthStencilView* depthStencil);
        DescriptorD3D11(DeviceD3D11& device, ID3D11Buffer* constantBuffer, uint32_t elementOffset = 0, uint32_t elementNum = 0);
        DescriptorD3D11(DeviceD3D11& device, ID3D11SamplerState* sampler);

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);

    private:
        ComPtr<ID3D11DeviceChild> m_Descriptor;
        SubresourceInfo m_SubresourceInfo = {};
        uint32_t m_ElementOffset = 0;
        uint32_t m_ElementNum = 0;
        DescriptorTypeDX11 m_Type = DescriptorTypeDX11::NO_SHADER_VISIBLE;
        bool m_IsIntegerFormat = false;
        DeviceD3D11& m_Device;
    };
}
