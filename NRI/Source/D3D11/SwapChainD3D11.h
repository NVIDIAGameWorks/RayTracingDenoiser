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
    struct DeviceD3D11;
    struct TextureD3D11;

    struct SwapChainD3D11
    {
        SwapChainD3D11(DeviceD3D11& device);
        ~SwapChainD3D11();

        inline DeviceD3D11& GetDevice() const
        { return m_Device; }

        Result Create(const VersionedDevice& device, const SwapChainDesc& swapChainDesc);

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);
        Texture* const* const GetTextures(uint32_t& textureNum, Format& format) const;
        uint32_t AcquireNextTexture(QueueSemaphore& textureReadyForRender);
        Result Present(QueueSemaphore& textureReadyForPresent);
        Result SetHdrMetadata(const HdrMetadata& hdrMetadata);

    private:
        VersionedSwapchain m_SwapChain;
        Vector<TextureD3D11> m_RenderTargets;
        Vector<Texture*> m_RenderTargetPointers;
        SwapChainDesc m_SwapChainDesc = {};
        Format m_Format = Format::UNKNOWN;
        bool m_IsTearingAllowed = false;
        DeviceD3D11& m_Device;
    };
}
