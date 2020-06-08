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
    struct SwapChain;

    enum class SwapChainFormat : uint16_t
    {
        // BT.709 - LDR, https://en.wikipedia.org/wiki/Rec._709
        // BT.2020 - HDR, https://en.wikipedia.org/wiki/Rec._2020
        // G10 - linear (gamma 1.0)
        // G22 - sRGB (gamma ~2.2)
        // G2084 - SMPTE ST.2084 (Perceptual Quantization)

        BT709_G10_8BIT,
        BT709_G10_16BIT,
        BT709_G22_8BIT,
        BT709_G22_10BIT,
        BT2020_G2084_10BIT,
        MAX_NUM
    };

    // SwapChain buffers will be created as "color attachment" resources
    struct SwapChainDesc
    {
        const void* windowHandle;
        const CommandQueue* commandQueue;
        uint16_t width;
        uint16_t height;
        uint16_t textureNum;
        SwapChainFormat format;
        uint32_t verticalSyncInterval;
        uint32_t physicalDeviceIndex;
    };

    struct HdrMetadata
    {
        float displayPrimaryRed[2];
        float displayPrimaryGreen[2];
        float displayPrimaryBlue[2];
        float whitePoint[2];
        float maxLuminance;
        float minLuminance;
        float maxContentLightLevel;
        float maxFrameAverageLightLevel;
    };

    struct SwapChainInterface
    {
        Result (NRI_CALL *CreateSwapChain)(Device& device, const SwapChainDesc& swapChainDesc, SwapChain*& swapChain);
        void (NRI_CALL *DestroySwapChain)(SwapChain& swapChain);
        void (NRI_CALL *SetSwapChainDebugName)(SwapChain& swapChain, const char* name);
        Texture* const* (NRI_CALL *GetSwapChainTextures)(const SwapChain& swapChain, uint32_t& textureNum, Format& format);
        uint32_t (NRI_CALL *AcquireNextSwapChainTexture)(SwapChain& swapChain, QueueSemaphore& textureReadyForRender);
        Result (NRI_CALL *SwapChainPresent)(SwapChain& swapChain, QueueSemaphore& textureReadyForPresent);
        Result (NRI_CALL *SetSwapChainHdrMetadata)(SwapChain& swapChain, const HdrMetadata& hdrMetadata);
    };
}
