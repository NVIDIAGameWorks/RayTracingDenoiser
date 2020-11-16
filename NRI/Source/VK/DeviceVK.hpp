/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#pragma region [  CoreInterface  ]

static const DeviceDesc& NRI_CALL GetDeviceDesc(const Device& device)
{
    return ((DeviceVK&)device).GetDesc();
}

static Result NRI_CALL GetCommandQueue(Device& device, CommandQueueType commandQueueType, CommandQueue*& commandQueue)
{
    return ((DeviceVK&)device).GetCommandQueue(commandQueueType, commandQueue);
}

static Result NRI_CALL CreateCommandAllocator(const CommandQueue& commandQueue, uint32_t physicalDeviceMask, CommandAllocator*& commandAllocator)
{
    DeviceVK& device = ((CommandQueueVK&)commandQueue).GetDevice();
    return device.CreateCommandAllocator(commandQueue, physicalDeviceMask, commandAllocator);
}

static Result NRI_CALL CreateDescriptorPool(Device& device, const DescriptorPoolDesc& descriptorPoolDesc, DescriptorPool*& descriptorPool)
{
    return ((DeviceVK&)device).CreateDescriptorPool(descriptorPoolDesc, descriptorPool);
}

static Result NRI_CALL CreateBuffer(Device& device, const BufferDesc& bufferDesc, Buffer*& buffer)
{
    return ((DeviceVK&)device).CreateBuffer(bufferDesc, buffer);
}

static Result NRI_CALL CreateTexture(Device& device, const TextureDesc& textureDesc, Texture*& texture)
{
    return ((DeviceVK&)device).CreateTexture(textureDesc, texture);
}

static Result NRI_CALL CreateBufferView(const BufferViewDesc& bufferViewDesc, Descriptor*& bufferView)
{
    DeviceVK& device = ((const BufferVK*)bufferViewDesc.buffer)->GetDevice();
    return device.CreateBufferView(bufferViewDesc, bufferView);
}

static Result NRI_CALL CreateTexture1DView(const Texture1DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    DeviceVK& device = ((const TextureVK*)textureViewDesc.texture)->GetDevice();
    return device.CreateTexture1DView(textureViewDesc, textureView);
}

static Result NRI_CALL CreateTexture2DView(const Texture2DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    DeviceVK& device = ((const TextureVK*)textureViewDesc.texture)->GetDevice();
    return device.CreateTexture2DView(textureViewDesc, textureView);
}

static Result NRI_CALL CreateTexture3DView(const Texture3DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    DeviceVK& device = ((const TextureVK*)textureViewDesc.texture)->GetDevice();
    return device.CreateTexture3DView(textureViewDesc, textureView);
}

static Result NRI_CALL CreateSampler(Device& device, const SamplerDesc& samplerDesc, Descriptor*& sampler)
{
    return ((DeviceVK&)device).CreateSampler(samplerDesc, sampler);
}

static Result NRI_CALL CreatePipelineLayout(Device& device, const PipelineLayoutDesc& pipelineLayoutDesc, PipelineLayout*& pipelineLayout)
{
    return ((DeviceVK&)device).CreatePipelineLayout(pipelineLayoutDesc, pipelineLayout);
}

static Result NRI_CALL CreateGraphicsPipeline(Device& device, const GraphicsPipelineDesc& graphicsPipelineDesc, Pipeline*& pipeline)
{
    return ((DeviceVK&)device).CreatePipeline(graphicsPipelineDesc, pipeline);
}

static Result NRI_CALL CreateComputePipeline(Device& device, const ComputePipelineDesc& computePipelineDesc, Pipeline*& pipeline)
{
    return ((DeviceVK&)device).CreatePipeline(computePipelineDesc, pipeline);
}

static Result NRI_CALL CreateFrameBuffer(Device& device, const FrameBufferDesc& frameBufferDesc, FrameBuffer*& frameBuffer)
{
    return ((DeviceVK&)device).CreateFrameBuffer(frameBufferDesc, frameBuffer);
}

static Result NRI_CALL CreateQueryPool(Device& device, const QueryPoolDesc& queryPoolDesc, QueryPool*& queryPool)
{
    return ((DeviceVK&)device).CreateQueryPool(queryPoolDesc, queryPool);
}

static Result NRI_CALL CreateQueueSemaphore(Device& device, QueueSemaphore*& queueSemaphore)
{
    return ((DeviceVK&)device).CreateQueueSemaphore(queueSemaphore);
}

static Result NRI_CALL CreateDeviceSemaphore(Device& device, bool signaled, DeviceSemaphore*& deviceSemaphore)
{
    return ((DeviceVK&)device).CreateDeviceSemaphore(signaled, deviceSemaphore);
}

static void NRI_CALL DestroyCommandAllocator(CommandAllocator& commandAllocator)
{
    ((CommandAllocatorVK&)commandAllocator).GetDevice().DestroyCommandAllocator(commandAllocator);
}

static void NRI_CALL DestroyDescriptorPool(DescriptorPool& descriptorPool)
{
    ((DescriptorPoolVK&)descriptorPool).GetDevice().DestroyDescriptorPool(descriptorPool);
}

static void NRI_CALL DestroyBuffer(Buffer& buffer)
{
    ((BufferVK&)buffer).GetDevice().DestroyBuffer(buffer);
}

static void NRI_CALL DestroyTexture(Texture& texture)
{
    ((TextureVK&)texture).GetDevice().DestroyTexture(texture);
}

static void NRI_CALL DestroyDescriptor(Descriptor& descriptor)
{
    ((DescriptorVK&)descriptor).GetDevice().DestroyDescriptor(descriptor);
}

static void NRI_CALL DestroyPipelineLayout(PipelineLayout& pipelineLayout)
{
    ((PipelineLayoutVK&)pipelineLayout).GetDevice().DestroyPipelineLayout(pipelineLayout);
}

static void NRI_CALL DestroyPipeline(Pipeline& pipeline)
{
    ((PipelineVK&)pipeline).GetDevice().DestroyPipeline(pipeline);
}

static void NRI_CALL DestroyFrameBuffer(FrameBuffer& frameBuffer)
{
    ((FrameBufferVK&)frameBuffer).GetDevice().DestroyFrameBuffer(frameBuffer);
}

static void NRI_CALL DestroyQueryPool(QueryPool& queryPool)
{
    ((QueryPoolVK&)queryPool).GetDevice().DestroyQueryPool(queryPool);
}

static void NRI_CALL DestroyQueueSemaphore(QueueSemaphore& queueSemaphore)
{
    ((QueueSemaphoreVK&)queueSemaphore).GetDevice().DestroyQueueSemaphore(queueSemaphore);
}

static void NRI_CALL DestroyDeviceSemaphore(DeviceSemaphore& deviceSemaphore)
{
    ((DeviceSemaphoreVK&)deviceSemaphore).GetDevice().DestroyDeviceSemaphore(deviceSemaphore);
}

static Result NRI_CALL AllocateMemory(Device& device, uint32_t physicalDeviceMask, MemoryType memoryType, uint64_t size, Memory*& memory)
{
    return ((DeviceVK&)device).AllocateMemory(physicalDeviceMask, memoryType, size, memory);
}

static Result NRI_CALL BindBufferMemory(Device& device, const BufferMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    return ((DeviceVK&)device).BindBufferMemory(memoryBindingDescs, memoryBindingDescNum);
}

static Result NRI_CALL BindTextureMemory(Device& device, const TextureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    return ((DeviceVK&)device).BindTextureMemory(memoryBindingDescs, memoryBindingDescNum);
}

static void NRI_CALL FreeMemory(Memory& memory)
{
    ((MemoryVK&)memory).GetDevice().FreeMemory(memory);
}

static FormatSupportBits NRI_CALL GetFormatSupport(const Device& device, Format format)
{
    return ((const DeviceVK&)device).GetFormatSupport(format);
}

static void NRI_CALL SetDeviceDebugName(Device& device, const char* name)
{
    ((DeviceVK&)device).SetDebugName(name);
}

void FillFunctionTableBufferVK(CoreInterface& coreInterface);
void FillFunctionTableCommandAllocatorVK(CoreInterface& coreInterface);
void FillFunctionTableCommandBufferVK(CoreInterface& coreInterface);
void FillFunctionTableCommandQueueVK(CoreInterface& coreInterface);
void FillFunctionTableDescriptorPoolVK(CoreInterface& coreInterface);
void FillFunctionTableDescriptorSetVK(CoreInterface& coreInterface);
void FillFunctionTableQueryPoolVK(CoreInterface& coreInterface);
void FillFunctionTableTextureVK(CoreInterface& coreInterface);
void FillFunctionTableDescriptorVK(CoreInterface& coreInterface);
void FillFunctionTableDeviceSemaphoreVK(CoreInterface& coreInterface);
void FillFunctionTableFrameBufferVK(CoreInterface& coreInterface);
void FillFunctionTableMemoryVK(CoreInterface& coreInterface);
void FillFunctionTablePipelineLayoutVK(CoreInterface& coreInterface);
void FillFunctionTablePipelineVK(CoreInterface& coreInterface);
void FillFunctionTableQueueSemaphoreVK(CoreInterface& coreInterface);

Result DeviceVK::FillFunctionTable(CoreInterface& coreInterface) const
{
    coreInterface = {};

    FillFunctionTableBufferVK(coreInterface);
    FillFunctionTableCommandAllocatorVK(coreInterface);
    FillFunctionTableCommandBufferVK(coreInterface);
    FillFunctionTableCommandQueueVK(coreInterface);
    FillFunctionTableDescriptorPoolVK(coreInterface);
    FillFunctionTableDescriptorSetVK(coreInterface);
    FillFunctionTableQueryPoolVK(coreInterface);
    FillFunctionTableTextureVK(coreInterface);
    FillFunctionTableDescriptorVK(coreInterface);
    FillFunctionTableDeviceSemaphoreVK(coreInterface);
    FillFunctionTableFrameBufferVK(coreInterface);
    FillFunctionTableMemoryVK(coreInterface);
    FillFunctionTablePipelineLayoutVK(coreInterface);
    FillFunctionTablePipelineVK(coreInterface);
    FillFunctionTableQueueSemaphoreVK(coreInterface);

    coreInterface.GetDeviceDesc = ::GetDeviceDesc;
    coreInterface.GetCommandQueue = ::GetCommandQueue;

    coreInterface.CreateCommandAllocator = ::CreateCommandAllocator;
    coreInterface.CreateDescriptorPool = ::CreateDescriptorPool;
    coreInterface.CreateBuffer = ::CreateBuffer;
    coreInterface.CreateTexture = ::CreateTexture;
    coreInterface.CreateBufferView = ::CreateBufferView;
    coreInterface.CreateTexture1DView = ::CreateTexture1DView;
    coreInterface.CreateTexture2DView = ::CreateTexture2DView;
    coreInterface.CreateTexture3DView = ::CreateTexture3DView;
    coreInterface.CreateSampler = ::CreateSampler;
    coreInterface.CreatePipelineLayout = ::CreatePipelineLayout;
    coreInterface.CreateGraphicsPipeline = ::CreateGraphicsPipeline;
    coreInterface.CreateComputePipeline = ::CreateComputePipeline;
    coreInterface.CreateFrameBuffer = ::CreateFrameBuffer;
    coreInterface.CreateQueryPool = ::CreateQueryPool;
    coreInterface.CreateQueueSemaphore = ::CreateQueueSemaphore;
    coreInterface.CreateDeviceSemaphore = ::CreateDeviceSemaphore;

    coreInterface.DestroyCommandAllocator = ::DestroyCommandAllocator;
    coreInterface.DestroyDescriptorPool = ::DestroyDescriptorPool;
    coreInterface.DestroyBuffer = ::DestroyBuffer;
    coreInterface.DestroyTexture = ::DestroyTexture;
    coreInterface.DestroyDescriptor = ::DestroyDescriptor;
    coreInterface.DestroyPipelineLayout = ::DestroyPipelineLayout;
    coreInterface.DestroyPipeline = ::DestroyPipeline;
    coreInterface.DestroyFrameBuffer = ::DestroyFrameBuffer;
    coreInterface.DestroyQueryPool = ::DestroyQueryPool;
    coreInterface.DestroyQueueSemaphore = ::DestroyQueueSemaphore;
    coreInterface.DestroyDeviceSemaphore = ::DestroyDeviceSemaphore;

    coreInterface.AllocateMemory = ::AllocateMemory;
    coreInterface.BindBufferMemory = ::BindBufferMemory;
    coreInterface.BindTextureMemory = ::BindTextureMemory;
    coreInterface.FreeMemory = ::FreeMemory;

    coreInterface.GetFormatSupport = ::GetFormatSupport;

    coreInterface.SetDeviceDebugName = ::SetDeviceDebugName;

    return ValidateFunctionTable(GetLog(), coreInterface);
}

#pragma endregion

#pragma region [  SwapChainInterface  ]

static Result NRI_CALL CreateSwapChain(Device& device, const SwapChainDesc& swapChainDesc, SwapChain*& swapChain)
{
    return ((DeviceVK&)device).CreateSwapChain(swapChainDesc, swapChain);
}

static void NRI_CALL DestroySwapChain(SwapChain& swapChain)
{
    return ((SwapChainVK&)swapChain).GetDevice().DestroySwapChain(swapChain);
}

void FillFunctionTableSwapChainVK(SwapChainInterface& swapChainInterface);

Result DeviceVK::FillFunctionTable(SwapChainInterface& swapChainInterface) const
{
    swapChainInterface = {};

    FillFunctionTableSwapChainVK(swapChainInterface);

    swapChainInterface.CreateSwapChain = ::CreateSwapChain;
    swapChainInterface.DestroySwapChain = ::DestroySwapChain;

    return ValidateFunctionTable(GetLog(), swapChainInterface);
}

#pragma endregion

#pragma region [  WrapperVKInterface  ]

static VkDevice NRI_CALL GetDeviceVK(const Device& device)
{
    return (DeviceVK&)device;
}

static VkPhysicalDevice NRI_CALL GetPhysicalDeviceVK(const Device& device)
{
    return (DeviceVK&)device;
}

static VkInstance NRI_CALL GetInstanceVK(const Device& device)
{
    return (DeviceVK&)device;
}

static Result NRI_CALL CreateCommandQueueVK(Device& device, const CommandQueueVulkanDesc& commandQueueVulkanDesc, CommandQueue*& commandQueue)
{
    return ((DeviceVK&)device).CreateCommandQueue(commandQueueVulkanDesc, commandQueue);
}

static Result NRI_CALL CreateCommandAllocatorVK(Device& device, const CommandAllocatorVulkanDesc& commandAllocatorVulkanDesc, CommandAllocator*& commandAllocator)
{
    return ((DeviceVK&)device).CreateCommandAllocator(commandAllocatorVulkanDesc, commandAllocator);
}

static Result NRI_CALL CreateCommandBufferVK(Device& device, const CommandBufferVulkanDesc& commandBufferVulkanDesc, CommandBuffer*& commandBuffer)
{
    return ((DeviceVK&)device).CreateCommandBuffer(commandBufferVulkanDesc, commandBuffer);
}

static Result NRI_CALL CreateDescriptorPoolVK(Device& device, VkDescriptorPool vkDescriptorPool, DescriptorPool*& descriptorPool)
{
    return ((DeviceVK&)device).CreateDescriptorPool(vkDescriptorPool, descriptorPool);
}

static Result NRI_CALL CreateBufferVK(Device& device, const BufferVulkanDesc& bufferVulkanDesc, Buffer*& buffer)
{
    return ((DeviceVK&)device).CreateBuffer(bufferVulkanDesc, buffer);
}

static Result NRI_CALL CreateTextureVK(Device& device, const TextureVulkanDesc& textureVulkanDesc, Texture*& texture)
{
    return ((DeviceVK&)device).CreateTexture(textureVulkanDesc, texture);
}

static Result NRI_CALL CreateMemoryVK(Device& device, const MemoryVulkanDesc& memoryVulkanDesc, Memory*& memory)
{
    return ((DeviceVK&)device).CreateMemory(memoryVulkanDesc, memory);
}

static Result NRI_CALL CreateGraphicsPipelineVK(Device& device, VkPipeline vkPipeline, Pipeline*& pipeline)
{
    return ((DeviceVK&)device).CreateGraphicsPipeline(vkPipeline, pipeline);
}

static Result NRI_CALL CreateComputePipelineVK(Device& device, VkPipeline vkPipeline, Pipeline*& pipeline)
{
    return ((DeviceVK&)device).CreateComputePipeline(vkPipeline, pipeline);
}

static Result NRI_CALL CreateQueryPoolVK(Device& device, const QueryPoolVulkanDesc& queryPoolVulkanDesc, QueryPool*& queryPool)
{
    return ((DeviceVK&)device).CreateQueryPool(queryPoolVulkanDesc, queryPool);
}

static Result NRI_CALL CreateQueueSemaphoreVK(Device& device, VkSemaphore vkSemaphore, QueueSemaphore*& queueSemaphore)
{
    return ((DeviceVK&)device).CreateQueueSemaphore(vkSemaphore, queueSemaphore);
}

static Result NRI_CALL CreateDeviceSemaphoreVK(Device& device, VkFence vkFence, DeviceSemaphore*& deviceSemaphore)
{
    return ((DeviceVK&)device).CreateDeviceSemaphore(vkFence, deviceSemaphore);
}

void FillFunctionTableCommandBufferVK(WrapperVKInterface& wrapperVKInterface);
void FillFunctionTableDescriptorVK(WrapperVKInterface& wrapperVKInterface);
void FillFunctionTableTextureVK(WrapperVKInterface& wrapperVKInterface);

Result DeviceVK::FillFunctionTable(WrapperVKInterface& wrapperVKInterface) const
{
    wrapperVKInterface = {};

    FillFunctionTableCommandBufferVK(wrapperVKInterface);
    FillFunctionTableDescriptorVK(wrapperVKInterface);
    FillFunctionTableTextureVK(wrapperVKInterface);

    wrapperVKInterface.CreateCommandQueueVK = ::CreateCommandQueueVK;
    wrapperVKInterface.CreateCommandAllocatorVK = ::CreateCommandAllocatorVK;
    wrapperVKInterface.CreateCommandBufferVK = ::CreateCommandBufferVK;
    wrapperVKInterface.CreateDescriptorPoolVK = ::CreateDescriptorPoolVK;
    wrapperVKInterface.CreateBufferVK = ::CreateBufferVK;
    wrapperVKInterface.CreateTextureVK = ::CreateTextureVK;
    wrapperVKInterface.CreateMemoryVK = ::CreateMemoryVK;
    wrapperVKInterface.CreateGraphicsPipelineVK = ::CreateGraphicsPipelineVK;
    wrapperVKInterface.CreateComputePipelineVK = ::CreateComputePipelineVK;
    wrapperVKInterface.CreateQueryPoolVK = ::CreateQueryPoolVK;
    wrapperVKInterface.CreateQueueSemaphoreVK = ::CreateQueueSemaphoreVK;
    wrapperVKInterface.CreateDeviceSemaphoreVK = ::CreateDeviceSemaphoreVK;

    wrapperVKInterface.GetDeviceVK = ::GetDeviceVK;
    wrapperVKInterface.GetPhysicalDeviceVK = ::GetPhysicalDeviceVK;
    wrapperVKInterface.GetInstanceVK = ::GetInstanceVK;

    return ValidateFunctionTable(GetLog(), wrapperVKInterface);
}

#pragma endregion

#pragma region [  RayTracingInterface  ]

static Result NRI_CALL CreateRayTracingPipeline(Device& device, const RayTracingPipelineDesc& pipelineDesc, Pipeline*& pipeline)
{
    return ((DeviceVK&)device).CreatePipeline(pipelineDesc, pipeline);
}

static Result NRI_CALL CreateAccelerationStructure(Device& device, const AccelerationStructureDesc& accelerationStructureDesc, AccelerationStructure*& accelerationStructure)
{
    return ((DeviceVK&)device).CreateAccelerationStructure(accelerationStructureDesc, accelerationStructure);
}

static Result NRI_CALL BindAccelerationStructureMemory(Device& device, const AccelerationStructureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    return ((DeviceVK&)device).BindAccelerationStructureMemory(memoryBindingDescs, memoryBindingDescNum);
}

static void NRI_CALL DestroyAccelerationStructure(AccelerationStructure& accelerationStructure)
{
    return ((AccelerationStructureVK&)accelerationStructure).GetDevice().DestroyAccelerationStructure(accelerationStructure);
}

void FillFunctionTableCommandBufferVK(RayTracingInterface& rayTracingInterface);
void FillFunctionTablePipelineVK(RayTracingInterface& rayTracingInterface);
void FillFunctionTableAccelerationStructureVK(RayTracingInterface& rayTracingInterface);

Result DeviceVK::FillFunctionTable(RayTracingInterface& rayTracingInterface) const
{
    if (!m_IsRayTracingExtSupported)
        return Result::UNSUPPORTED;

    rayTracingInterface = {};

    rayTracingInterface.CreateRayTracingPipeline = ::CreateRayTracingPipeline;
    rayTracingInterface.CreateAccelerationStructure = ::CreateAccelerationStructure;
    rayTracingInterface.BindAccelerationStructureMemory = ::BindAccelerationStructureMemory;
    rayTracingInterface.DestroyAccelerationStructure = ::DestroyAccelerationStructure;

    FillFunctionTableCommandBufferVK(rayTracingInterface);
    FillFunctionTablePipelineVK(rayTracingInterface);
    FillFunctionTableAccelerationStructureVK(rayTracingInterface);

    return ValidateFunctionTable(GetLog(), rayTracingInterface);
}

#pragma endregion

#pragma region [  MeshShaderInterface  ]

void FillFunctionTableCommandBufferVK(MeshShaderInterface& meshShaderInterface);

Result DeviceVK::FillFunctionTable(MeshShaderInterface& meshShaderInterface) const
{
    if (!m_IsMeshShaderExtSupported)
        return Result::UNSUPPORTED;

    meshShaderInterface = {};

    FillFunctionTableCommandBufferVK(meshShaderInterface);

    return ValidateFunctionTable(GetLog(), meshShaderInterface);
}

#pragma endregion

#pragma region [  HelperInterface  ]

static uint32_t NRI_CALL CountAllocationNumVK(Device& device, const ResourceGroupDesc& resourceGroupDesc)
{
    return ((DeviceVK&)device).CalculateAllocationNumber(resourceGroupDesc);
}

static Result NRI_CALL AllocateAndBindMemoryVK(Device& device, const ResourceGroupDesc& resourceGroupDesc, Memory** allocations)
{
    return ((DeviceVK&)device).AllocateAndBindMemory(resourceGroupDesc, allocations);
}

void FillFunctionTableCommandQueueVK(HelperInterface& helperInterface);

Result DeviceVK::FillFunctionTable(HelperInterface& helperInterface) const
{
    helperInterface = {};

    helperInterface.CalculateAllocationNumber = ::CountAllocationNumVK;
    helperInterface.AllocateAndBindMemory = ::AllocateAndBindMemoryVK;
    FillFunctionTableCommandQueueVK(helperInterface);

    return ValidateFunctionTable(GetLog(), helperInterface);
}

#pragma endregion
