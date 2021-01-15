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
    ((SwapChainVal*)&swapChain)->SetDebugName(name);
}

static Texture* const* NRI_CALL GetSwapChainTextures(const SwapChain& swapChain, uint32_t& textureNum, Format& format)
{
    return ((SwapChainVal*)&swapChain)->GetTextures(textureNum, format);
}

static uint32_t NRI_CALL AcquireNextSwapChainTexture(SwapChain& swapChain, QueueSemaphore& textureReadyForRender)
{
    return ((SwapChainVal*)&swapChain)->AcquireNextTexture(textureReadyForRender);
}

static Result NRI_CALL SwapChainPresent(SwapChain& swapChain, QueueSemaphore& textureReadyForPresent)
{
    return ((SwapChainVal*)&swapChain)->Present(textureReadyForPresent);
}

static Result NRI_CALL SetSwapChainHdrMetadata(SwapChain& swapChain, const HdrMetadata& hdrMetadata)
{
    return ((SwapChainVal*)&swapChain)->SetHdrMetadata(hdrMetadata);
}

void FillFunctionTableSwapChainVal(SwapChainInterface& swapChainInterface)
{
    swapChainInterface.SetSwapChainDebugName = SetSwapChainDebugName;
    swapChainInterface.GetSwapChainTextures = GetSwapChainTextures;
    swapChainInterface.AcquireNextSwapChainTexture = AcquireNextSwapChainTexture;
    swapChainInterface.SwapChainPresent = SwapChainPresent;
    swapChainInterface.SetSwapChainHdrMetadata = SetSwapChainHdrMetadata;
}

#pragma endregion
