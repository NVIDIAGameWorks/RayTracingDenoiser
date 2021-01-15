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
    struct TextureVal;

    struct SwapChainVal : public DeviceObjectVal<SwapChain>
    {
        SwapChainVal(DeviceVal& device, SwapChain& swapChain, const SwapChainDesc& swapChainDesc);
        ~SwapChainVal();

        void SetDebugName(const char* name);
        Texture* const* GetTextures(uint32_t& textureNum, Format& format) const;
        uint32_t AcquireNextTexture(QueueSemaphore& textureReadyForRender);
        Result Present(QueueSemaphore& textureReadyForPresent);
        Result SetHdrMetadata(const HdrMetadata& hdrMetadata);

    private:
        const SwapChainInterface& m_SwapChainAPI;
        mutable Vector<TextureVal*> m_Textures;
        SwapChainDesc m_SwapChainDesc = {};
    };
}
