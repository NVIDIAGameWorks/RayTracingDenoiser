/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

struct Barriers;

namespace nri
{
    struct DeviceVK;
    struct PipelineVK;
    struct PipelineLayoutVK;
    struct TextureVK;
    struct FrameBufferVK;

    struct CommandBufferVK
    {
        CommandBufferVK(DeviceVK& device);
        ~CommandBufferVK();

        operator VkCommandBuffer() const;
        DeviceVK& GetDevice() const;
        void Create(VkCommandPool commandPool, VkCommandBuffer commandBuffer, CommandQueueType type);
        Result Create(const CommandBufferVulkanDesc& commandBufferDesc);

        void SetDebugName(const char* name);
        Result Begin(const DescriptorPool* descriptorPool, uint32_t physicalDeviceIndex);
        Result End();

        void SetPipeline(const Pipeline& pipeline);
        void SetPipelineLayout(const PipelineLayout& pipelineLayout);
        void SetDescriptorSets(uint32_t baseSlot, uint32_t descriptorSetNum, const DescriptorSet* const* descriptorSets, const uint32_t* dynamicConstantBufferOffsets);
        void SetConstants(uint32_t pushConstantIndex, const void* data, uint32_t size);
        void SetDescriptorPool(const DescriptorPool& descriptorPool);
        void PipelineBarrier(const TransitionBarrierDesc* transitionBarriers, const AliasingBarrierDesc* aliasingBarriers, BarrierDependency dependency);

        void BeginRenderPass(const FrameBuffer& frameBuffer, RenderPassBeginFlag renderPassBeginFlag);
        void EndRenderPass();
        void SetViewports(const Viewport* viewports, uint32_t viewportNum);
        void SetScissors(const Rect* rects, uint32_t rectNum);
        void SetDepthBounds(float boundsMin, float boundsMax);
        void SetStencilReference(uint8_t reference);
        void SetSamplePositions(const SamplePosition* positions, uint32_t positionNum);
        void ClearAttachments(const ClearDesc* clearDescs, uint32_t clearDescNum, const Rect* rects, uint32_t rectNum);
        void SetIndexBuffer(const Buffer& buffer, uint64_t offset, IndexType indexType);
        void SetVertexBuffers(uint32_t baseSlot, uint32_t bufferNum, const Buffer* const* buffers, const uint64_t* offsets);

        void Draw(uint32_t vertexNum, uint32_t instanceNum, uint32_t baseVertex, uint32_t baseInstance);
        void DrawIndexed(uint32_t indexNum, uint32_t instanceNum, uint32_t baseIndex, uint32_t baseVertex, uint32_t baseInstance);
        void DrawIndirect(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride);
        void DrawIndexedIndirect(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride);
        void Dispatch(uint32_t x, uint32_t y, uint32_t z);
        void DispatchIndirect(const Buffer& buffer, uint64_t offset);
        void BeginQuery(const QueryPool& queryPool, uint32_t offset);
        void EndQuery(const QueryPool& queryPool, uint32_t offset);
        void BeginAnnotation(const char* name);
        void EndAnnotation();

        void ClearStorageBuffer(const ClearStorageBufferDesc& clearDesc);

        void ClearStorageTexture(const ClearStorageTextureDesc& clearDesc);

        void CopyBuffer(Buffer& dstBuffer, uint32_t dstPhysicalDeviceIndex, uint64_t dstOffset, const Buffer& srcBuffer,
            uint32_t srcPhysicalDeviceIndex, uint64_t srcOffset, uint64_t size);

        void CopyTexture(Texture& dstTexture, uint32_t dstPhysicalDeviceIndex, const TextureRegionDesc* dstRegionDesc,
            const Texture& srcTexture, uint32_t srcPhysicalDeviceIndex, const TextureRegionDesc* srcRegionDesc);

        void UploadBufferToTexture(Texture& dstTexture, const TextureRegionDesc& dstRegionDesc, const Buffer& srcBuffer,
            const TextureDataLayoutDesc& srcDataLayoutDesc);

        void ReadbackTextureToBuffer(Buffer& dstBuffer, TextureDataLayoutDesc& dstDataLayoutDesc, const Texture& srcTexture,
            const TextureRegionDesc& srcRegionDesc);

        void CopyQueries(const QueryPool& queryPool, uint32_t offset, uint32_t num, Buffer& dstBuffer, uint64_t dstOffset);
        void ResetQueries(const QueryPool& queryPool, uint32_t offset, uint32_t num);

        void BuildTopLevelAccelerationStructure(uint32_t instanceNum, const Buffer& buffer, uint64_t bufferOffset,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch, uint64_t scratchOffset);

        void BuildBottomLevelAccelerationStructure(uint32_t geometryObjectNum, const GeometryObject* geometryObjects,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch, uint64_t scratchOffset);

        void UpdateTopLevelAccelerationStructure(uint32_t instanceNum, const Buffer& buffer, uint64_t bufferOffset,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src, Buffer& scratch,
            uint64_t scratchOffset);

        void UpdateBottomLevelAccelerationStructure(uint32_t geometryObjectNum, const GeometryObject* geometryObjects,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src, Buffer& scratch,
            uint64_t scratchOffset);

        void CopyAccelerationStructure(AccelerationStructure& dst, AccelerationStructure& src, CopyMode copyMode);

        void WriteAccelerationStructureSize(const AccelerationStructure* const* accelerationStructures,
            uint32_t accelerationStructureNum, QueryPool& queryPool, uint32_t queryPoolOffset);

        void DispatchRays(const DispatchRaysDesc& dispatchRaysDesc);
        void DispatchMeshTasks(uint32_t taskNum);

    private:
        void FillAliasingBufferBarriers(const AliasingBarrierDesc& aliasing, Barriers& barriers) const;
        void FillAliasingImageBarriers(const AliasingBarrierDesc& aliasing, Barriers& barriers) const;
        void FillTransitionBufferBarriers(const TransitionBarrierDesc& transitions, Barriers& barriers) const;
        void FillTransitionImageBarriers(const TransitionBarrierDesc& transitions, Barriers& barriers) const;
        void CopyWholeTexture(const TextureVK& dstTexture, uint32_t dstPhysicalDeviceIndex, const TextureVK& srcTexture, uint32_t srcPhysicalDeviceIndex);

        const DispatchTable& m_VK;
        VkCommandBuffer m_Handle = VK_NULL_HANDLE;
        uint32_t m_PhysicalDeviceIndex = 0;
        DeviceVK& m_Device;
        CommandQueueType m_Type = (CommandQueueType)0;
        VkPipelineBindPoint m_CurrentPipelineBindPoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;
        VkPipelineLayout m_CurrentPipelineLayoutHandle = VK_NULL_HANDLE;
        const PipelineVK* m_CurrentPipeline = nullptr;
        const PipelineLayoutVK* m_CurrentPipelineLayout = nullptr;
        const FrameBufferVK* m_CurrentFrameBuffer = nullptr;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    };

    inline CommandBufferVK::operator VkCommandBuffer() const
    {
        return m_Handle;
    }

    inline DeviceVK& CommandBufferVK::GetDevice() const
    {
        return m_Device;
    }
}
