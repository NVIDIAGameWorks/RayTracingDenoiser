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
    struct CommandQueueVal;

    struct DeviceVal final : public DeviceBase
    {
        DeviceVal(const Log& log, const StdAllocator<uint8_t>& stdAllocator, DeviceBase& device, uint32_t physicalDeviceNum);
        ~DeviceVal();

        const CoreInterface& GetCoreInterface() const;
        const SwapChainInterface& GetSwapChainInterface() const;
        const WrapperD3D11Interface& GetWrapperD3D11Interface() const;
        const WrapperD3D12Interface& GetWrapperD3D12Interface() const;
        const WrapperVKInterface& GetWrapperVKInterface() const;
        const RayTracingInterface& GetRayTracingInterface() const;
        const MeshShaderInterface& GetMeshShaderInterface() const;
        const HelperInterface& GetHelperInterface() const;

        bool Create();
        void RegisterMemoryType(MemoryType memoryType, MemoryLocation memoryLocation);
        Result CreateSwapChain(const SwapChainDesc& swapChainDesc, SwapChain*& swapChain);
        void DestroySwapChain(SwapChain& swapChain);
        void SetDebugName(const char* name);
        const DeviceDesc& GetDesc() const;
        Result GetCommandQueue(CommandQueueType commandQueueType, CommandQueue*& commandQueue);
        Result CreateCommandAllocator(const CommandQueue& commandQueue, uint32_t physicalDeviceMask, CommandAllocator*& commandAllocator);
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
        Result AllocateMemory(uint32_t physicalDeviceMask, MemoryType memoryType, uint64_t size, Memory*& memory);
        Result BindBufferMemory(const BufferMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);
        Result BindTextureMemory(const TextureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);
        void FreeMemory(Memory& memory);
        Result CreateRayTracingPipeline(const RayTracingPipelineDesc& pipelineDesc, Pipeline*& pipeline);
        Result CreateAccelerationStructure(const AccelerationStructureDesc& accelerationStructureDesc, AccelerationStructure*& accelerationStructure);
        Result BindAccelerationStructureMemory(const AccelerationStructureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);
        void DestroyAccelerationStructure(AccelerationStructure& accelerationStructure);
        FormatSupportBits GetFormatSupport(Format format) const;

        VkDevice GetDeviceVK() const;
        VkPhysicalDevice GetPhysicalDeviceVK() const;
        VkInstance GetInstanceVK() const;
        Result CreateCommandQueueVK(const CommandQueueVulkanDesc& commandQueueDesc, CommandQueue*& commandQueue);
        Result CreateCommandAllocatorVK(const CommandAllocatorVulkanDesc& commandAllocatorDesc, CommandAllocator*& commandAllocator);
        Result CreateCommandBufferVK(const CommandBufferVulkanDesc& commandBufferDesc, CommandBuffer*& commandBuffer);
        Result CreateDescriptorPoolVK(VkDescriptorPool vkDescriptorPool, DescriptorPool*& descriptorPool);
        Result CreateBufferVK(const BufferVulkanDesc& bufferDesc, Buffer*& buffer);
        Result CreateTextureVK(const TextureVulkanDesc& textureVulkanDesc, Texture*& texture);
        Result CreateMemoryVK(const MemoryVulkanDesc& memoryVulkanDesc, Memory*& memory);
        Result CreateGraphicsPipelineVK(VkPipeline vkPipeline, Pipeline*& pipeline);
        Result CreateComputePipelineVK(VkPipeline vkPipeline, Pipeline*& pipeline);
        Result CreateQueryPoolVK(const QueryPoolVulkanDesc& queryPoolVulkanDesc, QueryPool*& queryPool);
        Result CreateQueueSemaphoreVK(VkSemaphore vkSemaphore, QueueSemaphore*& queueSemaphore);
        Result CreateDeviceSemaphoreVK(VkFence vkFence, DeviceSemaphore*& deviceSemaphore);

        Result CreateCommandBufferD3D11(const CommandBufferD3D11Desc& commandBufferDesc, CommandBuffer*& commandBuffer);
        Result CreateBufferD3D11(const BufferD3D11Desc& bufferDesc, Buffer*& buffer);
        Result CreateTextureD3D11(const TextureD3D11Desc& textureDesc, Texture*& texture);
        ID3D11Device* GetDeviceD3D11();

        Result CreateCommandBufferD3D12(const CommandBufferD3D12Desc& commandBufferDesc, CommandBuffer*& commandBuffer);
        Result CreateBufferD3D12(const BufferD3D12Desc& bufferDesc, Buffer*& buffer);
        Result CreateTextureD3D12(const TextureD3D12Desc& textureDesc, Texture*& texture);
        Result CreateMemoryD3D12(const MemoryD3D12Desc& memoryDesc, Memory*& memory);
        ID3D12Device* GetDeviceD3D12();

        uint32_t CalculateAllocationNumber(const ResourceGroupDesc& resourceGroupDesc) const;
        Result AllocateAndBindMemory(const ResourceGroupDesc& resourceGroupDesc, Memory** allocations);

        uint32_t GetPhysicalDeviceNum() const;
        bool IsPhysicalDeviceMaskValid(uint32_t physicalDeviceMask) const;

        //================================================================================================================
        // DeviceBase
        //================================================================================================================
        void Destroy();
        Result FillFunctionTable(CoreInterface& table) const;
        Result FillFunctionTable(SwapChainInterface& table) const;
        Result FillFunctionTable(WrapperD3D11Interface& table) const;
        Result FillFunctionTable(WrapperD3D12Interface& table) const;
        Result FillFunctionTable(WrapperVKInterface& table) const;
        Result FillFunctionTable(RayTracingInterface& table) const;
        Result FillFunctionTable(MeshShaderInterface& table) const;
        Result FillFunctionTable(HelperInterface& table) const;

    private:
        Device& m_Device;
        String m_Name;
        CoreInterface m_CoreAPI = {};
        SwapChainInterface m_SwapChainAPI = {};
        WrapperD3D11Interface m_WrapperD3D11API = {};
        WrapperD3D12Interface m_WrapperD3D12API = {};
        WrapperVKInterface m_WrapperVKAPI = {};
        RayTracingInterface m_RayTracingAPI = {};
        MeshShaderInterface m_MeshShaderAPI = {};
        HelperInterface m_HelperAPI = {};
        bool m_IsSwapChainSupported = false;
        bool m_IsWrapperD3D11Supported = false;
        bool m_IsWrapperD3D12Supported = false;
        bool m_IsWrapperVKSupported = false;
        bool m_IsRayTracingSupported = false;
        bool m_IsMeshShaderExtSupported = false;
        uint32_t m_PhysicalDeviceNum = 0;
        uint32_t m_PhysicalDeviceMask = 0;
        std::array<CommandQueueVal*, COMMAND_QUEUE_TYPE_NUM> m_CommandQueues = {};
        UnorderedMap<MemoryType, MemoryLocation> m_MemoryTypeMap;
        Lock m_Lock;
    };

    inline uint32_t DeviceVal::GetPhysicalDeviceNum() const
    {
        return m_PhysicalDeviceNum;
    }

    inline bool DeviceVal::IsPhysicalDeviceMaskValid(uint32_t physicalDeviceMask) const
    {
        return (physicalDeviceMask & m_PhysicalDeviceMask) == physicalDeviceMask;
    }

    inline const CoreInterface& DeviceVal::GetCoreInterface() const
    {
        return m_CoreAPI;
    }

    inline const SwapChainInterface& DeviceVal::GetSwapChainInterface() const
    {
        return m_SwapChainAPI;
    }

    inline const WrapperD3D11Interface& DeviceVal::GetWrapperD3D11Interface() const
    {
        return m_WrapperD3D11API;
    }

    inline const WrapperD3D12Interface& DeviceVal::GetWrapperD3D12Interface() const
    {
        return m_WrapperD3D12API;
    }

    inline const WrapperVKInterface& DeviceVal::GetWrapperVKInterface() const
    {
        return m_WrapperVKAPI;
    }

    inline const RayTracingInterface& DeviceVal::GetRayTracingInterface() const
    {
        return m_RayTracingAPI;
    }

    inline const MeshShaderInterface& DeviceVal::GetMeshShaderInterface() const
    {
        return m_MeshShaderAPI;
    }

    inline const HelperInterface& DeviceVal::GetHelperInterface() const
    {
        return m_HelperAPI;
    }
}
