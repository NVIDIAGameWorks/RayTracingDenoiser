/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nri
{
    enum class Message
    {
        TYPE_INFO,
        TYPE_WARNING,
        TYPE_ERROR,
    };

    struct MemoryAllocatorInterface
    {
        void* (*Allocate)(void* userArg, size_t size, size_t alignment);
        void* (*Reallocate)(void* userArg, void* memory, size_t size, size_t alignment);
        void (*Free)(void* userArg, void* memory);
        void* userArg;
    };

    struct CallbackInterface
    {
        void (*MessageCallback)(void* userArg, const char* message, Message messageType);
        void (*AbortExecution)(void* userArg);
        void* userArg;
    };

    struct DisplayDesc
    {
        int32_t originLeft;
        int32_t originTop;
        uint32_t width;
        uint32_t height;
    };

    struct PhysicalDeviceGroup
    {
        char description[128];
        uint64_t luid;
        uint64_t dedicatedVideoMemoryMB;
        Vendor vendor;
        uint32_t deviceID;
        uint32_t physicalDeviceGroupSize;
        const DisplayDesc* displays;
        uint32_t displayNum;
    };

    struct VulkanExtensions
    {
        const char* const* instanceExtensions;
        uint32_t instanceExtensionNum;
        const char* const* deviceExtensions;
        uint32_t deviceExtensionNum;
    };

    struct DeviceCreationDesc
    {
        const PhysicalDeviceGroup* physicalDeviceGroup;
        CallbackInterface callbackInterface;
        MemoryAllocatorInterface memoryAllocatorInterface;
        GraphicsAPI graphicsAPI;
        SPIRVBindingOffsets spirvBindingOffsets;
        VulkanExtensions vulkanExtensions;
        bool enableNRIValidation : 1;
        bool enableAPIValidation : 1;
        bool enableMGPU : 1;
        bool D3D11CommandBufferEmulation : 1;
    };

    NRI_API Result NRI_CALL GetPhysicalDevices(PhysicalDeviceGroup* physicalDeviceGroups, uint32_t& physicalDeviceGroupNum);
    NRI_API Result NRI_CALL CreateDevice(const DeviceCreationDesc& deviceCreationDesc, Device*& device);
    NRI_API void NRI_CALL DestroyDevice(Device& device);
}
