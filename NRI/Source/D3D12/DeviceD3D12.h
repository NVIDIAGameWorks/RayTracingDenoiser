/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <unordered_map>

struct IDXGIFactory2;
struct IDXGIAdapter;
struct ID3D12Device;
struct ID3D12Device5;
struct ID3D12DescriptorHeap;
struct ID3D12CommandSignature;
struct D3D12_CPU_DESCRIPTOR_HANDLE;

enum D3D12_DESCRIPTOR_HEAP_TYPE;
enum D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;

#define DESCRIPTORS_BATCH_SIZE          1024

typedef size_t DescriptorPointerCPU;
typedef uint64_t DescriptorPointerGPU;
typedef uint16_t HeapIndexType;
typedef uint16_t HeapOffsetType;

namespace nri
{
    struct CommandQueueD3D12;

    struct DescriptorHandle
    {
        HeapIndexType heapIndex;
        HeapOffsetType heapOffset;
    };

    struct DescriptorHeapDesc
    {
        ComPtr<ID3D12DescriptorHeap> descriptorHeap;
        DescriptorPointerCPU descriptorPointerCPU;
        DescriptorPointerGPU descriptorPointerGPU;
        uint32_t descriptorSize;
    };

    struct DeviceD3D12 final : public DeviceBase
    {
        DeviceD3D12(const Log& log, StdAllocator<uint8_t>& stdAllocator);
        ~DeviceD3D12();

        operator ID3D12Device*() const;
        operator ID3D12Device5*() const;

        template<typename Implementation, typename Interface, typename ... Args>
        Result CreateImplementation(Interface*& entity, const Args&... args);

        Result Create(IDXGIAdapter* dxgiAdapter, bool enableValidation);
        Result Create(const DeviceCreationD3D12Desc& deviceCreationDesc);

        Result CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorNum);
        Result GetDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE type, DescriptorHandle& descriptorHandle);
        DescriptorPointerCPU GetDescriptorPointerCPU(const DescriptorHandle& descriptorHandle) const;
        DescriptorPointerGPU GetDescriptorPointerGPU(const DescriptorHandle& descriptorHandle) const;
        void GetMemoryInfo(MemoryLocation memoryLocation, const D3D12_RESOURCE_DESC& resourceDesc, MemoryDesc& memoryDesc) const;

        ID3D12CommandSignature* CreateCommandSignature(D3D12_INDIRECT_ARGUMENT_TYPE indirectArgumentType, uint32_t stride);
        ID3D12CommandSignature* GetDrawCommandSignature(uint32_t stride);
        ID3D12CommandSignature* GetDrawIndexedCommandSignature(uint32_t stride);
        ID3D12CommandSignature* GetDispatchCommandSignature() const;

        bool IsMeshShaderSupported() const;
        const CoreInterface& GetCoreInterface() const;

        //================================================================================================================
        // NRI
        //================================================================================================================
        void SetDebugName(const char* name);

        const DeviceDesc& GetDesc() const;
        Result GetCommandQueue(CommandQueueType commandQueueType, CommandQueue*& commandQueue);

        Result CreateCommandQueue(CommandQueueType commandQueueType, CommandQueue*& commandQueue);
        Result CreateCommandQueue(void* d3d12commandQueue, CommandQueueD3D12*& commandQueue);
        Result CreateCommandAllocator(const CommandQueue& commandQueue, CommandAllocator*& commandAllocator);
        Result CreateDescriptorPool(const DescriptorPoolDesc& descriptorPoolDesc, DescriptorPool*& descriptorPool);
        Result CreateBuffer(const BufferDesc& bufferDesc, Buffer*& buffer);
        Result CreateTexture(const TextureDesc& textureDesc, Texture*& texture);
        Result CreateDescriptor(const BufferViewDesc& bufferViewDesc, Descriptor*& bufferView);
        Result CreateDescriptor(const Texture1DViewDesc& textureViewDesc, Descriptor*& textureView);
        Result CreateDescriptor(const Texture2DViewDesc& textureViewDesc, Descriptor*& textureView);
        Result CreateDescriptor(const Texture3DViewDesc& textureViewDesc, Descriptor*& textureView);
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
        Result CreateDescriptor(const AccelerationStructure& accelerationStructure, Descriptor*& accelerationStructureView);
#endif
        Result CreateDescriptor(const SamplerDesc& samplerDesc, Descriptor*& sampler);
        Result CreatePipelineLayout(const PipelineLayoutDesc& pipelineLayoutDesc, PipelineLayout*& pipelineLayout);
        Result CreatePipeline(const GraphicsPipelineDesc& graphicsPipelineDesc, Pipeline*& pipeline);
        Result CreatePipeline(const ComputePipelineDesc& computePipelineDesc, Pipeline*& pipeline);
        Result CreatePipeline(const RayTracingPipelineDesc& rayTracingPipelineDesc, Pipeline*& pipeline);
        Result CreateFrameBuffer(const FrameBufferDesc& frameBufferDesc, FrameBuffer*& frameBuffer);
        Result CreateQueryPool(const QueryPoolDesc& queryPoolDesc, QueryPool*& queryPool);
        Result CreateQueueSemaphore(QueueSemaphore*& queueSemaphore);
        Result CreateDeviceSemaphore(bool signaled, DeviceSemaphore*& deviceSemaphore);

        Result CreateCommandBuffer(const CommandBufferD3D12Desc& commandBufferDesc, CommandBuffer*& commandBuffer);
        Result CreateBuffer(const BufferD3D12Desc& commandBufferDesc, Buffer*& commandBuffer);
        Result CreateTexture(const TextureD3D12Desc& commandBufferDesc, Texture*& commandBuffer);
        Result CreateMemory(const MemoryD3D12Desc& memoryDesc, Memory*& memory);

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
        void DestroySwapChain(SwapChain& swapChain);

        Result AllocateMemory(const MemoryType memoryType, uint64_t size, Memory*& memory);
        Result BindBufferMemory(const BufferMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);
        Result BindTextureMemory(const TextureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
        Result CreateAccelerationStructure(const AccelerationStructureDesc& accelerationStructureDesc, AccelerationStructure*& accelerationStructure);
        Result BindAccelerationStructureMemory(const AccelerationStructureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);
        void DestroyAccelerationStructure(AccelerationStructure& accelerationStructure);
#endif
        void FreeMemory(Memory& memory);

        FormatSupportBits GetFormatSupport(Format format) const;

        uint32_t CalculateAllocationNumber(const ResourceGroupDesc& resourceGroupDesc) const;
        Result AllocateAndBindMemory(const ResourceGroupDesc& resourceGroupDesc, Memory** allocations);

        Result CreateSwapChain(const SwapChainDesc& swapChainDesc, SwapChain*& swapChain);

        //================================================================================================================
        // DeviceBase
        //================================================================================================================
        void Destroy();
        Result FillFunctionTable(CoreInterface& table) const;
        Result FillFunctionTable(SwapChainInterface& table) const;
        Result FillFunctionTable(WrapperD3D12Interface& wrapperD3D12Interface) const;
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
        Result FillFunctionTable(RayTracingInterface& rayTracingInterface) const;
#endif
#ifdef __ID3D12GraphicsCommandList6_INTERFACE_DEFINED__
        Result FillFunctionTable(MeshShaderInterface& meshShaderInterface) const;
#endif
        Result FillFunctionTable(HelperInterface& helperInterface) const;

    private:
        void UpdateDeviceDesc(IDXGIAdapter* adapter, bool enableValidation);
        MemoryType GetMemoryType(MemoryLocation memoryLocation, const D3D12_RESOURCE_DESC& resourceDesc) const;

    private:
        ComPtr<ID3D12Device> m_Device;
#ifdef __ID3D12Device5_INTERFACE_DEFINED__
        ComPtr<ID3D12Device5> m_Device5;
#endif
        bool m_IsRaytracingSupported = false;
        bool m_IsMeshShaderSupported = false;
        std::array<CommandQueueD3D12*, COMMAND_QUEUE_TYPE_NUM> m_CommandQueues = {};
        Vector<DescriptorHeapDesc> m_DescriptorHeaps;
        static const uint32_t m_DescriptorHeapTypeNum = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
        Vector<Vector<DescriptorHandle>> m_DescriptorPool;
        DeviceDesc m_DeviceDesc = {};
        UnorderedMap<uint32_t, ComPtr<ID3D12CommandSignature>> m_DrawCommandSignatures;
        UnorderedMap<uint32_t, ComPtr<ID3D12CommandSignature>> m_DrawIndexedCommandSignatures;
        ComPtr<ID3D12CommandSignature> m_DispatchCommandSignature;
        CoreInterface m_CoreInterface = {};
    };

    inline DeviceD3D12::operator ID3D12Device*() const
    {
        return m_Device.GetInterface();
    }

#ifdef __ID3D12Device5_INTERFACE_DEFINED__
    inline DeviceD3D12::operator ID3D12Device5*() const
    {
        return m_Device5.GetInterface();
    }
#endif

    inline bool DeviceD3D12::IsMeshShaderSupported() const
    {
        return m_IsMeshShaderSupported;
    }

    inline const CoreInterface& DeviceD3D12::GetCoreInterface() const
    {
        return m_CoreInterface;
    }
}
