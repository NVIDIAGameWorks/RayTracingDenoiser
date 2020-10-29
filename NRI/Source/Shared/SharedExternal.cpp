/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"

#include <cstdarg>
#include <windows.h>

constexpr std::array<const char*, uint32_t(nri::Message::TYPE_ERROR) + 1> MESSAGE_TYPE_NAME =
{
    "INFO",
    "WARNING",
    "ERROR"
};

constexpr std::array<const char*, uint32_t(nri::GraphicsAPI::VULKAN) + 1> GRAPHICS_API_NAME =
{
    "D3D11",
    "D3D12",
    "VULKAN"
};

Log::Log(nri::GraphicsAPI graphicsAPI, const nri::CallbackInterface& callbackInterface) :
    m_GraphicsAPI(graphicsAPI),
    m_CallbackInterface(callbackInterface)
{
}

void Log::ReportMessage(nri::Message message, const char* format, ...) const
{
    const char* messageTypeName = MESSAGE_TYPE_NAME[(size_t)message];
    const char* graphicsAPIName = GRAPHICS_API_NAME[(size_t)m_GraphicsAPI];

    char buffer[4096];
    int written = snprintf(buffer, GetCountOf(buffer), "[nri(%s)::%s] -- ", graphicsAPIName, messageTypeName);

    va_list	argptr;
    va_start(argptr, format);
    written += vsnprintf(buffer + written, GetCountOf(buffer) - written, format, argptr);
    va_end(argptr);

    const int end = std::min(written, (int)GetCountOf(buffer) - 2);
    buffer[end] = '\n';
    buffer[end + 1] = '\0';

    if (m_CallbackInterface.MessageCallback != nullptr)
        m_CallbackInterface.MessageCallback(m_CallbackInterface.userArg, buffer, message);

    if (message == nri::Message::TYPE_ERROR && m_CallbackInterface.AbortExecution != nullptr)
        m_CallbackInterface.AbortExecution(m_CallbackInterface.userArg);
}

void ConvertCharToWchar(const char* in, wchar_t* out, size_t outLength)
{
    if (outLength == 0)
        return;

    for (size_t i = 0; i < outLength - 1 && *in; i++)
        *out++ = *in++;

    *out = 0;
}

void ConvertWcharToChar(const wchar_t* in, char* out, size_t outLength)
{
    if (outLength == 0)
        return;

    for (size_t i = 0; i < outLength - 1 && *in; i++)
        *out++ = char(*in++);

    *out = 0;
}

nri::Result GetResultFromHRESULT(long result)
{
    if (SUCCEEDED(result))
        return nri::Result::SUCCESS;

    if (result == E_INVALIDARG || result == E_POINTER || result == E_HANDLE)
        return nri::Result::INVALID_ARGUMENT;

    if (result == DXGI_ERROR_UNSUPPORTED)
        return nri::Result::UNSUPPORTED;

    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
        return nri::Result::DEVICE_LOST;

    if (result == E_OUTOFMEMORY)
        return nri::Result::OUT_OF_MEMORY;

    return nri::Result::FAILURE;
}

static constexpr std::array<uint32_t, (size_t)nri::Format::MAX_NUM> TEXEL_BLOCK_WIDTH = {
    0, // UNKNOWN

    1, // R8_UNORM
    1, // R8_SNORM
    1, // R8_UINT
    1, // R8_SINT

    1, // RG8_UNORM
    1, // RG8_SNORM
    1, // RG8_UINT
    1, // RG8_SINT

    1, // BGRA8_UNORM

    1, // RGBA8_UNORM
    1, // RGBA8_SNORM
    1, // RGBA8_UINT
    1, // RGBA8_SINT
    1, // RGBA8_SRGB

    1, // R16_UNORM
    1, // R16_SNORM
    1, // R16_UINT
    1, // R16_SINT
    1, // R16_SFLOAT

    1, // RG16_UNORM
    1, // RG16_SNORM
    1, // RG16_UINT
    1, // RG16_SINT
    1, // RG16_SFLOAT

    1, // RGBA16_UNORM
    1, // RGBA16_SNORM
    1, // RGBA16_UINT
    1, // RGBA16_SINT
    1, // RGBA16_SFLOAT

    1, // R32_UINT
    1, // R32_SINT
    1, // R32_SFLOAT

    1, // RG32_UINT
    1, // RG32_SINT
    1, // RG32_SFLOAT

    1, // RGB32_UINT
    1, // RGB32_SINT
    1, // RGB32_SFLOAT

    1, // RGBA32_UINT
    1, // RGBA32_SINT
    1, // RGBA32_SFLOAT

    1, // R10_G10_B10_A2_UNORM
    1, // R10_G10_B10_A2_UINT
    1, // R11_G11_B10_UFLOAT
    1, // R9_G9_B9_E5_UFLOAT

    4, // BC1_RGBA_UNORM
    4, // BC1_RGBA_SRGB
    4, // BC2_RGBA_UNORM
    4, // BC2_RGBA_SRGB
    4, // BC3_RGBA_UNORM
    4, // BC3_RGBA_SRGB
    4, // BC4_R_UNORM
    4, // BC4_R_SNORM
    4, // BC5_RG_UNORM
    4, // BC5_RG_SNORM
    4, // BC6H_RGB_UFLOAT
    4, // BC6H_RGB_SFLOAT
    4, // BC7_RGBA_UNORM
    4, // BC7_RGBA_SRGB

    // DEPTH_STENCIL_ATTACHMENT views
    1, // D16_UNORM
    1, // D24_UNORM_S8_UINT
    1, // D32_SFLOAT
    1, // D32_SFLOAT_S8_UINT_X24

    // Depth-stencil specific SHADER_RESOURCE views
    0, // R24_UNORM_X8
    0, // X24_R8_UINT
    0, // X32_R8_UINT_X24
    0, // R32_SFLOAT_X8_X24
};

uint32_t GetTexelBlockWidth(nri::Format format)
{
    return TEXEL_BLOCK_WIDTH[(size_t)format];
}

static constexpr std::array<uint32_t, (size_t)nri::Format::MAX_NUM> TEXEL_BLOCK_SIZE = {
    0, // UNKNOWN

    1, // R8_UNORM
    1, // R8_SNORM
    1, // R8_UINT
    1, // R8_SINT

    2, // RG8_UNORM
    2, // RG8_SNORM
    2, // RG8_UINT
    2, // RG8_SINT

    4, // BGRA8_UNORM

    4, // RGBA8_UNORM
    4, // RGBA8_SNORM
    4, // RGBA8_UINT
    4, // RGBA8_SINT
    4, // RGBA8_SRGB

    2, // R16_UNORM
    2, // R16_SNORM
    2, // R16_UINT
    2, // R16_SINT
    2, // R16_SFLOAT

    4, // RG16_UNORM
    4, // RG16_SNORM
    4, // RG16_UINT
    4, // RG16_SINT
    4, // RG16_SFLOAT

    8, // RGBA16_UNORM
    8, // RGBA16_SNORM
    8, // RGBA16_UINT
    8, // RGBA16_SINT
    8, // RGBA16_SFLOAT

    4, // R32_UINT
    4, // R32_SINT
    4, // R32_SFLOAT

    8, // RG32_UINT
    8, // RG32_SINT
    8, // RG32_SFLOAT

    12, // RGB32_UINT
    12, // RGB32_SINT
    12, // RGB32_SFLOAT

    16, // RGBA32_UINT
    16, // RGBA32_SINT
    16, // RGBA32_SFLOAT

    4, // R10_G10_B10_A2_UNORM
    4, // R10_G10_B10_A2_UINT
    4, // R11_G11_B10_UFLOAT
    4, // R9_G9_B9_E5_UFLOAT

    8, // BC1_RGBA_UNORM
    8, // BC1_RGBA_SRGB
    16, // BC2_RGBA_UNORM
    16, // BC2_RGBA_SRGB
    16, // BC3_RGBA_UNORM
    16, // BC3_RGBA_SRGB
    8, // BC4_R_UNORM
    8, // BC4_R_SNORM
    16, // BC5_RG_UNORM
    16, // BC5_RG_SNORM
    16, // BC6H_RGB_UFLOAT
    16, // BC6H_RGB_SFLOAT
    16, // BC7_RGBA_UNORM
    16, // BC7_RGBA_SRGB

    // DEPTH_STENCIL_ATTACHMENT views
    2, // D16_UNORM
    4, // D24_UNORM_S8_UINT
    4, // D32_SFLOAT
    8, // D32_SFLOAT_S8_UINT_X24

    // Depth-stencil specific SHADER_RESOURCE views
    0, // R24_UNORM_X8
    0, // X24_R8_UINT
    0, // X32_R8_UINT_X24
    0, // R32_SFLOAT_X8_X24
};

uint32_t GetTexelBlockSize(nri::Format format)
{
    return TEXEL_BLOCK_SIZE[(size_t)format];
}

static constexpr std::array<nri::Format, 100> DXGI_FORMAT_TABLE =
{
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_UNKNOWN = 0,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R32G32B32A32_TYPELESS = 1,
    nri::Format::RGBA32_SFLOAT,                        // DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
    nri::Format::RGBA32_UINT,                          // DXGI_FORMAT_R32G32B32A32_UINT = 3,
    nri::Format::RGBA32_SINT,                          // DXGI_FORMAT_R32G32B32A32_SINT = 4,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R32G32B32_TYPELESS = 5,
    nri::Format::RGB32_SFLOAT,                         // DXGI_FORMAT_R32G32B32_FLOAT = 6,
    nri::Format::RGB32_UINT,                           // DXGI_FORMAT_R32G32B32_UINT = 7,
    nri::Format::RGB32_SINT,                           // DXGI_FORMAT_R32G32B32_SINT = 8,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R16G16B16A16_TYPELESS = 9,
    nri::Format::RGBA16_SFLOAT,                        // DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
    nri::Format::RGBA16_UNORM,                         // DXGI_FORMAT_R16G16B16A16_UNORM = 11,
    nri::Format::RGBA16_UINT,                          // DXGI_FORMAT_R16G16B16A16_UINT = 12,
    nri::Format::RGBA16_SNORM,                         // DXGI_FORMAT_R16G16B16A16_SNORM = 13,
    nri::Format::RGBA16_SINT,                          // DXGI_FORMAT_R16G16B16A16_SINT = 14,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R32G32_TYPELESS = 15,
    nri::Format::RG32_SFLOAT,                          // DXGI_FORMAT_R32G32_FLOAT = 16,
    nri::Format::RG32_UINT,                            // DXGI_FORMAT_R32G32_UINT = 17,
    nri::Format::RGB32_SINT,                           // DXGI_FORMAT_R32G32_SINT = 18,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R32G8X24_TYPELESS = 19,
    nri::Format::D32_SFLOAT_S8_UINT_X24,               // DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20,
    nri::Format::R32_SFLOAT_X8_X24,                    // DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
    nri::Format::X32_R8_UINT_X24,                      // DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = 22,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R10G10B10A2_TYPELESS = 23,
    nri::Format::R10_G10_B10_A2_UNORM,                 // DXGI_FORMAT_R10G10B10A2_UNORM = 24,
    nri::Format::R10_G10_B10_A2_UINT,                  // DXGI_FORMAT_R10G10B10A2_UINT = 25,
    nri::Format::R11_G11_B10_UFLOAT,                   // DXGI_FORMAT_R11G11B10_FLOAT = 26,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R8G8B8A8_TYPELESS = 27,
    nri::Format::RGBA8_UNORM,                          // DXGI_FORMAT_R8G8B8A8_UNORM = 28,
    nri::Format::RGBA8_SRGB,                           // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
    nri::Format::RGBA8_UINT,                           // DXGI_FORMAT_R8G8B8A8_UINT = 30,
    nri::Format::RGBA8_SNORM,                          // DXGI_FORMAT_R8G8B8A8_SNORM = 31,
    nri::Format::RGBA8_SINT,                           // DXGI_FORMAT_R8G8B8A8_SINT = 32,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R16G16_TYPELESS = 33,
    nri::Format::RG16_SFLOAT,                          // DXGI_FORMAT_R16G16_FLOAT = 34,
    nri::Format::RG16_UNORM,                           // DXGI_FORMAT_R16G16_UNORM = 35,
    nri::Format::RG16_UINT,                            // DXGI_FORMAT_R16G16_UINT = 36,
    nri::Format::RG16_SNORM,                           // DXGI_FORMAT_R16G16_SNORM = 37,
    nri::Format::RG16_SINT,                            // DXGI_FORMAT_R16G16_SINT = 38,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R32_TYPELESS = 39,
    nri::Format::D32_SFLOAT,                           // DXGI_FORMAT_D32_FLOAT = 40,
    nri::Format::R32_SFLOAT,                           // DXGI_FORMAT_R32_FLOAT = 41,
    nri::Format::R32_UINT,                             // DXGI_FORMAT_R32_UINT = 42,
    nri::Format::R32_SINT,                             // DXGI_FORMAT_R32_SINT = 43,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R24G8_TYPELESS = 44,
    nri::Format::D24_UNORM_S8_UINT,                    // DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
    nri::Format::R24_UNORM_X8,                         // DXGI_FORMAT_R24_UNORM_X8_TYPELESS = 46,
    nri::Format::X24_R8_UINT,                          // DXGI_FORMAT_X24_TYPELESS_G8_UINT = 47,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R8G8_TYPELESS = 48,
    nri::Format::RG8_UNORM,                            // DXGI_FORMAT_R8G8_UNORM = 49,
    nri::Format::RG8_UINT,                             // DXGI_FORMAT_R8G8_UINT = 50,
    nri::Format::RG8_SNORM,                            // DXGI_FORMAT_R8G8_SNORM = 51,
    nri::Format::RG8_SINT,                             // DXGI_FORMAT_R8G8_SINT = 52,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R16_TYPELESS = 53,
    nri::Format::R16_SFLOAT,                           // DXGI_FORMAT_R16_FLOAT = 54,
    nri::Format::D16_UNORM,                            // DXGI_FORMAT_D16_UNORM = 55,
    nri::Format::R16_UNORM,                            // DXGI_FORMAT_R16_UNORM = 56,
    nri::Format::R16_UINT,                             // DXGI_FORMAT_R16_UINT = 57,
    nri::Format::R16_SNORM,                            // DXGI_FORMAT_R16_SNORM = 58,
    nri::Format::R16_SINT,                             // DXGI_FORMAT_R16_SINT = 59,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R8_TYPELESS = 60,
    nri::Format::R8_UNORM,                             // DXGI_FORMAT_R8_UNORM = 61,
    nri::Format::R8_UINT,                              // DXGI_FORMAT_R8_UINT = 62,
    nri::Format::R8_SNORM,                             // DXGI_FORMAT_R8_SNORM = 63,
    nri::Format::R8_SINT,                              // DXGI_FORMAT_R8_SINT = 64,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_A8_UNORM = 65,

    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R1_UNORM = 66,
    nri::Format::R9_G9_B9_E5_UFLOAT,                   // DXGI_FORMAT_R9G9B9E5_SHAREDEXP = 67,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R8G8_B8G8_UNORM = 68,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_G8R8_G8B8_UNORM = 69,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_BC1_TYPELESS = 70,
    nri::Format::BC1_RGBA_UNORM,                       // DXGI_FORMAT_BC1_UNORM = 71,
    nri::Format::BC1_RGBA_SRGB,                        // DXGI_FORMAT_BC1_UNORM_SRGB = 72,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_BC2_TYPELESS = 73,
    nri::Format::BC2_RGBA_UNORM,                       // DXGI_FORMAT_BC2_UNORM = 74,
    nri::Format::BC2_RGBA_SRGB,                        // DXGI_FORMAT_BC2_UNORM_SRGB = 75,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_BC3_TYPELESS = 76,
    nri::Format::BC3_RGBA_UNORM,                       // DXGI_FORMAT_BC3_UNORM = 77,
    nri::Format::BC3_RGBA_SRGB,                        // DXGI_FORMAT_BC3_UNORM_SRGB = 78,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_BC4_TYPELESS = 79,
    nri::Format::BC4_R_UNORM,                          // DXGI_FORMAT_BC4_UNORM = 80,
    nri::Format::BC4_R_SNORM,                          // DXGI_FORMAT_BC4_SNORM = 81,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_BC5_TYPELESS = 82,
    nri::Format::BC5_RG_UNORM,                         // DXGI_FORMAT_BC5_UNORM = 83,
    nri::Format::BC5_RG_SNORM,                         // DXGI_FORMAT_BC5_SNORM = 84,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_B5G6R5_UNORM = 85,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_B5G5R5A1_UNORM = 86,
    nri::Format::BGRA8_UNORM,                          // DXGI_FORMAT_B8G8R8A8_UNORM = 87,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_B8G8R8X8_UNORM = 88,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_B8G8R8A8_TYPELESS = 90,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_B8G8R8X8_TYPELESS = 92,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = 93,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_BC6H_TYPELESS = 94,
    nri::Format::BC6H_RGB_UFLOAT,                      // DXGI_FORMAT_BC6H_UF16 = 95,
    nri::Format::BC6H_RGB_SFLOAT,                      // DXGI_FORMAT_BC6H_SF16 = 96,
    nri::Format::UNKNOWN,                              // DXGI_FORMAT_BC7_TYPELESS = 97,
    nri::Format::BC7_RGBA_UNORM,                       // DXGI_FORMAT_BC7_UNORM = 98,
    nri::Format::BC7_RGBA_SRGB,                        // DXGI_FORMAT_BC7_UNORM_SRGB = 99,
};

nri::Format GetFormat(uint32_t dxgiFormat)
{
    return DXGI_FORMAT_TABLE[dxgiFormat];
}

nri::Format GetFormatDXGI(uint32_t dxgiFormat)
{
    return GetFormat(dxgiFormat);
}

static void MessageCallback(void* userArg, const char* message, nri::Message messageType)
{
    OutputDebugStringA(message);
}

static void AbortExecution(void* userArg)
{
#if _DEBUG
    __debugbreak();
#endif
}

void CheckAndSetDefaultCallbacks(nri::CallbackInterface& callbackInterface)
{
    if (callbackInterface.MessageCallback == nullptr)
        callbackInterface.MessageCallback = MessageCallback;

    if (callbackInterface.AbortExecution == nullptr)
        callbackInterface.AbortExecution = AbortExecution;
}