/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static const DeviceDesc& NRI_CALL GetDeviceDesc(const Device& device)
{
    return ((const DeviceD3D12&)device).GetDesc();
}

static Result NRI_CALL GetCommandQueue(Device& device, CommandQueueType commandQueueType, CommandQueue*& commandQueue)
{
    return ((DeviceD3D12&)device).GetCommandQueue(commandQueueType, commandQueue);
}

static Result NRI_CALL CreateCommandAllocator(const CommandQueue& commandQueue, uint32_t physicalDeviceMask, CommandAllocator*& commandAllocator)
{
    // TODO: use physicalDeviceMask
    DeviceD3D12& device = ((CommandQueueD3D12&)commandQueue).GetDevice();
    return device.CreateCommandAllocator(commandQueue, commandAllocator);
}

static Result NRI_CALL CreateDescriptorPool(Device& device, const DescriptorPoolDesc& descriptorPoolDesc, DescriptorPool*& descriptorPool)
{
    return ((DeviceD3D12&)device).CreateDescriptorPool(descriptorPoolDesc, descriptorPool);
}

static Result NRI_CALL CreateBuffer(Device& device, const BufferDesc& bufferDesc, Buffer*& buffer)
{
    return ((DeviceD3D12&)device).CreateBuffer(bufferDesc, buffer);
}

static Result NRI_CALL CreateTexture(Device& device, const TextureDesc& textureDesc, Texture*& texture)
{
    return ((DeviceD3D12&)device).CreateTexture(textureDesc, texture);
}

static Result NRI_CALL CreateBufferView(const BufferViewDesc& bufferViewDesc, Descriptor*& bufferView)
{
    DeviceD3D12& device = ((const BufferD3D12*)bufferViewDesc.buffer)->GetDevice();
    return device.CreateDescriptor(bufferViewDesc, bufferView);
}

static Result NRI_CALL CreateTexture1DView(const Texture1DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    DeviceD3D12& device = ((const TextureD3D12*)textureViewDesc.texture)->GetDevice();
    return device.CreateDescriptor(textureViewDesc, textureView);
}

static Result NRI_CALL CreateTexture2DView(const Texture2DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    DeviceD3D12& device = ((const TextureD3D12*)textureViewDesc.texture)->GetDevice();
    return device.CreateDescriptor(textureViewDesc, textureView);
}

static Result NRI_CALL CreateTexture3DView(const Texture3DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    DeviceD3D12& device = ((const TextureD3D12*)textureViewDesc.texture)->GetDevice();
    return device.CreateDescriptor(textureViewDesc, textureView);
}

static Result NRI_CALL CreateSampler(Device& device, const SamplerDesc& samplerDesc, Descriptor*& sampler)
{
    return ((DeviceD3D12&)device).CreateDescriptor(samplerDesc, sampler);
}

static Result NRI_CALL CreatePipelineLayout(Device& device, const PipelineLayoutDesc& pipelineLayoutDesc, PipelineLayout*& pipelineLayout)
{
    return ((DeviceD3D12&)device).CreatePipelineLayout(pipelineLayoutDesc, pipelineLayout);
}

static Result NRI_CALL CreateGraphicsPipeline(Device& device, const GraphicsPipelineDesc& graphicsPipelineDesc, Pipeline*& pipeline)
{
    return ((DeviceD3D12&)device).CreatePipeline(graphicsPipelineDesc, pipeline);
}

static Result NRI_CALL CreateComputePipeline(Device& device, const ComputePipelineDesc& computePipelineDesc, Pipeline*& pipeline)
{
    return ((DeviceD3D12&)device).CreatePipeline(computePipelineDesc, pipeline);
}

static Result NRI_CALL CreateFrameBuffer(Device& device, const FrameBufferDesc& frameBufferDesc, FrameBuffer*& frameBuffer)
{
    return ((DeviceD3D12&)device).CreateFrameBuffer(frameBufferDesc, frameBuffer);
}

static Result NRI_CALL CreateQueryPool(Device& device, const QueryPoolDesc& queryPoolDesc, QueryPool*& queryPool)
{
    return ((DeviceD3D12&)device).CreateQueryPool(queryPoolDesc, queryPool);
}

static Result NRI_CALL CreateQueueSemaphore(Device& device, QueueSemaphore*& queueSemaphore)
{
    return ((DeviceD3D12&)device).CreateQueueSemaphore(queueSemaphore);
}

static Result NRI_CALL CreateDeviceSemaphore(Device& device, bool signaled, DeviceSemaphore*& deviceSemaphore)
{
    return ((DeviceD3D12&)device).CreateDeviceSemaphore(signaled, deviceSemaphore);
}

static void NRI_CALL DestroyCommandAllocator(CommandAllocator& commandAllocator)
{
    DeviceD3D12& device = ((CommandAllocatorD3D12&)commandAllocator).GetDevice();
    device.DestroyCommandAllocator(commandAllocator);
}

static void NRI_CALL DestroyDescriptorPool(DescriptorPool& descriptorPool)
{
    DeviceD3D12& device = ((DescriptorPoolD3D12&)descriptorPool).GetDevice();
    device.DestroyDescriptorPool(descriptorPool);
}

static void NRI_CALL DestroyBuffer(Buffer& buffer)
{
    DeviceD3D12& device = ((BufferD3D12&)buffer).GetDevice();
    device.DestroyBuffer(buffer);
}

static void NRI_CALL DestroyTexture(Texture& texture)
{
    DeviceD3D12& device = ((TextureD3D12&)texture).GetDevice();
    device.DestroyTexture(texture);
}

static void NRI_CALL DestroyDescriptor(Descriptor& descriptor)
{
    DeviceD3D12& device = ((DescriptorD3D12&)descriptor).GetDevice();
    device.DestroyDescriptor(descriptor);
}

static void NRI_CALL DestroyPipelineLayout(PipelineLayout& pipelineLayout)
{
    DeviceD3D12& device = ((PipelineLayoutD3D12&)pipelineLayout).GetDevice();
    device.DestroyPipelineLayout(pipelineLayout);
}

static void NRI_CALL DestroyPipeline(Pipeline& pipeline)
{
    DeviceD3D12& device = ((PipelineD3D12&)pipeline).GetDevice();
    device.DestroyPipeline(pipeline);
}

static void NRI_CALL DestroyFrameBuffer(FrameBuffer& frameBuffer)
{
    DeviceD3D12& device = ((FrameBufferD3D12&)frameBuffer).GetDevice();
    device.DestroyFrameBuffer(frameBuffer);
}

static void NRI_CALL DestroyQueryPool(QueryPool& queryPool)
{
    DeviceD3D12& device = ((QueryPoolD3D12&)queryPool).GetDevice();
    device.DestroyQueryPool(queryPool);
}

static void NRI_CALL DestroyQueueSemaphore(QueueSemaphore& queueSemaphore)
{
    DeviceD3D12& device = ((QueueSemaphoreD3D12&)queueSemaphore).GetDevice();
    device.DestroyQueueSemaphore(queueSemaphore);
}

static void NRI_CALL DestroyDeviceSemaphore(DeviceSemaphore& deviceSemaphore)
{
    DeviceD3D12& device = ((DeviceSemaphoreD3D12&)deviceSemaphore).GetDevice();
    device.DestroyDeviceSemaphore(deviceSemaphore);
}

static Result NRI_CALL AllocateMemory(Device& device, uint32_t physicalDeviceMask, MemoryType memoryType, uint64_t size, Memory*& memory)
{
    // TODO: unused physicalDeviceMask
    return ((DeviceD3D12&)device).AllocateMemory(memoryType, size, memory);
}

static Result NRI_CALL BindBufferMemory(Device& device, const BufferMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    return ((DeviceD3D12&)device).BindBufferMemory(memoryBindingDescs, memoryBindingDescNum);
}

static Result NRI_CALL BindTextureMemory(Device& device, const TextureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    return ((DeviceD3D12&)device).BindTextureMemory(memoryBindingDescs, memoryBindingDescNum);
}

static void NRI_CALL FreeMemory(Memory& memory)
{
    DeviceD3D12& device = ((MemoryD3D12&)memory).GetDevice();
    device.FreeMemory(memory);
}

static FormatSupportBits NRI_CALL GetFormatSupport(const Device& device, Format format)
{
    return ((const DeviceD3D12&)device).GetFormatSupport(format);
}

static void NRI_CALL SetDeviceDebugName(Device& device, const char* name)
{
    ((DeviceD3D12&)device).SetDebugName(name);
}

static void NRI_CALL SetDeviceSemaphoreDebugName(DeviceSemaphore& deviceSemaphore, const char* name)
{
    ((DeviceSemaphoreD3D12&)deviceSemaphore).SetDebugName(name);
}

static void NRI_CALL SetQueueSemaphoreDebugName(QueueSemaphore& queueSemaphore, const char* name)
{
    ((QueueSemaphoreD3D12&)queueSemaphore).SetDebugName(name);
}

static void NRI_CALL SetDescriptorDebugName(Descriptor& descriptor, const char* name)
{
    ((DescriptorD3D12&)descriptor).SetDebugName(name);
}

static void NRI_CALL SetPipelineDebugName(Pipeline& pipeline, const char* name)
{
    ((PipelineD3D12&)pipeline).SetDebugName(name);
}

static void NRI_CALL SetFrameBufferDebugName(FrameBuffer& frameBuffer, const char* name)
{
    ((FrameBufferD3D12&)frameBuffer).SetDebugName(name);
}

static void NRI_CALL SetMemoryDebugName(Memory& memory, const char* name)
{
    ((MemoryD3D12&)memory).SetDebugName(name);
}

void FillFunctionTableBufferD3D12(CoreInterface& coreInterface);
void FillFunctionTableCommandAllocatorD3D12(CoreInterface& coreInterface);
void FillFunctionTableCommandBufferD3D12(CoreInterface& coreInterface);
void FillFunctionTableCommandQueueD3D12(CoreInterface& coreInterface);
void FillFunctionTableDescriptorPoolD3D12(CoreInterface& coreInterface);
void FillFunctionTableDescriptorSetD3D12(CoreInterface& coreInterface);
void FillFunctionTableQueryPoolD3D12(CoreInterface& coreInterface);
void FillFunctionTableTextureD3D12(CoreInterface& coreInterface);
void FillFunctionTablePipelineLayoutD3D12(CoreInterface& coreInterface);

Result DeviceD3D12::FillFunctionTable(CoreInterface& coreInterface) const
{
    coreInterface = {};

    FillFunctionTableBufferD3D12(coreInterface);
    FillFunctionTableCommandAllocatorD3D12(coreInterface);
    FillFunctionTableCommandBufferD3D12(coreInterface);
    FillFunctionTableCommandQueueD3D12(coreInterface);
    FillFunctionTableDescriptorPoolD3D12(coreInterface);
    FillFunctionTableDescriptorSetD3D12(coreInterface);
    FillFunctionTableQueryPoolD3D12(coreInterface);
    FillFunctionTableTextureD3D12(coreInterface);
    FillFunctionTablePipelineLayoutD3D12(coreInterface);

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
    coreInterface.SetDeviceSemaphoreDebugName = ::SetDeviceSemaphoreDebugName;
    coreInterface.SetQueueSemaphoreDebugName = ::SetQueueSemaphoreDebugName;
    coreInterface.SetDescriptorDebugName = ::SetDescriptorDebugName;
    coreInterface.SetPipelineDebugName = ::SetPipelineDebugName;
    coreInterface.SetFrameBufferDebugName = ::SetFrameBufferDebugName;
    coreInterface.SetMemoryDebugName = ::SetMemoryDebugName;

    return ValidateFunctionTable(GetLog(), coreInterface);
}

#pragma endregion

#pragma region [  SwapChainInterface  ]

static Result NRI_CALL CreateSwapChain(Device& device, const SwapChainDesc& swapChainDesc, SwapChain*& swapChain)
{
    return ((DeviceD3D12&)device).CreateSwapChain(swapChainDesc, swapChain);
}

static void NRI_CALL DestroySwapChain(SwapChain& swapChain)
{
    DeviceD3D12& device = ((SwapChainD3D12&)swapChain).GetDevice();
    device.DestroySwapChain(swapChain);
}

void FillFunctionTableSwapChainD3D12(SwapChainInterface& swapChainInterface);

Result DeviceD3D12::FillFunctionTable(SwapChainInterface& swapChainInterface) const
{
    swapChainInterface = {};

    FillFunctionTableSwapChainD3D12(swapChainInterface);

    swapChainInterface.CreateSwapChain = ::CreateSwapChain;
    swapChainInterface.DestroySwapChain = ::DestroySwapChain;

    return ValidateFunctionTable(GetLog(), swapChainInterface);
}

#pragma endregion

#pragma region [  WrapperD3D12Interface  ]

static ID3D12Device* NRI_CALL GetDeviceD3D12(const Device& device)
{
    return (DeviceD3D12&)device;
}

static ID3D12Resource* NRI_CALL GetBufferD3D12(const Buffer& buffer)
{
    return (BufferD3D12&)buffer;
}

static ID3D12Resource* NRI_CALL GetTextureD3D12(const Texture& texture)
{
    return (TextureD3D12&)texture;
}

static Result NRI_CALL CreateCommandBufferD3D12(Device& device, const CommandBufferD3D12Desc& commandBufferDesc, CommandBuffer*& commandBuffer)
{
    return ((DeviceD3D12&)device).CreateCommandBuffer(commandBufferDesc, commandBuffer);
}

static Result NRI_CALL CreateBufferD3D12(Device& device, const BufferD3D12Desc& bufferDesc, Buffer*& buffer)
{
    return ((DeviceD3D12&)device).CreateBuffer(bufferDesc, buffer);
}

static Result NRI_CALL CreateTextureD3D12(Device& device, const TextureD3D12Desc& textureDesc, Texture*& texture)
{
    return ((DeviceD3D12&)device).CreateTexture(textureDesc, texture);
}

static Result NRI_CALL CreateMemoryD3D12(Device& device, const MemoryD3D12Desc& memoryDesc, Memory*& memory)
{
    return ((DeviceD3D12&)device).CreateMemory(memoryDesc, memory);
}

void FillFunctionTableCommandBufferD3D12(WrapperD3D12Interface& wrapperD3D11Interface);

Result DeviceD3D12::FillFunctionTable(WrapperD3D12Interface& wrapperD3D12Interface) const
{
    wrapperD3D12Interface = {};

    FillFunctionTableCommandBufferD3D12(wrapperD3D12Interface);

    wrapperD3D12Interface.CreateCommandBufferD3D12 = ::CreateCommandBufferD3D12;
    wrapperD3D12Interface.CreateBufferD3D12 = ::CreateBufferD3D12;
    wrapperD3D12Interface.CreateTextureD3D12 = ::CreateTextureD3D12;
    wrapperD3D12Interface.CreateMemoryD3D12 = ::CreateMemoryD3D12;

    wrapperD3D12Interface.GetDeviceD3D12 = ::GetDeviceD3D12;
    wrapperD3D12Interface.GetBufferD3D12 = ::GetBufferD3D12;
    wrapperD3D12Interface.GetTextureD3D12 = ::GetTextureD3D12;

    return ValidateFunctionTable(GetLog(), wrapperD3D12Interface);
}

#pragma endregion

#pragma region [  RayTracingInterface  ]

#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
static Result NRI_CALL CreateRayTracingPipeline(Device& device, const RayTracingPipelineDesc& rayTracingPipelineDesc, Pipeline*& pipeline)
{
    return ((DeviceD3D12&)device).CreatePipeline(rayTracingPipelineDesc, pipeline);
}

static Result NRI_CALL CreateAccelerationStructure(Device& device, const AccelerationStructureDesc& accelerationStructureDesc, AccelerationStructure*& accelerationStructure)
{
    return ((DeviceD3D12&)device).CreateAccelerationStructure(accelerationStructureDesc, accelerationStructure);
}

static void NRI_CALL DestroyAccelerationStructure(AccelerationStructure& accelerationStructure)
{
    DeviceD3D12& device = ((AccelerationStructureD3D12&)accelerationStructure).GetDevice();
    device.DestroyAccelerationStructure(accelerationStructure);
}

static Result NRI_CALL BindAccelerationStructureMemory(Device& device, const AccelerationStructureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    return ((DeviceD3D12&)device).BindAccelerationStructureMemory(memoryBindingDescs, memoryBindingDescNum);
}

void FillFunctionTablePipelineD3D12(RayTracingInterface& rayTracingInterface);
void FillFunctionTableCommandBufferD3D12(RayTracingInterface& rayTracingInterface);
void FillFunctionTableAccelerationStructureD3D12(RayTracingInterface& rayTracingInterface);

Result DeviceD3D12::FillFunctionTable(RayTracingInterface& rayTracingInterface) const
{
    rayTracingInterface = {};

    if (!m_Device5.GetInterface() || !m_IsRaytracingSupported)
        return Result::UNSUPPORTED;

    FillFunctionTablePipelineD3D12(rayTracingInterface);
    FillFunctionTableAccelerationStructureD3D12(rayTracingInterface);
    FillFunctionTableCommandBufferD3D12(rayTracingInterface);

    rayTracingInterface.CreateRayTracingPipeline = ::CreateRayTracingPipeline;
    rayTracingInterface.CreateAccelerationStructure = ::CreateAccelerationStructure;
    rayTracingInterface.BindAccelerationStructureMemory = ::BindAccelerationStructureMemory;
    rayTracingInterface.DestroyAccelerationStructure = ::DestroyAccelerationStructure;

    return ValidateFunctionTable(GetLog(), rayTracingInterface);
}
#endif

#pragma endregion

#pragma region [  MeshShaderInterface  ]

#ifdef __ID3D12GraphicsCommandList6_INTERFACE_DEFINED__
void FillFunctionTableCommandBufferD3D12(MeshShaderInterface& meshShaderInterface);

Result DeviceD3D12::FillFunctionTable(MeshShaderInterface& meshShaderInterface) const
{
    if (!m_IsMeshShaderSupported)
        return Result::UNSUPPORTED;

    meshShaderInterface = {};

    FillFunctionTableCommandBufferD3D12(meshShaderInterface);

    return ValidateFunctionTable(GetLog(), meshShaderInterface);
}
#endif

#pragma endregion

#pragma region [  HelperInterface  ]

static uint32_t NRI_CALL CountAllocationNumD3D12(Device& device, const ResourceGroupDesc& resourceGroupDesc)
{
    return ((DeviceD3D12&)device).CalculateAllocationNumber(resourceGroupDesc);
}

static Result NRI_CALL AllocateAndBindMemoryD3D12(Device& device, const ResourceGroupDesc& resourceGroupDesc, Memory** allocations)
{
    return ((DeviceD3D12&)device).AllocateAndBindMemory(resourceGroupDesc, allocations);
}

void FillFunctionTableCommandQueueD3D12(HelperInterface& helperInterface);

Result DeviceD3D12::FillFunctionTable(HelperInterface& helperInterface) const
{
    helperInterface = {};

    helperInterface.CalculateAllocationNumber = ::CountAllocationNumD3D12;
    helperInterface.AllocateAndBindMemory = ::AllocateAndBindMemoryD3D12;
    FillFunctionTableCommandQueueD3D12(helperInterface);

    return ValidateFunctionTable(GetLog(), helperInterface);
}

#pragma endregion
