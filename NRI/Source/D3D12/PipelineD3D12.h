/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "DescriptorSetD3D12.h"

namespace nri
{
    struct DeviceD3D12;
    struct PipelineLayoutD3D12;
    struct CommandBufferD3D12;

    struct PipelineD3D12
    {
        PipelineD3D12(DeviceD3D12& device);
        ~PipelineD3D12();

        DeviceD3D12& GetDevice() const;

        operator ID3D12PipelineState*() const;

        Result Create(const GraphicsPipelineDesc& graphicsPipelineDesc);
        Result Create(const ComputePipelineDesc& computePipelineDesc);
        Result Create(const RayTracingPipelineDesc& rayTracingPipelineDesc);
        Result WriteShaderGroupIdentifiers(uint32_t baseShaderGroupIndex, uint32_t shaderGroupNum, void* buffer) const;

        void Bind(ID3D12GraphicsCommandList* graphicsCommandList, D3D12_PRIMITIVE_TOPOLOGY& primitiveTopology) const;
        bool IsGraphicsPipeline() const;
        uint32_t GetIAStreamStride(uint32_t streamSlot) const;
        uint8_t GetSampleNum() const;
        const PipelineLayoutD3D12& GetPipelineLayout() const;

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);

    private:
        Result CreateFromStream(const GraphicsPipelineDesc& graphicsPipelineDesc);
        void FillInputLayout(D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc, const GraphicsPipelineDesc& graphicsPipelineDesc);
        void FillShaderBytecode(D3D12_SHADER_BYTECODE& shaderBytecode, const ShaderDesc& shaderDesc) const;
        void FillRasterizerState(D3D12_RASTERIZER_DESC& rasterizerDesc, const GraphicsPipelineDesc& graphicsPipelineDesc);
        void FillDepthStencilState(D3D12_DEPTH_STENCIL_DESC& depthStencilDesc, const OutputMergerDesc& outputMergerDesc) const;
        void FillBlendState(D3D12_BLEND_DESC& blendDesc, const GraphicsPipelineDesc& graphicsPipelineDesc);
        void FillSampleDesc(DXGI_SAMPLE_DESC& sampleDesc, UINT& sampleMask, const GraphicsPipelineDesc& graphicsPipelineDesc);

        DeviceD3D12& m_Device;
        std::array<uint32_t, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> m_IAStreamStride = {}; // TODO: optimize?
        ComPtr<ID3D12PipelineState> m_PipelineState;
#ifdef __ID3D12Device5_INTERFACE_DEFINED__
        ComPtr<ID3D12StateObject> m_StateObject;
        ComPtr<ID3D12StateObjectProperties> m_StateObjectProperties;
        Vector<std::wstring> m_ShaderGroupNames;
#endif
        const PipelineLayoutD3D12* m_PipelineLayout = nullptr;
        D3D_PRIMITIVE_TOPOLOGY m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        Color<float> m_BlendFactor = {};
        uint8_t m_SampleNum = 1;
        bool m_BlendEnabled = false;
        bool m_IsGraphicsPipeline = false;
    };

    inline PipelineD3D12::PipelineD3D12(DeviceD3D12& device)
        : m_Device(device)
#ifdef __ID3D12Device5_INTERFACE_DEFINED__
        , m_ShaderGroupNames(device.GetStdAllocator())
#endif
    {}

    inline PipelineD3D12::~PipelineD3D12()
    {}

    inline PipelineD3D12::operator ID3D12PipelineState*() const
    {
        return m_PipelineState.GetInterface();
    }

    inline bool PipelineD3D12::IsGraphicsPipeline() const
    {
        return m_IsGraphicsPipeline;
    }

    inline DeviceD3D12& PipelineD3D12::GetDevice() const
    {
        return m_Device;
    }

    inline const PipelineLayoutD3D12& PipelineD3D12::GetPipelineLayout() const
    {
        return *m_PipelineLayout;
    }
}
