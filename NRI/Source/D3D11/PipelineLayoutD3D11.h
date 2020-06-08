/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

struct NvAPI_D3D11_RASTERIZER_DESC_EX;

namespace nri
{
    struct DeviceD3D11;
    struct DescriptorSetD3D11;

    struct BindingSet
    {
        uint32_t descriptorNum;
        uint32_t rangeStart;
        uint32_t rangeEnd;
    };

    struct BindingRange
    {
        uint32_t baseSlot;
        uint32_t descriptorNum;
        uint32_t descriptorOffset;
        uint32_t shaderVisibility;
        DescriptorTypeDX11 descriptorType;
    };

    struct ConstantBuffer
    {
        ComPtr<ID3D11Buffer> buffer;
        uint32_t slot;
        uint32_t shaderVisibility;
    };

    struct StaticSampler
    {
        ComPtr<ID3D11SamplerState> sampler;
        uint32_t slot;
        uint32_t shaderVisibility;
    };

    union Vec4
    {
        uint32_t ui[4];
        float f[4];
    };

    struct BindingData
    {
        void** descriptors;
        uint32_t* constantFirst;
        uint32_t* constantNum;
    };

    struct PipelineLayoutD3D11
    {
        PipelineLayoutD3D11(DeviceD3D11& device, const VersionedDevice& versionedDevice);

        DeviceD3D11& GetDevice() const;
        const BindingSet& GetBindingSet(uint32_t set) const;
        const BindingRange& GetBindingRange(uint32_t range) const;
        uint32_t GetDynamicConstantBufferNum() const;

        Result Create(const PipelineLayoutDesc& pipelineDesc);

        void SetConstants(const VersionedContext& context, uint32_t pushConstantIndex, const Vec4* data, uint32_t size) const;

        void BindDescriptorSet(BindingState& currentBindingState, const VersionedContext& context,
            uint32_t setIndex, const DescriptorSetD3D11& descriptorSet, const uint32_t* dynamicConstantBufferOffsets) const;

        void Bind(const VersionedContext& context);

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);

    private:
        template<bool isGraphics>
        void BindDescriptorSetImpl(BindingState& currentBindingState, const VersionedContext& context, uint32_t setIndex,
            const DescriptorSetD3D11& descriptorSet, const uint32_t* dynamicConstantBufferOffsets) const;

        Vector<BindingSet> m_BindingSets;
        Vector<BindingRange> m_BindingRanges;
        Vector<ConstantBuffer> m_ConstantBuffers;
        Vector<StaticSampler> m_StaticSamplers;
        bool m_IsGraphicsPipelineLayout = false;
        uint32_t m_DynamicConstantBufferNum = 0;
        const VersionedDevice& m_VersionedDevice;
        DeviceD3D11& m_Device;
    };

    inline DeviceD3D11& PipelineLayoutD3D11::GetDevice() const
    {
        return m_Device;
    }

    inline const BindingSet& PipelineLayoutD3D11::GetBindingSet(uint32_t set) const
    {
        return m_BindingSets[set];
    }

    inline const BindingRange& PipelineLayoutD3D11::GetBindingRange(uint32_t range) const
    {
        return m_BindingRanges[range];
    }

    inline uint32_t PipelineLayoutD3D11::GetDynamicConstantBufferNum() const
    {
        return m_DynamicConstantBufferNum;
    }

}
