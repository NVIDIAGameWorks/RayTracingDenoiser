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
    struct PipelineD3D11;
    typedef Vector<uint32_t> PushBuffer;

    struct CommandBufferEmuD3D11 final : public CommandBufferHelper
    {
        CommandBufferEmuD3D11(DeviceD3D11& deviceImpl);
        ~CommandBufferEmuD3D11();

        //======================================================================================================================
        // nri::CommandBufferHelper
        //======================================================================================================================
        Result Create(ID3D11DeviceContext* precreatedContext);
        void Submit(const VersionedContext& context);
        StdAllocator<uint8_t>& GetStdAllocator() const;

        //======================================================================================================================
        // NRI
        //======================================================================================================================
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
        void BeginRenderPass(const FrameBuffer& frameBuffer, RenderPassBeginFlag renderPassBeginFlag);
        void EndRenderPass();
        void SetVertexBuffers(uint32_t baseSlot, uint32_t bufferNum, const Buffer* const* buffers, const uint64_t* offsets);
        void SetIndexBuffer(const Buffer& buffer, uint64_t offset, IndexType indexType);
        void SetPipelineLayout(const PipelineLayout& pipelineLayout);
        void SetPipeline(const Pipeline& pipeline);
        void SetDescriptorPool(const DescriptorPool& descriptorPool);
        void SetDescriptorSets(uint32_t baseIndex, uint32_t descriptorSetNum, const DescriptorSet* const* descriptorSets, const uint32_t* constantBufferOffsets);
        void SetConstants(uint32_t pushConstantIndex, const void* data, uint32_t size);
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
        void CopyQueries(const QueryPool& queryPool, uint32_t offset, uint32_t num, Buffer& dstBuffer, uint64_t dstOffset);
        void BeginAnnotation(const char* name);
        void EndAnnotation();

    private:
        const VersionedDevice& m_Device;
        PushBuffer m_PushBuffer;
        uint32_t m_DynamicConstantBufferNum = 0;
        DeviceD3D11& m_DeviceImpl;
    };
}
