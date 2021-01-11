/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedD3D11.h"
#include "SwapChainD3D11.h"

#include "TextureD3D11.h"
#include "QueueSemaphoreD3D11.h"

#include <dxgi1_5.h>

using namespace nri;

static std::array<DXGI_FORMAT, 5> g_swapChainFormat =
{
    DXGI_FORMAT_R8G8B8A8_UNORM,                     // BT709_G10_8BIT,
    DXGI_FORMAT_R16G16B16A16_FLOAT,                 // BT709_G10_16BIT,
    DXGI_FORMAT_R8G8B8A8_UNORM,                     // BT709_G22_8BIT,
    DXGI_FORMAT_R10G10B10A2_UNORM,                  // BT709_G22_10BIT,
    DXGI_FORMAT_R10G10B10A2_UNORM,                  // BT2020_G2084_10BIT
};

static std::array<DXGI_COLOR_SPACE_TYPE, 5> g_colorSpace =
{
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,        // BT709_G10_8BIT,
    DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,        // BT709_G10_16BIT,
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,        // BT709_G22_8BIT,
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,        // BT709_G22_10BIT,
    DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,     // BT2020_G2084_10BIT
};

static std::array<Format, 5> g_swapChainTextureFormat =
{
    Format::RGBA8_SRGB,                             // BT709_G10_8BIT,
    Format::RGBA16_SFLOAT,                          // BT709_G10_16BIT,
    Format::RGBA8_UNORM,                            // BT709_G22_8BIT,
    Format::R10_G10_B10_A2_UNORM,                   // BT709_G22_10BIT,
    Format::R10_G10_B10_A2_UNORM,                   // BT2020_G2084_10BIT
};

SwapChainD3D11::SwapChainD3D11(DeviceD3D11& device) :
    m_Device(device),
    m_RenderTargets(device.GetStdAllocator()),
    m_RenderTargetPointers(device.GetStdAllocator())
{
}

SwapChainD3D11::~SwapChainD3D11()
{
}

Result SwapChainD3D11::Create(const VersionedDevice& device, const SwapChainDesc& swapChainDesc)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "IUnknown::QueryInterface() - FAILED!");

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "IDXGIDevice::GetAdapter() - FAILED!");

    ComPtr<IDXGIFactory2> dxgiFactory2;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory2));
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "IDXGIObject::GetParent() - FAILED!");

    m_IsTearingAllowed = false;
    ComPtr<IDXGIFactory5> dxgiFactory5;
    hr = dxgiFactory2->QueryInterface(IID_PPV_ARGS(&dxgiFactory5));
    if (SUCCEEDED(hr))
    {
        uint32_t tearingSupport = 0;
        hr = dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupport, sizeof(tearingSupport));
        m_IsTearingAllowed = (SUCCEEDED(hr) && tearingSupport) ? true : false;
    }

    DXGI_FORMAT format = g_swapChainFormat[(uint32_t)swapChainDesc.format];
    DXGI_COLOR_SPACE_TYPE colorSpace = g_colorSpace[(uint32_t)swapChainDesc.format];

    const HWND hwnd = (HWND)swapChainDesc.windowHandle;

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferCount = swapChainDesc.textureNum;
    desc.Width = swapChainDesc.width;
    desc.Height = swapChainDesc.height;
    desc.Format = format;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc.Count = 1;
    desc.Flags = m_IsTearingAllowed ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain;
    hr = dxgiFactory2->CreateSwapChainForHwnd(device.ptr, hwnd, &desc, nullptr, nullptr, &swapChain);
    if (FAILED(hr))
    {
        // are we on Win7?
        desc.Scaling = DXGI_SCALING_STRETCH;
        hr = dxgiFactory2->CreateSwapChainForHwnd(device.ptr, hwnd, &desc, nullptr, nullptr, &swapChain);
    }
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "IDXGIFactory2::CreateSwapChainForHwnd() - FAILED!");

    hr = dxgiFactory2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "IDXGIFactory::MakeWindowAssociation() - FAILED!");

    hr = swapChain->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&m_SwapChain.ptr);
    m_SwapChain.version = 4;
    if (FAILED(hr))
    {
        REPORT_WARNING(m_Device.GetLog(), "QueryInterface(IDXGISwapChain4) - FAILED!");
        hr = device->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_SwapChain.ptr);
        m_SwapChain.version = 3;
        if (FAILED(hr))
        {
            REPORT_WARNING(m_Device.GetLog(), "QueryInterface(IDXGISwapChain3) - FAILED!");
            hr = device->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&m_SwapChain.ptr);
            m_SwapChain.version = 2;
            if (FAILED(hr))
            {
                REPORT_WARNING(m_Device.GetLog(), "QueryInterface(IDXGISwapChain2) - FAILED!");
                hr = device->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&m_SwapChain.ptr);
                m_SwapChain.version = 1;
                if (FAILED(hr))
                {
                    REPORT_WARNING(m_Device.GetLog(), "QueryInterface(IDXGISwapChain1) - FAILED!");
                    m_SwapChain.ptr = (IDXGISwapChain4*)swapChain.GetInterface();
                    m_SwapChain.version = 0;
                }
            }
        }
    }

    if (m_SwapChain.version >= 3)
    {
        UINT colorSpaceSupport = 0;
        hr = m_SwapChain->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport);

        if ( !(colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) )
            hr = E_FAIL;

        if (SUCCEEDED(hr))
            hr = m_SwapChain->SetColorSpace1(colorSpace);

        if (FAILED(hr))
           REPORT_WARNING(m_Device.GetLog(), "IDXGISwapChain3::SetColorSpace1() - FAILED!");
    }
    else
        REPORT_ERROR(m_Device.GetLog(), "IDXGISwapChain3::SetColorSpace1() is not supported by the OS!");

    if (m_SwapChain.version >= 1)
    {
        DXGI_RGBA color = {};
        hr = m_SwapChain->SetBackgroundColor(&color);
        if (FAILED(hr))
            REPORT_WARNING(m_Device.GetLog(), "IDXGISwapChain1::SetBackgroundColor() - FAILED!");
    }

    // in DX11 only 'bufferIndex = 0' can be used to create render targets, so set BufferCount to '1' and ignore 'desc.BufferCount'
    const uint32_t bufferCount = 1;

    m_RenderTargets.resize(bufferCount);
    m_RenderTargetPointers.clear();

    for (uint32_t i = 0; i < bufferCount; i++)
    {
        ComPtr<ID3D11Texture2D> backBuffer;
        hr = m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
        RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "IDXGISwapChain::GetBuffer() - FAILED!");

        TextureD3D11Desc textureDesc = {};
        textureDesc.d3d11Resource = backBuffer;
        m_RenderTargets[i].Create(m_Device, textureDesc);

        m_RenderTargetPointers.push_back((Texture*)&m_RenderTargets[i]);
    }

    m_Format = g_swapChainTextureFormat[(uint32_t)swapChainDesc.format];

    m_SwapChainDesc = swapChainDesc;
    m_SwapChainDesc.textureNum = bufferCount;

    return Result::SUCCESS;
}

inline void SwapChainD3D11::SetDebugName(const char* name)
{
    SetName(m_SwapChain.ptr, name);
}

inline Texture* const* SwapChainD3D11::GetTextures(uint32_t& textureNum, Format& format) const
{
    textureNum = m_SwapChainDesc.textureNum;
    format = m_Format;

    return &m_RenderTargetPointers[0];
}

inline uint32_t SwapChainD3D11::AcquireNextTexture(QueueSemaphore& textureReadyForRender)
{
    ((QueueSemaphoreD3D11&)textureReadyForRender).Signal();

    uint32_t nextTextureIndex = 0;
    if (m_SwapChain.version >= 3)
        nextTextureIndex = m_SwapChain->GetCurrentBackBufferIndex();

    return nextTextureIndex;
}

inline Result SwapChainD3D11::Present(QueueSemaphore& textureReadyForPresent)
{
    ((QueueSemaphoreD3D11&)textureReadyForPresent).Wait();

    UINT flags = (!m_SwapChainDesc.verticalSyncInterval && m_IsTearingAllowed) ? DXGI_PRESENT_ALLOW_TEARING : 0;

    const HRESULT result = m_SwapChain->Present(m_SwapChainDesc.verticalSyncInterval, flags);

    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), result, "Can't present the swapchain: IDXGISwapChain::Present() returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

inline Result SwapChainD3D11::SetHdrMetadata(const HdrMetadata& hdrMetadata)
{
    if (m_SwapChain.version < 4)
        return Result::UNSUPPORTED;

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
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "IDXGISwapChain4::SetHDRMetaData() - FAILED");

    return Result::SUCCESS;
}

#include "SwapChainD3D11.hpp"
