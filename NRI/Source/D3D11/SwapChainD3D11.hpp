/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  SwapChainInterface  ]

static void NRI_CALL SetSwapChainDebugName(SwapChain& swapChain, const char* name)
{
    ((SwapChainD3D11&)swapChain).SetDebugName(name);
}

static Texture* const* NRI_CALL GetSwapChainTextures(const SwapChain& swapChain, uint32_t& textureNum, Format& format)
{
    return ((SwapChainD3D11&)swapChain).GetTextures(textureNum, format);
}

static uint32_t NRI_CALL AcquireNextSwapChainTexture(SwapChain& swapChain, QueueSemaphore& textureReadyForRender)
{
    return ((SwapChainD3D11&)swapChain).AcquireNextTexture(textureReadyForRender);
}

static Result NRI_CALL SwapChainPresent(SwapChain& swapChain, QueueSemaphore& textureReadyForPresent)
{
    return ((SwapChainD3D11&)swapChain).Present(textureReadyForPresent);
}

static Result NRI_CALL SetSwapChainHdrMetadata(SwapChain& swapChain, const HdrMetadata& hdrMetadata)
{
    return ((SwapChainD3D11&)swapChain).SetHdrMetadata(hdrMetadata);
}

void FillFunctionTableSwapChainD3D11(SwapChainInterface& table)
{
    table.SetSwapChainDebugName = SetSwapChainDebugName;
    table.GetSwapChainTextures = GetSwapChainTextures;
    table.AcquireNextSwapChainTexture = AcquireNextSwapChainTexture;
    table.SwapChainPresent = SwapChainPresent;
    table.SetSwapChainHdrMetadata = SetSwapChainHdrMetadata;
}

#pragma endregion
