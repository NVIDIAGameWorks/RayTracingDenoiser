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
    struct DeviceVK;
    struct CommandQueueVK;
    struct TextureVK;

    struct SwapChainVK
    {
        SwapChainVK(DeviceVK& device);
        ~SwapChainVK();

        DeviceVK& GetDevice() const;
        Result Create(const SwapChainDesc& swapChainDesc);

        void SetDebugName(const char* name);
        Texture* const* GetTextures(uint32_t& textureNum, Format& format) const;
        uint32_t AcquireNextTexture(QueueSemaphore& textureReadyForRender);
        Result Present(QueueSemaphore& textureReadyForPresent);
        Result SetHdrMetadata(const HdrMetadata& hdrMetadata);

    private:
        VkSwapchainKHR m_Handle = VK_NULL_HANDLE;
        const CommandQueueVK* m_CommandQueue = nullptr;
        uint32_t m_TextureIndex = std::numeric_limits<uint32_t>::max();
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        Vector<TextureVK*> m_Textures;
        Format m_Format = Format::UNKNOWN;
        uint32_t m_SwapInterval = 0;
        DeviceVK& m_Device;
    };

    inline DeviceVK& SwapChainVK::GetDevice() const
    {
        return m_Device;
    }
}