/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static const DeviceDesc& NRI_CALL GetDeviceDesc(const Device& device)
{
    return ((const DeviceD3D11&)device).GetDesc();
}

static Result NRI_CALL GetCommandQueue(Device& device, CommandQueueType commandQueueType, CommandQueue*& commandQueue)
{
    return ((DeviceD3D11&)device).GetCommandQueue(commandQueueType, commandQueue);
}

static Result NRI_CALL CreateCommandAllocator(const CommandQueue& commandQueue, uint32_t physicalDeviceMask, CommandAllocator*& commandAllocator)
{
    DeviceD3D11& device = ((CommandQueueD3D11&)commandQueue).GetDevice();
    return device.CreateCommandAllocator(commandQueue, commandAllocator);
}

static Result NRI_CALL CreateDescriptorPool(Device& device, const DescriptorPoolDesc& descriptorPoolDesc, DescriptorPool*& descriptorPool)
{
    return ((DeviceD3D11&)device).CreateDescriptorPool(descriptorPoolDesc, descriptorPool);
}

static Result NRI_CALL CreateBuffer(Device& device, const BufferDesc& bufferDesc, Buffer*& buffer)
{
    return ((DeviceD3D11&)device).CreateBuffer(bufferDesc, buffer);
}

static Result NRI_CALL CreateTexture(Device& device, const TextureDesc& textureDesc, Texture*& texture)
{
    return ((DeviceD3D11&)device).CreateTexture(textureDesc, texture);
}

static Result NRI_CALL CreateBufferView(const BufferViewDesc& bufferViewDesc, Descriptor*& bufferView)
{
    DeviceD3D11& device = ((const BufferD3D11*)bufferViewDesc.buffer)->GetDevice();
    return device.CreateDescriptor(bufferViewDesc, bufferView);
}

static Result NRI_CALL CreateTexture1DView(const Texture1DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    DeviceD3D11& device = ((const TextureD3D11*)textureViewDesc.texture)->GetDevice();
    return device.CreateDescriptor(textureViewDesc, textureView);
}

static Result NRI_CALL CreateTexture2DView(const Texture2DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    DeviceD3D11& device = ((const TextureD3D11*)textureViewDesc.texture)->GetDevice();
    return device.CreateDescriptor(textureViewDesc, textureView);
}

static Result NRI_CALL CreateTexture3DView(const Texture3DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    DeviceD3D11& device = ((const TextureD3D11*)textureViewDesc.texture)->GetDevice();
    return device.CreateDescriptor(textureViewDesc, textureView);
}

static Result NRI_CALL CreateSampler(Device& device, const SamplerDesc& samplerDesc, Descriptor*& sampler)
{
    return ((DeviceD3D11&)device).CreateDescriptor(samplerDesc, sampler);
}

static Result NRI_CALL CreatePipelineLayout(Device& device, const PipelineLayoutDesc& pipelineLayoutDesc, PipelineLayout*& pipelineLayout)
{
    return ((DeviceD3D11&)device).CreatePipelineLayout(pipelineLayoutDesc, pipelineLayout);
}

static Result NRI_CALL CreateGraphicsPipeline(Device& device, const GraphicsPipelineDesc& graphicsPipelineDesc, Pipeline*& pipeline)
{
    return ((DeviceD3D11&)device).CreatePipeline(graphicsPipelineDesc, pipeline);
}

static Result NRI_CALL CreateComputePipeline(Device& device, const ComputePipelineDesc& computePipelineDesc, Pipeline*& pipeline)
{
    return ((DeviceD3D11&)device).CreatePipeline(computePipelineDesc, pipeline);
}

static Result NRI_CALL CreateFrameBuffer(Device& device, const FrameBufferDesc& frameBufferDesc, FrameBuffer*& frameBuffer)
{
    return ((DeviceD3D11&)device).CreateFrameBuffer(frameBufferDesc, frameBuffer);
}

static Result NRI_CALL CreateQueryPool(Device& device, const QueryPoolDesc& queryPoolDesc, QueryPool*& queryPool)
{
    return ((DeviceD3D11&)device).CreateQueryPool(queryPoolDesc, queryPool);
}

static Result NRI_CALL CreateQueueSemaphore(Device& device, QueueSemaphore*& queueSemaphore)
{
    return ((DeviceD3D11&)device).CreateQueueSemaphore(queueSemaphore);
}

static Result NRI_CALL CreateDeviceSemaphore(Device& device, bool signaled, DeviceSemaphore*& deviceSemaphore)
{
    return ((DeviceD3D11&)device).CreateDeviceSemaphore(signaled, deviceSemaphore);
}

static void NRI_CALL DestroyCommandAllocator(CommandAllocator& commandAllocator)
{
    DeviceD3D11& device = ((CommandAllocatorD3D11&)commandAllocator).GetDevice();
    device.DestroyCommandAllocator(commandAllocator);
}

static void NRI_CALL DestroyDescriptorPool(DescriptorPool& descriptorPool)
{
    DeviceD3D11& device = ((DescriptorPoolD3D11&)descriptorPool).GetDevice();
    device.DestroyDescriptorPool(descriptorPool);
}

static void NRI_CALL DestroyBuffer(Buffer& buffer)
{
    DeviceD3D11& device = ((BufferD3D11&)buffer).GetDevice();
    device.DestroyBuffer(buffer);
}

static void NRI_CALL DestroyTexture(Texture& texture)
{
    DeviceD3D11& device = ((TextureD3D11&)texture).GetDevice();
    device.DestroyTexture(texture);
}

static void NRI_CALL DestroyDescriptor(Descriptor& descriptor)
{
    DeviceD3D11& device = ((DescriptorD3D11&)descriptor).GetDevice();
    device.DestroyDescriptor(descriptor);
}

static void NRI_CALL DestroyPipelineLayout(PipelineLayout& pipelineLayout)
{
    DeviceD3D11& device = ((PipelineLayoutD3D11&)pipelineLayout).GetDevice();
    device.DestroyPipelineLayout(pipelineLayout);
}

static void NRI_CALL DestroyPipeline(Pipeline& pipeline)
{
    DeviceD3D11& device = ((PipelineD3D11&)pipeline).GetDevice();
    device.DestroyPipeline(pipeline);
}

static void NRI_CALL DestroyFrameBuffer(FrameBuffer& frameBuffer)
{
    DeviceD3D11& device = ((FrameBufferD3D11&)frameBuffer).GetDevice();
    device.DestroyFrameBuffer(frameBuffer);
}

static void NRI_CALL DestroyQueryPool(QueryPool& queryPool)
{
    DeviceD3D11& device = ((QueryPoolD3D11&)queryPool).GetDevice();
    device.DestroyQueryPool(queryPool);
}

static void NRI_CALL DestroyQueueSemaphore(QueueSemaphore& queueSemaphore)
{
    DeviceD3D11& device = ((QueueSemaphoreD3D11&)queueSemaphore).GetDevice();
    device.DestroyQueueSemaphore(queueSemaphore);
}

static void NRI_CALL DestroyDeviceSemaphore(DeviceSemaphore& deviceSemaphore)
{
    DeviceD3D11& device = ((DeviceSemaphoreD3D11&)deviceSemaphore).GetDevice();
    device.DestroyDeviceSemaphore(deviceSemaphore);
}

static Result NRI_CALL AllocateMemory(Device& device, uint32_t physicalDeviceMask, MemoryType memoryType, uint64_t size, Memory*& memory)
{
    return ((DeviceD3D11&)device).AllocateMemory(memoryType, size, memory);
}

static Result NRI_CALL BindBufferMemory(Device& device, const BufferMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    return ((DeviceD3D11&)device).BindBufferMemory(memoryBindingDescs, memoryBindingDescNum);
}

static Result NRI_CALL BindTextureMemory(Device& device, const TextureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    return ((DeviceD3D11&)device).BindTextureMemory(memoryBindingDescs, memoryBindingDescNum);
}

static void NRI_CALL FreeMemory(Memory& memory)
{
    DeviceD3D11& device = ((MemoryD3D11&)memory).GetDevice();
    device.FreeMemory(memory);
}

static FormatSupportBits NRI_CALL GetFormatSupport(const Device& device, Format format)
{
    return ((const DeviceD3D11&)device).GetFormatSupport(format);
}

static void NRI_CALL SetDeviceDebugName(Device& device, const char* name)
{
    ((DeviceD3D11&)device).SetDebugName(name);
}

void FillFunctionTableBufferD3D11(CoreInterface& coreInterface);
void FillFunctionTableCommandAllocatorD3D11(CoreInterface& coreInterface);
void FillFunctionTableCommandBufferD3D11(CoreInterface& coreInterface);
void FillFunctionTableCommandBufferEmuD3D11(CoreInterface& coreInterface);
void FillFunctionTableCommandQueueD3D11(CoreInterface& coreInterface);
void FillFunctionTableDescriptorD3D11(CoreInterface& coreInterface);
void FillFunctionTableDescriptorPoolD3D11(CoreInterface& coreInterface);
void FillFunctionTableDescriptorSetD3D11(CoreInterface& coreInterface);
void FillFunctionTableDeviceSemaphoreD3D11(CoreInterface& coreInterface);
void FillFunctionTableFrameBufferD3D11(CoreInterface& coreInterface);
void FillFunctionTableMemoryD3D11(CoreInterface& coreInterface);
void FillFunctionTablePipelineLayoutD3D11(CoreInterface& coreInterface);
void FillFunctionTablePipelineD3D11(CoreInterface& coreInterface);
void FillFunctionTableQueryPoolD3D11(CoreInterface& coreInterface);
void FillFunctionTableQueueSemaphoreD3D11(CoreInterface& coreInterface);
void FillFunctionTableTextureD3D11(CoreInterface& coreInterface);

Result DeviceD3D11::FillFunctionTable(CoreInterface& coreInterface) const
{
    coreInterface = {};

    FillFunctionTableBufferD3D11(coreInterface);
    FillFunctionTableCommandAllocatorD3D11(coreInterface);
    FillFunctionTableCommandQueueD3D11(coreInterface);
    FillFunctionTableDescriptorD3D11(coreInterface);
    FillFunctionTableDescriptorPoolD3D11(coreInterface);
    FillFunctionTableDescriptorSetD3D11(coreInterface);
    FillFunctionTableDeviceSemaphoreD3D11(coreInterface);
    FillFunctionTableFrameBufferD3D11(coreInterface);
    FillFunctionTableMemoryD3D11(coreInterface);
    FillFunctionTablePipelineLayoutD3D11(coreInterface);
    FillFunctionTablePipelineD3D11(coreInterface);
    FillFunctionTableQueryPoolD3D11(coreInterface);
    FillFunctionTableQueueSemaphoreD3D11(coreInterface);
    FillFunctionTableTextureD3D11(coreInterface);

    if (m_Device.isDeferredContextsEmulated)
        FillFunctionTableCommandBufferEmuD3D11(coreInterface);
    else
        FillFunctionTableCommandBufferD3D11(coreInterface);

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
    return ((DeviceD3D11&)device).CreateSwapChain(swapChainDesc, swapChain);
}

static void NRI_CALL DestroySwapChain(SwapChain& swapChain)
{
    DeviceD3D11& device = ((SwapChainD3D11&)swapChain).GetDevice();
    return device.DestroySwapChain(swapChain);
}

void FillFunctionTableSwapChainD3D11(SwapChainInterface& swapChainInterface);

Result DeviceD3D11::FillFunctionTable(SwapChainInterface& swapChainInterface) const
{
    swapChainInterface = {};

    FillFunctionTableSwapChainD3D11(swapChainInterface);

    swapChainInterface.CreateSwapChain = ::CreateSwapChain;
    swapChainInterface.DestroySwapChain = ::DestroySwapChain;

    return ValidateFunctionTable(GetLog(), swapChainInterface);
}

#pragma endregion

#pragma region [  WrapperD3D11Interface  ]

Result CreateDeviceD3D11(const DeviceCreationD3D11Desc& deviceCreationD3D11Desc, DeviceBase*& device)
{
    DeviceCreationDesc deviceCreationDesc = {};
    deviceCreationDesc.callbackInterface = deviceCreationD3D11Desc.callbackInterface;
    deviceCreationDesc.callbackInterfaceUserArg = deviceCreationD3D11Desc.callbackInterfaceUserArg;
    deviceCreationDesc.memoryAllocatorInterface = deviceCreationD3D11Desc.memoryAllocatorInterface;
    deviceCreationDesc.memoryAllocatorInterfaceUserArg = deviceCreationD3D11Desc.memoryAllocatorInterfaceUserArg;
    deviceCreationDesc.graphicsAPI = GraphicsAPI::D3D11;

    Log log(GraphicsAPI::D3D11, deviceCreationDesc.callbackInterface, deviceCreationDesc.callbackInterfaceUserArg);
    StdAllocator<uint8_t> allocator(deviceCreationDesc.memoryAllocatorInterface, deviceCreationDesc.memoryAllocatorInterfaceUserArg);

    ID3D11Device* d3d11Device = (ID3D11Device*)deviceCreationD3D11Desc.d3d11Device;

    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3d11Device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    RETURN_ON_BAD_HRESULT(log, hr, "Can't create device. Failed to query IDXGIDevice from ID3D11Device. (result: %d)", (int32_t)hr);

    hr = dxgiDevice->GetAdapter(&adapter);
    RETURN_ON_BAD_HRESULT(log, hr, "Can't create device. IDXGIDevice::GetAdapter() failed. (result: %d)", (int32_t)hr);

    DeviceD3D11* implementation = Allocate<DeviceD3D11>(allocator, log, allocator);
    const nri::Result result = implementation->Create(deviceCreationDesc, adapter, d3d11Device, (AGSContext*)deviceCreationD3D11Desc.agsContextAssociatedWithDevice);

    if (result == nri::Result::SUCCESS)
    {
        device = (DeviceBase*)implementation;
        return nri::Result::SUCCESS;
    }

    Deallocate(allocator, implementation);
    return result;
}

static Result NRI_CALL CreateCommandBufferD3D11(Device& device, const CommandBufferD3D11Desc& commandBufferDesc, CommandBuffer*& commandBuffer)
{
    DeviceD3D11& deviceD3D11 = (DeviceD3D11&)device;

    return ::CreateCommandBuffer(deviceD3D11, (ID3D11DeviceContext*)commandBufferDesc.d3d11DeviceContext, commandBuffer);
}

static Result NRI_CALL CreateBufferD3D11(Device& device, const BufferD3D11Desc& bufferDesc, Buffer*& buffer)
{
    DeviceD3D11& deviceD3D11 = (DeviceD3D11&)device;

    BufferD3D11* implementation = Allocate<BufferD3D11>(deviceD3D11.GetStdAllocator(), deviceD3D11, deviceD3D11.GetImmediateContext());
    const nri::Result res = implementation->Create(bufferDesc);

    if (res == nri::Result::SUCCESS)
    {
        buffer = (Buffer*)implementation;
        return nri::Result::SUCCESS;
    }

    Deallocate(deviceD3D11.GetStdAllocator(), implementation);
    return res;
}

static Result NRI_CALL CreateTextureD3D11(Device& device, const TextureD3D11Desc& textureDesc, Texture*& texture)
{
    DeviceD3D11& deviceD3D11 = (DeviceD3D11&)device;

    TextureD3D11* implementation = Allocate<TextureD3D11>(deviceD3D11.GetStdAllocator());
    const nri::Result res = implementation->Create(deviceD3D11, textureDesc);

    if (res == nri::Result::SUCCESS)
    {
        texture = (Texture*)implementation;
        return nri::Result::SUCCESS;
    }

    Deallocate(deviceD3D11.GetStdAllocator(), implementation);
    return res;
}

static ID3D11Device* NRI_CALL GetDeviceD3D11(const Device& device)
{
    return ((DeviceD3D11&)device).GetDevice().ptr;
}

static ID3D11Resource* NRI_CALL GetBufferD3D11(const Buffer& buffer)
{
    return (BufferD3D11&)buffer;
}

static ID3D11Resource* NRI_CALL GetTextureD3D11(const Texture& texture)
{
    return (TextureD3D11&)texture;
}

void FillFunctionTableCommandBufferD3D11(WrapperD3D11Interface& wrapperD3D11Interface);

Result DeviceD3D11::FillFunctionTable(WrapperD3D11Interface& wrapperD3D11Interface) const
{
    wrapperD3D11Interface = {};

    FillFunctionTableCommandBufferD3D11(wrapperD3D11Interface);

    wrapperD3D11Interface.CreateCommandBufferD3D11 = ::CreateCommandBufferD3D11;
    wrapperD3D11Interface.CreateTextureD3D11 = ::CreateTextureD3D11;
    wrapperD3D11Interface.CreateBufferD3D11 = ::CreateBufferD3D11;

    wrapperD3D11Interface.GetDeviceD3D11 = ::GetDeviceD3D11;
    wrapperD3D11Interface.GetBufferD3D11 = ::GetBufferD3D11;
    wrapperD3D11Interface.GetTextureD3D11 = ::GetTextureD3D11;

    return ValidateFunctionTable(GetLog(), wrapperD3D11Interface);
}

#pragma endregion
