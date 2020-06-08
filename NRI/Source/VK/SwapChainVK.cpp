/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "SwapChainVK.h"
#include "CommandQueueVK.h"
#include "QueueSemaphoreVK.h"
#include "TextureVK.h"
#include "DeviceVK.h"

using namespace nri;

SwapChainVK::SwapChainVK(DeviceVK& device) :
    m_Textures(device.GetStdAllocator()),
    m_Device(device)
{
}

SwapChainVK::~SwapChainVK()
{
    const auto& vk = m_Device.GetDispatchTable();

    for (size_t i = 0; i < m_Textures.size(); i++)
    {
        m_Textures[i]->ClearHandle();
        Deallocate(m_Device.GetStdAllocator(), m_Textures[i]);
    }
    m_Textures.clear();

    if (m_Handle != VK_NULL_HANDLE)
        vk.DestroySwapchainKHR(m_Device, m_Handle, m_Device.GetAllocationCallbacks());

    if (m_Surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(m_Device, m_Surface, m_Device.GetAllocationCallbacks());
}

Result SwapChainVK::Create(const SwapChainDesc& swapChainDesc)
{
    m_CommandQueue = (CommandQueueVK*)swapChainDesc.commandQueue;

#ifdef VK_USE_PLATFORM_WIN32_KHR
    const VkWin32SurfaceCreateInfoKHR surfaceInfo = {
        VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        nullptr,
        (VkWin32SurfaceCreateFlagsKHR)0,
        (HINSTANCE)nullptr,
        (HWND)swapChainDesc.windowHandle
    };

    {
        const VkResult result = vkCreateWin32SurfaceKHR(m_Device, &surfaceInfo, m_Device.GetAllocationCallbacks(), &m_Surface);

        RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
            "Can't create a surface: vkCreateWin32SurfaceKHR returned %d.", (int32_t)result);
    }
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    // TODO: Implement
#else
#error "Unsupported platform!"
#endif

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_Device, m_CommandQueue->GetFamilyIndex(), m_Surface, &supported);

    if (supported == VK_FALSE)
    {
        REPORT_ERROR(m_Device.GetLog(), "The specified surface is not supported by the physical device.");
        return Result::UNSUPPORTED;
    }

    VkSurfaceCapabilitiesKHR capabilites = {};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Device, m_Surface, &capabilites);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't get physical device surface capabilities: vkGetPhysicalDeviceSurfaceCapabilitiesKHR returned %d.", (int32_t)result);

    const bool isWidthValid = swapChainDesc.width >= capabilites.minImageExtent.width &&
        swapChainDesc.width <= capabilites.maxImageExtent.width;
    const bool isHeightValid = swapChainDesc.height >= capabilites.minImageExtent.height &&
        swapChainDesc.height <= capabilites.maxImageExtent.height;

    if (!isWidthValid || !isHeightValid)
    {
        REPORT_ERROR(m_Device.GetLog(), "Invalid SwapChainVK buffer size.");
        return Result::INVALID_ARGUMENT;
    }

    uint32_t formatNum = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(m_Device, m_Surface, &formatNum, nullptr);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't get physical device surface formats: vkGetPhysicalDeviceSurfaceFormatsKHR returned %d.", (int32_t)result);

    VkSurfaceFormatKHR* surfaceFormats = STACK_ALLOC(VkSurfaceFormatKHR, formatNum);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(m_Device, m_Surface, &formatNum, surfaceFormats);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't get physical device surface formats: vkGetPhysicalDeviceSurfaceFormatsKHR returned %d.", (int32_t)result);

    VkSurfaceFormatKHR surfaceFormat = {};

    // TODO: Implement swapchain formats

    surfaceFormat = surfaceFormats[0];
    m_Format = GetNRIFormat(surfaceFormat.format);

    uint32_t presentModeNum = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(m_Device, m_Surface, &presentModeNum, nullptr);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't get supported present modes for the surface: vkGetPhysicalDeviceSurfacePresentModesKHR returned %d.", (int32_t)result);

    VkPresentModeKHR* presentModes = STACK_ALLOC(VkPresentModeKHR, presentModeNum);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(m_Device, m_Surface, &presentModeNum, presentModes);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't get supported present modes for the surface: vkGetPhysicalDeviceSurfacePresentModesKHR returned %d.", (int32_t)result);

    // Both of these modes use v-sync for preseting, but FIFO blocks execution
    VkPresentModeKHR desiredPresentMode = swapChainDesc.verticalSyncInterval ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    m_SwapInterval = swapChainDesc.verticalSyncInterval;

    supported = false;
    for (uint32_t i = 0; i < presentModeNum && !supported; i++)
        supported = desiredPresentMode == presentModes[i];

    if (!supported)
    {
        REPORT_WARNING(m_Device.GetLog(),
            "The present mode is not supported. Using the first mode from the list of supported modes. (Mode: %d)", (int32_t)desiredPresentMode);

        desiredPresentMode = presentModes[0];
    }

    const uint32_t familyIndex = m_CommandQueue->GetFamilyIndex();
    const uint32_t minImageNum = std::max<uint32_t>(capabilites.minImageCount, swapChainDesc.textureNum);

    const VkSwapchainCreateInfoKHR swapchainInfo = {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        nullptr,
        (VkSwapchainCreateFlagsKHR)0,
        m_Surface,
        minImageNum,
        surfaceFormat.format,
        surfaceFormat.colorSpace,
        { swapChainDesc.width, swapChainDesc.height },
        1,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        1,
        &familyIndex,
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        desiredPresentMode,
        VK_FALSE,
        VK_NULL_HANDLE
    };

    const auto& vk = m_Device.GetDispatchTable();
    result = vk.CreateSwapchainKHR(m_Device, &swapchainInfo, m_Device.GetAllocationCallbacks(), &m_Handle);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't create a swapchain: vkCreateSwapchainKHR returned %d.", (int32_t)result);

    uint32_t imageNum = 0;
    vk.GetSwapchainImagesKHR(m_Device, m_Handle, &imageNum, nullptr);

    VkImage* imageHandles = STACK_ALLOC(VkImage, imageNum);
    vk.GetSwapchainImagesKHR(m_Device, m_Handle, &imageNum, imageHandles);

    m_Textures.resize(imageNum);
    for (uint32_t i = 0; i < imageNum; i++)
    {
        TextureVK* texture = Allocate<TextureVK>(m_Device.GetStdAllocator(), m_Device);
        const VkExtent3D extent = { swapchainInfo.imageExtent.width, swapchainInfo.imageExtent.height, 1 };
        texture->Create(imageHandles[i], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_TYPE_2D, extent, m_Format);
        m_Textures[i] = texture;
    }

    return Result::SUCCESS;
}

inline void SwapChainVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_SURFACE_KHR, m_Surface, name);
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_SWAPCHAIN_KHR, m_Handle, name);
}

inline Texture* const* SwapChainVK::GetTextures(uint32_t& textureNum, Format& format) const
{
    textureNum = (uint32_t)m_Textures.size();
    format = m_Format;
    return (Texture* const*)m_Textures.data();
}

inline uint32_t SwapChainVK::AcquireNextTexture(QueueSemaphore& textureReadyForRender)
{
    const uint64_t timeout = 5000000000; // 5 seconds
    m_TextureIndex = std::numeric_limits<uint32_t>::max();

    const auto& vk = m_Device.GetDispatchTable();
    const VkResult result = vk.AcquireNextImageKHR(m_Device, m_Handle, timeout, *(QueueSemaphoreVK*)&textureReadyForRender,
        VK_NULL_HANDLE, &m_TextureIndex);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, m_TextureIndex,
        "Can't acquire the next texture of the swapchain: vkAcquireNextImageKHR returned %d.", (int32_t)result);

    return m_TextureIndex;
}

inline Result SwapChainVK::Present(QueueSemaphore& textureReadyForPresent)
{
    const VkSemaphore semaphore = (QueueSemaphoreVK&)textureReadyForPresent;

    VkResult presentRes = VK_SUCCESS;

    const VkPresentInfoKHR info = {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        nullptr,
        1,
        &semaphore,
        1,
        &m_Handle,
        &m_TextureIndex,
        &presentRes
    };

    const auto& vk = m_Device.GetDispatchTable();
    const VkResult result = vk.QueuePresentKHR(*m_CommandQueue, &info);

    for (uint32_t i = 1; i < m_SwapInterval; i++)
    {
        // TODO: emulate swap interval here
    }

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't present the swapchain: vkQueuePresentKHR returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

inline Result SwapChainVK::SetHdrMetadata(const HdrMetadata& hdrMetadata)
{
    const auto& vk = m_Device.GetDispatchTable();
    if (!vk.SetHdrMetadataEXT)
        return Result::UNSUPPORTED;

    const VkHdrMetadataEXT data = {
        VK_STRUCTURE_TYPE_HDR_METADATA_EXT,
        nullptr,
        {hdrMetadata.displayPrimaryRed[0], hdrMetadata.displayPrimaryRed[1]},
        {hdrMetadata.displayPrimaryGreen[0], hdrMetadata.displayPrimaryGreen[1]},
        {hdrMetadata.displayPrimaryBlue[0], hdrMetadata.displayPrimaryBlue[1]},
        {hdrMetadata.whitePoint[0], hdrMetadata.whitePoint[1]},
        hdrMetadata.maxLuminance,
        hdrMetadata.minLuminance,
        hdrMetadata.maxContentLightLevel,
        hdrMetadata.maxFrameAverageLightLevel
    };

    vk.SetHdrMetadataEXT(m_Device, 1, &m_Handle, &data);
    return Result::SUCCESS;
}

#include "SwapChainVK.hpp"
