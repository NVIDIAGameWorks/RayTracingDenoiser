/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedD3D11.h"

#include "NVAPI/nvapi.h"
#include "amd_ags.h"

static constexpr std::array<FormatInfo, (size_t)nri::Format::MAX_NUM> FORMAT_INFO_TABLE =
{{
    {DXGI_FORMAT_UNKNOWN,                       DXGI_FORMAT_UNKNOWN,                         0,                     false}, // UNKNOWN,

    {DXGI_FORMAT_R8_TYPELESS,                   DXGI_FORMAT_R8_UNORM,                        sizeof(uint8_t),       false}, // R8_UNORM,
    {DXGI_FORMAT_R8_TYPELESS,                   DXGI_FORMAT_R8_SNORM,                        sizeof(int8_t),        false}, // R8_SNORM,
    {DXGI_FORMAT_R8_TYPELESS,                   DXGI_FORMAT_R8_UINT,                         sizeof(uint8_t),       true},  // R8_UINT,
    {DXGI_FORMAT_R8_TYPELESS,                   DXGI_FORMAT_R8_SINT,                         sizeof(int8_t),        true},  // R8_SINT,

    {DXGI_FORMAT_R8G8_TYPELESS,                 DXGI_FORMAT_R8G8_UNORM,                      sizeof(uint8_t) * 2,   false}, // RG8_UNORM,
    {DXGI_FORMAT_R8G8_TYPELESS,                 DXGI_FORMAT_R8G8_SNORM,                      sizeof(int8_t) * 2,    false}, // RG8_SNORM,
    {DXGI_FORMAT_R8G8_TYPELESS,                 DXGI_FORMAT_R8G8_UINT,                       sizeof(uint8_t) * 2,   true},  // RG8_UINT,
    {DXGI_FORMAT_R8G8_TYPELESS,                 DXGI_FORMAT_R8G8_SINT,                       sizeof(int8_t) * 2,    true},  // RG8_SINT,

    {DXGI_FORMAT_B8G8R8A8_TYPELESS,             DXGI_FORMAT_B8G8R8A8_UNORM,                  sizeof(uint8_t) * 4,   false}, // BGRA8_UNORM,
    {DXGI_FORMAT_B8G8R8A8_TYPELESS,             DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,             sizeof(uint8_t) * 4,   false}, // BGRA8_SRGB,

    {DXGI_FORMAT_R8G8B8A8_TYPELESS,             DXGI_FORMAT_R8G8B8A8_UNORM,                  sizeof(uint8_t) * 4,   false}, // RGBA8_UNORM,
    {DXGI_FORMAT_R8G8B8A8_TYPELESS,             DXGI_FORMAT_R8G8B8A8_SNORM,                  sizeof(int8_t) * 4,    false}, // RGBA8_SNORM,
    {DXGI_FORMAT_R8G8B8A8_TYPELESS,             DXGI_FORMAT_R8G8B8A8_UINT,                   sizeof(uint8_t) * 4,   true},  // RGBA8_UINT,
    {DXGI_FORMAT_R8G8B8A8_TYPELESS,             DXGI_FORMAT_R8G8B8A8_SINT,                   sizeof(int8_t) * 4,    true},  // RGBA8_SINT,
    {DXGI_FORMAT_R8G8B8A8_TYPELESS,             DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,             sizeof(uint8_t) * 4,   false}, // RGBA8_SRGB,

    {DXGI_FORMAT_R16_TYPELESS,                  DXGI_FORMAT_R16_UNORM,                       sizeof(uint16_t),      false}, // R16_UNORM,
    {DXGI_FORMAT_R16_TYPELESS,                  DXGI_FORMAT_R16_SNORM,                       sizeof(int16_t),       false}, // R16_SNORM,
    {DXGI_FORMAT_R16_TYPELESS,                  DXGI_FORMAT_R16_UINT,                        sizeof(uint16_t),      true},  // R16_UINT,
    {DXGI_FORMAT_R16_TYPELESS,                  DXGI_FORMAT_R16_SINT,                        sizeof(int16_t),       true},  // R16_SINT,
    {DXGI_FORMAT_R16_TYPELESS,                  DXGI_FORMAT_R16_FLOAT,                       sizeof(uint16_t),      false}, // R16_SFLOAT,

    {DXGI_FORMAT_R16G16_TYPELESS,               DXGI_FORMAT_R16G16_UNORM,                    sizeof(uint16_t) * 2,  false}, // RG16_UNORM,
    {DXGI_FORMAT_R16G16_TYPELESS,               DXGI_FORMAT_R16G16_SNORM,                    sizeof(int16_t) * 2,   false}, // RG16_SNORM,
    {DXGI_FORMAT_R16G16_TYPELESS,               DXGI_FORMAT_R16G16_UINT,                     sizeof(uint16_t) * 2,  true},  // RG16_UINT,
    {DXGI_FORMAT_R16G16_TYPELESS,               DXGI_FORMAT_R16G16_SINT,                     sizeof(int16_t) * 2,   true},  // RG16_SINT,
    {DXGI_FORMAT_R16G16_TYPELESS,               DXGI_FORMAT_R16G16_FLOAT,                    sizeof(uint16_t) * 2,  false}, // RG16_SFLOAT,

    {DXGI_FORMAT_R16G16B16A16_TYPELESS,         DXGI_FORMAT_R16G16B16A16_UNORM,              sizeof(uint16_t) * 4,  false}, // RGBA16_UNORM,
    {DXGI_FORMAT_R16G16B16A16_TYPELESS,         DXGI_FORMAT_R16G16B16A16_SNORM,              sizeof(int16_t) * 4,   false}, // RGBA16_SNORM,
    {DXGI_FORMAT_R16G16B16A16_TYPELESS,         DXGI_FORMAT_R16G16B16A16_UINT,               sizeof(uint16_t) * 4,  true},  // RGBA16_UINT,
    {DXGI_FORMAT_R16G16B16A16_TYPELESS,         DXGI_FORMAT_R16G16B16A16_SINT,               sizeof(int16_t) * 4,   true},  // RGBA16_SINT,
    {DXGI_FORMAT_R16G16B16A16_TYPELESS,         DXGI_FORMAT_R16G16B16A16_FLOAT,              sizeof(uint16_t) * 4,  false}, // RGBA16_SFLOAT,

    {DXGI_FORMAT_R32_TYPELESS,                  DXGI_FORMAT_R32_UINT,                        sizeof(uint32_t),      true},  // R32_UINT,
    {DXGI_FORMAT_R32_TYPELESS,                  DXGI_FORMAT_R32_SINT,                        sizeof(int32_t),       true},  // R32_SINT,
    {DXGI_FORMAT_R32_TYPELESS,                  DXGI_FORMAT_R32_FLOAT,                       sizeof(float),         false}, // R32_SFLOAT,

    {DXGI_FORMAT_R32G32_TYPELESS,               DXGI_FORMAT_R32G32_UINT,                     sizeof(uint32_t) * 2,  true},  // RG32_UINT,
    {DXGI_FORMAT_R32G32_TYPELESS,               DXGI_FORMAT_R32G32_SINT,                     sizeof(int32_t) * 2,   true},  // RG32_SINT,
    {DXGI_FORMAT_R32G32_TYPELESS,               DXGI_FORMAT_R32G32_FLOAT,                    sizeof(float) * 2,     false}, // RG32_SFLOAT,

    {DXGI_FORMAT_R32G32B32_TYPELESS,            DXGI_FORMAT_R32G32B32_UINT,                  sizeof(uint32_t) * 3,  true},  // RGB32_UINT,
    {DXGI_FORMAT_R32G32B32_TYPELESS,            DXGI_FORMAT_R32G32B32_SINT,                  sizeof(int32_t) * 3,   true},  // RGB32_SINT,
    {DXGI_FORMAT_R32G32B32_TYPELESS,            DXGI_FORMAT_R32G32B32_FLOAT,                 sizeof(float) * 3,     false}, // RGB32_SFLOAT,

    {DXGI_FORMAT_R32G32B32A32_TYPELESS,         DXGI_FORMAT_R32G32B32A32_UINT,               sizeof(uint32_t) * 4,  true},  // RGBA32_UINT,
    {DXGI_FORMAT_R32G32B32A32_TYPELESS,         DXGI_FORMAT_R32G32B32A32_SINT,               sizeof(int32_t) * 4,   true},  // RGBA32_SINT,
    {DXGI_FORMAT_R32G32B32A32_TYPELESS,         DXGI_FORMAT_R32G32B32A32_FLOAT,              sizeof(float) * 4,     false}, // RGBA32_SFLOAT,

    {DXGI_FORMAT_R10G10B10A2_TYPELESS,          DXGI_FORMAT_R10G10B10A2_UNORM,               sizeof(uint32_t),      false}, // R10_G10_B10_A2_UNORM,
    {DXGI_FORMAT_R10G10B10A2_TYPELESS,          DXGI_FORMAT_R10G10B10A2_UINT,                sizeof(uint32_t),      true},  // R10_G10_B10_A2_UINT,
    {DXGI_FORMAT_R11G11B10_FLOAT,               DXGI_FORMAT_R11G11B10_FLOAT,                 sizeof(uint32_t),      false}, // R11_G11_B10_UFLOAT,
    {DXGI_FORMAT_R9G9B9E5_SHAREDEXP,            DXGI_FORMAT_R9G9B9E5_SHAREDEXP,              sizeof(uint32_t),      false}, // R9_G9_B9_E5_UFLOAT,

    {DXGI_FORMAT_BC1_TYPELESS,                  DXGI_FORMAT_BC1_UNORM,                       8,                     false}, // BC1_RGBA_UNORM,
    {DXGI_FORMAT_BC1_TYPELESS,                  DXGI_FORMAT_BC1_UNORM_SRGB,                  8,                     false}, // BC1_RGBA_SRGB,
    {DXGI_FORMAT_BC2_TYPELESS,                  DXGI_FORMAT_BC2_UNORM,                       16,                    false}, // BC2_RGBA_UNORM,
    {DXGI_FORMAT_BC2_TYPELESS,                  DXGI_FORMAT_BC2_UNORM_SRGB,                  16,                    false}, // BC2_RGBA_SRGB,
    {DXGI_FORMAT_BC3_TYPELESS,                  DXGI_FORMAT_BC3_UNORM,                       16,                    false}, // BC3_RGBA_UNORM,
    {DXGI_FORMAT_BC3_TYPELESS,                  DXGI_FORMAT_BC3_UNORM_SRGB,                  16,                    false}, // BC3_RGBA_SRGB,
    {DXGI_FORMAT_BC4_TYPELESS,                  DXGI_FORMAT_BC4_UNORM,                       8,                     false}, // BC4_R_UNORM,
    {DXGI_FORMAT_BC4_TYPELESS,                  DXGI_FORMAT_BC4_SNORM,                       8,                     false}, // BC4_R_SNORM,
    {DXGI_FORMAT_BC5_TYPELESS,                  DXGI_FORMAT_BC5_UNORM,                       16,                    false}, // BC5_RG_UNORM,
    {DXGI_FORMAT_BC5_TYPELESS,                  DXGI_FORMAT_BC5_SNORM,                       16,                    false}, // BC5_RG_SNORM,
    {DXGI_FORMAT_BC6H_TYPELESS,                 DXGI_FORMAT_BC6H_UF16,                       16,                    false}, // BC6H_RGB_UFLOAT,
    {DXGI_FORMAT_BC6H_TYPELESS,                 DXGI_FORMAT_BC6H_SF16,                       16,                    false}, // BC6H_RGB_SFLOAT,
    {DXGI_FORMAT_BC7_TYPELESS,                  DXGI_FORMAT_BC7_UNORM,                       16,                    false}, // BC7_RGBA_UNORM,
    {DXGI_FORMAT_BC7_TYPELESS,                  DXGI_FORMAT_BC7_UNORM_SRGB,                  16,                    false}, // BC7_RGBA_SRGB,

    {DXGI_FORMAT_R16_TYPELESS,                  DXGI_FORMAT_D16_UNORM,                       sizeof(uint16_t),      false}, // D16_UNORM,
    {DXGI_FORMAT_R24G8_TYPELESS,                DXGI_FORMAT_D24_UNORM_S8_UINT,               sizeof(uint32_t),      false}, // D24_UNORM_S8_UINT,
    {DXGI_FORMAT_R32_TYPELESS,                  DXGI_FORMAT_D32_FLOAT,                       sizeof(uint32_t),      false}, // D32_SFLOAT,
    {DXGI_FORMAT_R32G8X24_TYPELESS,             DXGI_FORMAT_D32_FLOAT_S8X24_UINT,            sizeof(uint64_t),      false}, // D32_SFLOAT_S8_UINT_X24,

    {DXGI_FORMAT_R24G8_TYPELESS,                DXGI_FORMAT_R24_UNORM_X8_TYPELESS,           sizeof(uint32_t),      false}, // R24_UNORM_X8,
    {DXGI_FORMAT_R24G8_TYPELESS,                DXGI_FORMAT_X24_TYPELESS_G8_UINT,            sizeof(uint32_t),      false}, // X24_R8_UINT,
    {DXGI_FORMAT_R32G8X24_TYPELESS,             DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,         sizeof(uint64_t),      false}, // X32_R8_UINT_X24,
    {DXGI_FORMAT_R32G8X24_TYPELESS,             DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,        sizeof(uint64_t),      false}, // R32_SFLOAT_X8_X24,
}};

const FormatInfo& GetFormatInfo(nri::Format format)
{
    return FORMAT_INFO_TABLE[(size_t)format];
}

static constexpr std::array<D3D11_LOGIC_OP, (size_t)nri::LogicFunc::MAX_NUM> LOGIC_FUNC_TABLE =
{
    D3D11_LOGIC_OP_CLEAR,                               // NONE,
    D3D11_LOGIC_OP_CLEAR,                               // CLEAR,
    D3D11_LOGIC_OP_AND,                                 // AND,
    D3D11_LOGIC_OP_AND_REVERSE,                         // AND_REVERSE,
    D3D11_LOGIC_OP_COPY,                                // COPY,
    D3D11_LOGIC_OP_AND_INVERTED,                        // AND_INVERTED,
    D3D11_LOGIC_OP_XOR,                                 // XOR,
    D3D11_LOGIC_OP_OR,                                  // OR,
    D3D11_LOGIC_OP_NOR,                                 // NOR,
    D3D11_LOGIC_OP_EQUIV,                               // EQUIVALENT,
    D3D11_LOGIC_OP_INVERT,                              // INVERT,
    D3D11_LOGIC_OP_OR_REVERSE,                          // OR_REVERSE,
    D3D11_LOGIC_OP_COPY_INVERTED,                       // COPY_INVERTED,
    D3D11_LOGIC_OP_OR_INVERTED,                         // OR_INVERTED,
    D3D11_LOGIC_OP_NAND,                                // NAND,
    D3D11_LOGIC_OP_SET                                  // SET
};

D3D11_LOGIC_OP GetD3D11LogicOpFromLogicFunc(nri::LogicFunc logicalFunc)
{
    return LOGIC_FUNC_TABLE[(size_t)logicalFunc];
}

static constexpr std::array<D3D11_BLEND_OP, (size_t)nri::BlendFunc::MAX_NUM> BLEND_FUNC_TABLE =
{
    D3D11_BLEND_OP_ADD,                                 // ADD,
    D3D11_BLEND_OP_SUBTRACT,                            // SUBTRACT,
    D3D11_BLEND_OP_REV_SUBTRACT,                        // REVERSE_SUBTRACT,
    D3D11_BLEND_OP_MIN,                                 // MIN,
    D3D11_BLEND_OP_MAX                                  // MAX
};

D3D11_BLEND_OP GetD3D11BlendOpFromBlendFunc(nri::BlendFunc blendFunc)
{
    return BLEND_FUNC_TABLE[(size_t)blendFunc];
}

static constexpr std::array<D3D11_BLEND, (size_t)nri::BlendFactor::MAX_NUM> BLEND_FACTOR_TABLE =
{
    D3D11_BLEND_ZERO,                                   // ZERO,
    D3D11_BLEND_ONE,                                    // ONE,
    D3D11_BLEND_SRC_COLOR,                              // SRC_COLOR,
    D3D11_BLEND_INV_SRC_COLOR,                          // ONE_MINUS_SRC_COLOR,
    D3D11_BLEND_DEST_COLOR,                             // DST_COLOR,
    D3D11_BLEND_INV_DEST_COLOR,                         // ONE_MINUS_DST_COLOR,
    D3D11_BLEND_SRC_ALPHA,                              // SRC_ALPHA,
    D3D11_BLEND_INV_SRC_ALPHA,                          // ONE_MINUS_SRC_ALPHA,
    D3D11_BLEND_DEST_ALPHA,                             // DST_ALPHA,
    D3D11_BLEND_INV_DEST_ALPHA,                         // ONE_MINUS_DST_ALPHA,
    D3D11_BLEND_BLEND_FACTOR,                           // CONSTANT_COLOR,
    D3D11_BLEND_INV_BLEND_FACTOR,                       // ONE_MINUS_CONSTANT_COLOR,
    D3D11_BLEND_BLEND_FACTOR,                           // CONSTANT_ALPHA,
    D3D11_BLEND_INV_BLEND_FACTOR,                       // ONE_MINUS_CONSTANT_ALPHA,
    D3D11_BLEND_SRC_ALPHA_SAT,                          // SRC_ALPHA_SATURATE,
    D3D11_BLEND_SRC1_COLOR,                             // SRC1_COLOR,
    D3D11_BLEND_INV_SRC1_COLOR,                         // ONE_MINUS_SRC1_COLOR,
    D3D11_BLEND_SRC1_ALPHA,                             // SRC1_ALPHA,
    D3D11_BLEND_INV_SRC1_ALPHA                          // ONE_MINUS_SRC1_ALPHA,
};

D3D11_BLEND GetD3D11BlendFromBlendFactor(nri::BlendFactor blendFactor)
{
    return BLEND_FACTOR_TABLE[(size_t)blendFactor];
}

static constexpr std::array<D3D11_STENCIL_OP, (size_t)nri::StencilFunc::MAX_NUM> STENCIL_FUNC_TABLE =
{
    D3D11_STENCIL_OP_KEEP,                              // KEEP,
    D3D11_STENCIL_OP_ZERO,                              // ZERO,
    D3D11_STENCIL_OP_REPLACE,                           // REPLACE,
    D3D11_STENCIL_OP_INCR_SAT,                          // INCREMENT_AND_CLAMP,
    D3D11_STENCIL_OP_DECR_SAT,                          // DECREMENT_AND_CLAMP,
    D3D11_STENCIL_OP_INVERT,                            // INVERT,
    D3D11_STENCIL_OP_INCR,                              // INCREMENT_AND_WRAP,
    D3D11_STENCIL_OP_DECR                               // DECREMENT_AND_WRAP
};

D3D11_STENCIL_OP GetD3D11StencilOpFromStencilFunc(nri::StencilFunc stencilFunc)
{
    return STENCIL_FUNC_TABLE[(size_t)stencilFunc];
}

static constexpr std::array<D3D11_COMPARISON_FUNC, (size_t)nri::CompareFunc::MAX_NUM> COMPARE_FUNC_TABLE =
{
    D3D11_COMPARISON_ALWAYS,                            // NONE,
    D3D11_COMPARISON_ALWAYS,                            // ALWAYS,
    D3D11_COMPARISON_NEVER,                             // NEVER,
    D3D11_COMPARISON_LESS,                              // LESS,
    D3D11_COMPARISON_LESS_EQUAL,                        // LESS_EQUAL,
    D3D11_COMPARISON_EQUAL,                             // EQUAL,
    D3D11_COMPARISON_GREATER_EQUAL,                     // GREATER_EQUAL,
    D3D11_COMPARISON_GREATER                            // GREATER
};

D3D11_COMPARISON_FUNC GetD3D11ComparisonFuncFromCompareFunc(nri::CompareFunc compareFunc)
{
    return COMPARE_FUNC_TABLE[(size_t)compareFunc];
}

static constexpr std::array<D3D11_CULL_MODE, (size_t)nri::CullMode::MAX_NUM> CULL_MODE_TABLE =
{
    D3D11_CULL_NONE,                                    // NONE,
    D3D11_CULL_FRONT,                                   // FRONT,
    D3D11_CULL_BACK                                     // BACK
};

D3D11_CULL_MODE GetD3D11CullModeFromCullMode(nri::CullMode cullMode)
{
    return CULL_MODE_TABLE[(size_t)cullMode];
}

static constexpr std::array<uint32_t, (size_t)nri::Topology::MAX_NUM> TOPOLOGY_TABLE =
{
    D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,                  // POINT_LIST,
    D3D11_PRIMITIVE_TOPOLOGY_LINELIST,                   // LINE_LIST,
    D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,                  // LINE_STRIP,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,               // TRIANGLE_LIST,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,              // TRIANGLE_STRIP,
    D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ,               // LINE_LIST_WITH_ADJACENCY,
    D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ,              // LINE_STRIP_WITH_ADJACENCY,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ,           // TRIANGLE_LIST_WITH_ADJACENCY,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ,          // TRIANGLE_STRIP_WITH_ADJACENCY,
    D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST   // PATCH_LIST
};

D3D11_PRIMITIVE_TOPOLOGY GetD3D11TopologyFromTopology(nri::Topology topology, uint32_t patchPoints)
{
    uint32_t res = TOPOLOGY_TABLE[(size_t)topology];

    if (res == D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST)
        res += patchPoints - 1;

    return (D3D11_PRIMITIVE_TOPOLOGY)res;
}

//==============================================================================================================================

inline bool DoesLibraryExist(const Log& log, const char* moduleName)
{
    HMODULE handle = LoadLibraryA(moduleName);
    if (handle)
        FreeLibrary(handle);
    else
        REPORT_INFO(log, "'%s' is not found. It's not required, but to enable AMD extensions put it in the working/system directory.", moduleName);

    return handle != nullptr;
}

DX11Extensions::~DX11Extensions()
{
    if (isAvailableNVAPI)
        NvAPI_Unload();

    if (isAvailableAGS)
        agsDeInit(agsContext);
}

void DX11Extensions::Create(const Log& l, nri::Vendor vendor, AGSContext* context)
{
    this->log = &l;

    const NvAPI_Status res1 = NvAPI_Initialize();
    isAvailableNVAPI = (res1 == NVAPI_OK);

    if (!context)
    {
        AGSReturnCode res2 = AGS_ERROR_MISSING_DLL;

        if (DoesLibraryExist(l, "amd_ags_x64.dll"))
            res2 = agsInit(&agsContext, nullptr, nullptr);

        isAvailableAGS = (res2 == AGS_SUCCESS);
    }
    else
    {
        agsContext = context;
        isAvailableAGS = (context != nullptr);
    }

    switch (vendor)
    {
    case nri::Vendor::NVIDIA:
        {
            if (!isAvailableNVAPI)
                REPORT_WARNING(l, "NVAPI library is not available!");

            isAvailableAGS = false;
        }
        break;
    case nri::Vendor::AMD:
        {
            if (!isAvailableAGS)
                REPORT_WARNING(l, "AMDAGS library is not available!");

            isAvailableNVAPI = false;
        }
        break;
    default:
        {
            isAvailableAGS = false;
            isAvailableNVAPI = false;
        }
        break;
    }
}

void DX11Extensions::BeginUAVOverlap(const VersionedContext& context) const
{
    if (isAvailableNVAPI)
    {
        const NvAPI_Status res = NvAPI_D3D11_BeginUAVOverlap(context.ptr);
        CHECK(*log, res == NVAPI_OK, "NvAPI_D3D11_BeginUAVOverlap() - FAILED!");
    }
    else if (isAvailableAGS)
    {
        const AGSReturnCode res = agsDriverExtensionsDX11_BeginUAVOverlap(agsContext, context.ptr);
        CHECK(*log, res == AGS_SUCCESS, "agsDriverExtensionsDX11_BeginUAVOverlap() - FAILED!");
    }
}

void DX11Extensions::EndUAVOverlap(const VersionedContext& context) const
{
    if (isAvailableNVAPI)
    {
        const NvAPI_Status status = NvAPI_D3D11_EndUAVOverlap(context.ptr);
        CHECK(*log, status == NVAPI_OK, "NvAPI_D3D11_EndUAVOverlap() - FAILED!");
    }
    else if (isAvailableAGS)
    {
        const AGSReturnCode res = agsDriverExtensionsDX11_EndUAVOverlap(agsContext, context.ptr);
        CHECK(*log, res == AGS_SUCCESS, "agsDriverExtensionsDX11_EndUAVOverlap() - FAILED!");
    }
}

void DX11Extensions::WaitForDrain(const VersionedContext& context, nri::BarrierDependency dependency) const
{
    if (isAvailableNVAPI)
    {
        uint32_t flags;

        if (dependency == nri::BarrierDependency::GRAPHICS_STAGE)
            flags = NVAPI_D3D_BEGIN_UAV_OVERLAP_GFX_WFI;
        else if (dependency == nri::BarrierDependency::COMPUTE_STAGE)
            flags = NVAPI_D3D_BEGIN_UAV_OVERLAP_COMP_WFI;
        else
            flags = NVAPI_D3D_BEGIN_UAV_OVERLAP_GFX_WFI | NVAPI_D3D_BEGIN_UAV_OVERLAP_COMP_WFI;

        const NvAPI_Status res = NvAPI_D3D11_BeginUAVOverlapEx(context.ptr, flags);
        CHECK(*log, res == NVAPI_OK, "NvAPI_D3D11_BeginUAVOverlap() - FAILED!");
    }
    else if (isAvailableAGS)
    {
        REPORT_WARNING(*log, "Verify that this code actually works on AMD!");

        const AGSReturnCode res1 = agsDriverExtensionsDX11_EndUAVOverlap(agsContext, context.ptr);
        CHECK(*log, res1 == AGS_SUCCESS, "agsDriverExtensionsDX11_EndUAVOverlap() - FAILED!");
        const AGSReturnCode res2 = agsDriverExtensionsDX11_BeginUAVOverlap(agsContext, context.ptr);
        CHECK(*log, res2 == AGS_SUCCESS, "agsDriverExtensionsDX11_BeginUAVOverlap() - FAILED!");
    }
}

void DX11Extensions::SetDepthBounds(const VersionedContext& context, float minBound, float maxBound) const
{
    bool isEnabled = minBound != 0.0f || maxBound != 1.0f;

    if (isAvailableNVAPI)
    {
        const NvAPI_Status status = NvAPI_D3D11_SetDepthBoundsTest(context.ptr, isEnabled, minBound, maxBound);
        CHECK(*log, status == NVAPI_OK, "NvAPI_D3D11_SetDepthBoundsTest() - FAILED!");
    }
    else if (isAvailableAGS)
    {
        const AGSReturnCode res = agsDriverExtensionsDX11_SetDepthBounds(agsContext, context.ptr, isEnabled, minBound, maxBound);
        CHECK(*log, res == AGS_SUCCESS, "agsDriverExtensionsDX11_SetDepthBounds() - FAILED!");
    }
}

void DX11Extensions::MultiDrawIndirect(const VersionedContext& context, ID3D11Buffer* buffer, uint64_t offset, uint32_t drawNum, uint32_t stride) const
{
    if (isAvailableNVAPI)
    {
        const NvAPI_Status status = NvAPI_D3D11_MultiDrawInstancedIndirect(context.ptr, drawNum, buffer, (uint32_t)offset, stride);
        CHECK(*log, status == NVAPI_OK, "NvAPI_D3D11_MultiDrawInstancedIndirect() - FAILED!");
    }
    else if (isAvailableAGS)
    {
        const AGSReturnCode res = agsDriverExtensionsDX11_MultiDrawInstancedIndirect(agsContext, context.ptr, drawNum, buffer, (uint32_t)offset, stride);
        CHECK(*log, res == AGS_SUCCESS, "agsDriverExtensionsDX11_MultiDrawIndexedInstancedIndirect() - FAILED!");
    }
    else
    {
        for (uint32_t i = 0; i < drawNum; i++)
        {
            context->DrawInstancedIndirect(buffer, (uint32_t)offset);
            offset += stride;
        }
    }
}

void DX11Extensions::MultiDrawIndexedIndirect(const VersionedContext& context, ID3D11Buffer* buffer, uint64_t offset, uint32_t drawNum, uint32_t stride) const
{
    if (isAvailableNVAPI)
    {
        const NvAPI_Status status = NvAPI_D3D11_MultiDrawIndexedInstancedIndirect(context.ptr, drawNum, buffer, (uint32_t)offset, stride);
        CHECK(*log, status == NVAPI_OK, "NvAPI_D3D11_MultiDrawInstancedIndirect() - FAILED!");
    }
    else if (isAvailableAGS)
    {
        const AGSReturnCode res = agsDriverExtensionsDX11_MultiDrawIndexedInstancedIndirect(agsContext, context.ptr, drawNum, buffer, (uint32_t)offset, stride);
        CHECK(*log, res == AGS_SUCCESS, "agsDriverExtensionsDX11_MultiDrawIndexedInstancedIndirect() - FAILED!");
    }
    else
    {
        for (uint32_t i = 0; i < drawNum; i++)
        {
            context->DrawIndexedInstancedIndirect(buffer, (uint32_t)offset);
            offset += stride;
        }
    }
}

namespace nri
{
    void GetTextureDescD3D11(const TextureD3D11Desc& textureD3D11Desc, TextureDesc& textureDesc)
    {
        D3D11_RESOURCE_DIMENSION dimension;
        textureD3D11Desc.d3d11Resource->GetType(&dimension);

        switch(dimension)
        {
        case D3D11_RESOURCE_DIMENSION_BUFFER:
            break;
        case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
            {
                D3D11_TEXTURE1D_DESC desc = {};
                ((ID3D11Texture1D*)textureD3D11Desc.d3d11Resource)->GetDesc(&desc);

                textureDesc = {};
                textureDesc.type = TextureType::TEXTURE_1D;

                textureDesc.usageMask = (TextureUsageBits)0xffff;
                static_assert(sizeof(TextureUsageBits) == sizeof(uint16_t), "invalid sizeof");

                textureDesc.format = GetFormatDXGI(desc.Format);
                textureDesc.size[0] = (uint16_t)desc.Width;
                textureDesc.size[1] = 1;
                textureDesc.size[2] = 1;
                textureDesc.mipNum = (uint16_t)desc.MipLevels;
                textureDesc.arraySize = (uint16_t)desc.ArraySize;
                textureDesc.sampleNum = 1;
                textureDesc.physicalDeviceMask = 0x1; // unsupported in D3D11
                break;
            }
        case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
            {
                D3D11_TEXTURE2D_DESC desc = {};
                ((ID3D11Texture2D*)textureD3D11Desc.d3d11Resource)->GetDesc(&desc);

                textureDesc = {};
                textureDesc.type = TextureType::TEXTURE_2D;

                textureDesc.usageMask = (TextureUsageBits)0xffff;
                static_assert(sizeof(TextureUsageBits) == sizeof(uint16_t), "invalid sizeof");

                textureDesc.format = GetFormatDXGI(desc.Format);
                textureDesc.size[0] = (uint16_t)desc.Width;
                textureDesc.size[1] = (uint16_t)desc.Height;
                textureDesc.size[2] = 1;
                textureDesc.mipNum = (uint16_t)desc.MipLevels;
                textureDesc.arraySize = (uint16_t)desc.ArraySize;
                textureDesc.sampleNum = (uint8_t)desc.SampleDesc.Count;
                textureDesc.physicalDeviceMask = 0x1; // unsupported in D3D11
                break;
            }
        case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
            {
                D3D11_TEXTURE3D_DESC desc = {};
                ((ID3D11Texture3D*)textureD3D11Desc.d3d11Resource)->GetDesc(&desc);

                textureDesc = {};
                textureDesc.type = TextureType::TEXTURE_3D;

                textureDesc.usageMask = (TextureUsageBits)0xffff;
                static_assert(sizeof(TextureUsageBits) == sizeof(uint16_t), "invalid sizeof");

                textureDesc.format = GetFormatDXGI(desc.Format);
                textureDesc.size[0] = (uint16_t)desc.Width;
                textureDesc.size[1] = (uint16_t)desc.Height;
                textureDesc.size[2] = (uint16_t)desc.Depth;
                textureDesc.mipNum = (uint16_t)desc.MipLevels;
                textureDesc.arraySize = 1;
                textureDesc.sampleNum = 1;
                textureDesc.physicalDeviceMask = 0x1; // unsupported in D3D11
                break;
            }
        }
    }

    void GetBufferDescD3D11(const nri::BufferD3D11Desc& bufferD3D11Desc, nri::BufferDesc& bufferDesc)
    {
        D3D11_BUFFER_DESC desc = {};
        ((ID3D11Buffer*)bufferD3D11Desc.d3d11Resource)->GetDesc(&desc);

        bufferDesc.usageMask = (BufferUsageBits)0xffff;
        static_assert(sizeof(BufferUsageBits) == sizeof(uint16_t), "invalid sizeof");

        bufferDesc.size = desc.ByteWidth;
        bufferDesc.structureStride = desc.StructureByteStride;
        bufferDesc.physicalDeviceMask = 0x1; // unsupported in D3D11
    }
}
