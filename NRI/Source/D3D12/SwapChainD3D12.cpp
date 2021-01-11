/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "SwapChainD3D12.h"
#include "DeviceD3D12.h"
#include "CommandQueueD3D12.h"
#include "TextureD3D12.h"
#include "QueueSemaphoreD3D12.h"

#include <dxgi1_5.h>

using namespace nri;

static std::array<DXGI_FORMAT, 5> g_SwapChainFormat =
{
    DXGI_FORMAT_R8G8B8A8_UNORM,                     // BT709_G10_8BIT,
    DXGI_FORMAT_R16G16B16A16_FLOAT,                 // BT709_G10_16BIT,
    DXGI_FORMAT_R8G8B8A8_UNORM,                     // BT709_G22_8BIT,
    DXGI_FORMAT_R10G10B10A2_UNORM,                  // BT709_G22_10BIT,
    DXGI_FORMAT_R10G10B10A2_UNORM,                  // BT2020_G2084_10BIT
};

static std::array<DXGI_COLOR_SPACE_TYPE, 5> g_ColorSpace =
{
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,        // BT709_G10_8BIT,
    DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,        // BT709_G10_16BIT,
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,        // BT709_G22_8BIT,
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,        // BT709_G22_10BIT,
    DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,     // BT2020_G2084_10BIT
};

static std::array<Format, 5> g_SwapChainTextureFormat =
{
    Format::RGBA8_SRGB,                             // BT709_G10_8BIT,
    Format::RGBA16_SFLOAT,                          // BT709_G10_16BIT,
    Format::RGBA8_UNORM,                            // BT709_G22_8BIT,
    Format::R10_G10_B10_A2_UNORM,                   // BT709_G22_10BIT,
    Format::R10_G10_B10_A2_UNORM,                   // BT2020_G2084_10BIT
};

SwapChainD3D12::SwapChainD3D12(DeviceD3D12& device)
    : m_Device(device)
    , m_Textures(device.GetStdAllocator())
    , m_TexturePointer(device.GetStdAllocator())
{}

Result SwapChainD3D12::Create(const SwapChainDesc& swapChainDesc)
{
    ID3D12Device* device = m_Device;

    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "CreateDXGIFactory2(), error code: 0x%X.", hr);

    ComPtr<IDXGIAdapter> adapter;
    hr = factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter));
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "IDXGIFactory4::EnumAdapterByLuid(), error code: 0x%X.", hr);

    m_IsTearingAllowed = false;
    ComPtr<IDXGIFactory5> dxgiFactory5;
    hr = factory->QueryInterface(IID_PPV_ARGS(&dxgiFactory5));
    if (SUCCEEDED(hr))
    {
        uint32_t tearingSupport = 0;
        hr = dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupport, sizeof(tearingSupport));
        m_IsTearingAllowed = (SUCCEEDED(hr) && tearingSupport) ? true : false;
    }

    CommandQueue* commandQueue;
    if (m_Device.GetCommandQueue(CommandQueueType::GRAPHICS, commandQueue) != Result::SUCCESS)
        return Result::FAILURE;

    CommandQueueD3D12& commandQueueD3D12 = (CommandQueueD3D12&)*commandQueue;

    DXGI_FORMAT format = g_SwapChainFormat[(uint32_t)swapChainDesc.format];
    DXGI_COLOR_SPACE_TYPE colorSpace = g_ColorSpace[(uint32_t)swapChainDesc.format];

    if (!swapChainDesc.windowHandle)
        return Result::INVALID_ARGUMENT;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc1 = {};
    swapChainDesc1.BufferCount = swapChainDesc.textureNum;
    swapChainDesc1.Width = swapChainDesc.width;
    swapChainDesc1.Height = swapChainDesc.height;
    swapChainDesc1.Format = format;
    swapChainDesc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc1.SampleDesc.Count = 1;
    swapChainDesc1.Flags = m_IsTearingAllowed ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    swapChainDesc1.Scaling = DXGI_SCALING_NONE;

    ComPtr<IDXGISwapChain1> swapChain;
    hr = factory->CreateSwapChainForHwnd((ID3D12CommandQueue*)commandQueueD3D12, (HWND)swapChainDesc.windowHandle, &swapChainDesc1, nullptr, nullptr, &swapChain);
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "IDXGIFactory2::CreateSwapChainForHwnd() failed, error code: 0x%X.", hr);

    hr = factory->MakeWindowAssociation((HWND)swapChainDesc.windowHandle, DXGI_MWA_NO_ALT_ENTER);
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "CreateSwapChainForHwnd::MakeWindowAssociation() failed, error code: 0x%X.", hr);

    hr = swapChain->QueryInterface(IID_PPV_ARGS(&m_SwapChain));
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "IDXGISwapChain1::QueryInterface() failed, error code: 0x%X.", hr);

    UINT colorSpaceSupport = 0;
    hr = m_SwapChain->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport);

    if (!(colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
        hr = E_FAIL;

    if (SUCCEEDED(hr))
        hr = m_SwapChain->SetColorSpace1(colorSpace);

    if (FAILED(hr))
        REPORT_ERROR(m_Device.GetLog(), "IDXGISwapChain3::SetColorSpace1() failed, error code: 0x%X.", hr);

    m_SwapChainDesc = swapChainDesc;

    m_Format = g_SwapChainTextureFormat[(uint32_t)swapChainDesc.format];
    for (uint32_t i = 0; i < swapChainDesc.textureNum; i++)
    {
        ComPtr<ID3D12Resource> resource;
        hr = m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&resource));
        if (FAILED(hr))
        {
            REPORT_ERROR(m_Device.GetLog(), "IDXGISwapChain4::GetBuffer() failed, error code: 0x%X.", hr);
            return Result::FAILURE;
        }

        m_Textures.emplace_back(m_Device);
        m_Textures[i].Initialize(resource);
    }

    m_TexturePointer.resize(swapChainDesc.textureNum);
    for (uint32_t i = 0; i < swapChainDesc.textureNum; i++)
        m_TexturePointer[i] = (Texture*)&m_Textures[i];

    m_CommandQueue = (ID3D12CommandQueue*)commandQueueD3D12;

    return Result::SUCCESS;
}

inline void SwapChainD3D12::SetDebugName(const char* name)
{
    SET_D3D_DEBUG_OBJECT_NAME(m_SwapChain, name);
}

inline Texture* const* SwapChainD3D12::GetTextures(uint32_t& textureNum, Format& format) const
{
    textureNum = (uint32_t)m_TexturePointer.size();
    format = m_Format;

    return &m_TexturePointer[0];
}

inline uint32_t SwapChainD3D12::AcquireNextTexture(QueueSemaphore& textureReadyForRender)
{
    ((QueueSemaphoreD3D12&)textureReadyForRender).Signal(m_CommandQueue);

    return m_SwapChain->GetCurrentBackBufferIndex();
}

inline Result SwapChainD3D12::Present(QueueSemaphore& textureReadyForPresent)
{
    ((QueueSemaphoreD3D12&)textureReadyForPresent).Wait(m_CommandQueue);

    UINT flags = (!m_SwapChainDesc.verticalSyncInterval && m_IsTearingAllowed) ? DXGI_PRESENT_ALLOW_TEARING : 0;

    const HRESULT result = m_SwapChain->Present(m_SwapChainDesc.verticalSyncInterval, flags);
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), result, "Can't present the swapchain: IDXGISwapChain::Present() returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

inline Result SwapChainD3D12::SetHdrMetadata(const HdrMetadata& hdrMetadata)
{
    DXGI_HDR_METADATA_HDR10 data = {};
    data.RedPrimary[0] = uint16_t(hdrMetadata.displayPrimaryRed[0] * 50000.0f);
    data.RedPrimary[1] = uint16_t(hdrMetadata.displayPrimaryRed[1] * 50000.0f);
    data.GreenPrimary[0] = uint16_t(hdrMetadata.displayPrimaryGreen[0] * 50000.0f);
    data.GreenPrimary[1] = uint16_t(hdrMetadata.displayPrimaryGreen[1] * 50000.0f);
    data.BluePrimary[0] = uint16_t(hdrMetadata.displayPrimaryBlue[0] * 50000.0f);
    data.BluePrimary[1] = uint16_t(hdrMetadata.displayPrimaryBlue[1] * 50000.0f);
    data.WhitePoint[0] = uint16_t(hdrMetadata.whitePoint[0] * 50000.0f);
    data.WhitePoint[1] = uint16_t(hdrMetadata.whitePoint[1] * 50000.0f);
    data.MaxMasteringLuminance = uint32_t(hdrMetadata.maxLuminance);
    data.MinMasteringLuminance = uint32_t(hdrMetadata.minLuminance);
    data.MaxContentLightLevel = uint16_t(hdrMetadata.maxContentLightLevel);
    data.MaxFrameAverageLightLevel = uint16_t(hdrMetadata.maxFrameAverageLightLevel);

    HRESULT hr = m_SwapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &data);
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "IDXGISwapChain4::SetHDRMetaData() failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    return Result::SUCCESS;
}

#include "SwapChainD3D12.hpp"
