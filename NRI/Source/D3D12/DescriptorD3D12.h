/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "DeviceD3D12.h"

struct ID3D12Resource;

namespace nri
{
    struct DeviceD3D12;

    struct DescriptorD3D12
    {
        DescriptorD3D12(DeviceD3D12& device);
        ~DescriptorD3D12();

        operator ID3D12Resource*() const;

        DeviceD3D12& GetDevice() const;

        Result Create(const BufferViewDesc& bufferViewDesc);
        Result Create(const Texture1DViewDesc& textureViewDesc);
        Result Create(const Texture2DViewDesc& textureViewDesc);
        Result Create(const Texture3DViewDesc& textureViewDesc);
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
        Result Create(const AccelerationStructure& accelerationStructure);
#endif
        Result Create(const SamplerDesc& samplerDesc);
        DescriptorPointerCPU GetPointerCPU() const;
        D3D12_GPU_VIRTUAL_ADDRESS GetBufferLocation() const;
        bool IsFloatingPointUAV() const;

        //================================================================================================================
        // NRI
        //================================================================================================================
        void SetDebugName(const char* name);

    private:
        Result CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc);
        Result CreateShaderResourceView(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);
        Result CreateUnorderedAccessView(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc, Format format);
        Result CreateRenderTargetView(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& desc);
        Result CreateDepthStencilView(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& desc);

    private:
        DeviceD3D12& m_Device;
        ID3D12Resource* m_Resource = nullptr;
        bool m_IsFloatingPointFormatUAV = false;
        D3D12_GPU_VIRTUAL_ADDRESS m_BufferLocation = 0;
        DescriptorHandle m_DescriptorHandle = {};
    };

    inline DescriptorD3D12::DescriptorD3D12(DeviceD3D12& device)
        : m_Device(device)
    {}

    inline DescriptorD3D12::~DescriptorD3D12()
    {}

    inline DescriptorD3D12::operator ID3D12Resource*() const
    {
        return m_Resource;
    }

    inline DescriptorPointerCPU DescriptorD3D12::GetPointerCPU() const
    {
        return m_Device.GetDescriptorPointerCPU(m_DescriptorHandle);
    }

    inline D3D12_GPU_VIRTUAL_ADDRESS DescriptorD3D12::GetBufferLocation() const
    {
        return m_BufferLocation;
    }

    inline bool DescriptorD3D12::IsFloatingPointUAV() const
    {
        return m_IsFloatingPointFormatUAV;
    }

    inline DeviceD3D12& DescriptorD3D12::GetDevice() const
    {
        return m_Device;
    }
}
