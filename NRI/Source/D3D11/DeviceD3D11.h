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
    struct CommandQueueD3D11;

    struct DeviceD3D11 final : public DeviceBase
    {
        DeviceD3D11(const Log& log, StdAllocator<uint8_t>& stdAllocator);
        ~DeviceD3D11();

        const VersionedContext& GetImmediateContext() const;
        const VersionedDevice& GetDevice() const;
        const CoreInterface& GetCoreInterface() const;

        Result Create(const DeviceCreationDesc& deviceCreationDesc, IDXGIAdapter* adapter, ID3D11Device* precreatedDevice, AGSContext* agsContext);

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        Result CreateSwapChain(const SwapChainDesc& swapChainDesc, SwapChain*& swapChain);
        void DestroySwapChain(SwapChain& swapChain);
        void SetDebugName(const char* name);
        const DeviceDesc& GetDesc() const;
        Result GetCommandQueue(CommandQueueType commandQueueType, CommandQueue*& commandQueue);
        Result CreateCommandAllocator(const CommandQueue& commandQueue, CommandAllocator*& commandAllocator);
        Result CreateDescriptorPool(const DescriptorPoolDesc& descriptorPoolDesc, DescriptorPool*& descriptorPool);
        Result CreateBuffer(const BufferDesc& bufferDesc, Buffer*& buffer);
        Result CreateTexture(const TextureDesc& textureDesc, Texture*& texture);
        Result CreateDescriptor(const BufferViewDesc& bufferViewDesc, Descriptor*& bufferView);
        Result CreateDescriptor(const Texture1DViewDesc& textureViewDesc, Descriptor*& textureView);
        Result CreateDescriptor(const Texture2DViewDesc& textureViewDesc, Descriptor*& textureView);
        Result CreateDescriptor(const Texture3DViewDesc& textureViewDesc, Descriptor*& textureView);
        Result CreateDescriptor(const SamplerDesc& samplerDesc, Descriptor*& sampler);
        Result CreatePipelineLayout(const PipelineLayoutDesc& pipelineLayoutDesc, PipelineLayout*& pipelineLayout);
        Result CreatePipeline(const GraphicsPipelineDesc& graphicsPipelineDesc, Pipeline*& pipeline);
        Result CreatePipeline(const ComputePipelineDesc& computePipelineDesc, Pipeline*& pipeline);
        Result CreateFrameBuffer(const FrameBufferDesc& frameBufferDesc, FrameBuffer*& frameBuffer);
        Result CreateQueryPool(const QueryPoolDesc& queryPoolDesc, QueryPool*& queryPool);
        Result CreateQueueSemaphore(QueueSemaphore*& queueSemaphore);
        Result CreateDeviceSemaphore(bool signaled, DeviceSemaphore*& deviceSemaphore);
        void DestroyCommandAllocator(CommandAllocator& commandAllocator);
        void DestroyDescriptorPool(DescriptorPool& descriptorPool);
        void DestroyBuffer(Buffer& buffer);
        void DestroyTexture(Texture& texture);
        void DestroyDescriptor(Descriptor& descriptor);
        void DestroyPipelineLayout(PipelineLayout& pipelineLayout);
        void DestroyPipeline(Pipeline& pipeline);
        void DestroyFrameBuffer(FrameBuffer& frameBuffer);
        void DestroyQueryPool(QueryPool& queryPool);
        void DestroyQueueSemaphore(QueueSemaphore& queueSemaphore);
        void DestroyDeviceSemaphore(DeviceSemaphore& deviceSemaphore);
        Result AllocateMemory(MemoryType memoryType, uint64_t size, Memory*& memory);
        Result BindBufferMemory(const BufferMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);
        Result BindTextureMemory(const TextureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);
        void FreeMemory(Memory& memory);

        FormatSupportBits GetFormatSupport(Format format) const;

        uint32_t CalculateAllocationNumber(const ResourceGroupDesc& resourceGroupDesc) const;
        Result AllocateAndBindMemory(const ResourceGroupDesc& resourceGroupDesc, Memory** allocations);

        //================================================================================================================
        // DeviceBase
        //================================================================================================================
        void Destroy();
        Result FillFunctionTable(CoreInterface& table) const;
        Result FillFunctionTable(SwapChainInterface& table) const;
        Result FillFunctionTable(WrapperD3D11Interface& table) const;
        Result FillFunctionTable(HelperInterface& helperInterface) const;

    private:
        void InitVersionedDevice(ID3D11Device* device, bool isDeferredContextsEmulationRequested);
        void InitVersionedContext();

        template<typename Implementation, typename Interface, typename ConstructorArg, typename ... Args>
        nri::Result CreateImplementationWithNonEmptyConstructor(Interface*& entity, ConstructorArg&& constructorArg, const Args&... args);

    private:
        // don't sort - ~DX11Extensions must be called last!
        DX11Extensions m_Ext = {};
        VersionedDevice m_Device = {};
        VersionedContext m_ImmediateContext = {};
        Vector<CommandQueueD3D11> m_CommandQueues;
        DeviceDesc m_Desc = {};
        CRITICAL_SECTION m_CriticalSection = {};
        CoreInterface m_CoreInterface = {};

    private:
        void FillLimits(bool isValidationEnabled, Vendor vendor);
    };

    inline const VersionedContext& DeviceD3D11::GetImmediateContext() const
    {
        return m_ImmediateContext;
    }
    
    inline const VersionedDevice& DeviceD3D11::GetDevice() const
    {
        return m_Device;
    }
    
    inline const CoreInterface& DeviceD3D11::GetCoreInterface() const
    {
        return m_CoreInterface;
    }
}
