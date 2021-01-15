/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  SwapChainInterface  ]

static void NRI_CALL SetSwapChainDebugName(SwapChain& swapChain, const char* name)
{
    ((SwapChainD3D12&)swapChain).SetDebugName(name);
}

static Texture* const* NRI_CALL GetSwapChainTextures(const SwapChain& swapChain, uint32_t& textureNum, Format& format)
{
    return ((SwapChainD3D12&)swapChain).GetTextures(textureNum, format);
}

static uint32_t NRI_CALL AcquireNextSwapChainTexture(SwapChain& swapChain, QueueSemaphore& textureReadyForRender)
{
    return ((SwapChainD3D12&)swapChain).AcquireNextTexture(textureReadyForRender);
}

static Result NRI_CALL SwapChainPresent(SwapChain& swapChain, QueueSemaphore& textureReadyForPresent)
{
    return ((SwapChainD3D12&)swapChain).Present(textureReadyForPresent);
}

static Result NRI_CALL SetSwapChainHdrMetadata(SwapChain& swapChain, const HdrMetadata& hdrMetadata)
{
    return ((SwapChainD3D12&)swapChain).SetHdrMetadata(hdrMetadata);
}

void FillFunctionTableSwapChainD3D12(SwapChainInterface& swapChainInterface)
{
    swapChainInterface.SetSwapChainDebugName = SetSwapChainDebugName;
    swapChainInterface.GetSwapChainTextures = GetSwapChainTextures;
    swapChainInterface.AcquireNextSwapChainTexture = AcquireNextSwapChainTexture;
    swapChainInterface.SwapChainPresent = SwapChainPresent;
    swapChainInterface.SetSwapChainHdrMetadata = SetSwapChainHdrMetadata;
}

#pragma endregion