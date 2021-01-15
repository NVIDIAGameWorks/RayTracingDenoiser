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
#include "DeviceVal.h"
#include "SharedVal.h"
#include "SwapChainVal.h"

#include "QueueSemaphoreVal.h"
#include "TextureVal.h"

using namespace nri;

SwapChainVal::SwapChainVal(DeviceVal& device, SwapChain& swapChain, const SwapChainDesc& swapChainDesc) :
    DeviceObjectVal(device, swapChain),
    m_SwapChainAPI(device.GetSwapChainInterface()),
    m_SwapChainDesc(swapChainDesc),
    m_Textures(device.GetStdAllocator())
{
}

SwapChainVal::~SwapChainVal()
{
    for (size_t i = 0; i < m_Textures.size(); i++)
        Deallocate(m_Device.GetStdAllocator(), m_Textures[i]);
}

inline void SwapChainVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_SwapChainAPI.SetSwapChainDebugName(m_ImplObject, name);
}

inline Texture* const* SwapChainVal::GetTextures(uint32_t& textureNum, Format& format) const
{
    Texture* const* textures = m_SwapChainAPI.GetSwapChainTextures(m_ImplObject, textureNum, format);

    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::TEXTURE_2D;
    textureDesc.usageMask = TextureUsageBits::SHADER_RESOURCE | TextureUsageBits::COLOR_ATTACHMENT;
    textureDesc.format = format;
    textureDesc.size[0] = m_SwapChainDesc.width;
    textureDesc.size[1] = m_SwapChainDesc.height;
    textureDesc.size[2] = 1;
    textureDesc.mipNum = 1;
    textureDesc.arraySize = 1;
    textureDesc.sampleNum = 1;
    textureDesc.physicalDeviceMask = 0;

    m_Textures.resize(textureNum);
    for (uint32_t i = 0; i < textureNum; i++)
        m_Textures[i] = Allocate<TextureVal>(m_Device.GetStdAllocator(), m_Device, *textures[i], textureDesc);

    return (Texture* const*)m_Textures.data();
}

inline uint32_t SwapChainVal::AcquireNextTexture(QueueSemaphore& textureReadyForRender)
{
    ((QueueSemaphoreVal*)&textureReadyForRender)->Signal();
    QueueSemaphore* queueSemaphoreImpl = NRI_GET_IMPL(QueueSemaphore, &textureReadyForRender);

    return m_SwapChainAPI.AcquireNextSwapChainTexture(m_ImplObject, *queueSemaphoreImpl);
}

inline Result SwapChainVal::Present(QueueSemaphore& textureReadyForPresent)
{
    ((QueueSemaphoreVal*)&textureReadyForPresent)->Wait();
    QueueSemaphore* queueSemaphoreImpl = NRI_GET_IMPL(QueueSemaphore, &textureReadyForPresent);

    return m_SwapChainAPI.SwapChainPresent(m_ImplObject, *queueSemaphoreImpl);
}

inline Result SwapChainVal::SetHdrMetadata(const HdrMetadata& hdrMetadata)
{
    return m_SwapChainAPI.SetSwapChainHdrMetadata(m_ImplObject, hdrMetadata);
}

#include "SwapChainVal.hpp"
