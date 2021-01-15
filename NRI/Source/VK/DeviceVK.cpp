/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "DeviceVK.h"
#include "CommandQueueVK.h"
#include "CommandAllocatorVK.h"
#include "CommandBufferVK.h"
#include "TextureVK.h"
#include "BufferVK.h"
#include "DescriptorVK.h"
#include "FrameBufferVK.h"
#include "DeviceSemaphoreVK.h"
#include "QueueSemaphoreVK.h"
#include "SwapChainVK.h"
#include "QueryPoolVK.h"
#include "DescriptorPoolVK.h"
#include "DescriptorSetVK.h"
#include "PipelineLayoutVK.h"
#include "PipelineVK.h"
#include "AccelerationStructureVK.h"
#include "MemoryVK.h"

using namespace nri;

void* vkAllocateHostMemory(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
{
    StdAllocator<uint8_t>& stdAllocator = *(StdAllocator<uint8_t>*)pUserData;
    const auto& lowLevelAllocator = stdAllocator.GetInterface();

    return lowLevelAllocator.Allocate(lowLevelAllocator.userArg, size, alignment);
}

void* vkReallocateHostMemory(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
{
    StdAllocator<uint8_t>& stdAllocator = *(StdAllocator<uint8_t>*)pUserData;
    const auto& lowLevelAllocator = stdAllocator.GetInterface();

    return lowLevelAllocator.Reallocate(lowLevelAllocator.userArg, pOriginal, size, alignment);
}

void vkFreeHostMemory(void* pUserData, void* pMemory)
{
    StdAllocator<uint8_t>& stdAllocator = *(StdAllocator<uint8_t>*)pUserData;
    const auto& lowLevelAllocator = stdAllocator.GetInterface();

    return lowLevelAllocator.Free(lowLevelAllocator.userArg, pMemory);
}

void vkHostMemoryInternalAllocationNotification(void* pUserData, size_t size, VkInternalAllocationType allocationType,
    VkSystemAllocationScope allocationScope)
{
}

void vkHostMemoryInternalFreeNotification(void* pUserData, size_t size, VkInternalAllocationType allocationType,
    VkSystemAllocationScope allocationScope)
{
}

template< typename Implementation, typename Interface, typename ... Args >
Result DeviceVK::CreateImplementation(Interface*& entity, const Args&... args)
{
    Implementation* implementation = Allocate<Implementation>(GetStdAllocator(), *this);
    const Result result = implementation->Create(args...);

    if (result == Result::SUCCESS)
    {
        entity = (Interface*)implementation;
        return Result::SUCCESS;
    }

    Deallocate(GetStdAllocator(), implementation);
    return result;
}

DeviceVK::DeviceVK(const Log& log, const StdAllocator<uint8_t>& stdAllocator) :
    DeviceBase(log, stdAllocator),
    m_PhysicalDevices(GetStdAllocator()),
    m_PhysicalDeviceIndices(GetStdAllocator()),
    m_ConcurrentSharingModeQueueIndices(GetStdAllocator())
{
    if (FillFunctionTable(m_CoreInterface) != Result::SUCCESS)
        REPORT_ERROR(GetLog(), "Failed to get 'CoreInterface' interface in DeviceVK().");
}

DeviceVK::~DeviceVK()
{
    if (m_Device == VK_NULL_HANDLE)
        return;

    for (uint32_t i = 0; i < m_Queues.size(); i++)
        Deallocate(GetStdAllocator(), m_Queues[i]);

    if (m_Messenger != VK_NULL_HANDLE)
    {
        typedef PFN_vkDestroyDebugUtilsMessengerEXT Func;
        Func destroyCallback = (Func)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
        destroyCallback(m_Instance, m_Messenger, m_AllocationCallbackPtr);
    }

    if (m_OwnsNativeObjects)
    {
        vkDestroyDevice(m_Device, m_AllocationCallbackPtr);
        vkDestroyInstance(m_Instance, m_AllocationCallbackPtr);
    }
}

Result DeviceVK::Create(const DeviceCreationDesc& deviceCreationDesc)
{
    m_OwnsNativeObjects = true;

    m_AllocationCallbacks.pUserData = &GetStdAllocator();
    m_AllocationCallbacks.pfnAllocation = vkAllocateHostMemory;
    m_AllocationCallbacks.pfnReallocation = vkReallocateHostMemory;
    m_AllocationCallbacks.pfnFree = vkFreeHostMemory;
    m_AllocationCallbacks.pfnInternalAllocation = vkHostMemoryInternalAllocationNotification;
    m_AllocationCallbacks.pfnInternalFree = vkHostMemoryInternalFreeNotification;

    if (deviceCreationDesc.enableAPIValidation)
        m_AllocationCallbackPtr = &m_AllocationCallbacks;

    Result res = CreateInstance(deviceCreationDesc);
    if (res != Result::SUCCESS)
        return res;

    res = FindPhysicalDeviceGroup(deviceCreationDesc.physicalDeviceGroup, deviceCreationDesc.enableMGPU);
    if (res != Result::SUCCESS)
        return res;

    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevices.front(), &m_MemoryProps);
    FillFamilyIndices(false, nullptr, 0);

    res = CreateLogicalDevice(deviceCreationDesc);
    if (res != Result::SUCCESS)
        return res;

    RetrieveRayTracingInfo();
    RetrieveMeshShaderInfo();
    CreateCommandQueues();

    res = ResolveDispatchTable();
    if (res != Result::SUCCESS)
        return res;

    SetDeviceLimits(deviceCreationDesc.enableAPIValidation);

    const uint32_t groupSize = m_DeviceDesc.phyiscalDeviceGroupSize;
    m_PhysicalDeviceIndices.resize(groupSize * groupSize);
    const auto begin = m_PhysicalDeviceIndices.begin();
    for (uint32_t i = 0; i < groupSize; i++)
        std::fill(begin + i * groupSize, begin + (i + 1) * groupSize, i);

    if (deviceCreationDesc.enableAPIValidation)
        ReportDeviceGroupInfo();

    m_SPIRVBindingOffsets = deviceCreationDesc.spirvBindingOffsets;

    return res;
}

Result DeviceVK::Create(const DeviceCreationVulkanDesc& deviceCreationVulkanDesc)
{
    m_OwnsNativeObjects = false;

    m_Instance = (VkInstance)deviceCreationVulkanDesc.vkInstance;

    const VkPhysicalDevice* physicalDevices = (VkPhysicalDevice*)deviceCreationVulkanDesc.vkPhysicalDevices;
    m_PhysicalDevices.insert(m_PhysicalDevices.begin(), physicalDevices, physicalDevices + deviceCreationVulkanDesc.deviceGroupSize);

    m_Device = (VkDevice)deviceCreationVulkanDesc.vkDevice;

    m_AllocationCallbacks.pfnAllocation = vkAllocateHostMemory;
    m_AllocationCallbacks.pfnReallocation = vkReallocateHostMemory;
    m_AllocationCallbacks.pfnFree = vkFreeHostMemory;
    m_AllocationCallbacks.pfnInternalAllocation = vkHostMemoryInternalAllocationNotification;
    m_AllocationCallbacks.pfnInternalFree = vkHostMemoryInternalFreeNotification;

    if (deviceCreationVulkanDesc.enableAPIValidation)
        m_AllocationCallbackPtr = &m_AllocationCallbacks;

    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevices.front(), &m_MemoryProps);

    FillFamilyIndices(true, deviceCreationVulkanDesc.queueFamilyIndices, deviceCreationVulkanDesc.queueFamilyIndexNum);
    CreateCommandQueues();

    Result res = ResolveDispatchTable();
    if (res != Result::SUCCESS)
        return res;

    SetDeviceLimits(deviceCreationVulkanDesc.enableAPIValidation);

    if (deviceCreationVulkanDesc.enableAPIValidation)
        ReportDeviceGroupInfo();

    return res;
}

bool DeviceVK::GetMemoryType(MemoryLocation memoryLocation, uint32_t memoryTypeMask, MemoryTypeInfo& memoryTypeInfo) const
{
    const auto host = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    const auto device = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const VkMemoryPropertyFlags flags = IsHostVisibleMemory(memoryLocation) ? host : device;

    memoryTypeInfo.isHostCoherent = IsHostVisibleMemory(memoryLocation);

    memoryTypeInfo.location = (uint8_t)memoryLocation;
    static_assert((uint32_t)MemoryLocation::MAX_NUM <= std::numeric_limits<uint8_t>::max(), "Unexpected number of memory locations");

    for (uint_fast16_t i = 0; i < m_MemoryProps.memoryTypeCount; i++)
    {
        const bool isMemoryTypeSupported = memoryTypeMask & (1 << i);
        const bool isPropSupported = (m_MemoryProps.memoryTypes[i].propertyFlags & flags) == flags;

        if (isMemoryTypeSupported && isPropSupported)
        {
            memoryTypeInfo.memoryTypeIndex = (uint16_t)i;
            return true;
        }
    }

    return false;
}

bool DeviceVK::GetMemoryType(uint32_t index, MemoryTypeInfo& memoryTypeInfo) const
{
    const auto host = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    const auto device = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (index >= m_MemoryProps.memoryTypeCount)
        return false;

    const VkMemoryType& memoryType = m_MemoryProps.memoryTypes[index];

    memoryTypeInfo.memoryTypeIndex = (uint16_t)index;
    memoryTypeInfo.isHostCoherent = memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    const bool isHostVisible = memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memoryTypeInfo.location = isHostVisible ? (uint8_t)MemoryLocation::HOST_UPLOAD : (uint8_t)MemoryLocation::DEVICE;
    static_assert((uint32_t)MemoryLocation::MAX_NUM <= std::numeric_limits<uint8_t>::max(), "Unexpected number of memory locations");

    return true;
}

void DeviceVK::SetDebugName(const char* name)
{
    SetDebugNameToTrivialObject(VK_OBJECT_TYPE_DEVICE, m_Device, name);
}

const DeviceDesc& DeviceVK::GetDesc() const
{
    return m_DeviceDesc;
}

Result DeviceVK::GetCommandQueue(CommandQueueType commandQueueType, CommandQueue*& commandQueue)
{
    SharedScope sharedScope(m_Lock);

    if (m_FamilyIndices[(uint32_t)commandQueueType] == std::numeric_limits<uint32_t>::max())
        return Result::UNSUPPORTED;

    commandQueue = (CommandQueue*)m_Queues[(uint32_t)commandQueueType];
    return Result::SUCCESS;
}

Result DeviceVK::CreateCommandAllocator(const CommandQueue& commandQueue, uint32_t physicalDeviceMask, CommandAllocator*& commandAllocator)
{
    return CreateImplementation<CommandAllocatorVK>(commandAllocator, commandQueue, physicalDeviceMask);
}

Result DeviceVK::CreateDescriptorPool(const DescriptorPoolDesc& descriptorPoolDesc, DescriptorPool*& descriptorPool)
{
    return CreateImplementation<DescriptorPoolVK>(descriptorPool, descriptorPoolDesc);
}

Result DeviceVK::CreateBuffer(const BufferDesc& bufferDesc, Buffer*& buffer)
{
    return CreateImplementation<BufferVK>(buffer, bufferDesc);
}

Result DeviceVK::CreateTexture(const TextureDesc& textureDesc, Texture*& texture)
{
    return CreateImplementation<TextureVK>(texture, textureDesc);
}

Result DeviceVK::CreateBufferView(const BufferViewDesc& bufferViewDesc, Descriptor*& bufferView)
{
    return CreateImplementation<DescriptorVK>(bufferView, bufferViewDesc);
}

Result DeviceVK::CreateTexture1DView(const Texture1DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    return CreateImplementation<DescriptorVK>(textureView, textureViewDesc);
}

Result DeviceVK::CreateTexture2DView(const Texture2DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    return CreateImplementation<DescriptorVK>(textureView, textureViewDesc);
}

Result DeviceVK::CreateTexture3DView(const Texture3DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    return CreateImplementation<DescriptorVK>(textureView, textureViewDesc);
}

Result DeviceVK::CreateSampler(const SamplerDesc& samplerDesc, Descriptor*& sampler)
{
    return CreateImplementation<DescriptorVK>(sampler, samplerDesc);
}

Result DeviceVK::CreatePipelineLayout(const PipelineLayoutDesc& pipelineLayoutDesc, PipelineLayout*& pipelineLayout)
{
    return CreateImplementation<PipelineLayoutVK>(pipelineLayout, pipelineLayoutDesc);
}

Result DeviceVK::CreatePipeline(const GraphicsPipelineDesc& graphicsPipelineDesc, Pipeline*& pipeline)
{
    return CreateImplementation<PipelineVK>(pipeline, graphicsPipelineDesc);
}

Result DeviceVK::CreatePipeline(const ComputePipelineDesc& computePipelineDesc, Pipeline*& pipeline)
{
    return CreateImplementation<PipelineVK>(pipeline, computePipelineDesc);
}

Result DeviceVK::CreateFrameBuffer(const FrameBufferDesc& frameBufferDesc, FrameBuffer*& frameBuffer)
{
    return CreateImplementation<FrameBufferVK>(frameBuffer, frameBufferDesc);
}

Result DeviceVK::CreateQueryPool(const QueryPoolDesc& queryPoolDesc, QueryPool*& queryPool)
{
    return CreateImplementation<QueryPoolVK>(queryPool, queryPoolDesc);
}

Result DeviceVK::CreateQueueSemaphore(QueueSemaphore*& queueSemaphore)
{
    return CreateImplementation<QueueSemaphoreVK>(queueSemaphore);
}

Result DeviceVK::CreateDeviceSemaphore(bool signaled, DeviceSemaphore*& deviceSemaphore)
{
    return CreateImplementation<DeviceSemaphoreVK>(deviceSemaphore, signaled);
}

Result DeviceVK::CreateSwapChain(const SwapChainDesc& swapChainDesc, SwapChain*& swapChain)
{
    return CreateImplementation<SwapChainVK>(swapChain, swapChainDesc);
}

Result DeviceVK::CreatePipeline(const RayTracingPipelineDesc& rayTracingPipelineDesc, Pipeline*& pipeline)
{
    return CreateImplementation<PipelineVK>(pipeline, rayTracingPipelineDesc);
}

Result DeviceVK::CreateAccelerationStructure(const AccelerationStructureDesc& accelerationStructureDesc, AccelerationStructure*& accelerationStructure)
{
    return CreateImplementation<AccelerationStructureVK>(accelerationStructure, accelerationStructureDesc);
}

Result DeviceVK::CreateCommandQueue(const CommandQueueVulkanDesc& commandQueueVulkanDesc, CommandQueue*& commandQueue)
{
    const uint32_t commandQueueTypeIndex = (uint32_t)commandQueueVulkanDesc.commandQueueType;

    ExclusiveScope exclusiveScope(m_Lock);

    const bool isFamilyIndexSame = m_FamilyIndices[commandQueueTypeIndex] == commandQueueVulkanDesc.familyIndex;
    const bool isQueueSame = (VkQueue)m_Queues[commandQueueTypeIndex] == (VkQueue)commandQueueVulkanDesc.vkQueue;
    if (isFamilyIndexSame && isQueueSame)
    {
        commandQueue = (CommandQueue*)m_Queues[commandQueueTypeIndex];
        return Result::SUCCESS;
    }

    CreateImplementation<CommandQueueVK>(commandQueue, commandQueueVulkanDesc);

    if (m_Queues[commandQueueTypeIndex] != nullptr)
        Deallocate(GetStdAllocator(), m_Queues[commandQueueTypeIndex]);

    m_FamilyIndices[commandQueueTypeIndex] = commandQueueVulkanDesc.familyIndex;
    m_Queues[commandQueueTypeIndex] = (CommandQueueVK*)commandQueue;

    return Result::SUCCESS;
}

Result DeviceVK::CreateCommandAllocator(const CommandAllocatorVulkanDesc& commandAllocatorVulkanDesc, CommandAllocator*& commandAllocator)
{
    return CreateImplementation<CommandAllocatorVK>(commandAllocator, commandAllocatorVulkanDesc);
}

Result DeviceVK::CreateCommandBuffer(const CommandBufferVulkanDesc& commandBufferVulkanDesc, CommandBuffer*& commandBuffer)
{
    return CreateImplementation<CommandBufferVK>(commandBuffer, commandBufferVulkanDesc);
}

Result DeviceVK::CreateDescriptorPool(VkDescriptorPool vkDescriptorPool, DescriptorPool*& descriptorPool)
{
    return CreateImplementation<DescriptorPoolVK>(descriptorPool, vkDescriptorPool);
}

Result DeviceVK::CreateBuffer(const BufferVulkanDesc& bufferDesc, Buffer*& buffer)
{
    return CreateImplementation<BufferVK>(buffer, bufferDesc);
}

Result DeviceVK::CreateTexture(const TextureVulkanDesc& textureVulkanDesc, Texture*& texture)
{
    return CreateImplementation<TextureVK>(texture, textureVulkanDesc);
}

Result DeviceVK::CreateMemory(const MemoryVulkanDesc& memoryVulkanDesc, Memory*& memory)
{
    return CreateImplementation<MemoryVK>(memory, memoryVulkanDesc);
}

Result DeviceVK::CreateGraphicsPipeline(VkPipeline vkPipeline, Pipeline*& pipeline)
{
    PipelineVK* implementation = Allocate<PipelineVK>(GetStdAllocator(), *this);
    const Result result = implementation->CreateGraphics(vkPipeline);

    if (result != Result::SUCCESS)
    {
        pipeline = (Pipeline*)implementation;
        return result;
    }

    Deallocate(GetStdAllocator(), implementation);

    return result;
}

Result DeviceVK::CreateComputePipeline(VkPipeline vkPipeline, Pipeline*& pipeline)
{
    PipelineVK* implementation = Allocate<PipelineVK>(GetStdAllocator(), *this);
    const Result result = implementation->CreateCompute(vkPipeline);

    if (result != Result::SUCCESS)
    {
        pipeline = (Pipeline*)implementation;
        return result;
    }

    Deallocate(GetStdAllocator(), implementation);

    return result;
}

Result DeviceVK::CreateQueryPool(const QueryPoolVulkanDesc& queryPoolVulkanDesc, QueryPool*& queryPool)
{
    return CreateImplementation<QueryPoolVK>(queryPool, queryPoolVulkanDesc);
}

Result DeviceVK::CreateQueueSemaphore(VkSemaphore vkSemaphore, QueueSemaphore*& queueSemaphore)
{
    return CreateImplementation<QueueSemaphoreVK>(queueSemaphore, vkSemaphore);
}

Result DeviceVK::CreateDeviceSemaphore(VkFence vkFence, DeviceSemaphore*& deviceSemaphore)
{
    return CreateImplementation<DeviceSemaphoreVK>(deviceSemaphore, vkFence);
}

void DeviceVK::DestroyCommandAllocator(CommandAllocator& commandAllocator)
{
    Deallocate(GetStdAllocator(), (CommandAllocatorVK*)&commandAllocator);
}

void DeviceVK::DestroyDescriptorPool(DescriptorPool& descriptorPool)
{
    Deallocate(GetStdAllocator(), (DescriptorPoolVK*)&descriptorPool);
}

void DeviceVK::DestroyBuffer(Buffer& buffer)
{
    Deallocate(GetStdAllocator(), (BufferVK*)&buffer);
}

void DeviceVK::DestroyTexture(Texture& texture)
{
    Deallocate(GetStdAllocator(), (TextureVK*)&texture);
}

void DeviceVK::DestroyDescriptor(Descriptor& descriptor)
{
    Deallocate(GetStdAllocator(), (DescriptorVK*)&descriptor);
}

void DeviceVK::DestroyPipelineLayout(PipelineLayout& pipelineLayout)
{
    Deallocate(GetStdAllocator(), (PipelineLayoutVK*)&pipelineLayout);
}

void DeviceVK::DestroyPipeline(Pipeline& pipeline)
{
    Deallocate(GetStdAllocator(), (PipelineVK*)&pipeline);
}

void DeviceVK::DestroyFrameBuffer(FrameBuffer& frameBuffer)
{
    Deallocate(GetStdAllocator(), (FrameBufferVK*)&frameBuffer);
}

void DeviceVK::DestroyQueryPool(QueryPool& queryPool)
{
    Deallocate(GetStdAllocator(), (QueryPoolVK*)&queryPool);
}

void DeviceVK::DestroyQueueSemaphore(QueueSemaphore& queueSemaphore)
{
    Deallocate(GetStdAllocator(), (QueueSemaphoreVK*)&queueSemaphore);
}

void DeviceVK::DestroyDeviceSemaphore(DeviceSemaphore& deviceSemaphore)
{
    Deallocate(GetStdAllocator(), (DeviceSemaphoreVK*)&deviceSemaphore);
}

void DeviceVK::DestroySwapChain(SwapChain& swapChain)
{
    Deallocate(GetStdAllocator(), (SwapChainVK*)&swapChain);
}

void DeviceVK::DestroyAccelerationStructure(AccelerationStructure& accelerationStructure)
{
    Deallocate(GetStdAllocator(), (AccelerationStructureVK*)&accelerationStructure);
}

Result DeviceVK::AllocateMemory(uint32_t physicalDeviceMask, MemoryType memoryType, uint64_t size, Memory*& memory)
{
    return CreateImplementation<MemoryVK>(memory, physicalDeviceMask, memoryType, size);
}

Result DeviceVK::BindBufferMemory(const BufferMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    if (memoryBindingDescNum == 0)
        return Result::SUCCESS;

    const uint32_t infoMaxNum = memoryBindingDescNum * m_DeviceDesc.phyiscalDeviceGroupSize;

    VkBindBufferMemoryInfo* infos = STACK_ALLOC(VkBindBufferMemoryInfo, infoMaxNum);
    uint32_t infoNum = 0;

    VkBindBufferMemoryDeviceGroupInfo* deviceGroupInfos = nullptr;
    if (m_DeviceDesc.phyiscalDeviceGroupSize > 1)
        deviceGroupInfos = STACK_ALLOC(VkBindBufferMemoryDeviceGroupInfo, infoMaxNum);

    for (uint32_t i = 0; i < memoryBindingDescNum; i++)
    {
        const BufferMemoryBindingDesc& bindingDesc = memoryBindingDescs[i];

        MemoryVK& memoryImpl = *(MemoryVK*)bindingDesc.memory;
        BufferVK& bufferImpl = *(BufferVK*)bindingDesc.buffer;

        const MemoryTypeUnpack unpack = { memoryImpl.GetType() };
        const MemoryTypeInfo& memoryTypeInfo = unpack.info;

        const MemoryLocation memoryLocation = (MemoryLocation)memoryTypeInfo.location;

        uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(bindingDesc.physicalDeviceMask);
        if (IsHostVisibleMemory(memoryLocation))
            physicalDeviceMask = 0x1;

        if (memoryTypeInfo.isDedicated == 1)
            memoryImpl.CreateDedicated(bufferImpl, physicalDeviceMask);

        for (uint32_t j = 0; j < m_DeviceDesc.phyiscalDeviceGroupSize; j++)
        {
            if ((1u << j) & physicalDeviceMask)
            {
                VkBindBufferMemoryInfo& info = infos[infoNum++];

                info = {};
                info.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
                info.buffer = bufferImpl.GetHandle(j);
                info.memory = memoryImpl.GetHandle(j);
                info.memoryOffset = bindingDesc.offset;

                if (IsHostVisibleMemory(memoryLocation))
                    bufferImpl.SetHostMemory(memoryImpl, info.memoryOffset);

                if (deviceGroupInfos != nullptr)
                {
                    VkBindBufferMemoryDeviceGroupInfo& deviceGroupInfo = deviceGroupInfos[infoNum - 1];
                    deviceGroupInfo = {};
                    deviceGroupInfo.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_DEVICE_GROUP_INFO;
                    deviceGroupInfo.deviceIndexCount = m_DeviceDesc.phyiscalDeviceGroupSize;
                    deviceGroupInfo.pDeviceIndices = &m_PhysicalDeviceIndices[j * m_DeviceDesc.phyiscalDeviceGroupSize];
                    info.pNext = &deviceGroupInfo;
                }
            }
        }
    }

    VkResult result = VK_SUCCESS;
    if (infoNum > 0)
        result = m_VK.BindBufferMemory2(m_Device, infoNum, infos);

    RETURN_ON_FAILURE(GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't bind a memory to a buffer: vkBindBufferMemory2 returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

Result DeviceVK::BindTextureMemory(const TextureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    const uint32_t infoMaxNum = memoryBindingDescNum * m_DeviceDesc.phyiscalDeviceGroupSize;

    VkBindImageMemoryInfo* infos = STACK_ALLOC(VkBindImageMemoryInfo, infoMaxNum);
    uint32_t infoNum = 0;

    VkBindImageMemoryDeviceGroupInfo* deviceGroupInfos = nullptr;
    if (m_DeviceDesc.phyiscalDeviceGroupSize > 1)
        deviceGroupInfos = STACK_ALLOC(VkBindImageMemoryDeviceGroupInfo, infoMaxNum);

    for (uint32_t i = 0; i < memoryBindingDescNum; i++)
    {
        const TextureMemoryBindingDesc& bindingDesc = memoryBindingDescs[i];

        const uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(bindingDesc.physicalDeviceMask);

        MemoryVK& memoryImpl = *(MemoryVK*)bindingDesc.memory;
        TextureVK& textureImpl = *(TextureVK*)bindingDesc.texture;

        const MemoryTypeUnpack unpack = { memoryImpl.GetType() };
        const MemoryTypeInfo& memoryTypeInfo = unpack.info;

        if (memoryTypeInfo.isDedicated == 1)
            memoryImpl.CreateDedicated(textureImpl, physicalDeviceMask);

        for (uint32_t j = 0; j < m_DeviceDesc.phyiscalDeviceGroupSize; j++)
        {
            if ((1u << j) & physicalDeviceMask)
            {
                VkBindImageMemoryInfo& info = infos[infoNum++];
                info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
                info.pNext = nullptr;
                info.image = textureImpl.GetHandle(j);
                info.memory = memoryImpl.GetHandle(j);
                info.memoryOffset = bindingDesc.offset;

                if (deviceGroupInfos != nullptr)
                {
                    VkBindImageMemoryDeviceGroupInfo& deviceGroupInfo = deviceGroupInfos[infoNum - 1];
                    deviceGroupInfo = {};
                    deviceGroupInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO;
                    deviceGroupInfo.deviceIndexCount = m_DeviceDesc.phyiscalDeviceGroupSize;
                    deviceGroupInfo.pDeviceIndices = &m_PhysicalDeviceIndices[j * m_DeviceDesc.phyiscalDeviceGroupSize];
                    info.pNext = &deviceGroupInfo;
                }
            }
        }
    }

    VkResult result = VK_SUCCESS;
    if (infoNum > 0)
        result = m_VK.BindImageMemory2(m_Device, infoNum, infos);

    RETURN_ON_FAILURE(GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't bind a memory to a texture: vkBindImageMemory2 returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

Result DeviceVK::BindAccelerationStructureMemory(const AccelerationStructureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    if (memoryBindingDescNum == 0)
        return Result::SUCCESS;

    auto* infos = STACK_ALLOC(VkBindAccelerationStructureMemoryInfoNV, memoryBindingDescNum * m_DeviceDesc.phyiscalDeviceGroupSize);
    uint32_t infoNum = 0;

    for (uint32_t i = 0; i < memoryBindingDescNum; i++)
    {
        const AccelerationStructureMemoryBindingDesc& bindingDesc = memoryBindingDescs[i];

        const uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(bindingDesc.physicalDeviceMask);

        MemoryVK& memoryImpl = *(MemoryVK*)bindingDesc.memory;
        AccelerationStructureVK& accelerationStructureImpl = *(AccelerationStructureVK*)bindingDesc.accelerationStructure;

        for (uint32_t j = 0; j < m_DeviceDesc.phyiscalDeviceGroupSize; j++)
        {
            if ((1u << j) & physicalDeviceMask)
            {
                VkBindAccelerationStructureMemoryInfoNV& info = infos[infoNum++];
                info.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
                info.pNext = nullptr;
                info.accelerationStructure = accelerationStructureImpl.GetHandle(j);
                info.memory = memoryImpl.GetHandle(j);
                info.memoryOffset = bindingDesc.offset;
                info.deviceIndexCount = m_DeviceDesc.phyiscalDeviceGroupSize;
                info.pDeviceIndices = &m_PhysicalDeviceIndices[j * m_DeviceDesc.phyiscalDeviceGroupSize];
            }
        }
    }

    const VkResult res = m_VK.BindAccelerationStructureMemoryNV(m_Device, infoNum, infos);
    RETURN_ON_FAILURE(GetLog(), res == VK_SUCCESS, GetReturnCode(res),
        "Can't bind a memory to an acceleratrion structure: BindAccelerationStructureMemoryNVX returned %d.", (int32_t)res);

    for (uint32_t i = 0; i < memoryBindingDescNum; i++)
    {
        auto& accelerationStructure = *(AccelerationStructureVK*)memoryBindingDescs[i].accelerationStructure;

        const Result result = accelerationStructure.RetrieveNativeHandle();
        if (result != Result::SUCCESS)
            return result;
    }

    return Result::SUCCESS;
}

void DeviceVK::FreeMemory(Memory& memory)
{
    Deallocate(GetStdAllocator(), (MemoryVK*)&memory);
}

FormatSupportBits DeviceVK::GetFormatSupport(Format format) const
{
    const VkFormat vulkanFormat = GetVkFormat(format);
    const VkPhysicalDevice physicalDevice = m_PhysicalDevices.front();

    VkFormatProperties formatProperties = {};
    m_VK.GetPhysicalDeviceFormatProperties(physicalDevice, vulkanFormat, &formatProperties);

    constexpr uint32_t transferBits = VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;

    constexpr uint32_t textureBits = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | transferBits;
    constexpr uint32_t storageTextureBits = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | transferBits;
    constexpr uint32_t bufferBits = VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT | transferBits;
    constexpr uint32_t storageBufferBits = VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT | transferBits;
    constexpr uint32_t colorAttachmentBits = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | transferBits;
    constexpr uint32_t depthAttachmentBits = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | transferBits;
    constexpr uint32_t vertexBufferBits = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT | transferBits;

    FormatSupportBits mask = FormatSupportBits::UNSUPPORTED;

    if (formatProperties.optimalTilingFeatures & textureBits)
        mask |= FormatSupportBits::TEXTURE;

    if (formatProperties.optimalTilingFeatures & storageTextureBits)
        mask |= FormatSupportBits::STORAGE_TEXTURE;

    if (formatProperties.optimalTilingFeatures & colorAttachmentBits)
        mask |= FormatSupportBits::COLOR_ATTACHMENT;

    if (formatProperties.optimalTilingFeatures & depthAttachmentBits)
        mask |= FormatSupportBits::DEPTH_STENCIL_ATTACHMENT;

    if (formatProperties.bufferFeatures & bufferBits)
        mask |= FormatSupportBits::BUFFER;

    if (formatProperties.bufferFeatures & storageBufferBits)
        mask |= FormatSupportBits::STORAGE_BUFFER;

    if (formatProperties.bufferFeatures & vertexBufferBits)
        mask |= FormatSupportBits::VERTEX_BUFFER;

    return mask;
}

uint32_t DeviceVK::CalculateAllocationNumber(const ResourceGroupDesc& resourceGroupDesc) const
{
    HelperDeviceMemoryAllocator allocator(m_CoreInterface, (Device&)*this, m_StdAllocator);

    return allocator.CalculateAllocationNumber(resourceGroupDesc);
}

Result DeviceVK::AllocateAndBindMemory(const ResourceGroupDesc& resourceGroupDesc, nri::Memory** allocations)
{
    HelperDeviceMemoryAllocator allocator(m_CoreInterface, (Device&)*this, m_StdAllocator);

    return allocator.AllocateAndBindMemory(resourceGroupDesc, allocations);
}

const char* GetObjectTypeName(VkObjectType objectType)
{
    switch(objectType)
    {
    case VK_OBJECT_TYPE_INSTANCE:
        return "VkInstance";
    case VK_OBJECT_TYPE_PHYSICAL_DEVICE:
        return "VkPhysicalDevice";
    case VK_OBJECT_TYPE_DEVICE:
        return "VkDevice";
    case VK_OBJECT_TYPE_QUEUE:
        return "VkQueue";
    case VK_OBJECT_TYPE_SEMAPHORE:
        return "VkSemaphore";
    case VK_OBJECT_TYPE_COMMAND_BUFFER:
        return "VkCommandBuffer";
    case VK_OBJECT_TYPE_FENCE:
        return "VkFence";
    case VK_OBJECT_TYPE_DEVICE_MEMORY:
        return "VkDeviceMemory";
    case VK_OBJECT_TYPE_BUFFER:
        return "VkBuffer";
    case VK_OBJECT_TYPE_IMAGE:
        return "VkImage";
    case VK_OBJECT_TYPE_EVENT:
        return "VkEvent";
    case VK_OBJECT_TYPE_QUERY_POOL:
        return "VkQueryPool";
    case VK_OBJECT_TYPE_BUFFER_VIEW:
        return "VkBufferView";
    case VK_OBJECT_TYPE_IMAGE_VIEW:
        return "VkImageView";
    case VK_OBJECT_TYPE_SHADER_MODULE:
        return "VkShaderModule";
    case VK_OBJECT_TYPE_PIPELINE_CACHE:
        return "VkPipelineCache";
    case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
        return "VkPipelineLayout";
    case VK_OBJECT_TYPE_RENDER_PASS:
        return "VkRenderPass";
    case VK_OBJECT_TYPE_PIPELINE:
        return "VkPipeline";
    case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
        return "VkDescriptorSetLayout";
    case VK_OBJECT_TYPE_SAMPLER:
        return "VkSampler";
    case VK_OBJECT_TYPE_DESCRIPTOR_POOL:
        return "VkDescriptorPool";
    case VK_OBJECT_TYPE_DESCRIPTOR_SET:
        return "VkDescriptorSet";
    case VK_OBJECT_TYPE_FRAMEBUFFER:
        return "VkFramebuffer";
    case VK_OBJECT_TYPE_COMMAND_POOL:
        return "VkCommandPool";
    case VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION:
        return "VkSamplerYcbcrConversion";
    case VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE:
        return "VkDescriptorUpdateTemplate";
    case VK_OBJECT_TYPE_SURFACE_KHR:
        return "VkSurfaceKHR";
    case VK_OBJECT_TYPE_SWAPCHAIN_KHR:
        return "VkSwapchainKHR";
    case VK_OBJECT_TYPE_DISPLAY_KHR:
        return "VkDisplayKHR";
    case VK_OBJECT_TYPE_DISPLAY_MODE_KHR:
        return "VkDisplayModeKHR";
    case VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT:
        return "VkDebugReportCallbackEXT";
    case VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT:
        return "VkDebugUtilsMessengerEXT";
    case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR:
        return "VkAccelerationStructureKHR";
    case VK_OBJECT_TYPE_VALIDATION_CACHE_EXT:
        return "VkValidationCacheEXT";
    case VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL:
        return "VkPerformanceConfigurationINTEL";
    case VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR:
        return "VkDeferredOperationKHR";
    case VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV:
        return "VkIndirectCommandsLayoutNV";
    }
    return "unknown";
}

VkBool32 VKAPI_PTR DebugUtilsMessenger(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData)
{
    bool isError = false;
    bool isWarning = false;

    const char* type = "unknown";
    switch( messageSeverity )
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        type = "verbose";
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        type = "info";
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        type = "warning";
        isWarning = true;
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        type = "error";
        isError = true;
        break;
    }

    if (!isWarning && !isError)
        return VK_FALSE;

    DeviceVK& device = *(DeviceVK*)userData;

    String message(device.GetStdAllocator());
    message += std::to_string(callbackData->messageIdNumber);
    message += " ";
    message += callbackData->pMessageIdName;
    message += " ";
    message += callbackData->pMessage;

    if (strstr(callbackData->pMessage, "vkCmdCopyBufferToImage(): For optimal performance VkImage") != nullptr)
        return VK_FALSE;
    if (strstr(callbackData->pMessage, "vkCmdCopyImage(): For optimal performance VkImage") != nullptr)
        return VK_FALSE;

    if (strstr(callbackData->pMessage, "VkPhysicalDeviceRayTracingFeaturesKHR::rayTracing but is not enabled on the device") != nullptr)
        return VK_FALSE;

    if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        message += "\nObjectNum: " + std::to_string(callbackData->objectCount);

        char buffer[64];

        for (uint32_t i = 0; i < callbackData->objectCount; i++)
        {
            const VkDebugUtilsObjectNameInfoEXT& object = callbackData->pObjects[i];
            snprintf(buffer, sizeof(buffer), "0x%llx", (unsigned long long)object.objectHandle);

            message += "\n\tObject ";
            message += object.pObjectName != nullptr ? object.pObjectName : "";
            message += " ";
            message += GetObjectTypeName(object.objectType);
            message += " (";
            message += buffer;
            message += ")";
        }

        REPORT_ERROR(device.GetLog(), "DebugUtilsMessenger: %s, %s", type, message.c_str());
    }
    else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        REPORT_WARNING(device.GetLog(), "DebugUtilsMessenger: %s, %s", type, message.c_str());
    }
    else
    {
        REPORT_INFO(device.GetLog(), "DebugUtilsMessenger: %s, %s", type, message.c_str());
    }

    return VK_FALSE;
}

void DeviceVK::FilterInstanceLayers(Vector<const char*>& layers)
{
    uint32_t layerNum = 0;
    vkEnumerateInstanceLayerProperties(&layerNum, nullptr);

    Vector<VkLayerProperties> supportedLayers(layerNum, GetStdAllocator());
    vkEnumerateInstanceLayerProperties(&layerNum, supportedLayers.data());

    for (size_t i = 0; i < layers.size(); i++)
    {
        bool found = false;
        for (uint32_t j = 0; j < layerNum && !found; j++)
        {
            if (strcmp(supportedLayers[j].layerName, layers[i]) == 0)
                found = true;
        }

        if (!found)
            layers.erase(layers.begin() + i--);
    }
}

bool DeviceVK::FilterInstanceExtensions(Vector<const char*>& extensions)
{
    uint32_t extensionNum = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionNum, nullptr);

    Vector<VkExtensionProperties> supportedExtensions(extensionNum, GetStdAllocator());
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionNum, supportedExtensions.data());

    bool allFound = true;
    for (size_t i = 0; i < extensions.size(); i++)
    {
        bool found = false;
        for (uint32_t j = 0; j < extensionNum && !found; j++)
        {
            if (strcmp(supportedExtensions[j].extensionName, extensions[i]) == 0)
                found = true;
        }

        if (!found)
        {
            extensions.erase(extensions.begin() + i--);
            allFound = false;
        }
    }

    return allFound;
}

Result DeviceVK::CreateInstance(const DeviceCreationDesc& deviceCreationDesc)
{
    Vector<const char*> layers(GetStdAllocator());
    Vector<const char*> extensions(GetStdAllocator());

    #ifdef VK_USE_PLATFORM_WIN32_KHR
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    #endif
    #ifdef VK_USE_PLATFORM_XLIB_KHR
        extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
    #endif

    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

    for (uint32_t i = 0; i < deviceCreationDesc.vulkanExtensions.instanceExtensionNum; i++)
        extensions.push_back(deviceCreationDesc.vulkanExtensions.instanceExtensions[i]);

    if (!FilterInstanceExtensions(extensions))
    {
        REPORT_ERROR(GetLog(), "Can't create VkInstance: the required extensions are not supported.");
        return Result::UNSUPPORTED;
    }

    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    if (deviceCreationDesc.enableAPIValidation)
        layers.push_back("VK_LAYER_KHRONOS_validation");

    FilterInstanceLayers(layers);
    FilterInstanceExtensions(extensions);

    const VkApplicationInfo appInfo = {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        nullptr,
        0,
        nullptr,
        0,
        VK_API_VERSION_1_2
    };

    const VkInstanceCreateInfo info = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        (VkInstanceCreateFlags)0,
        &appInfo,
        (uint32_t)layers.size(),
        layers.data(),
        (uint32_t)extensions.size(),
        extensions.data(),
    };

    VkResult result = vkCreateInstance(&info, m_AllocationCallbackPtr, &m_Instance);

    RETURN_ON_FAILURE(GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't create a VkInstance: vkCreateInstance returned %d.", (int32_t)result);

    if (deviceCreationDesc.enableAPIValidation)
    {
        typedef PFN_vkCreateDebugUtilsMessengerEXT Func;
        Func vkCreateDebugUtilsMessengerEXT = nullptr;
        vkCreateDebugUtilsMessengerEXT = (Func)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");

        VkDebugUtilsMessengerCreateInfoEXT createInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };

        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        createInfo.messageType |= VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

        createInfo.pUserData = this;
        createInfo.pfnUserCallback = DebugUtilsMessenger;

        result = vkCreateDebugUtilsMessengerEXT(m_Instance, &createInfo, m_AllocationCallbackPtr, &m_Messenger);

        RETURN_ON_FAILURE(GetLog(), result == VK_SUCCESS, GetReturnCode(result),
            "Can't create a debug utils messenger callback: vkCreateDebugUtilsMessengerEXT returned %d.", (int32_t)result);
    }

    return Result::SUCCESS;
}

Result DeviceVK::FindPhysicalDeviceGroup(const PhysicalDeviceGroup* physicalDeviceGroup, bool enableMGPU)
{
    uint32_t deviceGroupNum = 0;
    vkEnumeratePhysicalDeviceGroups(m_Instance, &deviceGroupNum, nullptr);

    VkPhysicalDeviceGroupProperties* deviceGroups = STACK_ALLOC(VkPhysicalDeviceGroupProperties, deviceGroupNum);
    VkResult result = vkEnumeratePhysicalDeviceGroups(m_Instance, &deviceGroupNum, deviceGroups);

    RETURN_ON_FAILURE(GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't enumerate physical devices: vkEnumeratePhysicalDevices returned %d.", (int32_t)result);

    VkPhysicalDeviceIDProperties idProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    props.pNext = &idProps;

    bool isVulkan11Supported = false;

    uint32_t i = 0;
    for (; i < deviceGroupNum && m_PhysicalDevices.empty(); i++)
    {
        const VkPhysicalDeviceGroupProperties& group = deviceGroups[i];
        vkGetPhysicalDeviceProperties2(group.physicalDevices[0], &props);

        const uint32_t majorVersion = VK_VERSION_MAJOR(props.properties.apiVersion);
        const uint32_t minorVersion = VK_VERSION_MINOR(props.properties.apiVersion);
        isVulkan11Supported = majorVersion > 1 || (majorVersion == 1 && minorVersion >= 1);

        const bool isPhysicalDeviceSpecified = physicalDeviceGroup != nullptr;

        if (isPhysicalDeviceSpecified)
        {
            const uint64_t luid = *(uint64_t*)idProps.deviceLUID;
            if (luid == physicalDeviceGroup->luid && group.physicalDeviceCount == physicalDeviceGroup->physicalDeviceGroupSize)
            {
                RETURN_ON_FAILURE(GetLog(), isVulkan11Supported, Result::UNSUPPORTED,
                    "Can't create a device: the specified physical device does not support Vulkan 1.1.");
                break;
            }
        }
        else
        {
            if (isVulkan11Supported)
                break;
        }
    }

    RETURN_ON_FAILURE(GetLog(), i != deviceGroupNum, Result::UNSUPPORTED,
        "Can't create a device: physical device not found.");

    const VkPhysicalDeviceGroupProperties& group = deviceGroups[i];

    m_IsSubsetAllocationSupported = true;
    if (group.subsetAllocation == VK_FALSE && group.physicalDeviceCount > 1)
    {
        m_IsSubsetAllocationSupported = false;
        REPORT_WARNING(GetLog(), "The device group does not support memory allocation on a subset of the physical devices.");
    }

    m_PhysicalDevices.insert(m_PhysicalDevices.begin(), group.physicalDevices, group.physicalDevices + group.physicalDeviceCount);

    if (!enableMGPU)
        m_PhysicalDevices.resize(1);

    return Result::SUCCESS;
}

inline uint8_t GetMaxSampleCount(VkSampleCountFlags flags)
{
    return (uint8_t)flags;
}

void DeviceVK::SetDeviceLimits(bool enableValidation)
{
    uint8_t conservativeRasterTier = 0;
    if (m_IsConservativeRasterExtSupported)
    {
        VkPhysicalDeviceConservativeRasterizationPropertiesEXT cr = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT
        };
        VkPhysicalDeviceProperties2 props = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            &cr
        };
        vkGetPhysicalDeviceProperties2(m_PhysicalDevices.front(), &props);

        if ( cr.fullyCoveredFragmentShaderInputVariable && cr.primitiveOverestimationSize <= (1.0 / 256.0f) )
            conservativeRasterTier = 3;
        else if ( cr.degenerateTrianglesRasterized && cr.primitiveOverestimationSize < (1.0f / 2.0f) )
            conservativeRasterTier = 2;
        else
            conservativeRasterTier = 1;
    }

    VkPhysicalDeviceFeatures features = {};
    vkGetPhysicalDeviceFeatures(m_PhysicalDevices.front(), &features);

    uint32_t familyNum = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevices.front(), &familyNum, nullptr);

    Vector<VkQueueFamilyProperties> familyProperties(familyNum, m_StdAllocator);
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevices.front(), &familyNum, familyProperties.data());

    uint32_t copyQueueTimestampValidBits = 0;
    const uint32_t copyQueueFamilyIndex = m_FamilyIndices[(uint32_t)CommandQueueType::COPY];
    if (copyQueueFamilyIndex != std::numeric_limits<uint32_t>::max())
        copyQueueTimestampValidBits = familyProperties[copyQueueFamilyIndex].timestampValidBits;

    VkPhysicalDeviceProperties props = {};
    vkGetPhysicalDeviceProperties(m_PhysicalDevices.front(), &props);
    const VkPhysicalDeviceLimits& limits = props.limits;

    m_DeviceDesc.graphicsAPI = GraphicsAPI::VULKAN;
    m_DeviceDesc.vendor = GetVendorFromID(props.vendorID);
    m_DeviceDesc.nriVersionMajor = NRI_VERSION_MAJOR;
    m_DeviceDesc.nriVersionMinor = NRI_VERSION_MINOR;

    m_DeviceDesc.maxViewports = limits.maxViewports;
    m_DeviceDesc.viewportBoundsRange[0] = int32_t(limits.viewportBoundsRange[0]);
    m_DeviceDesc.viewportBoundsRange[1] = int32_t(limits.viewportBoundsRange[1]);
    m_DeviceDesc.viewportSubPixelBits = limits.viewportSubPixelBits;
    m_DeviceDesc.maxFrameBufferSize = std::min(limits.maxFramebufferWidth, limits.maxFramebufferHeight);
    m_DeviceDesc.maxFrameBufferLayers = limits.maxFramebufferLayers;
    m_DeviceDesc.maxColorAttachments = limits.maxColorAttachments;
    m_DeviceDesc.maxFrameBufferColorSampleCount = GetMaxSampleCount(limits.framebufferColorSampleCounts);
    m_DeviceDesc.maxFrameBufferDepthSampleCount = GetMaxSampleCount(limits.framebufferDepthSampleCounts);
    m_DeviceDesc.maxFrameBufferStencilSampleCount = GetMaxSampleCount(limits.framebufferStencilSampleCounts);
    m_DeviceDesc.maxFrameBufferNoAttachmentsSampleCount = GetMaxSampleCount(limits.framebufferNoAttachmentsSampleCounts);
    m_DeviceDesc.maxTextureColorSampleCount = GetMaxSampleCount(limits.sampledImageColorSampleCounts);
    m_DeviceDesc.maxTextureIntegerSampleCount = GetMaxSampleCount(limits.sampledImageIntegerSampleCounts);
    m_DeviceDesc.maxTextureDepthSampleCount = GetMaxSampleCount(limits.sampledImageDepthSampleCounts);
    m_DeviceDesc.maxTextureStencilSampleCount = GetMaxSampleCount(limits.sampledImageStencilSampleCounts);
    m_DeviceDesc.maxStorageTextureSampleCount = GetMaxSampleCount(limits.storageImageSampleCounts);
    m_DeviceDesc.maxTexture1DSize = limits.maxImageDimension1D;
    m_DeviceDesc.maxTexture2DSize = limits.maxImageDimension2D;
    m_DeviceDesc.maxTexture3DSize = limits.maxImageDimension3D;
    m_DeviceDesc.maxTextureArraySize = limits.maxImageArrayLayers;
    m_DeviceDesc.maxTexelBufferElements = limits.maxTexelBufferElements;
    m_DeviceDesc.maxConstantBufferRange = limits.maxUniformBufferRange;
    m_DeviceDesc.maxStorageBufferRange = limits.maxStorageBufferRange;
    m_DeviceDesc.maxPushConstantsSize = limits.maxPushConstantsSize;
    m_DeviceDesc.maxMemoryAllocationCount = limits.maxMemoryAllocationCount;
    m_DeviceDesc.maxSamplerAllocationCount = limits.maxSamplerAllocationCount;
    m_DeviceDesc.bufferTextureGranularity = (uint32_t)limits.bufferImageGranularity;
    m_DeviceDesc.maxBoundDescriptorSets = limits.maxBoundDescriptorSets;
    m_DeviceDesc.maxPerStageDescriptorSamplers = limits.maxPerStageDescriptorSamplers;
    m_DeviceDesc.maxPerStageDescriptorConstantBuffers = limits.maxPerStageDescriptorUniformBuffers;
    m_DeviceDesc.maxPerStageDescriptorStorageBuffers = limits.maxPerStageDescriptorStorageBuffers;
    m_DeviceDesc.maxPerStageDescriptorTextures = limits.maxPerStageDescriptorSampledImages;
    m_DeviceDesc.maxPerStageDescriptorStorageTextures = limits.maxPerStageDescriptorStorageImages;
    m_DeviceDesc.maxPerStageResources = limits.maxPerStageResources;
    m_DeviceDesc.maxDescriptorSetSamplers = limits.maxDescriptorSetSamplers;
    m_DeviceDesc.maxDescriptorSetConstantBuffers = limits.maxDescriptorSetUniformBuffers;
    m_DeviceDesc.maxDescriptorSetStorageBuffers = limits.maxDescriptorSetStorageBuffers;
    m_DeviceDesc.maxDescriptorSetTextures = limits.maxDescriptorSetSampledImages;
    m_DeviceDesc.maxDescriptorSetStorageTextures = limits.maxDescriptorSetStorageImages;
    m_DeviceDesc.maxVertexAttributes = limits.maxVertexInputAttributes;
    m_DeviceDesc.maxVertexStreams = limits.maxVertexInputBindings;
    m_DeviceDesc.maxVertexOutputComponents = limits.maxVertexOutputComponents;
    m_DeviceDesc.maxTessGenerationLevel = (float)limits.maxTessellationGenerationLevel;
    m_DeviceDesc.maxTessPatchSize = limits.maxTessellationPatchSize;
    m_DeviceDesc.maxTessControlPerVertexInputComponents = limits.maxTessellationControlPerVertexInputComponents;
    m_DeviceDesc.maxTessControlPerVertexOutputComponents = limits.maxTessellationControlPerVertexOutputComponents;
    m_DeviceDesc.maxTessControlPerPatchOutputComponents = limits.maxTessellationControlPerPatchOutputComponents;
    m_DeviceDesc.maxTessControlTotalOutputComponents = limits.maxTessellationControlTotalOutputComponents;
    m_DeviceDesc.maxTessEvaluationInputComponents = limits.maxTessellationEvaluationInputComponents;
    m_DeviceDesc.maxTessEvaluationOutputComponents = limits.maxTessellationEvaluationOutputComponents;
    m_DeviceDesc.maxGeometryShaderInvocations = limits.maxGeometryShaderInvocations;
    m_DeviceDesc.maxGeometryInputComponents = limits.maxGeometryInputComponents;
    m_DeviceDesc.maxGeometryOutputComponents = limits.maxGeometryOutputComponents;
    m_DeviceDesc.maxGeometryOutputVertices = limits.maxGeometryOutputVertices;
    m_DeviceDesc.maxGeometryTotalOutputComponents = limits.maxGeometryTotalOutputComponents;
    m_DeviceDesc.maxFragmentInputComponents = limits.maxFragmentInputComponents;
    m_DeviceDesc.maxFragmentOutputAttachments = limits.maxFragmentOutputAttachments;
    m_DeviceDesc.maxFragmentDualSrcAttachments = limits.maxFragmentDualSrcAttachments;
    m_DeviceDesc.maxFragmentCombinedOutputResources = limits.maxFragmentCombinedOutputResources;
    m_DeviceDesc.maxComputeSharedMemorySize = limits.maxComputeSharedMemorySize;
    m_DeviceDesc.maxComputeWorkGroupCount[0] = limits.maxComputeWorkGroupCount[0];
    m_DeviceDesc.maxComputeWorkGroupCount[1] = limits.maxComputeWorkGroupCount[1];
    m_DeviceDesc.maxComputeWorkGroupCount[2] = limits.maxComputeWorkGroupCount[2];
    m_DeviceDesc.maxComputeWorkGroupInvocations = limits.maxComputeWorkGroupInvocations;
    m_DeviceDesc.maxComputeWorkGroupSize[0] = limits.maxComputeWorkGroupSize[0];
    m_DeviceDesc.maxComputeWorkGroupSize[1] = limits.maxComputeWorkGroupSize[1];
    m_DeviceDesc.maxComputeWorkGroupSize[2] = limits.maxComputeWorkGroupSize[2];
    m_DeviceDesc.subPixelPrecisionBits = limits.subPixelPrecisionBits;
    m_DeviceDesc.subTexelPrecisionBits = limits.subTexelPrecisionBits;
    m_DeviceDesc.mipmapPrecisionBits = limits.mipmapPrecisionBits;
    m_DeviceDesc.drawIndexedIndex16ValueMax = std::min<uint32_t>(std::numeric_limits<uint16_t>::max(), limits.maxDrawIndexedIndexValue);
    m_DeviceDesc.drawIndexedIndex32ValueMax = limits.maxDrawIndexedIndexValue;
    m_DeviceDesc.maxDrawIndirectCount = limits.maxDrawIndirectCount;
    m_DeviceDesc.samplerLodBiasMin = -limits.maxSamplerLodBias;
    m_DeviceDesc.samplerLodBiasMax = limits.maxSamplerLodBias;
    m_DeviceDesc.samplerAnisotropyMax = limits.maxSamplerAnisotropy;
    m_DeviceDesc.uploadBufferTextureRowAlignment = 1;
    m_DeviceDesc.uploadBufferTextureSliceAlignment = 1;
    m_DeviceDesc.typedBufferOffsetAlignment = (uint32_t)limits.minTexelBufferOffsetAlignment;
    m_DeviceDesc.constantBufferOffsetAlignment = (uint32_t)limits.minUniformBufferOffsetAlignment;
    m_DeviceDesc.storageBufferOffsetAlignment = (uint32_t)limits.minStorageBufferOffsetAlignment;
    m_DeviceDesc.minTexelOffset = limits.minTexelOffset;
    m_DeviceDesc.maxTexelOffset = limits.maxTexelOffset;
    m_DeviceDesc.minTexelGatherOffset = limits.minTexelGatherOffset;
    m_DeviceDesc.maxTexelGatherOffset = limits.maxTexelGatherOffset;
    m_DeviceDesc.timestampFrequencyHz = uint64_t( 1e9f / limits.timestampPeriod + 0.5f );
    m_DeviceDesc.maxClipDistances = limits.maxClipDistances;
    m_DeviceDesc.maxCullDistances = limits.maxCullDistances;
    m_DeviceDesc.maxCombinedClipAndCullDistances = limits.maxCombinedClipAndCullDistances;
    m_DeviceDesc.maxBufferSize = std::numeric_limits<uint64_t>::max();
    m_DeviceDesc.conservativeRasterTier = conservativeRasterTier;
    m_DeviceDesc.isAPIValidationEnabled = enableValidation;
    m_DeviceDesc.isTextureFilterMinMaxSupported = m_IsMinMaxFilterExtSupported;
    m_DeviceDesc.isLogicOpSupported = features.logicOp;
    m_DeviceDesc.isDepthBoundsTestSupported = features.depthBounds;
    m_DeviceDesc.isProgrammableSampleLocationsSupported = m_IsSampleLocationExtSupported;
    m_DeviceDesc.isComputeQueueSupported = m_Queues[(uint32_t)CommandQueueType::COMPUTE] != nullptr;
    m_DeviceDesc.isCopyQueueSupported = m_Queues[(uint32_t)CommandQueueType::COPY] != nullptr;
    m_DeviceDesc.isCopyQueueTimestampSupported = copyQueueTimestampValidBits == 64;
    m_DeviceDesc.isRegisterAliasingSupported = true;
    m_DeviceDesc.isSubsetAllocationSupported = m_IsSubsetAllocationSupported;
    m_DeviceDesc.phyiscalDeviceGroupSize = (uint32_t)m_PhysicalDevices.size();
}

bool DeviceVK::FilterDeviceExtensions(Vector<const char*>& extensions)
{
    uint32_t extensionNum = 0;
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevices.front(), nullptr, &extensionNum, nullptr);

    Vector<VkExtensionProperties> supportedExtensions(extensionNum, GetStdAllocator());
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevices.front(), nullptr, &extensionNum, supportedExtensions.data());

    bool allFound = true;
    for (size_t i = 0; i < extensions.size(); i++)
    {
        bool found = false;
        for (uint32_t j = 0; j < extensionNum && !found; j++)
        {
            if (strcmp(supportedExtensions[j].extensionName, extensions[i]) == 0)
                found = true;
        }

        if (!found)
        {
            extensions.erase(extensions.begin() + i--);
            allFound = false;
        }
    }

    return allFound;
}

void DeviceVK::FillFamilyIndices(bool useEnabledFamilyIndices, const uint32_t* enabledFamilyIndices, uint32_t familyIndexNum)
{
    uint32_t familyNum = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevices.front(), &familyNum, nullptr);

    Vector<VkQueueFamilyProperties> familyProps(familyNum, GetStdAllocator());
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevices.front(), &familyNum, familyProps.data());

    memset(m_FamilyIndices.data(), 0xff, m_FamilyIndices.size() * sizeof(uint32_t));

    for (uint32_t i = 0; i < familyProps.size(); i++)
    {
        const VkQueueFlags mask = familyProps[i].queueFlags;
        const bool graphics = mask & VK_QUEUE_GRAPHICS_BIT;
        const bool compute = mask & VK_QUEUE_COMPUTE_BIT;
        const bool copy = mask & VK_QUEUE_TRANSFER_BIT;

        if (useEnabledFamilyIndices)
        {
            bool isFamilyEnabled = false;
            for (uint32_t j = 0; j < familyIndexNum && !isFamilyEnabled; j++)
                isFamilyEnabled = enabledFamilyIndices[j] == i;

            if (!isFamilyEnabled)
                continue;
        }

        if (graphics)
            m_FamilyIndices[(uint32_t)CommandQueueType::GRAPHICS] = i;
        else if (compute)
            m_FamilyIndices[(uint32_t)CommandQueueType::COMPUTE] = i;
        else if (copy)
            m_FamilyIndices[(uint32_t)CommandQueueType::COPY] = i;
    }
}

bool IsExtensionInList( const char* extension, const Vector<const char*>& list )
{
    for (auto& extensionFromList : list)
    {
        if (strcmp(extension, extensionFromList) == 0)
            return true;
    }

    return false;
}

void EraseIncompatibleExtension(Vector<const char*>& extensions, const char* extensionToErase)
{
    size_t i = 0;
    for (; i < extensions.size() && strcmp(extensions[i], extensionToErase) != 0; i++)
    {}

    if (i < extensions.size())
        extensions.erase(extensions.begin() + i);
}

Result DeviceVK::CreateLogicalDevice(const DeviceCreationDesc& deviceCreationDesc)
{
    Vector<const char*> extensions(GetStdAllocator());

    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    for (uint32_t i = 0; i < deviceCreationDesc.vulkanExtensions.deviceExtensionNum; i++)
        extensions.push_back(deviceCreationDesc.vulkanExtensions.deviceExtensions[i]);

    if (!FilterDeviceExtensions(extensions))
    {
        REPORT_ERROR(GetLog(), "Can't create VkDevice: Swapchain extension is unsupported.");
        return Result::UNSUPPORTED;
    }

    extensions.push_back(VK_NV_RAY_TRACING_EXTENSION_NAME);
    extensions.push_back(VK_NV_MESH_SHADER_EXTENSION_NAME);
    extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    extensions.push_back(VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME);
    extensions.push_back(VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME);
    extensions.push_back(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
    extensions.push_back(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);

    FilterDeviceExtensions(extensions);

    EraseIncompatibleExtension(extensions, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    m_IsDescriptorIndexingExtSupported = IsExtensionInList(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, extensions);
    m_IsRayTracingExtSupported = m_IsDescriptorIndexingExtSupported && IsExtensionInList(VK_NV_RAY_TRACING_EXTENSION_NAME, extensions);
    m_IsSampleLocationExtSupported = IsExtensionInList(VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME, extensions);
    m_IsMinMaxFilterExtSupported = IsExtensionInList(VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME, extensions);
    m_IsConservativeRasterExtSupported = IsExtensionInList(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME, extensions);
    m_IsMeshShaderExtSupported = IsExtensionInList(VK_NV_MESH_SHADER_EXTENSION_NAME, extensions);

    const bool isDemoteToHelperInvocationSupported = IsExtensionInList(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME, extensions);

    VkPhysicalDeviceFeatures2 deviceFeatures2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };

    VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptorIndexingFeatures =
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT };

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures =
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };

    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demoteToHelperInvocationFeatures =
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT };

    VkPhysicalDeviceMeshShaderFeaturesNV meshShaderFeatures =
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV };

    deviceFeatures2.pNext = &bufferDeviceAddressFeatures;

    if (m_IsDescriptorIndexingExtSupported)
    {
        descriptorIndexingFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &descriptorIndexingFeatures;
    }

    if (isDemoteToHelperInvocationSupported)
    {
        demoteToHelperInvocationFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &demoteToHelperInvocationFeatures;
    }

    if (m_IsMeshShaderExtSupported)
    {
        meshShaderFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &meshShaderFeatures;
    }

    vkGetPhysicalDeviceFeatures2(m_PhysicalDevices.front(), &deviceFeatures2);

    if (!deviceCreationDesc.enableAPIValidation)
        deviceFeatures2.features.robustBufferAccess = false;

    Vector<VkDeviceQueueCreateInfo> queues(GetStdAllocator());
    const float priorities = 1.0f;
    for (size_t i = 0; i < m_FamilyIndices.size(); i++)
    {
        if (m_FamilyIndices[i] == std::numeric_limits<uint32_t>::max())
            continue;

        VkDeviceQueueCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        info.queueCount = 1;
        info.queueFamilyIndex = m_FamilyIndices[i];
        info.pQueuePriorities = &priorities;
        queues.push_back(info);
    }

    VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceCreateInfo.pNext = &deviceFeatures2;
    deviceCreateInfo.queueCreateInfoCount = (uint32_t)queues.size();
    deviceCreateInfo.pQueueCreateInfos = queues.data();
    deviceCreateInfo.enabledExtensionCount = (uint32_t)extensions.size();
    deviceCreateInfo.ppEnabledExtensionNames = extensions.data();

    VkDeviceGroupDeviceCreateInfo deviceGroupInfo;
    if (m_PhysicalDevices.size() > 1)
    {
        deviceGroupInfo = { VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO };
        deviceGroupInfo.pNext = deviceCreateInfo.pNext;
        deviceGroupInfo.physicalDeviceCount = (uint32_t)m_PhysicalDevices.size();
        deviceGroupInfo.pPhysicalDevices = m_PhysicalDevices.data();
        deviceCreateInfo.pNext = &deviceGroupInfo;
    }

    const VkResult result = vkCreateDevice(m_PhysicalDevices.front(), &deviceCreateInfo, m_AllocationCallbackPtr, &m_Device);

    RETURN_ON_FAILURE(GetLog(), result == VK_SUCCESS, GetReturnCode(result), "Can't create a device: "
        "vkCreateDevice returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

void DeviceVK::CreateCommandQueues()
{
    for (uint32_t i = 0; i < m_FamilyIndices.size(); i++)
    {
        if (m_FamilyIndices[i] == std::numeric_limits<uint32_t>::max())
            continue;

        VkQueue handle = VK_NULL_HANDLE;
        vkGetDeviceQueue(m_Device, m_FamilyIndices[i], 0, &handle);

        m_Queues[i] = Allocate<CommandQueueVK>(GetStdAllocator(), *this, handle, m_FamilyIndices[i], (CommandQueueType)i);

        m_ConcurrentSharingModeQueueIndices.push_back(m_FamilyIndices[i]);
    }
}

void DeviceVK::RetrieveRayTracingInfo()
{
    m_RayTracingDeviceProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV };

    if (!m_IsRayTracingExtSupported)
        return;

    VkPhysicalDeviceProperties2 props = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        &m_RayTracingDeviceProperties
    };

    vkGetPhysicalDeviceProperties2(m_PhysicalDevices.front(), &props);

    m_DeviceDesc.rayTracingShaderGroupIdentifierSize = m_RayTracingDeviceProperties.shaderGroupHandleSize;
    m_DeviceDesc.rayTracingMaxRecursionDepth = m_RayTracingDeviceProperties.maxRecursionDepth;
    m_DeviceDesc.rayTracingGeometryObjectMaxNum = (uint32_t)m_RayTracingDeviceProperties.maxGeometryCount;
    m_DeviceDesc.rayTracingShaderTableAligment = m_RayTracingDeviceProperties.shaderGroupBaseAlignment;
    m_DeviceDesc.rayTracingShaderTableMaxStride = m_RayTracingDeviceProperties.maxShaderGroupStride;
}

void DeviceVK::RetrieveMeshShaderInfo()
{
    VkPhysicalDeviceMeshShaderPropertiesNV meshShaderProperties =
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_NV };

    if (!m_IsMeshShaderExtSupported)
        return;

    VkPhysicalDeviceProperties2 props = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        &meshShaderProperties
    };

    vkGetPhysicalDeviceProperties2(m_PhysicalDevices.front(), &props);

    m_DeviceDesc.maxMeshTasksCount = meshShaderProperties.maxDrawMeshTasksCount;
    m_DeviceDesc.maxTaskWorkGroupInvocations = meshShaderProperties.maxTaskWorkGroupInvocations;
    m_DeviceDesc.maxTaskWorkGroupSize[0] = meshShaderProperties.maxTaskWorkGroupSize[0];
    m_DeviceDesc.maxTaskWorkGroupSize[1] = meshShaderProperties.maxTaskWorkGroupSize[1];
    m_DeviceDesc.maxTaskWorkGroupSize[2] = meshShaderProperties.maxTaskWorkGroupSize[2];
    m_DeviceDesc.maxTaskTotalMemorySize = meshShaderProperties.maxTaskTotalMemorySize;
    m_DeviceDesc.maxTaskOutputCount = meshShaderProperties.maxTaskOutputCount;
    m_DeviceDesc.maxMeshWorkGroupInvocations = meshShaderProperties.maxMeshWorkGroupInvocations;
    m_DeviceDesc.maxMeshWorkGroupSize[0] = meshShaderProperties.maxMeshWorkGroupSize[0];
    m_DeviceDesc.maxMeshWorkGroupSize[1] = meshShaderProperties.maxMeshWorkGroupSize[1];
    m_DeviceDesc.maxMeshWorkGroupSize[2] = meshShaderProperties.maxMeshWorkGroupSize[2];
    m_DeviceDesc.maxMeshTotalMemorySize = meshShaderProperties.maxMeshTotalMemorySize;
    m_DeviceDesc.maxMeshOutputVertices = meshShaderProperties.maxMeshOutputVertices;
    m_DeviceDesc.maxMeshOutputPrimitives = meshShaderProperties.maxMeshOutputPrimitives;
    m_DeviceDesc.maxMeshMultiviewViewCount = meshShaderProperties.maxMeshMultiviewViewCount;
    m_DeviceDesc.meshOutputPerVertexGranularity = meshShaderProperties.meshOutputPerVertexGranularity;
    m_DeviceDesc.meshOutputPerPrimitiveGranularity = meshShaderProperties.meshOutputPerPrimitiveGranularity;
}

void DeviceVK::SetDebugNameToTrivialObject(VkObjectType objectType, const void* handle, const char* name)
{
    if (m_VK.SetDebugUtilsObjectNameEXT == nullptr)
        return;

    VkDebugUtilsObjectNameInfoEXT info = {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        nullptr,
        objectType,
        (uint64_t)handle,
        name
    };

    const VkResult result = m_VK.SetDebugUtilsObjectNameEXT(m_Device, &info);

    RETURN_ON_FAILURE(GetLog(), result == VK_SUCCESS, ReturnVoid(),
        "Can't set a debug name to an object: vkSetDebugUtilsObjectNameEXT returned %d.", (int32_t)result);
}

void DeviceVK::SetDebugNameToDeviceGroupObject(VkObjectType objectType, const void* const* handles, const char* name)
{
    if (m_VK.SetDebugUtilsObjectNameEXT == nullptr)
        return;

    const size_t nameLength = strlen(name);
    constexpr size_t deviceIndexSuffixLength = 16; // " (PD%u)"

    char* nameWithDeviceIndex = STACK_ALLOC(char, nameLength + deviceIndexSuffixLength);
    memcpy(nameWithDeviceIndex, name, nameLength);

    VkDebugUtilsObjectNameInfoEXT info = {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        nullptr,
        objectType,
        (uint64_t)0,
        nameWithDeviceIndex
    };

    for (uint32_t i = 0; i < m_DeviceDesc.phyiscalDeviceGroupSize; i++)
    {
        if (handles[i] != VK_NULL_HANDLE)
        {
            info.objectHandle = (uint64_t)handles[i];
            snprintf(nameWithDeviceIndex + nameLength, deviceIndexSuffixLength, " (PD%u)", i);

            const VkResult result = m_VK.SetDebugUtilsObjectNameEXT(m_Device, &info);
            RETURN_ON_FAILURE(GetLog(), result == VK_SUCCESS, ReturnVoid(),
                "Can't set a debug name to an object: vkSetDebugUtilsObjectNameEXT returned %d.", (int32_t)result);
        }
    }
}

void DeviceVK::ReportDeviceGroupInfo()
{
    REPORT_INFO(GetLog(), "Available device memory heaps:");

    for (uint32_t i = 0; i < m_MemoryProps.memoryHeapCount; i++)
    {
        String text(GetStdAllocator());
        if (m_MemoryProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            text += "DEVICE_LOCAL_BIT ";
        if (m_MemoryProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT)
            text += "MULTI_INSTANCE_BIT ";

        const double size = double(m_MemoryProps.memoryHeaps[i].size) / (1024.0 * 1024.0);

        REPORT_INFO(GetLog(), "\tHeap%u %.1lfMiB - %s", i, size, text.c_str());

        if (m_DeviceDesc.phyiscalDeviceGroupSize == 1)
            continue;

        for (uint32_t j = 0; j < m_DeviceDesc.phyiscalDeviceGroupSize; j++)
        {
            REPORT_INFO(GetLog(), "\t\tPhysicalDevice%u", j);

            for (uint32_t k = 0; k < m_DeviceDesc.phyiscalDeviceGroupSize; k++)
            {
                if (j == k)
                    continue;

                VkPeerMemoryFeatureFlags flags = 0;
                vkGetDeviceGroupPeerMemoryFeatures(m_Device, i, j, k, &flags);

                text.clear();
                if (flags & VK_PEER_MEMORY_FEATURE_COPY_SRC_BIT)
                    text += "COPY_SRC_BIT ";
                if (flags & VK_PEER_MEMORY_FEATURE_COPY_DST_BIT)
                    text += "COPY_DST_BIT ";
                if (flags & VK_PEER_MEMORY_FEATURE_GENERIC_SRC_BIT)
                    text += "GENERIC_SRC_BIT ";
                if (flags & VK_PEER_MEMORY_FEATURE_GENERIC_DST_BIT)
                    text += "GENERIC_DST_BIT ";

                REPORT_INFO(GetLog(), "\t\t\tPhysicalDevice%u - %s", k, text.c_str());
            }
        }
    }
}

#define RESOLVE_DEVICE_FUNCTION( name ) \
    m_VK.name = (PFN_vk ## name)vkGetDeviceProcAddr(m_Device, "vk" #name)
#define RESOLVE_INSTANCE_FUNCTION( name ) \
    m_VK.name = (PFN_vk ## name)vkGetInstanceProcAddr(m_Instance, "vk" #name)

Result DeviceVK::ResolveDispatchTable()
{
    m_VK = {};

    RESOLVE_DEVICE_FUNCTION(CreateBuffer);
    RESOLVE_DEVICE_FUNCTION(CreateImage);
    RESOLVE_DEVICE_FUNCTION(CreateBufferView);
    RESOLVE_DEVICE_FUNCTION(CreateImageView);
    RESOLVE_DEVICE_FUNCTION(CreateSampler);
    RESOLVE_DEVICE_FUNCTION(CreateRenderPass);
    RESOLVE_DEVICE_FUNCTION(CreateFramebuffer);
    RESOLVE_DEVICE_FUNCTION(CreateQueryPool);
    RESOLVE_DEVICE_FUNCTION(CreateCommandPool);
    RESOLVE_DEVICE_FUNCTION(CreateFence);
    RESOLVE_DEVICE_FUNCTION(CreateSemaphore);
    RESOLVE_DEVICE_FUNCTION(CreateDescriptorPool);
    RESOLVE_DEVICE_FUNCTION(CreatePipelineLayout);
    RESOLVE_DEVICE_FUNCTION(CreateDescriptorSetLayout);
    RESOLVE_DEVICE_FUNCTION(CreateShaderModule);
    RESOLVE_DEVICE_FUNCTION(CreateGraphicsPipelines);
    RESOLVE_DEVICE_FUNCTION(CreateComputePipelines);
    RESOLVE_DEVICE_FUNCTION(CreateSwapchainKHR);

    RESOLVE_DEVICE_FUNCTION(DestroyBuffer);
    RESOLVE_DEVICE_FUNCTION(DestroyImage);
    RESOLVE_DEVICE_FUNCTION(DestroyBufferView);
    RESOLVE_DEVICE_FUNCTION(DestroyImageView);
    RESOLVE_DEVICE_FUNCTION(DestroySampler);
    RESOLVE_DEVICE_FUNCTION(DestroyRenderPass);
    RESOLVE_DEVICE_FUNCTION(DestroyFramebuffer);
    RESOLVE_DEVICE_FUNCTION(DestroyQueryPool);
    RESOLVE_DEVICE_FUNCTION(DestroyCommandPool);
    RESOLVE_DEVICE_FUNCTION(DestroyFence);
    RESOLVE_DEVICE_FUNCTION(DestroySemaphore);
    RESOLVE_DEVICE_FUNCTION(DestroyDescriptorPool);
    RESOLVE_DEVICE_FUNCTION(DestroyPipelineLayout);
    RESOLVE_DEVICE_FUNCTION(DestroyDescriptorSetLayout);
    RESOLVE_DEVICE_FUNCTION(DestroyShaderModule);
    RESOLVE_DEVICE_FUNCTION(DestroyPipeline);
    RESOLVE_DEVICE_FUNCTION(DestroySwapchainKHR);

    RESOLVE_DEVICE_FUNCTION(AllocateMemory);
    RESOLVE_DEVICE_FUNCTION(MapMemory);
    RESOLVE_DEVICE_FUNCTION(UnmapMemory);
    RESOLVE_DEVICE_FUNCTION(FreeMemory);
    RESOLVE_DEVICE_FUNCTION(BindBufferMemory2);
    RESOLVE_DEVICE_FUNCTION(BindImageMemory2);

    RESOLVE_DEVICE_FUNCTION(QueueWaitIdle);
    RESOLVE_DEVICE_FUNCTION(WaitForFences);
    RESOLVE_DEVICE_FUNCTION(ResetFences);
    RESOLVE_DEVICE_FUNCTION(AcquireNextImageKHR);
    RESOLVE_DEVICE_FUNCTION(QueueSubmit);
    RESOLVE_DEVICE_FUNCTION(QueuePresentKHR);

    RESOLVE_DEVICE_FUNCTION(ResetCommandPool);
    RESOLVE_DEVICE_FUNCTION(ResetDescriptorPool);
    RESOLVE_DEVICE_FUNCTION(AllocateCommandBuffers);
    RESOLVE_DEVICE_FUNCTION(AllocateDescriptorSets);
    RESOLVE_DEVICE_FUNCTION(FreeCommandBuffers);
    RESOLVE_DEVICE_FUNCTION(FreeDescriptorSets);

    RESOLVE_DEVICE_FUNCTION(UpdateDescriptorSets);

    RESOLVE_DEVICE_FUNCTION(BeginCommandBuffer);
    RESOLVE_DEVICE_FUNCTION(CmdSetDepthBounds);
    RESOLVE_DEVICE_FUNCTION(CmdSetViewport);
    RESOLVE_DEVICE_FUNCTION(CmdSetScissor);
    RESOLVE_DEVICE_FUNCTION(CmdSetStencilReference);
    RESOLVE_DEVICE_FUNCTION(CmdClearAttachments);
    RESOLVE_DEVICE_FUNCTION(CmdClearColorImage);
    RESOLVE_DEVICE_FUNCTION(CmdBeginRenderPass);
    RESOLVE_DEVICE_FUNCTION(CmdBindVertexBuffers);
    RESOLVE_DEVICE_FUNCTION(CmdBindIndexBuffer);
    RESOLVE_DEVICE_FUNCTION(CmdBindPipeline);
    RESOLVE_DEVICE_FUNCTION(CmdBindDescriptorSets);
    RESOLVE_DEVICE_FUNCTION(CmdPushConstants);
    RESOLVE_DEVICE_FUNCTION(CmdDispatch);
    RESOLVE_DEVICE_FUNCTION(CmdDispatchIndirect);
    RESOLVE_DEVICE_FUNCTION(CmdDraw);
    RESOLVE_DEVICE_FUNCTION(CmdDrawIndexed);
    RESOLVE_DEVICE_FUNCTION(CmdDrawIndirect);
    RESOLVE_DEVICE_FUNCTION(CmdDrawIndexedIndirect);
    RESOLVE_DEVICE_FUNCTION(CmdCopyBuffer);
    RESOLVE_DEVICE_FUNCTION(CmdCopyImage);
    RESOLVE_DEVICE_FUNCTION(CmdCopyBufferToImage);
    RESOLVE_DEVICE_FUNCTION(CmdCopyImageToBuffer);
    RESOLVE_DEVICE_FUNCTION(CmdPipelineBarrier);
    RESOLVE_DEVICE_FUNCTION(CmdBeginQuery);
    RESOLVE_DEVICE_FUNCTION(CmdEndQuery);
    RESOLVE_DEVICE_FUNCTION(CmdWriteTimestamp);
    RESOLVE_DEVICE_FUNCTION(CmdCopyQueryPoolResults);
    RESOLVE_DEVICE_FUNCTION(CmdResetQueryPool);
    RESOLVE_DEVICE_FUNCTION(CmdBeginDebugUtilsLabelEXT);
    RESOLVE_DEVICE_FUNCTION(CmdEndDebugUtilsLabelEXT);
    RESOLVE_DEVICE_FUNCTION(CmdEndRenderPass);
    RESOLVE_DEVICE_FUNCTION(CmdFillBuffer);
    RESOLVE_DEVICE_FUNCTION(EndCommandBuffer);

    RESOLVE_DEVICE_FUNCTION(GetBufferMemoryRequirements2);
    RESOLVE_DEVICE_FUNCTION(GetImageMemoryRequirements2);

    RESOLVE_DEVICE_FUNCTION(GetSwapchainImagesKHR);
    RESOLVE_DEVICE_FUNCTION(SetHdrMetadataEXT);

    RESOLVE_DEVICE_FUNCTION(SetDebugUtilsObjectNameEXT);

    if (m_IsRayTracingExtSupported)
    {
        RESOLVE_DEVICE_FUNCTION(CreateAccelerationStructureNV);
        RESOLVE_DEVICE_FUNCTION(CreateRayTracingPipelinesNV);
        RESOLVE_DEVICE_FUNCTION(DestroyAccelerationStructureNV);
        RESOLVE_DEVICE_FUNCTION(GetAccelerationStructureHandleNV);
        RESOLVE_DEVICE_FUNCTION(GetAccelerationStructureMemoryRequirementsNV);
        RESOLVE_DEVICE_FUNCTION(GetRayTracingShaderGroupHandlesNV);
        RESOLVE_DEVICE_FUNCTION(BindAccelerationStructureMemoryNV);
        RESOLVE_DEVICE_FUNCTION(CmdBuildAccelerationStructureNV);
        RESOLVE_DEVICE_FUNCTION(CmdCopyAccelerationStructureNV);
        RESOLVE_DEVICE_FUNCTION(CmdWriteAccelerationStructuresPropertiesNV);
        RESOLVE_DEVICE_FUNCTION(CmdTraceRaysNV);
    }

    if (m_IsMeshShaderExtSupported)
    {
        RESOLVE_DEVICE_FUNCTION(CmdDrawMeshTasksNV);
    }

    RESOLVE_INSTANCE_FUNCTION(GetPhysicalDeviceFormatProperties);

    return Result::SUCCESS;
}

void DeviceVK::Destroy()
{
    Deallocate(GetStdAllocator(), this);
}

Result CreateDeviceVK(const DeviceCreationDesc& deviceCreationDesc, DeviceBase*& device)
{
    Log log(GraphicsAPI::VULKAN, deviceCreationDesc.callbackInterface);
    StdAllocator<uint8_t> allocator(deviceCreationDesc.memoryAllocatorInterface);

    DeviceVK* implementation = Allocate<DeviceVK>(allocator, log, allocator);

    const Result res = implementation->Create(deviceCreationDesc);

    if (res == Result::SUCCESS)
    {
        device = implementation;
        return Result::SUCCESS;
    }

    Deallocate(allocator, implementation);
    return res;
}

Result CreateDeviceVK(const DeviceCreationVulkanDesc& deviceCreationDesc, DeviceBase*& device)
{
    Log log(GraphicsAPI::VULKAN, deviceCreationDesc.callbackInterface);
    StdAllocator<uint8_t> allocator(deviceCreationDesc.memoryAllocatorInterface);

    DeviceVK* implementation = Allocate<DeviceVK>(allocator, log, allocator);
    const Result res = implementation->Create(deviceCreationDesc);

    if (res == Result::SUCCESS)
    {
        device = implementation;
        return Result::SUCCESS;
    }

    Deallocate(allocator, implementation);
    return res;
}

Format GetFormatVK(uint32_t vkFormat)
{
    return ::GetNRIFormat((VkFormat)vkFormat);
}

#include "DeviceVK.hpp"
