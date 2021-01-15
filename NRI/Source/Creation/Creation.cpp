/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "DeviceBase.h"

#include "Include/vulkan/vulkan.h"

#define NRI_STRINGIFY(name) #name

#define USE_D3D11 1
#define USE_D3D12 1
#define USE_VULKAN 1

using namespace nri;

Result CreateDeviceD3D11(const DeviceCreationDesc& deviceCreationDesc, DeviceBase*& device);
Result CreateDeviceD3D11(const DeviceCreationD3D11Desc& deviceDesc, DeviceBase*& device);
Result CreateDeviceD3D12(const DeviceCreationDesc& deviceCreationDesc, DeviceBase*& device);
Result CreateDeviceD3D12(const DeviceCreationD3D12Desc& deviceCreationDesc, DeviceBase*& device);
Result CreateDeviceVK(const DeviceCreationDesc& deviceCreationDesc, DeviceBase*& device);
Result CreateDeviceVK(const DeviceCreationVulkanDesc& deviceDesc, DeviceBase*& device);
DeviceBase* CreateDeviceValidation(const DeviceCreationDesc& deviceCreationDesc, DeviceBase& device);
Format GetFormatDXGI(uint32_t dxgiFormat);
Format GetFormatVK(uint32_t vkFormat);

constexpr uint64_t Hash( const char* name )
{
    return *name != 0 ? *name ^ ( 33 * Hash(name + 1) ) : 5381;
}

Result NRI_CALL nri::GetInterface(const Device& device, const char* interfaceName, size_t interfaceSize, void* interfacePtr)
{
    const uint64_t hash = Hash(interfaceName);
    size_t realInterfaceSize = size_t(-1);
    Result result = Result::INVALID_ARGUMENT;
    const DeviceBase& deviceBase = (DeviceBase&)device;

    if (hash == Hash( NRI_STRINGIFY(nri::CoreInterface) ))
    {
        realInterfaceSize = sizeof(CoreInterface);
        result = deviceBase.FillFunctionTable(*(CoreInterface*)interfacePtr);
    }
    else if (hash == Hash( NRI_STRINGIFY(nri::SwapChainInterface) ))
    {
        realInterfaceSize = sizeof(SwapChainInterface);
        result = deviceBase.FillFunctionTable(*(SwapChainInterface*)interfacePtr);
    }
    else if (hash == Hash( NRI_STRINGIFY(nri::WrapperD3D11Interface) ))
    {
        realInterfaceSize = sizeof(WrapperD3D11Interface);
        result = deviceBase.FillFunctionTable(*(WrapperD3D11Interface*)interfacePtr);
    }
    else if (hash == Hash(NRI_STRINGIFY(nri::WrapperD3D12Interface)))
    {
        realInterfaceSize = sizeof(WrapperD3D12Interface);
        result = deviceBase.FillFunctionTable(*(WrapperD3D12Interface*)interfacePtr);
    }
    else if (hash == Hash( NRI_STRINGIFY(nri::WrapperVKInterface) ))
    {
        realInterfaceSize = sizeof(WrapperVKInterface);
        result = deviceBase.FillFunctionTable(*(WrapperVKInterface*)interfacePtr);
    }
    else if (hash == Hash( NRI_STRINGIFY(nri::RayTracingInterface) ))
    {
        realInterfaceSize = sizeof(RayTracingInterface);
        result = deviceBase.FillFunctionTable(*(RayTracingInterface*)interfacePtr);
    }
    else if (hash == Hash( NRI_STRINGIFY(nri::MeshShaderInterface) ))
    {
        realInterfaceSize = sizeof(MeshShaderInterface);
        result = deviceBase.FillFunctionTable(*(MeshShaderInterface*)interfacePtr);
    }
    else if (hash == Hash( NRI_STRINGIFY(nri::HelperInterface) ))
    {
        realInterfaceSize = sizeof(HelperInterface);
        result = deviceBase.FillFunctionTable(*(HelperInterface*)interfacePtr);
    }

    if (result == Result::INVALID_ARGUMENT)
        REPORT_ERROR(deviceBase.GetLog(), "Unknown interface '%s'!", interfaceName);
    else if (interfaceSize > realInterfaceSize)
        REPORT_ERROR(deviceBase.GetLog(), "Interface '%s' has invalid size (%u bytes, at least %u bytes expected by the implementation)", interfaceName, interfaceSize, realInterfaceSize);
    else if (result == Result::UNSUPPORTED)
        REPORT_WARNING(deviceBase.GetLog(), "Interface '%s' is not supported by the device!", interfaceName);

    return result;
}

bool IsValidDeviceGroup(const VkPhysicalDeviceGroupProperties& group)
{
    VkPhysicalDeviceProperties baseProperties = {};
    vkGetPhysicalDeviceProperties(group.physicalDevices[0], &baseProperties);

    for (uint32_t j = 1; j < group.physicalDeviceCount; j++)
    {
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(group.physicalDevices[j], &properties);

        if (properties.deviceID != baseProperties.deviceID)
            return false;
    }

    return true;
}

Result NRI_CALL nri::GetPhysicalDevices(PhysicalDeviceGroup* physicalDeviceGroups, uint32_t& physicalDeviceGroupNum)
{
    VkApplicationInfo applicationInfo = {};
    applicationInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &applicationInfo;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &instance);

    if (result != VK_SUCCESS)
    {
        vkDestroyInstance(instance, nullptr);
        return Result::UNSUPPORTED;
    }

    uint32_t deviceGroupNum = 0;
    vkEnumeratePhysicalDeviceGroups(instance, &deviceGroupNum, nullptr);

    VkPhysicalDeviceGroupProperties* deviceGroupProperties = STACK_ALLOC(VkPhysicalDeviceGroupProperties, deviceGroupNum);
    result = vkEnumeratePhysicalDeviceGroups(instance, &deviceGroupNum, deviceGroupProperties);

    if (result != VK_SUCCESS)
    {
        vkDestroyInstance(instance, nullptr);
        return Result::UNSUPPORTED;
    }

    if (physicalDeviceGroupNum == 0)
    {
        for (uint32_t i = 0; i < deviceGroupNum; i++)
        {
            if (!IsValidDeviceGroup(deviceGroupProperties[i]))
                deviceGroupNum--;
        }
        physicalDeviceGroupNum = deviceGroupNum;
        vkDestroyInstance(instance, nullptr);
        return Result::SUCCESS;
    }

    VkPhysicalDeviceIDProperties deviceIDProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };

    VkPhysicalDeviceProperties2 properties2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    properties2.pNext = &deviceIDProperties;

    VkPhysicalDeviceMemoryProperties memoryProperties = {};

    VkPhysicalDeviceProperties& properties = properties2.properties;

    for (uint32_t i = 0, j = 0; i < deviceGroupNum && j < physicalDeviceGroupNum; i++)
    {
        if (!IsValidDeviceGroup(deviceGroupProperties[i]))
            continue;

        const VkPhysicalDevice physicalDevice = deviceGroupProperties[i].physicalDevices[0];
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

        if (deviceIDProperties.deviceLUIDValid == VK_FALSE)
        {
            vkDestroyInstance(instance, nullptr);
            return Result::UNSUPPORTED;
        }

        PhysicalDeviceGroup& group = physicalDeviceGroups[j++];

        group.vendor = GetVendorFromID(properties.vendorID);
        group.deviceID = properties.deviceID;
        group.luid = *(uint64_t*)&deviceIDProperties.deviceLUID[0];
        group.physicalDeviceGroupSize = deviceGroupProperties[i].physicalDeviceCount;

        const size_t descriptionLength = std::min(strlen(properties.deviceName), (size_t)GetCountOf(group.description));
        strncpy(group.description, properties.deviceName, descriptionLength);

        group.dedicatedVideoMemoryMB = 0;
        for (uint32_t k = 0; k < memoryProperties.memoryHeapCount; k++)
        {
            if (memoryProperties.memoryHeaps[k].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                group.dedicatedVideoMemoryMB += memoryProperties.memoryHeaps[k].size / (1024 * 1024);
        }
    }

    vkDestroyInstance(instance, nullptr);
    return Result::SUCCESS;
}

template< typename T >
Result FinalizeDeviceCreation(const T& deviceCreationDesc, DeviceBase& deviceImpl, Device*& device)
{
    if (deviceCreationDesc.enableNRIValidation)
    {
        Device* deviceVal = (Device*)CreateDeviceValidation(deviceCreationDesc, deviceImpl);
        if (deviceVal == nullptr)
        {
            nri::DestroyDevice((Device&)deviceImpl);
            return Result::FAILURE;
        }

        device = deviceVal;
    }
    else
    {
        device = (Device*)&deviceImpl;
    }

    return Result::SUCCESS;
}

NRI_API Result NRI_CALL nri::CreateDevice(const DeviceCreationDesc& deviceCreationDesc, Device*& device)
{
    Result result = Result::UNSUPPORTED;
    DeviceBase* deviceImpl = nullptr;

    DeviceCreationDesc modifiedDeviceCreationDesc = deviceCreationDesc;
    CheckAndSetDefaultCallbacks(modifiedDeviceCreationDesc.callbackInterface);
    CheckAndSetDefaultAllocator(modifiedDeviceCreationDesc.memoryAllocatorInterface);

    #if (USE_D3D11 == 1)
        if (modifiedDeviceCreationDesc.graphicsAPI == GraphicsAPI::D3D11)
            result = CreateDeviceD3D11(modifiedDeviceCreationDesc, deviceImpl);
    #endif

    #if (USE_D3D12 == 1)
        if (modifiedDeviceCreationDesc.graphicsAPI == GraphicsAPI::D3D12)
            result = CreateDeviceD3D12(modifiedDeviceCreationDesc, deviceImpl);
    #endif

    #if (USE_VULKAN == 1)
        if (modifiedDeviceCreationDesc.graphicsAPI == GraphicsAPI::VULKAN)
            result = CreateDeviceVK(modifiedDeviceCreationDesc, deviceImpl);
    #endif

    if (result != Result::SUCCESS)
        return result;

    return FinalizeDeviceCreation(modifiedDeviceCreationDesc, *deviceImpl, device);
}

NRI_API Result NRI_CALL nri::CreateDeviceFromD3D11Device(const DeviceCreationD3D11Desc& deviceCreationD3D11Desc, Device*& device)
{
    DeviceCreationDesc deviceCreationDesc = {};
    deviceCreationDesc.callbackInterface = deviceCreationD3D11Desc.callbackInterface;
    deviceCreationDesc.memoryAllocatorInterface = deviceCreationD3D11Desc.memoryAllocatorInterface;
    deviceCreationDesc.graphicsAPI = GraphicsAPI::D3D11;
    deviceCreationDesc.enableNRIValidation = deviceCreationD3D11Desc.enableNRIValidation;
    deviceCreationDesc.enableAPIValidation = deviceCreationD3D11Desc.enableAPIValidation;

    CheckAndSetDefaultCallbacks(deviceCreationDesc.callbackInterface);
    CheckAndSetDefaultAllocator(deviceCreationDesc.memoryAllocatorInterface);

    Result result = Result::UNSUPPORTED;
    DeviceBase* deviceImpl = nullptr;

    #if (USE_D3D11 == 1)
        result = CreateDeviceD3D11(deviceCreationD3D11Desc, deviceImpl);
    #endif

    if (result != Result::SUCCESS)
        return result;

    return FinalizeDeviceCreation(deviceCreationDesc, *deviceImpl, device);
}

NRI_API Result NRI_CALL nri::CreateDeviceFromD3D12Device(const DeviceCreationD3D12Desc& deviceCreationD3D12Desc, Device*& device)
{
    DeviceCreationDesc deviceCreationDesc = {};
    deviceCreationDesc.callbackInterface = deviceCreationD3D12Desc.callbackInterface;
    deviceCreationDesc.memoryAllocatorInterface = deviceCreationD3D12Desc.memoryAllocatorInterface;
    deviceCreationDesc.graphicsAPI = GraphicsAPI::D3D12;
    deviceCreationDesc.enableNRIValidation = deviceCreationD3D12Desc.enableNRIValidation;
    deviceCreationDesc.enableAPIValidation = deviceCreationD3D12Desc.enableAPIValidation;

    CheckAndSetDefaultCallbacks(deviceCreationDesc.callbackInterface);
    CheckAndSetDefaultAllocator(deviceCreationDesc.memoryAllocatorInterface);

    DeviceCreationD3D12Desc tempDeviceCreationD3D12Desc = deviceCreationD3D12Desc;

    Result result = Result::UNSUPPORTED;
    DeviceBase* deviceImpl = nullptr;

    CheckAndSetDefaultCallbacks(tempDeviceCreationD3D12Desc.callbackInterface);
    CheckAndSetDefaultAllocator(tempDeviceCreationD3D12Desc.memoryAllocatorInterface);

#if (USE_D3D12 == 1)
    result = CreateDeviceD3D12(tempDeviceCreationD3D12Desc, deviceImpl);
#endif

    if (result != Result::SUCCESS)
        return result;

    return FinalizeDeviceCreation(deviceCreationDesc, *deviceImpl, device);
}

NRI_API Result NRI_CALL nri::CreateDeviceFromVkDevice(const DeviceCreationVulkanDesc& deviceCreationVulkanDesc, Device*& device)
{
    DeviceCreationDesc deviceCreationDesc = {};
    deviceCreationDesc.callbackInterface = deviceCreationVulkanDesc.callbackInterface;
    deviceCreationDesc.memoryAllocatorInterface = deviceCreationVulkanDesc.memoryAllocatorInterface;
    deviceCreationDesc.graphicsAPI = GraphicsAPI::VULKAN;
    deviceCreationDesc.enableNRIValidation = deviceCreationVulkanDesc.enableNRIValidation;
    deviceCreationDesc.enableAPIValidation = deviceCreationVulkanDesc.enableAPIValidation;

    CheckAndSetDefaultCallbacks(deviceCreationDesc.callbackInterface);
    CheckAndSetDefaultAllocator(deviceCreationDesc.memoryAllocatorInterface);

    DeviceCreationVulkanDesc tempDeviceCreationVulkanDesc = deviceCreationVulkanDesc;

    CheckAndSetDefaultCallbacks(tempDeviceCreationVulkanDesc.callbackInterface);
    CheckAndSetDefaultAllocator(tempDeviceCreationVulkanDesc.memoryAllocatorInterface);

    Result result = Result::UNSUPPORTED;
    DeviceBase* deviceImpl = nullptr;

    #if (USE_VULKAN == 1)
        result = CreateDeviceVK(tempDeviceCreationVulkanDesc, deviceImpl);
    #endif

    if (result != Result::SUCCESS)
        return result;

    return FinalizeDeviceCreation(deviceCreationDesc, *deviceImpl, device);
}

NRI_API Format NRI_CALL nri::GetFormatVK(uint32_t vkFormat)
{
    #if (USE_VULKAN == 1)
        return ::GetFormatVK(vkFormat);
    #else
        return nri::Format::UNKNOWN;
    #endif
}

NRI_API Format NRI_CALL nri::GetFormatDXGI(uint32_t dxgiFormat)
{
    #if (USE_D3D11 == 1 || USE_D3D12 == 1)
        return ::GetFormatDXGI(dxgiFormat);
    #else
        return nri::Format::UNKNOWN;
    #endif
}

NRI_API void NRI_CALL nri::DestroyDevice(Device& device)
{
    ((DeviceBase&)device).Destroy();
}
