/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static void NRI_CALL SetCommandBufferDebugName(CommandBuffer& commandBuffer, const char* name)
{
    ((CommandBufferVal*)&commandBuffer)->SetDebugName(name);
}

static Result NRI_CALL BeginCommandBuffer(CommandBuffer& commandBuffer, const DescriptorPool* descriptorPool, uint32_t physicalDeviceIndex)
{
    return ((CommandBufferVal*)&commandBuffer)->Begin(descriptorPool, physicalDeviceIndex);
}

static Result NRI_CALL EndCommandBuffer(CommandBuffer& commandBuffer)
{
    return ((CommandBufferVal*)&commandBuffer)->End();
}

static void NRI_CALL CmdSetPipelineLayout(CommandBuffer& commandBuffer, const PipelineLayout& pipelineLayout)
{
    ((CommandBufferVal*)&commandBuffer)->SetPipelineLayout(pipelineLayout);
}

static void NRI_CALL CmdSetPipeline(CommandBuffer& commandBuffer, const Pipeline& pipeline)
{
    ((CommandBufferVal*)&commandBuffer)->SetPipeline(pipeline);
}

static void NRI_CALL CmdPipelineBarrier(CommandBuffer& commandBuffer, const TransitionBarrierDesc* transitionBarriers, const AliasingBarrierDesc* aliasingBarriers, BarrierDependency dependency)
{
    ((CommandBufferVal*)&commandBuffer)->PipelineBarrier(transitionBarriers, aliasingBarriers, dependency);
}

static void NRI_CALL CmdSetDescriptorPool(CommandBuffer& commandBuffer, const DescriptorPool& descriptorPool)
{
    ((CommandBufferVal*)&commandBuffer)->SetDescriptorPool(descriptorPool);
}

static void NRI_CALL CmdSetDescriptorSets(CommandBuffer& commandBuffer, uint32_t baseSlot, uint32_t descriptorSetNum, const DescriptorSet* const* descriptorSets, const uint32_t* dynamicConstantBufferOffsets)
{
    ((CommandBufferVal*)&commandBuffer)->SetDescriptorSets(baseSlot, descriptorSetNum, descriptorSets, dynamicConstantBufferOffsets);
}

static void NRI_CALL CmdSetConstants(CommandBuffer& commandBuffer, uint32_t pushConstantIndex, const void* data, uint32_t size)
{
    ((CommandBufferVal*)&commandBuffer)->SetConstants(pushConstantIndex, data, size);
}

static void NRI_CALL CmdBeginRenderPass(CommandBuffer& commandBuffer, const FrameBuffer& frameBuffer, FramebufferBindFlag bindFlag)
{
    ((CommandBufferVal*)&commandBuffer)->BeginRenderPass(frameBuffer, bindFlag);
}

static void NRI_CALL CmdEndRenderPass(CommandBuffer& commandBuffer)
{
    ((CommandBufferVal*)&commandBuffer)->EndRenderPass();
}

static void NRI_CALL CmdSetViewports(CommandBuffer& commandBuffer, const Viewport* viewports, uint32_t viewportNum)
{
    ((CommandBufferVal*)&commandBuffer)->SetViewports(viewports, viewportNum);
}

static void NRI_CALL CmdSetScissors(CommandBuffer& commandBuffer, const Rect* rects, uint32_t rectNum)
{
    ((CommandBufferVal*)&commandBuffer)->SetScissors(rects, rectNum);
}

static void NRI_CALL CmdSetDepthBounds(CommandBuffer& commandBuffer, float boundsMin, float boundsMax)
{
    ((CommandBufferVal*)&commandBuffer)->SetDepthBounds(boundsMin, boundsMax);
}

static void NRI_CALL CmdSetStencilReference(CommandBuffer& commandBuffer, uint8_t reference)
{
    ((CommandBufferVal*)&commandBuffer)->SetStencilReference(reference);
}

static void NRI_CALL CmdSetSamplePositions(CommandBuffer& commandBuffer, const SamplePosition* positions, uint32_t positionNum)
{
    ((CommandBufferVal*)&commandBuffer)->SetSamplePositions(positions, positionNum);
}

static void NRI_CALL CmdClearAttachments(CommandBuffer& commandBuffer, const ClearDesc* clearDescs, uint32_t clearDescNum, const Rect* rects, uint32_t rectNum)
{
    ((CommandBufferVal*)&commandBuffer)->ClearAttachments(clearDescs, clearDescNum, rects, rectNum);
}

static void NRI_CALL CmdSetIndexBuffer(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset, IndexType indexType)
{
    ((CommandBufferVal*)&commandBuffer)->SetIndexBuffer(buffer, offset, indexType);
}

static void NRI_CALL CmdSetVertexBuffers(CommandBuffer& commandBuffer, uint32_t baseSlot, uint32_t bufferNum, const Buffer* const* buffers, const uint64_t* offsets)
{
    ((CommandBufferVal*)&commandBuffer)->SetVertexBuffers(baseSlot, bufferNum, buffers, offsets);
}

static void NRI_CALL CmdDraw(CommandBuffer& commandBuffer, uint32_t vertexNum, uint32_t instanceNum, uint32_t baseVertex, uint32_t baseInstance)
{
    ((CommandBufferVal*)&commandBuffer)->Draw(vertexNum, instanceNum, baseVertex, baseInstance);
}

static void NRI_CALL CmdDrawIndexed(CommandBuffer& commandBuffer, uint32_t indexNum, uint32_t instanceNum, uint32_t baseIndex, uint32_t baseVertex, uint32_t baseInstance)
{
    ((CommandBufferVal*)&commandBuffer)->DrawIndexed(indexNum, instanceNum, baseIndex, baseVertex, baseInstance);
}

static void NRI_CALL CmdDrawIndirect(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride)
{
    ((CommandBufferVal*)&commandBuffer)->DrawIndirect(buffer, offset, drawNum, stride);
}

static void NRI_CALL CmdDrawIndexedIndirect(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride)
{
    ((CommandBufferVal*)&commandBuffer)->DrawIndexedIndirect(buffer, offset, drawNum, stride);
}

static void NRI_CALL CmdDispatch(CommandBuffer& commandBuffer, uint32_t x, uint32_t y, uint32_t z)
{
    ((CommandBufferVal*)&commandBuffer)->Dispatch(x, y, z);
}

static void NRI_CALL CmdDispatchIndirect(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset)
{
    ((CommandBufferVal*)&commandBuffer)->DispatchIndirect(buffer, offset);
}

static void NRI_CALL CmdBeginQuery(CommandBuffer& commandBuffer, const QueryPool& queryPool, uint32_t offset)
{
    ((CommandBufferVal*)&commandBuffer)->BeginQuery(queryPool, offset);
}

static void NRI_CALL CmdEndQuery(CommandBuffer& commandBuffer, const QueryPool& queryPool, uint32_t offset)
{
    ((CommandBufferVal*)&commandBuffer)->EndQuery(queryPool, offset);
}

static void NRI_CALL CmdBeginAnnotation(CommandBuffer& commandBuffer, const char* name)
{
    ((CommandBufferVal*)&commandBuffer)->BeginAnnotation(name);
}

static void NRI_CALL CmdEndAnnotation(CommandBuffer& commandBuffer)
{
    ((CommandBufferVal*)&commandBuffer)->EndAnnotation();
}

static void NRI_CALL CmdClearStorageBuffer(CommandBuffer& commandBuffer, const ClearStorageBufferDesc& clearDesc)
{
    ((CommandBufferVal*)&commandBuffer)->ClearStorageBuffer(clearDesc);
}

static void NRI_CALL CmdClearStorageTexture(CommandBuffer& commandBuffer, const ClearStorageTextureDesc& clearDesc)
{
    ((CommandBufferVal*)&commandBuffer)->ClearStorageTexture(clearDesc);
}

static void NRI_CALL CmdCopyBuffer(CommandBuffer& commandBuffer, Buffer& dstBuffer, uint32_t dstPhysicalDeviceIndex, uint64_t dstOffset, const Buffer& srcBuffer, uint32_t srcPhysicalDeviceIndex, uint64_t srcOffset, uint64_t size)
{
    ((CommandBufferVal*)&commandBuffer)->CopyBuffer(dstBuffer, dstPhysicalDeviceIndex, dstOffset, srcBuffer, srcPhysicalDeviceIndex, srcOffset, size);
}

static void NRI_CALL CmdCopyTexture(CommandBuffer& commandBuffer, Texture& dstTexture, uint32_t dstPhysicalDeviceIndex, const TextureRegionDesc* dstRegionDesc, const Texture& srcTexture, uint32_t srcPhysicalDeviceIndex, const TextureRegionDesc* srcRegionDesc)
{
    ((CommandBufferVal*)&commandBuffer)->CopyTexture(dstTexture, dstPhysicalDeviceIndex, dstRegionDesc, srcTexture, srcPhysicalDeviceIndex, srcRegionDesc);
}

static void NRI_CALL CmdUploadBufferToTexture(CommandBuffer& commandBuffer, Texture& dstTexture, const TextureRegionDesc& dstRegionDesc, const Buffer& srcBuffer, const TextureDataLayoutDesc& srcDataLayoutDesc)
{
    ((CommandBufferVal*)&commandBuffer)->UploadBufferToTexture(dstTexture, dstRegionDesc, srcBuffer, srcDataLayoutDesc);
}

static void NRI_CALL CmdReadbackTextureToBuffer(CommandBuffer& commandBuffer, Buffer& dstBuffer, TextureDataLayoutDesc& dstDataLayoutDesc, const Texture& srcTexture, const TextureRegionDesc& srcRegionDesc)
{
    ((CommandBufferVal*)&commandBuffer)->ReadbackTextureToBuffer(dstBuffer, dstDataLayoutDesc, srcTexture, srcRegionDesc);
}

static void NRI_CALL CmdCopyQueries(CommandBuffer& commandBuffer, const QueryPool& queryPool, uint32_t offset, uint32_t num, Buffer& dstBuffer, uint64_t dstOffset)
{
    ((CommandBufferVal*)&commandBuffer)->CopyQueries(queryPool, offset, num, dstBuffer, dstOffset);
}

static void NRI_CALL DestroyCommandBuffer(CommandBuffer& commandBuffer)
{
    ((CommandBufferVal*)&commandBuffer)->Destroy();
}

void FillFunctionTableCommandBufferVal(CoreInterface& coreInterface)
{
    coreInterface.SetCommandBufferDebugName = SetCommandBufferDebugName;
    coreInterface.DestroyCommandBuffer = DestroyCommandBuffer;

    coreInterface.BeginCommandBuffer = BeginCommandBuffer;
    coreInterface.EndCommandBuffer = EndCommandBuffer;

    coreInterface.CmdSetPipelineLayout = CmdSetPipelineLayout;
    coreInterface.CmdSetPipeline = CmdSetPipeline;
    coreInterface.CmdPipelineBarrier = CmdPipelineBarrier;
    coreInterface.CmdSetDescriptorPool = CmdSetDescriptorPool;
    coreInterface.CmdSetDescriptorSets = CmdSetDescriptorSets;
    coreInterface.CmdSetConstants = CmdSetConstants;

    coreInterface.CmdBeginRenderPass = CmdBeginRenderPass;
    coreInterface.CmdEndRenderPass = CmdEndRenderPass;
    coreInterface.CmdSetViewports = CmdSetViewports;
    coreInterface.CmdSetScissors = CmdSetScissors;
    coreInterface.CmdSetDepthBounds = CmdSetDepthBounds;
    coreInterface.CmdSetStencilReference = CmdSetStencilReference;
    coreInterface.CmdSetSamplePositions = CmdSetSamplePositions;
    coreInterface.CmdClearAttachments = CmdClearAttachments;
    coreInterface.CmdSetIndexBuffer = CmdSetIndexBuffer;
    coreInterface.CmdSetVertexBuffers = CmdSetVertexBuffers;

    coreInterface.CmdDraw = CmdDraw;
    coreInterface.CmdDrawIndexed = CmdDrawIndexed;
    coreInterface.CmdDrawIndirect = CmdDrawIndirect;
    coreInterface.CmdDrawIndexedIndirect = CmdDrawIndexedIndirect;
    coreInterface.CmdDispatch = CmdDispatch;
    coreInterface.CmdDispatchIndirect = CmdDispatchIndirect;
    coreInterface.CmdBeginQuery = CmdBeginQuery;
    coreInterface.CmdEndQuery = CmdEndQuery;
    coreInterface.CmdBeginAnnotation = CmdBeginAnnotation;
    coreInterface.CmdEndAnnotation = CmdEndAnnotation;

    coreInterface.CmdClearStorageBuffer = CmdClearStorageBuffer;
    coreInterface.CmdClearStorageTexture = CmdClearStorageTexture;
    coreInterface.CmdCopyBuffer = CmdCopyBuffer;
    coreInterface.CmdCopyTexture = CmdCopyTexture;
    coreInterface.CmdUploadBufferToTexture = CmdUploadBufferToTexture;
    coreInterface.CmdReadbackTextureToBuffer = CmdReadbackTextureToBuffer;
    coreInterface.CmdCopyQueries = CmdCopyQueries;
}

#pragma endregion

#pragma region [  WrapperD3D11Interface  ]

static ID3D11DeviceContext* NRI_CALL GetCommandBufferD3D11(const CommandBuffer& commandBuffer)
{
    return ((CommandBufferVal*)&commandBuffer)->GetCommandBufferD3D11();
}

void FillFunctionTableCommandBufferVal(WrapperD3D11Interface& wrapperD3D11Interface)
{
    wrapperD3D11Interface.GetCommandBufferD3D11 = GetCommandBufferD3D11;
}

#pragma endregion

#pragma region [  WrapperD3D12Interface  ]

static ID3D12GraphicsCommandList* NRI_CALL GetCommandBufferD3D12(const CommandBuffer& commandBuffer)
{
    return ((CommandBufferVal*)&commandBuffer)->GetCommandBufferD3D12();
}

void FillFunctionTableCommandBufferVal(WrapperD3D12Interface& wrapperD3D12Interface)
{
    wrapperD3D12Interface.GetCommandBufferD3D12 = GetCommandBufferD3D12;
}

#pragma endregion

#pragma region [  WrapperVKInterface  ]

static VkCommandBuffer NRI_CALL GetCommandBufferVK(const CommandBuffer& commandBuffer)
{
    return ((CommandBufferVal&)commandBuffer).GetCommandBufferVK();
}

void FillFunctionTableCommandBufferVal(WrapperVKInterface& wrapperVKInterface)
{
    wrapperVKInterface.GetCommandBufferVK = ::GetCommandBufferVK;
}

#pragma endregion

#pragma region [  RayTracingInterface  ]

static void NRI_CALL CmdBuildTopLevelAccelerationStructure(CommandBuffer& commandBuffer, uint32_t instanceNum, const Buffer& buffer, uint64_t bufferOffset,
    AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch, uint64_t scratchOffset)
{
    ((CommandBufferVal*)&commandBuffer)->BuildTopLevelAccelerationStructure(instanceNum, buffer, bufferOffset, flags, dst, scratch, scratchOffset);
}

static void NRI_CALL CmdBuildBottomLevelAccelerationStructure(CommandBuffer& commandBuffer, uint32_t geometryObjectNum, const GeometryObject* geometryObjects,
    AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch, uint64_t scratchOffset)
{
    ((CommandBufferVal*)&commandBuffer)->BuildBottomLevelAccelerationStructure(geometryObjectNum, geometryObjects, flags, dst, scratch, scratchOffset);
}

static void NRI_CALL CmdUpdateTopLevelAccelerationStructure(CommandBuffer& commandBuffer, uint32_t instanceNum, const Buffer& buffer, uint64_t bufferOffset,
    AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src, Buffer& scratch, uint64_t scratchOffset)
{
    ((CommandBufferVal*)&commandBuffer)->UpdateTopLevelAccelerationStructure(instanceNum, buffer, bufferOffset, flags, dst, src, scratch, scratchOffset);
}

static void NRI_CALL CmdUpdateBottomLevelAccelerationStructure(CommandBuffer& commandBuffer, uint32_t geometryObjectNum, const GeometryObject* geometryObjects,
    AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src, Buffer& scratch, uint64_t scratchOffset)
{
    ((CommandBufferVal*)&commandBuffer)->UpdateBottomLevelAccelerationStructure(geometryObjectNum, geometryObjects, flags, dst, src, scratch, scratchOffset);
}

static void NRI_CALL CmdCopyAccelerationStructure(CommandBuffer& commandBuffer, AccelerationStructure& dst, AccelerationStructure& src, CopyMode mode)
{
    ((CommandBufferVal*)&commandBuffer)->CopyAccelerationStructure(dst, src, mode);
}

static void NRI_CALL CmdWriteAccelerationStructureSize(CommandBuffer& commandBuffer, const AccelerationStructure* const* accelerationStructures,
    uint32_t accelerationStructureNum, QueryPool& queryPool, uint32_t queryOffset)
{
    ((CommandBufferVal*)&commandBuffer)->WriteAccelerationStructureSize(accelerationStructures, accelerationStructureNum, queryPool, queryOffset);
}

static void NRI_CALL CmdDispatchRays(CommandBuffer& commandBuffer, const DispatchRaysDesc& dispatchRaysDesc)
{
    ((CommandBufferVal*)&commandBuffer)->DispatchRays(dispatchRaysDesc);
}

void FillFunctionTableCommandBufferVal(RayTracingInterface& rayTracingInterface)
{
    rayTracingInterface.CmdBuildTopLevelAccelerationStructure = CmdBuildTopLevelAccelerationStructure;
    rayTracingInterface.CmdBuildBottomLevelAccelerationStructure = CmdBuildBottomLevelAccelerationStructure;
    rayTracingInterface.CmdUpdateTopLevelAccelerationStructure = CmdUpdateTopLevelAccelerationStructure;
    rayTracingInterface.CmdUpdateBottomLevelAccelerationStructure = CmdUpdateBottomLevelAccelerationStructure;
    rayTracingInterface.CmdCopyAccelerationStructure = CmdCopyAccelerationStructure;
    rayTracingInterface.CmdWriteAccelerationStructureSize = CmdWriteAccelerationStructureSize;
    rayTracingInterface.CmdDispatchRays = CmdDispatchRays;

}

#pragma endregion
