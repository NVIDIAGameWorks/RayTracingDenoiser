/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

struct ID3D12GraphicsCommandList;
struct ID3D12GraphicsCommandList4;
struct ID3D12CommandAllocator;
struct ID3D12Resource;

namespace nri
{
    struct DeviceD3D12;
    struct PipelineD3D12;
    struct PipelineLayoutD3D12;
    struct DescriptorPoolD3D12;
    struct FrameBufferD3D12;
    struct DescriptorSetD3D12;

    struct CommandBufferD3D12
    {
        CommandBufferD3D12(DeviceD3D12& device);
        ~CommandBufferD3D12();

        operator ID3D12GraphicsCommandList*() const;

        DeviceD3D12& GetDevice() const;

        Result Create(D3D12_COMMAND_LIST_TYPE commandListType, ID3D12CommandAllocator* commandAllocator);
        Result Create(const CommandBufferD3D12Desc& commandBufferDesc);

        //================================================================================================================
        // NRI
        //================================================================================================================
        void SetDebugName(const char* name);

        Result Begin(const DescriptorPool* descriptorPool);
        Result End();
        void SetViewports(const Viewport* viewports, uint32_t viewportNum);
        void SetScissors(const Rect* rects, uint32_t rectNum);
        void SetDepthBounds(float boundsMin, float boundsMax);
        void SetStencilReference(uint8_t reference);
        void SetSamplePositions(const SamplePosition* positions, uint32_t positionNum);
        void ClearAttachments(const ClearDesc* clearDescs, uint32_t clearDescNum, const Rect* rects, uint32_t rectNum);
        void ClearStorageBuffer(const ClearStorageBufferDesc& clearDesc);
        void ClearStorageTexture(const ClearStorageTextureDesc& clearDesc);
        void BeginRenderPass(const FrameBuffer& frameBuffer, FramebufferBindFlag bindFlag);
        void EndRenderPass();
        void SetVertexBuffers(uint32_t baseSlot, uint32_t bufferNum, const Buffer* const* buffers, const uint64_t* offsets);
        void SetIndexBuffer(const Buffer& buffer, uint64_t offset, IndexType indexType);
        void SetPipelineLayout(const PipelineLayout& pipelineLayout);
        void SetPipeline(const Pipeline& pipeline);
        void SetDescriptorPool(const DescriptorPool& descriptorPool);
        void SetDescriptorSets(uint32_t baseIndex, uint32_t setNum, const DescriptorSet* const* descriptorSets, const uint32_t* offsets);
        void SetConstants(uint32_t pushConstantRangeIndex, const void* data, uint32_t size);
        void Draw(uint32_t vertexNum, uint32_t instanceNum, uint32_t baseVertex, uint32_t baseInstance);
        void DrawIndexed(uint32_t indexNum, uint32_t instanceNum, uint32_t baseIndex, uint32_t baseVertex, uint32_t baseInstance);
        void DrawIndirect(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride);
        void DrawIndexedIndirect(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride);
        void CopyBuffer(Buffer& dstBuffer, uint64_t dstOffset, const Buffer& srcBuffer, uint64_t srcOffset, uint64_t size);
        void CopyTexture(Texture& dstTexture, const TextureRegionDesc* dstRegionDesc, const Texture& srcTexture, const TextureRegionDesc* srcRegionDesc);
        void UploadBufferToTexture(Texture& dstTexture, const TextureRegionDesc& dstRegionDesc, const Buffer& srcBuffer, const TextureDataLayoutDesc& srcDataLayoutDesc);
        void ReadbackTextureToBuffer(Buffer& dstBuffer, TextureDataLayoutDesc& dstDataLayoutDesc, const Texture& srcTexture, const TextureRegionDesc& srcRegionDesc);
        void Dispatch(uint32_t x, uint32_t y, uint32_t z);
        void DispatchIndirect(const Buffer& buffer, uint64_t offset);
        void PipelineBarrier(const TransitionBarrierDesc* transitionBarriers, const AliasingBarrierDesc* aliasingBarriers, BarrierDependency dependency);
        void BeginQuery(const QueryPool& queryPool, uint32_t offset);
        void EndQuery(const QueryPool& queryPool, uint32_t offset);
        void CopyQueries(const QueryPool& queryPool, uint32_t offset, uint32_t num, Buffer& buffer, uint64_t alignedBufferOffset);
        void BeginAnnotation(const char* name);
        void EndAnnotation();

        void BuildTopLevelAccelerationStructure(uint32_t instanceNum, const Buffer& buffer, uint64_t bufferOffset,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch, uint64_t scratchOffset);
        void BuildBottomLevelAccelerationStructure(uint32_t geometryObjectNum, const GeometryObject* geometryObjects,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch, uint64_t scratchOffset);
        void UpdateTopLevelAccelerationStructure(uint32_t instanceNum, const Buffer& buffer, uint64_t bufferOffset,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src, Buffer& scratch, uint64_t scratchOffset);
        void UpdateBottomLevelAccelerationStructure(uint32_t geometryObjectNum, const GeometryObject* geometryObjects,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src, Buffer& scratch, uint64_t scratchOffset);
        void CopyAccelerationStructure(AccelerationStructure& dst, AccelerationStructure& src, CopyMode copyMode);
        void WriteAccelerationStructureSize(const AccelerationStructure* const* accelerationStructures, uint32_t accelerationStructureNum, QueryPool& queryPool, uint32_t queryOffset);
        void DispatchRays(const DispatchRaysDesc& dispatchRaysDesc);

    private:
        static void AddResourceBarrier(ID3D12Resource* resource, AccessBits before, AccessBits after, D3D12_RESOURCE_BARRIER& resourceBarrier, uint32_t subresource);

        DeviceD3D12& m_Device;
        ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
        ComPtr<ID3D12GraphicsCommandList> m_GraphicsCommandList;
        ComPtr<ID3D12GraphicsCommandList1> m_GraphicsCommandList1;
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
        ComPtr<ID3D12GraphicsCommandList4> m_GraphicsCommandList4;
#endif
        const PipelineLayoutD3D12* m_PipelineLayout = nullptr;
        bool m_IsGraphicsPipelineLayout = false;
        PipelineD3D12* m_Pipeline = nullptr;
        FrameBufferD3D12* m_FrameBuffer = nullptr;
        D3D12_PRIMITIVE_TOPOLOGY m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        std::array<DescriptorSetD3D12*, 64> m_DescriptorSets;
    };

    inline CommandBufferD3D12::CommandBufferD3D12(DeviceD3D12& device)
        : m_Device(device)
    {}

    inline CommandBufferD3D12::~CommandBufferD3D12()
    {}

    inline CommandBufferD3D12::operator ID3D12GraphicsCommandList*() const
    {
        return m_GraphicsCommandList.GetInterface();
    }

    inline DeviceD3D12& CommandBufferD3D12::GetDevice() const
    {
        return m_Device;
    }
}
