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
    struct PipelineLayoutD3D11;

    struct RasterizerState
    {
        ComPtr<ID3D11RasterizerState2> ptr;
        uint64_t samplePositionHash = 0;
    };

    struct PipelineD3D11
    {
        PipelineD3D11(DeviceD3D11& device, const VersionedDevice& versionedDevice);
        ~PipelineD3D11();

        inline DeviceD3D11& GetDevice() const
        { return m_Device; }

        inline uint32_t GetInputAssemblyStride(uint32_t bindingSlot) const
        { return m_InputAssemplyStrides[bindingSlot]; }

        inline bool IsValid() const
        { return &m_VersionedDevice != nullptr; }

        inline bool IsCompute() const
        { return m_ComputeShader != nullptr; }

        inline bool IsRasterizerDiscarded() const
        { return m_IsRasterizerDiscarded; }

        Result Create(const GraphicsPipelineDesc& pipelineDesc);
        Result Create(const ComputePipelineDesc& pipelineDesc);
        void Bind(const VersionedContext& context, const PipelineD3D11* currentPipeline) const;

        // dynamic state
        void ChangeSamplePositions(const VersionedContext& context, const SamplePositionsState& samplePositionState, DynamicState mode);
        void ChangeStencilReference(const VersionedContext& context, uint8_t stencilRef, DynamicState mode);

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);

    private:
        const VersionedDevice& m_VersionedDevice;

        Vector<uint32_t> m_InputAssemplyStrides;
        Vector<RasterizerState> m_RasterizerStates;

        ComPtr<ID3D11VertexShader> m_VertexShader;
        ComPtr<ID3D11HullShader> m_TessControlShader;
        ComPtr<ID3D11DomainShader> m_TessEvaluationShader;
        ComPtr<ID3D11GeometryShader> m_GeometryShader;
        ComPtr<ID3D11PixelShader> m_FragmentShader;
        ComPtr<ID3D11ComputeShader> m_ComputeShader;
        ComPtr<ID3D11InputLayout> m_InputLayout;
        ComPtr<ID3D11DepthStencilState> m_DepthStencilState;
        ComPtr<ID3D11BlendState1> m_BlendState;

        ID3D11RasterizerState2* m_RasterizerState = nullptr;
        NvAPI_D3D11_RASTERIZER_DESC_EX* m_RasterizerStateExDesc = nullptr;

        D3D11_PRIMITIVE_TOPOLOGY m_Topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        Color<float> m_BlendFactor = {};
        uint16_t m_SampleMask = 0;
        uint16_t m_StencilRef = uint16_t(-1);
        bool m_IsRasterizerDiscarded = false;
        const PipelineLayoutD3D11* m_PipelineLayout = nullptr;
        DeviceD3D11& m_Device;

        static PipelineD3D11* s_NullGraphicsPipeline;

    public:

        static PipelineD3D11* GetNullPipeline()
        { return s_NullGraphicsPipeline; }

        static void CreateNullPipeline(DeviceD3D11& device)
        { s_NullGraphicsPipeline = Allocate<PipelineD3D11>(device.GetStdAllocator(), device, *(VersionedDevice*)nullptr); }

        static void DestroyNullPipeline(StdAllocator<uint8_t>& allocator)
        { Deallocate(allocator, s_NullGraphicsPipeline); }
    };
}
