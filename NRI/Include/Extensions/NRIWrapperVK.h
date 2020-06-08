/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef struct VkCommandPool_T* VkCommandPool;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkImage_T* VkImage;
typedef struct VkBuffer_T* VkBuffer;
typedef struct VkDeviceMemory_T* VkDeviceMemory;
typedef struct VkQueryPool_T* VkQueryPool;
typedef struct VkPipeline_T* VkPipeline;
typedef struct VkDescriptorPool_T* VkDescriptorPool;
typedef struct VkSemaphore_T* VkSemaphore;
typedef struct VkFence_T* VkFence;

typedef struct VkImageView_T* VkImageView;
typedef struct VkBufferView_T* VkBufferView;
struct VkImageSubresourceRange;

namespace nri
{
    struct DeviceCreationVulkanDesc
    {
        VkInstance vkInstance;
        VkDevice vkDevice;
        const VkPhysicalDevice* vkPhysicalDevices;
        uint32_t deviceGroupSize;
        const uint32_t* queueFamilyIndices;
        uint32_t queueFamilyIndexNum;
        CallbackInterface callbackInterface;
        void* callbackInterfaceUserArg;
        MemoryAllocatorInterface memoryAllocatorInterface;
        void* memoryAllocatorInterfaceUserArg;
        bool enableNRIValidation;
        bool enableAPIValidation;
    };

    struct CommandQueueVulkanDesc
    {
        VkQueue vkQueue;
        uint32_t familyIndex;
        CommandQueueType commandQueueType;
    };

    struct CommandAllocatorVulkanDesc
    {
        VkCommandPool vkCommandPool;
        CommandQueueType commandQueueType;
    };

    struct CommandBufferVulkanDesc
    {
        VkCommandBuffer vkCommandBuffer;
        CommandQueueType commandQueueType;
    };

    struct BufferVulkanDesc
    {
        VkBuffer vkBuffer;
        uint64_t bufferSize;
        Memory* memory;
        uint64_t memoryOffset;
        uint32_t physicalDeviceMask;
    };

    struct TextureVulkanDesc
    {
        VkImage vkImage;
        uint32_t vkFormat;
        uint32_t vkImageAspectFlags;
        uint32_t vkImageType;
        uint16_t size[3];
        uint16_t mipNum;
        uint16_t arraySize;
        uint8_t sampleNum;
        uint32_t physicalDeviceMask;
    };

    struct MemoryVulkanDesc
    {
        VkDeviceMemory vkDeviceMemory;
        uint64_t size;
        uint32_t memoryTypeIndex;
        uint32_t physicalDeviceMask;
    };

    struct QueryPoolVulkanDesc
    {
        VkQueryPool vkQueryPool;
        uint32_t vkQueryType;
        uint32_t physicalDeviceMask;
    };

    NRI_API Result NRI_CALL CreateDeviceFromVkDevice(const DeviceCreationVulkanDesc& deviceDesc, Device*& device);
    NRI_API Format NRI_CALL GetFormatVK(uint32_t vkFormat);

    struct WrapperVKInterface
    {
        Result (NRI_CALL *CreateCommandQueueVK)(Device& device, const CommandQueueVulkanDesc& commandQueueVulkanDesc, CommandQueue*& commandQueue);
        Result (NRI_CALL *CreateCommandAllocatorVK)(Device& device, const CommandAllocatorVulkanDesc& commandAllocatorVulkanDesc, CommandAllocator*& commandAllocator);
        Result (NRI_CALL *CreateCommandBufferVK)(Device& device, const CommandBufferVulkanDesc& commandBufferVulkanDesc, CommandBuffer*& commandBuffer);
        Result (NRI_CALL *CreateDescriptorPoolVK)(Device& device, VkDescriptorPool vkDescriptorPool, DescriptorPool*& descriptorPool);
        Result (NRI_CALL *CreateBufferVK)(Device& device, const BufferVulkanDesc& bufferVulkanDesc, Buffer*& buffer);
        Result (NRI_CALL *CreateTextureVK)(Device& device, const TextureVulkanDesc& textureVulkanDesc, Texture*& texture);
        Result (NRI_CALL *CreateMemoryVK)(Device& device, const MemoryVulkanDesc& memoryVulkanDesc, Memory*& memory);
        Result (NRI_CALL *CreateGraphicsPipelineVK)(Device& device, VkPipeline vkPipeline, Pipeline*& pipeline);
        Result (NRI_CALL *CreateComputePipelineVK)(Device& device, VkPipeline vkPipeline, Pipeline*& pipeline);
        Result (NRI_CALL *CreateQueryPoolVK)(Device& device, const QueryPoolVulkanDesc& queryPoolVulkanDesc, QueryPool*& queryPool);
        Result (NRI_CALL *CreateQueueSemaphoreVK)(Device& device, VkSemaphore vkSemaphore, QueueSemaphore*& queueSemaphore);
        Result (NRI_CALL *CreateDeviceSemaphoreVK)(Device& device, VkFence vkFence, DeviceSemaphore*& deviceSemaphore);

        VkDevice (NRI_CALL *GetDeviceVK)(const Device& device);
        VkPhysicalDevice (NRI_CALL *GetPhysicalDeviceVK)(const Device& device);
        VkInstance (NRI_CALL *GetInstanceVK)(const Device& device);
        VkCommandBuffer (NRI_CALL *GetCommandBufferVK)(const CommandBuffer& commandBuffer);

        void (NRI_CALL *GetTextureVK)(const Texture& texture, uint32_t physicalDeviceIndex, TextureVulkanDesc& textureVulkanDesc);
        VkImageView (NRI_CALL *GetTextureDescriptorVK)(const Descriptor& descriptor, uint32_t physicalDeviceIndex, VkImageSubresourceRange& subresource);
        VkBufferView (NRI_CALL *GetBufferDescriptorVK)(const Descriptor& descriptor, uint32_t physicalDeviceIndex);
    };
}
