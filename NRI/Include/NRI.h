/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// Documentation: https://docs.google.com/document/d/1OidQtZnm-3grhua7Oy1WKDUzh8ExTdbO83VTAWS_nNM/

#pragma once

#include <cstdint>

#define NRI_VERSION_MAJOR 1
#define NRI_VERSION_MINOR 61
#define NRI_VERSION_DATE "6 November 2020"
#define NRI_INTERFACE( name ) #name, sizeof(name)

#if _WIN32
    #define NRI_CALL __fastcall
#else
    #define NRI_CALL
#endif

#ifndef NRI_API
    #define NRI_API extern "C"
#endif

#include "NRIDescs.h"

namespace nri
{
    NRI_API Result NRI_CALL GetInterface(const Device& device, const char* interfaceName, size_t interfaceSize, void* interfacePtr);

    struct CoreInterface
    {
        const DeviceDesc& (NRI_CALL *GetDeviceDesc)(const Device& device);
        Result (NRI_CALL *GetCommandQueue)(Device& device, CommandQueueType commandQueueType, CommandQueue*& commandQueue);

        Result (NRI_CALL *CreateCommandAllocator)(const CommandQueue& commandQueue, uint32_t physicalDeviceMask, CommandAllocator*& commandAllocator);
        Result (NRI_CALL *CreateDescriptorPool)(Device& device, const DescriptorPoolDesc& descriptorPoolDesc, DescriptorPool*& descriptorPool);
        Result (NRI_CALL *CreateBuffer)(Device& device, const BufferDesc& bufferDesc, Buffer*& buffer);
        Result (NRI_CALL *CreateTexture)(Device& device, const TextureDesc& textureDesc, Texture*& texture);
        Result (NRI_CALL *CreateBufferView)(const BufferViewDesc& bufferViewDesc, Descriptor*& bufferView);
        Result (NRI_CALL *CreateTexture1DView)(const Texture1DViewDesc& textureViewDesc, Descriptor*& textureView);
        Result (NRI_CALL *CreateTexture2DView)(const Texture2DViewDesc& textureViewDesc, Descriptor*& textureView);
        Result (NRI_CALL *CreateTexture3DView)(const Texture3DViewDesc& textureViewDesc, Descriptor*& textureView);
        Result (NRI_CALL *CreateSampler)(Device& device, const SamplerDesc& samplerDesc, Descriptor*& sampler);
        Result (NRI_CALL *CreatePipelineLayout)(Device& device, const PipelineLayoutDesc& pipelineLayoutDesc, PipelineLayout*& pipelineLayout);
        Result (NRI_CALL *CreateGraphicsPipeline)(Device& device, const GraphicsPipelineDesc& graphicsPipelineDesc, Pipeline*& pipeline);
        Result (NRI_CALL *CreateComputePipeline)(Device& device, const ComputePipelineDesc& computePipelineDesc, Pipeline*& pipeline);
        Result (NRI_CALL *CreateFrameBuffer)(Device& device, const FrameBufferDesc& frameBufferDesc, FrameBuffer*& frameBuffer);
        Result (NRI_CALL *CreateQueryPool)(Device& device, const QueryPoolDesc& queryPoolDesc, QueryPool*& queryPool);
        Result (NRI_CALL *CreateQueueSemaphore)(Device& device, QueueSemaphore*& queueSemaphore);
        Result (NRI_CALL *CreateDeviceSemaphore)(Device& device, bool signaled, DeviceSemaphore*& deviceSemaphore);
        Result (NRI_CALL *CreateCommandBuffer)(CommandAllocator& commandAllocator, CommandBuffer*& commandBuffer);

        void (NRI_CALL *DestroyCommandAllocator)(CommandAllocator& commandAllocator);
        void (NRI_CALL *DestroyDescriptorPool)(DescriptorPool& descriptorPool);
        void (NRI_CALL *DestroyBuffer)(Buffer& buffer);
        void (NRI_CALL *DestroyTexture)(Texture& texture);
        void (NRI_CALL *DestroyDescriptor)(Descriptor& descriptor);
        void (NRI_CALL *DestroyPipelineLayout)(PipelineLayout& pipelineLayout);
        void (NRI_CALL *DestroyPipeline)(Pipeline& pipeline);
        void (NRI_CALL *DestroyFrameBuffer)(FrameBuffer& frameBuffer);
        void (NRI_CALL *DestroyQueryPool)(QueryPool& queryPool);
        void (NRI_CALL *DestroyQueueSemaphore)(QueueSemaphore& queueSemaphore);
        void (NRI_CALL *DestroyDeviceSemaphore)(DeviceSemaphore& deviceSemaphore);
        void (NRI_CALL *DestroyCommandBuffer)(CommandBuffer& commandBuffer);

        Result (NRI_CALL *AllocateMemory)(Device& device, uint32_t physicalDeviceMask, MemoryType memoryType, uint64_t size, Memory*& memory);
        Result (NRI_CALL *BindBufferMemory)(Device& device, const BufferMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);
        Result (NRI_CALL *BindTextureMemory)(Device& device, const TextureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);
        void (NRI_CALL *FreeMemory)(Memory& memory);

        Result (NRI_CALL *BeginCommandBuffer)(CommandBuffer& commandBuffer, const DescriptorPool* descriptorPool, uint32_t physicalDeviceIndex);
        Result (NRI_CALL *EndCommandBuffer)(CommandBuffer& commandBuffer);

        void (NRI_CALL *CmdSetPipeline)(CommandBuffer& commandBuffer, const Pipeline& pipeline);
        void (NRI_CALL *CmdSetPipelineLayout)(CommandBuffer& commandBuffer, const PipelineLayout& pipelineLayout);
        void (NRI_CALL *CmdSetDescriptorSets)(CommandBuffer& commandBuffer, uint32_t baseSlot, uint32_t descriptorSetNum, const DescriptorSet* const* descriptorSets, const uint32_t* dynamicConstantBufferOffsets);
        void (NRI_CALL *CmdSetConstants)(CommandBuffer& commandBuffer, uint32_t pushConstantIndex, const void* data, uint32_t size);
        void (NRI_CALL *CmdSetDescriptorPool)(CommandBuffer& commandBuffer, const DescriptorPool& descriptorPool);
        void (NRI_CALL *CmdPipelineBarrier)(CommandBuffer& commandBuffer, const TransitionBarrierDesc* transitionBarriers, const AliasingBarrierDesc* aliasingBarriers, BarrierDependency dependency);

        void (NRI_CALL *CmdBeginRenderPass)(CommandBuffer& commandBuffer, const FrameBuffer& frameBuffer, RenderPassBeginFlag renderPassBeginFlag);
        void (NRI_CALL *CmdEndRenderPass)(CommandBuffer& commandBuffer);
        void (NRI_CALL *CmdSetViewports)(CommandBuffer& commandBuffer, const Viewport* viewports, uint32_t viewportNum);
        void (NRI_CALL *CmdSetScissors)(CommandBuffer& commandBuffer, const Rect* rects, uint32_t rectNum);
        void (NRI_CALL *CmdSetDepthBounds)(CommandBuffer& commandBuffer, float boundsMin, float boundsMax);
        void (NRI_CALL *CmdSetStencilReference)(CommandBuffer& commandBuffer, uint8_t reference);
        void (NRI_CALL *CmdSetSamplePositions)(CommandBuffer& commandBuffer, const SamplePosition* positions, uint32_t positionNum);
        void (NRI_CALL *CmdClearAttachments)(CommandBuffer& commandBuffer, const ClearDesc* clearDescs, uint32_t clearDescNum, const Rect* rects, uint32_t rectNum);
        void (NRI_CALL *CmdSetIndexBuffer)(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset, IndexType indexType);
        void (NRI_CALL *CmdSetVertexBuffers)(CommandBuffer& commandBuffer, uint32_t baseSlot, uint32_t bufferNum, const Buffer* const* buffers, const uint64_t* offsets);

        void (NRI_CALL *CmdDraw)(CommandBuffer& commandBuffer, uint32_t vertexNum, uint32_t instanceNum, uint32_t baseVertex, uint32_t baseInstance);
        void (NRI_CALL *CmdDrawIndexed)(CommandBuffer& commandBuffer, uint32_t indexNum, uint32_t instanceNum, uint32_t baseIndex, uint32_t baseVertex, uint32_t baseInstance);
        void (NRI_CALL *CmdDrawIndirect)(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride);
        void (NRI_CALL *CmdDrawIndexedIndirect)(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride);
        void (NRI_CALL *CmdDispatch)(CommandBuffer& commandBuffer, uint32_t x, uint32_t y, uint32_t z);
        void (NRI_CALL *CmdDispatchIndirect)(CommandBuffer& commandBuffer, const Buffer& buffer, uint64_t offset);
        void (NRI_CALL *CmdBeginQuery)(CommandBuffer& commandBuffer, const QueryPool& queryPool, uint32_t offset);
        void (NRI_CALL *CmdEndQuery)(CommandBuffer& commandBuffer, const QueryPool& queryPool, uint32_t offset);
        void (NRI_CALL *CmdBeginAnnotation)(CommandBuffer& commandBuffer, const char* name);
        void (NRI_CALL *CmdEndAnnotation)(CommandBuffer& commandBuffer);

        void (NRI_CALL *CmdClearStorageBuffer)(CommandBuffer& commandBuffer, const ClearStorageBufferDesc& clearDesc);
        void (NRI_CALL *CmdClearStorageTexture)(CommandBuffer& commandBuffer, const ClearStorageTextureDesc& clearDesc);
        void (NRI_CALL *CmdCopyBuffer)(CommandBuffer& commandBuffer, Buffer& dstBuffer, uint32_t dstPhysicalDeviceIndex, uint64_t dstOffset, const Buffer& srcBuffer, uint32_t srcPhysicalDeviceIndex, uint64_t srcOffset, uint64_t size);
        void (NRI_CALL *CmdCopyTexture)(CommandBuffer& commandBuffer, Texture& dstTexture, uint32_t dstPhysicalDeviceIndex, const TextureRegionDesc* dstRegionDesc, const Texture& srcTexture, uint32_t srcPhysicalDeviceIndex, const TextureRegionDesc* srcRegionDesc);
        void (NRI_CALL *CmdUploadBufferToTexture)(CommandBuffer& commandBuffer, Texture& dstTexture, const TextureRegionDesc& dstRegionDesc, const Buffer& srcBuffer, const TextureDataLayoutDesc& srcDataLayoutDesc);
        void (NRI_CALL *CmdReadbackTextureToBuffer)(CommandBuffer& commandBuffer, Buffer& dstBuffer, TextureDataLayoutDesc& dstDataLayoutDesc, const Texture& srcTexture, const TextureRegionDesc& srcRegionDesc);
        void (NRI_CALL *CmdCopyQueries)(CommandBuffer& commandBuffer, const QueryPool& queryPool, uint32_t offset, uint32_t num, Buffer& dstBuffer, uint64_t dstOffset);

        void (NRI_CALL *SubmitQueueWork)(CommandQueue& commandQueue, const WorkSubmissionDesc& workSubmissionDesc, DeviceSemaphore* deviceSemaphore);
        void (NRI_CALL *WaitForSemaphore)(CommandQueue& commandQueue, DeviceSemaphore& deviceSemaphore);

        void (NRI_CALL *UpdateDescriptorRanges)(DescriptorSet& descriptorSet, uint32_t physicalDeviceMask, uint32_t baseRange, uint32_t rangeNum, const DescriptorRangeUpdateDesc* rangeUpdateDescs);
        void (NRI_CALL *UpdateDynamicConstantBuffers)(DescriptorSet& descriptorSet, uint32_t physicalDeviceMask, uint32_t baseBuffer, uint32_t bufferNum, const Descriptor* const* descriptors);
        void (NRI_CALL *CopyDescriptorSet)(DescriptorSet& descriptorSet, const DescriptorSetCopyDesc& descriptorSetCopyDesc);

        Result (NRI_CALL *AllocateDescriptorSets)(DescriptorPool& descriptorPool, const PipelineLayout& pipelineLayout, uint32_t setIndex, DescriptorSet** const descriptorSets, uint32_t instanceNum, uint32_t physicalDeviceMask, uint32_t variableDescriptorNum);
        void (NRI_CALL *ResetDescriptorPool)(DescriptorPool& descriptorPool);

        void (NRI_CALL *ResetCommandAllocator)(CommandAllocator& commandAllocator);

        uint32_t (NRI_CALL *GetQuerySize)(const QueryPool& queryPool);

        void (NRI_CALL *GetBufferMemoryInfo)(const Buffer& buffer, MemoryLocation memoryLocation, MemoryDesc& memoryDesc);
        void* (NRI_CALL *MapBuffer)(Buffer& buffer, uint64_t offset, uint64_t size);
        void (NRI_CALL *UnmapBuffer)(Buffer& buffer);

        void (NRI_CALL *GetTextureMemoryInfo)(const Texture& texture, MemoryLocation memoryLocation, MemoryDesc& memoryDesc);

        FormatSupportBits (NRI_CALL *GetFormatSupport)(const Device& device, Format format);

        void (NRI_CALL *SetDeviceDebugName)(Device& device, const char* name);
        void (NRI_CALL *SetCommandQueueDebugName)(CommandQueue& commandQueue, const char* name);
        void (NRI_CALL *SetDeviceSemaphoreDebugName)(DeviceSemaphore& deviceSemaphore, const char* name);
        void (NRI_CALL *SetQueueSemaphoreDebugName)(QueueSemaphore& queueSemaphore, const char* name);
        void (NRI_CALL *SetCommandAllocatorDebugName)(CommandAllocator& commandAllocator, const char* name);
        void (NRI_CALL *SetDescriptorPoolDebugName)(DescriptorPool& descriptorPool, const char* name);
        void (NRI_CALL *SetBufferDebugName)(Buffer& buffer, const char* name);
        void (NRI_CALL *SetTextureDebugName)(Texture& texture, const char* name);
        void (NRI_CALL *SetDescriptorDebugName)(Descriptor& descriptor, const char* name);
        void (NRI_CALL *SetPipelineLayoutDebugName)(PipelineLayout& pipelineLayout, const char* name);
        void (NRI_CALL *SetPipelineDebugName)(Pipeline& pipeline, const char* name);
        void (NRI_CALL *SetFrameBufferDebugName)(FrameBuffer& frameBuffer, const char* name);
        void (NRI_CALL *SetQueryPoolDebugName)(QueryPool& queryPool, const char* name);
        void (NRI_CALL *SetDescriptorSetDebugName)(DescriptorSet& descriptorSet, const char* name);
        void (NRI_CALL *SetCommandBufferDebugName)(CommandBuffer& commandBuffer, const char* name);
        void (NRI_CALL *SetMemoryDebugName)(Memory& memory, const char* name);
    };
}
