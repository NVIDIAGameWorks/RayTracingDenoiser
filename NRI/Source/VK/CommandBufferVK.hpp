/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#pragma region [  CoreInterface  ]

static void NRI_CALL SetCommandBufferDebugName(CommandBuffer& commandBuffer, const char* name)
{
    ((CommandBufferVK&)commandBuffer).SetDebugName(name);
}

static Result NRI_CALL BeginCommandBuffer(CommandBuffer& commandBuffer, const DescriptorPool* descriptorPool, uint32_t physicalDeviceIndex)
{
    return ((CommandBufferVK&)commandBuffer).Begin(descriptorPool, physicalDeviceIndex);
}

static Result NRI_CALL EndCommandBuffer(CommandBuffer& commandBuffer)
{
    return ((CommandBufferVK&)commandBuffer).End();
}

static void NRI_CALL CmdSetPipelineLayout(CommandBuffer& commandBuffer, const PipelineLayout& pipelineLayout)
{
    ((CommandBufferVK&)commandBuffer).SetPipelineLayout(pipelineLayout);
}

static void NRI_CALL CmdSetPipeline(CommandBuffer& commandBuffer, const Pipeline& pipeline)
{
    ((CommandBufferVK&)commandBuffer).SetPipeline(pipeline);
}

static void NRI_CALL CmdPipelineBarrier(CommandBuffer& commandBuffer, const TransitionBarrierDesc* transitionBarriers,
    const AliasingBarrierDesc* aliasingBarriers, BarrierDependency dependency)
{
    ((CommandBufferVK&)commandBuffer).PipelineBarrier(transitionBarriers, aliasingBarriers, dependency);
}

static void NRI_CALL CmdSetDescriptorPool(CommandBuffer& commandBuffer, const DescriptorPool& descriptorPool)
{
    ((CommandBufferVK&)commandBuffer).SetDescriptorPool(descriptorPool);
}

static void NRI_CALL CmdSetDescriptorSets(CommandBuffer& commandBuffer, uint32_t baseSlot, uint32_t descriptorSetNum,
    const DescriptorSet* const* descriptorSets, const uint32_t* dynamicConstantBufferOffsets)
{
    ((CommandBufferVK&)commandBuffer).SetDescriptorSets(baseSlot, descriptorSetNum, descriptorSets,
        dynamicConstantBufferOffsets);
}

static void NRI_CALL CmdSetConstants(CommandBuffer& commandBuffer, uint32_t pushConstantIndex, const void* data,
    uint32_t size)
{
    ((CommandBufferVK&)commandBuffer).SetConstants(pushConstantIndex, data, size);
}

static void NRI_CALL CmdBeginRenderPass(CommandBuffer& commandBuffer, const FrameBuffer& frameBuffer,
    RenderPassBeginFlag renderPassBeginFlag)
{
    ((CommandBufferVK&)commandBuffer).BeginRenderPass(frameBuffer, renderPassBeginFlag);
}

static void NRI_CALL CmdEndRenderPass(CommandBuffer& commandBuffer)
{
    ((CommandBufferVK&)commandBuffer).EndRenderPass();
}

static void NRI_CALL CmdSetViewports(CommandBuffer& commandBuffer, const Viewport* viewports, uint32_t viewportNum)
{
    ((CommandBufferVK&)commandBuffer).SetViewports(viewports, viewportNum);
}

static void NRI_CALL CmdSetScissors(CommandBuffer& commandBuffer, const Rect* rects, uint32_t rectNum)
{
    ((CommandBufferVK&)commandBuffer).SetScissors(rects, rectNum);
}

static void NRI_CALL CmdSetDepthBounds(CommandBuffer& commandBuffer, float boundsMin, float boundsMax)
{
    ((CommandBufferVK&)commandBuffer).SetDepthBounds(boundsMin, boundsMax);
}

static void NRI_CALL CmdSetStencilReference(CommandBuffer& commandBuffer, uint8_t reference)
{
    ((CommandBufferVK&)commandBuffer).SetStencilReference(reference);
}

static void NRI_CALL CmdSetSamplePositions(CommandBuffer& commandBuffer, const SamplePosition* positions,
    uint32_t positionNum)
{
    ((CommandBufferVK&)commandBuffer).SetSamplePositions(positions, positionNum);
}

static void NRI_CALL CmdClearAttachments(CommandBuffer& commandBuffer, const ClearDesc* clearDescs,
    uint32_t clearDescNum, const Rect* rects, uint32_t rectNum)
{
    ((CommandBufferVK&)commandBuffer).ClearAttachments(clearDescs, clearDescNum, rects, rectNum);
}

static void NRI_CALL CmdSetIndexBuffer(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset,
    IndexType indexType)
{
    ((CommandBufferVK&)commandBuffer).SetIndexBuffer(buffer, offset, indexType);
}

static void NRI_CALL CmdSetVertexBuffers(CommandBuffer& commandBuffer, uint32_t baseSlot, uint32_t bufferNum,
    const Buffer* const* buffers, const uint64_t* offsets)
{
    ((CommandBufferVK&)commandBuffer).SetVertexBuffers(baseSlot, bufferNum, buffers, offsets);
}

static void NRI_CALL CmdDraw(CommandBuffer& commandBuffer, uint32_t vertexNum, uint32_t instanceNum,
    uint32_t baseVertex, uint32_t baseInstance)
{
    ((CommandBufferVK&)commandBuffer).Draw(vertexNum, instanceNum, baseVertex, baseInstance);
}

static void NRI_CALL CmdDrawIndexed(CommandBuffer& commandBuffer, uint32_t indexNum, uint32_t instanceNum,
    uint32_t baseIndex, uint32_t baseVertex, uint32_t baseInstance)
{
    ((CommandBufferVK&)commandBuffer).DrawIndexed(indexNum, instanceNum, baseIndex, baseVertex, baseInstance);
}

static void NRI_CALL CmdDrawIndirect(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset,
    uint32_t drawNum, uint32_t stride)
{
    ((CommandBufferVK&)commandBuffer).DrawIndirect(buffer, offset, drawNum, stride);
}

static void NRI_CALL CmdDrawIndexedIndirect(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset,
    uint32_t drawNum, uint32_t stride)
{
    ((CommandBufferVK&)commandBuffer).DrawIndexedIndirect(buffer, offset, drawNum, stride);
}

static void NRI_CALL CmdDispatch(CommandBuffer& commandBuffer, uint32_t x, uint32_t y, uint32_t z)
{
    ((CommandBufferVK&)commandBuffer).Dispatch(x, y, z);
}

static void NRI_CALL CmdDispatchIndirect(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset)
{
    ((CommandBufferVK&)commandBuffer).DispatchIndirect(buffer, offset);
}

static void NRI_CALL CmdBeginQuery(CommandBuffer& commandBuffer, const QueryPool& queryPool, uint32_t offset)
{
    ((CommandBufferVK&)commandBuffer).BeginQuery(queryPool, offset);
}

static void NRI_CALL CmdEndQuery(CommandBuffer& commandBuffer, const QueryPool& queryPool, uint32_t offset)
{
    ((CommandBufferVK&)commandBuffer).EndQuery(queryPool, offset);
}

static void NRI_CALL CmdBeginAnnotation(CommandBuffer& commandBuffer, const char* name)
{
    ((CommandBufferVK&)commandBuffer).BeginAnnotation(name);
}

static void NRI_CALL CmdEndAnnotation(CommandBuffer& commandBuffer)
{
    ((CommandBufferVK&)commandBuffer).EndAnnotation();
}

static void NRI_CALL CmdClearStorageBuffer(CommandBuffer& commandBuffer, const ClearStorageBufferDesc& clearDesc)
{
    ((CommandBufferVK&)commandBuffer).ClearStorageBuffer(clearDesc);
}

static void NRI_CALL CmdClearStorageTexture(CommandBuffer& commandBuffer, const ClearStorageTextureDesc& clearDesc)
{
    ((CommandBufferVK&)commandBuffer).ClearStorageTexture(clearDesc);
}

static void NRI_CALL CmdCopyBuffer(CommandBuffer& commandBuffer, Buffer& dstBuffer, uint32_t dstPhysicalDeviceIndex,
    uint64_t dstOffset, const Buffer& srcBuffer, uint32_t srcPhysicalDeviceIndex, uint64_t srcOffset, uint64_t size)
{
    // TODO: use dstPhysicalDeviceIndex and srcPhysicalDeviceIndex
    ((CommandBufferVK&)commandBuffer).CopyBuffer(dstBuffer, dstPhysicalDeviceIndex, dstOffset, srcBuffer, srcPhysicalDeviceIndex, srcOffset, size);
}

static void NRI_CALL CmdCopyTexture(CommandBuffer& commandBuffer, Texture& dstTexture, uint32_t dstPhysicalDeviceIndex,
    const TextureRegionDesc* dstRegionDesc, const Texture& srcTexture, uint32_t srcPhysicalDeviceIndex,
    const TextureRegionDesc* srcRegionDesc)
{
    // TODO: use dstPhysicalDeviceIndex and srcPhysicalDeviceIndex
    ((CommandBufferVK&)commandBuffer).CopyTexture(dstTexture, dstPhysicalDeviceIndex, dstRegionDesc, srcTexture, srcPhysicalDeviceIndex, srcRegionDesc);
}

static void NRI_CALL CmdUploadBufferToTexture(CommandBuffer& commandBuffer, Texture& dstTexture,
    const TextureRegionDesc& dstRegionDesc, const Buffer& srcBuffer, const TextureDataLayoutDesc& srcDataLayoutDesc)
{
    ((CommandBufferVK&)commandBuffer).UploadBufferToTexture(dstTexture, dstRegionDesc, srcBuffer, srcDataLayoutDesc);
}

static void NRI_CALL CmdReadbackTextureToBuffer(CommandBuffer& commandBuffer, Buffer& dstBuffer,
    TextureDataLayoutDesc& dstDataLayoutDesc, const Texture& srcTexture, const TextureRegionDesc& srcRegionDesc)
{
    ((CommandBufferVK&)commandBuffer).ReadbackTextureToBuffer(dstBuffer, dstDataLayoutDesc, srcTexture, srcRegionDesc);
}

static void NRI_CALL CmdCopyQueries(CommandBuffer& commandBuffer, const QueryPool& queryPool, uint32_t offset, uint32_t num,
    Buffer& dstBuffer, uint64_t dstOffset)
{
    ((CommandBufferVK&)commandBuffer).CopyQueries(queryPool, offset, num, dstBuffer, dstOffset);
}

static void NRI_CALL DestroyCommandBuffer(CommandBuffer& commandBuffer)
{
    StdAllocator<uint8_t>& allocator = ((CommandBufferVK&)commandBuffer).GetDevice().GetStdAllocator();
    Deallocate(allocator, (CommandBufferVK*)&commandBuffer);
}

void FillFunctionTableCommandBufferVK(CoreInterface& coreInterface)
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

#pragma region [  WrapperVKInterface  ]

static VkCommandBuffer NRI_CALL GetCommandBufferVK(const CommandBuffer& commandBuffer)
{
    return (CommandBufferVK&)commandBuffer;
}

void FillFunctionTableCommandBufferVK(WrapperVKInterface& wrapperVKInterface)
{
    wrapperVKInterface.GetCommandBufferVK = ::GetCommandBufferVK;
}

#pragma endregion

#pragma region [  RayTracingInterface  ]

void NRI_CALL CmdBuildTopLevelAccelerationStructure(CommandBuffer& commandBuffer, uint32_t instanceNum, const Buffer& buffer,
    uint64_t bufferOffset, AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch,
    uint64_t scratchOffset)
{
    ((CommandBufferVK&)commandBuffer).BuildTopLevelAccelerationStructure(instanceNum, buffer, bufferOffset, flags, dst,
        scratch, scratchOffset);
}

void NRI_CALL CmdBuildBottomLevelAccelerationStructure(CommandBuffer& commandBuffer, uint32_t geometryObjectNum,
    const GeometryObject* geometryObjects, AccelerationStructureBuildBits flags, AccelerationStructure& dst,
    Buffer& scratch, uint64_t scratchOffset)
{
    ((CommandBufferVK&)commandBuffer).BuildBottomLevelAccelerationStructure(geometryObjectNum, geometryObjects, flags, dst,
        scratch, scratchOffset);
}

void NRI_CALL CmdUpdateTopLevelAccelerationStructure(CommandBuffer& commandBuffer, uint32_t instanceNum, const Buffer& buffer,
    uint64_t bufferOffset, AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src,
    Buffer& scratch, uint64_t scratchOffset)
{
    ((CommandBufferVK&)commandBuffer).UpdateTopLevelAccelerationStructure(instanceNum, buffer, bufferOffset, flags, dst, src,
        scratch, scratchOffset);
}

void NRI_CALL CmdUpdateBottomLevelAccelerationStructure(CommandBuffer& commandBuffer, uint32_t geometryObjectNum,
    const GeometryObject* geometryObjects, AccelerationStructureBuildBits flags, AccelerationStructure& dst,
    AccelerationStructure& src, Buffer& scratch, uint64_t scratchOffset)
{
    ((CommandBufferVK&)commandBuffer).UpdateBottomLevelAccelerationStructure(geometryObjectNum, geometryObjects, flags, dst,
        src, scratch, scratchOffset);
}

void NRI_CALL CmdCopyAccelerationStructure(CommandBuffer& commandBuffer, AccelerationStructure& dst,
    AccelerationStructure& src, CopyMode mode)
{
    ((CommandBufferVK&)commandBuffer).CopyAccelerationStructure(dst, src, mode);
}

void NRI_CALL CmdWriteAccelerationStructureSize(CommandBuffer& commandBuffer,
    const AccelerationStructure* const* accelerationStructures, uint32_t accelerationStructureNum, QueryPool& queryPool, uint32_t queryOffset)
{
    ((CommandBufferVK&)commandBuffer).WriteAccelerationStructureSize(accelerationStructures, accelerationStructureNum, queryPool, queryOffset);
}

void NRI_CALL CmdDispatchRays(CommandBuffer& commandBuffer, const DispatchRaysDesc& dispatchRaysDesc)
{
    ((CommandBufferVK&)commandBuffer).DispatchRays(dispatchRaysDesc);
}

void FillFunctionTableCommandBufferVK(RayTracingInterface& rayTracingInterface)
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

#pragma region [  MeshShaderInterface  ]

static void NRI_CALL CmdDispatchMeshTasks(CommandBuffer& commandBuffer, uint32_t taskNum)
{
    ((CommandBufferVK&)commandBuffer).DispatchMeshTasks(taskNum);
}

void FillFunctionTableCommandBufferVK(MeshShaderInterface& meshShaderInterface)
{
    meshShaderInterface.CmdDispatchMeshTasks = CmdDispatchMeshTasks;
}

#pragma endregion
