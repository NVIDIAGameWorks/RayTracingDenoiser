/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "BufferD3D12.h"

using namespace nri;

static constexpr std::array<D3D12_COMMAND_LIST_TYPE, (uint32_t)CommandQueueType::MAX_NUM> COMMAND_LIST_TYPES = {
    D3D12_COMMAND_LIST_TYPE_DIRECT,             // GRAPHICS
    D3D12_COMMAND_LIST_TYPE_COMPUTE,            // COMPUTE
    D3D12_COMMAND_LIST_TYPE_COPY                // COPY
};

D3D12_COMMAND_LIST_TYPE GetCommandListType(CommandQueueType commandQueueType)
{
    return COMMAND_LIST_TYPES[(uint32_t)commandQueueType];
}

static constexpr std::array<D3D12_HEAP_TYPE, (uint32_t)MemoryLocation::MAX_NUM> HEAP_TYPES =
{
    D3D12_HEAP_TYPE_DEFAULT,                    // DEVICE
    D3D12_HEAP_TYPE_UPLOAD,                     // HOST_UPLOAD
    D3D12_HEAP_TYPE_READBACK                    // HOST_READBACK
};

MemoryType GetMemoryType(D3D12_HEAP_TYPE heapType, D3D12_HEAP_FLAGS heapFlags)
{
    return ((uint32_t)heapFlags) | ((uint32_t)heapType << 16);
}

MemoryType GetMemoryType(MemoryLocation memoryLocation, const D3D12_RESOURCE_DESC& resourceDesc)
{
    D3D12_HEAP_TYPE heapType = HEAP_TYPES[(uint32_t)memoryLocation];
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;

    // Required for Tier 1 resource heaps https://msdn.microsoft.com/en-us/library/windows/desktop/dn986743(v=vs.85).aspx
    if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        heapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    else if (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET || resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        heapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    else
        heapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;

    return GetMemoryType(heapType, heapFlags);
}

D3D12_HEAP_TYPE GetHeapType(MemoryType memoryType)
{
    return (D3D12_HEAP_TYPE)(memoryType >> 16);
}

D3D12_HEAP_FLAGS GetHeapFlags(MemoryType memoryType)
{
    return (D3D12_HEAP_FLAGS)(memoryType & 0xffff);
}

bool RequiresDedicatedAllocation(MemoryType memoryType)
{
    D3D12_HEAP_FLAGS heapFlags = GetHeapFlags(memoryType);

    if ((heapFlags & D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES) == D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES )
        return true;

    return false;
}

D3D12_RESOURCE_STATES GetResourceStates(AccessBits accessMask)
{
    D3D12_RESOURCE_STATES resourceStates = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COMMON; // TODO: PS resource and/or non-PS resource?

    if (accessMask & (AccessBits::CONSTANT_BUFFER | AccessBits::VERTEX_BUFFER))
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    if (accessMask & AccessBits::INDEX_BUFFER)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_INDEX_BUFFER;
    if (accessMask & AccessBits::ARGUMENT_BUFFER)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    if (accessMask & AccessBits::SHADER_RESOURCE_STORAGE)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if (accessMask & AccessBits::COLOR_ATTACHMENT)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (accessMask & AccessBits::DEPTH_STENCIL_READ)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_DEPTH_READ;
    if (accessMask & AccessBits::DEPTH_STENCIL_WRITE)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_DEPTH_WRITE;
    if (accessMask & AccessBits::COPY_SOURCE)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COPY_SOURCE;
    if (accessMask & AccessBits::COPY_DESTINATION)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COPY_DEST;
    if (accessMask & AccessBits::SHADER_RESOURCE)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (accessMask & AccessBits::ACCELERATION_STRUCTURE_READ)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    if (accessMask & AccessBits::ACCELERATION_STRUCTURE_WRITE)
        resourceStates |= D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    return resourceStates;
}

D3D12_RESOURCE_FLAGS GetBufferFlags(BufferUsageBits bufferUsageMask)
{
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

    if (bufferUsageMask & BufferUsageBits::SHADER_RESOURCE_STORAGE ||
        bufferUsageMask & BufferUsageBits::RAY_TRACING_SCRATCH_BUFFER)
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    return flags;
};

D3D12_RESOURCE_FLAGS GetTextureFlags(TextureUsageBits textureUsageMask)
{
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

    if (textureUsageMask & TextureUsageBits::SHADER_RESOURCE_STORAGE)
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (textureUsageMask & TextureUsageBits::COLOR_ATTACHMENT)
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    if (textureUsageMask & TextureUsageBits::DEPTH_STENCIL_ATTACHMENT)
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        if ( !(textureUsageMask & TextureUsageBits::SHADER_RESOURCE) )
            flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }

    return flags;
};

D3D12_RESOURCE_DIMENSION GetResourceDimension(TextureType textureType)
{
    if (textureType == TextureType::TEXTURE_1D)
        return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    else if (textureType == TextureType::TEXTURE_2D)
        return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    else if (textureType == TextureType::TEXTURE_3D)
        return D3D12_RESOURCE_DIMENSION_TEXTURE3D;

    return D3D12_RESOURCE_DIMENSION_UNKNOWN;
}

static constexpr std::array<D3D12_DESCRIPTOR_RANGE_TYPE, (uint32_t)DescriptorType::MAX_NUM> DESCRIPTOR_RANGE_TYPES = {
    D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,        // SAMPLER
    D3D12_DESCRIPTOR_RANGE_TYPE_CBV,            // CONSTANT_BUFFER
    D3D12_DESCRIPTOR_RANGE_TYPE_SRV,            // TEXTURE
    D3D12_DESCRIPTOR_RANGE_TYPE_UAV,            // STORAGE_TEXTURE
    D3D12_DESCRIPTOR_RANGE_TYPE_SRV,            // BUFFER
    D3D12_DESCRIPTOR_RANGE_TYPE_UAV,            // STORAGE_BUFFER
    D3D12_DESCRIPTOR_RANGE_TYPE_SRV             // ACCELERATION_STRUCTURE
};

D3D12_DESCRIPTOR_RANGE_TYPE GetDescriptorRangesType(DescriptorType descriptorType)
{
    return DESCRIPTOR_RANGE_TYPES[(uint32_t)descriptorType];
}

D3D12_DESCRIPTOR_HEAP_TYPE GetDescriptorHeapType(DescriptorType descriptorType)
{
    return descriptorType == DescriptorType::SAMPLER ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
}

static constexpr std::array<D3D12_SHADER_VISIBILITY, (uint32_t)ShaderStage::MAX_NUM> SHADER_STAGES = {
    D3D12_SHADER_VISIBILITY_ALL,                // ALL
    D3D12_SHADER_VISIBILITY_VERTEX,             // VERTEX
    D3D12_SHADER_VISIBILITY_HULL,               // TESS_CONTROL
    D3D12_SHADER_VISIBILITY_DOMAIN,             // TESS_EVALUATION
    D3D12_SHADER_VISIBILITY_GEOMETRY,           // GEOMETRY
    D3D12_SHADER_VISIBILITY_PIXEL,              // FRAGMENT
    D3D12_SHADER_VISIBILITY_ALL,                // COMPUTE
    D3D12_SHADER_VISIBILITY_ALL,                // RAYGEN
    D3D12_SHADER_VISIBILITY_ALL,                // MISS
    D3D12_SHADER_VISIBILITY_ALL,                // INTERSECTION
    D3D12_SHADER_VISIBILITY_ALL,                // CLOSEST_HIT
    D3D12_SHADER_VISIBILITY_ALL,                // ANY_HIT
    D3D12_SHADER_VISIBILITY_ALL                 // CALLABLE
};

D3D12_SHADER_VISIBILITY GetShaderVisibility(ShaderStage shaderStage)
{
    return SHADER_STAGES[(uint32_t)shaderStage];
}

static constexpr std::array<D3D12_PRIMITIVE_TOPOLOGY_TYPE, (uint32_t)Topology::MAX_NUM> PRIMITIVE_TOPOLOGY_TYPES = {
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT,        // POINT_LIST
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,         // LINE_LIST
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,         // LINE_STRIP
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,     // TRIANGLE_LIST
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,     // TRIANGLE_STRIP
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,         // LINE_LIST_WITH_ADJACENCY
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,         // LINE_STRIP_WITH_ADJACENCY
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,     // TRIANGLE_LIST_WITH_ADJACENCY
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,     // TRIANGLE_STRIP_WITH_ADJACENCY
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH         // PATCH_LIST
};

D3D12_PRIMITIVE_TOPOLOGY_TYPE GetPrimitiveTopologyType(Topology topology)
{
    return PRIMITIVE_TOPOLOGY_TYPES[(uint32_t)topology];
}

static constexpr std::array<D3D_PRIMITIVE_TOPOLOGY, 9> PRIMITIVE_TOPOLOGIES = {
    D3D_PRIMITIVE_TOPOLOGY_POINTLIST,           // POINT_LIST
    D3D_PRIMITIVE_TOPOLOGY_LINELIST,            // LINE_LIST
    D3D_PRIMITIVE_TOPOLOGY_LINESTRIP,           // LINE_STRIP
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,        // TRIANGLE_LIST
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,       // TRIANGLE_STRIP
    D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ,        // LINE_LIST_WITH_ADJACENCY
    D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ,       // LINE_STRIP_WITH_ADJACENCY
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ,    // TRIANGLE_LIST_WITH_ADJACENCY
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ    // TRIANGLE_STRIP_WITH_ADJACENCY
};

D3D_PRIMITIVE_TOPOLOGY GetPrimitiveTopology(Topology topology, uint8_t tessControlPointNum)
{
    if (topology == Topology::PATCH_LIST)
        return (D3D_PRIMITIVE_TOPOLOGY)((uint8_t)D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ + tessControlPointNum);
    else
        return PRIMITIVE_TOPOLOGIES[(uint32_t)topology];
}

static constexpr std::array<D3D12_FILL_MODE, (uint32_t)FillMode::MAX_NUM> FILL_MODES = {
    D3D12_FILL_MODE_SOLID,                      // SOLID
    D3D12_FILL_MODE_WIREFRAME                   // WIREFRAME
};

D3D12_FILL_MODE GetFillMode(FillMode fillMode)
{
    return FILL_MODES[(uint32_t)fillMode];
}

static constexpr std::array<D3D12_CULL_MODE, (uint32_t)CullMode::MAX_NUM> CULL_MODES = {
    D3D12_CULL_MODE_NONE,                       // NONE
    D3D12_CULL_MODE_FRONT,                      // FRONT
    D3D12_CULL_MODE_BACK                        // BACK
};

D3D12_CULL_MODE GetCullMode(CullMode cullMode)
{
    return CULL_MODES[(uint32_t)cullMode];
}

UINT8 GetRenderTargetWriteMask(ColorWriteBits colorWriteMask)
{
    return colorWriteMask & ColorWriteBits::RGBA;
}

static constexpr std::array<D3D12_COMPARISON_FUNC, (uint32_t)CompareFunc::MAX_NUM> COMPARISON_FUNCS = {
    D3D12_COMPARISON_FUNC_ALWAYS,               // NONE
    D3D12_COMPARISON_FUNC_ALWAYS,               // ALWAYS
    D3D12_COMPARISON_FUNC_NEVER,                // NEVER
    D3D12_COMPARISON_FUNC_LESS,                 // LESS
    D3D12_COMPARISON_FUNC_LESS_EQUAL,           // LESS_EQUAL
    D3D12_COMPARISON_FUNC_EQUAL,                // EQUAL
    D3D12_COMPARISON_FUNC_GREATER_EQUAL,        // GREATER_EQUAL
    D3D12_COMPARISON_FUNC_GREATER,              // GREATER
    D3D12_COMPARISON_FUNC_NOT_EQUAL             // NOT_EQUAL
};

D3D12_COMPARISON_FUNC GetComparisonFunc(CompareFunc compareFunc)
{
    return COMPARISON_FUNCS[(uint32_t)compareFunc];
}

static constexpr std::array<D3D12_STENCIL_OP, (uint32_t)StencilFunc::MAX_NUM> STENCIL_OPS = {
    D3D12_STENCIL_OP_KEEP,                      // KEEP
    D3D12_STENCIL_OP_ZERO,                      // ZERO
    D3D12_STENCIL_OP_REPLACE,                   // REPLACE
    D3D12_STENCIL_OP_INCR_SAT,                  // INCREMENT_AND_CLAMP
    D3D12_STENCIL_OP_DECR_SAT,                  // DECREMENT_AND_CLAMP
    D3D12_STENCIL_OP_INVERT,                    // INVERT
    D3D12_STENCIL_OP_INCR,                      // INCREMENT_AND_WRAP
    D3D12_STENCIL_OP_DECR                       // DECREMENT_AND_WRAP
};

D3D12_STENCIL_OP GetStencilOp(StencilFunc stencilFunc)
{
    return STENCIL_OPS[(uint32_t)stencilFunc];
}

static constexpr std::array<D3D12_LOGIC_OP, (uint32_t)LogicFunc::MAX_NUM> LOGIC_OPS = {
    D3D12_LOGIC_OP_NOOP,                        // NONE
    D3D12_LOGIC_OP_CLEAR,                       // CLEAR
    D3D12_LOGIC_OP_AND,                         // AND
    D3D12_LOGIC_OP_AND_REVERSE,                 // AND_REVERSE
    D3D12_LOGIC_OP_COPY,                        // COPY
    D3D12_LOGIC_OP_AND_INVERTED,                // AND_INVERTED
    D3D12_LOGIC_OP_XOR,                         // XOR
    D3D12_LOGIC_OP_OR,                          // OR
    D3D12_LOGIC_OP_NOR,                         // NOR
    D3D12_LOGIC_OP_EQUIV,                       // EQUIVALENT
    D3D12_LOGIC_OP_INVERT,                      // INVERT
    D3D12_LOGIC_OP_OR_REVERSE,                  // OR_REVERSE
    D3D12_LOGIC_OP_COPY_INVERTED,               // COPY_INVERTED
    D3D12_LOGIC_OP_OR_INVERTED,                 // OR_INVERTED
    D3D12_LOGIC_OP_NAND,                        // NAND
    D3D12_LOGIC_OP_SET                          // SET
};

D3D12_LOGIC_OP GetLogicOp(LogicFunc logicFunc)
{
    return LOGIC_OPS[(uint32_t)logicFunc];
}

static constexpr std::array<D3D12_BLEND, (uint32_t)BlendFactor::MAX_NUM> BLENDS =
{
    D3D12_BLEND_ZERO,                           // ZERO
    D3D12_BLEND_ONE,                            // ONE
    D3D12_BLEND_SRC_COLOR,                      // SRC_COLOR
    D3D12_BLEND_INV_SRC_COLOR,                  // ONE_MINUS_SRC_COLOR
    D3D12_BLEND_DEST_COLOR,                     // DST_COLOR
    D3D12_BLEND_INV_DEST_COLOR,                 // ONE_MINUS_DST_COLOR
    D3D12_BLEND_SRC_ALPHA,                      // SRC_ALPHA
    D3D12_BLEND_INV_SRC_ALPHA,                  // ONE_MINUS_SRC_ALPHA
    D3D12_BLEND_DEST_ALPHA,                     // DST_ALPHA
    D3D12_BLEND_INV_DEST_ALPHA,                 // ONE_MINUS_DST_ALPHA
    D3D12_BLEND_BLEND_FACTOR,                   // CONSTANT_COLOR
    D3D12_BLEND_INV_BLEND_FACTOR,               // ONE_MINUS_CONSTANT_COLOR
    D3D12_BLEND_BLEND_FACTOR,                   // CONSTANT_ALPHA
    D3D12_BLEND_INV_BLEND_FACTOR,               // ONE_MINUS_CONSTANT_ALPHA
    D3D12_BLEND_SRC_ALPHA_SAT,                  // SRC_ALPHA_SATURATE
    D3D12_BLEND_SRC1_COLOR,                     // SRC1_COLOR
    D3D12_BLEND_INV_SRC1_COLOR,                 // ONE_MINUS_SRC1_COLOR
    D3D12_BLEND_SRC1_ALPHA,                     // SRC1_ALPHA
    D3D12_BLEND_INV_SRC1_ALPHA                  // ONE_MINUS_SRC1_ALPHA
};

D3D12_BLEND GetBlend(BlendFactor blendFactor)
{
    return BLENDS[(uint32_t)blendFactor];
}

static constexpr std::array<D3D12_BLEND_OP, (uint32_t)BlendFunc::MAX_NUM> BLEND_OPS =
{
    D3D12_BLEND_OP_ADD,                         // ADD
    D3D12_BLEND_OP_SUBTRACT,                    // SUBTRACT
    D3D12_BLEND_OP_REV_SUBTRACT,                // REVERSE_SUBTRACT
    D3D12_BLEND_OP_MIN,                         // MIN
    D3D12_BLEND_OP_MAX                          // MAX
};

D3D12_BLEND_OP GetBlendOp(BlendFunc blendFunc)
{
    return BLEND_OPS[(uint32_t)blendFunc];
}

static constexpr std::array<D3D12_TEXTURE_ADDRESS_MODE, (uint32_t)AddressMode::MAX_NUM> TEXTURE_ADDRESS_MODES = {
    D3D12_TEXTURE_ADDRESS_MODE_WRAP,            // REPEAT
    D3D12_TEXTURE_ADDRESS_MODE_MIRROR,          // MIRRORED_REPEAT
    D3D12_TEXTURE_ADDRESS_MODE_CLAMP,           // CLAMP_TO_EDGE
    D3D12_TEXTURE_ADDRESS_MODE_BORDER           // CLAMP_TO_BORDER
};

D3D12_TEXTURE_ADDRESS_MODE GetAddressMode(AddressMode addressMode)
{
    return TEXTURE_ADDRESS_MODES[(uint32_t)addressMode];
}

D3D12_FILTER GetFilterIsotropic(Filter mip, Filter magnification, Filter minification, FilterExt filterExt, bool useComparison)
{
    uint32_t combinedMask = 0;
    combinedMask |= mip == Filter::LINEAR ? 0x1 : 0;
    combinedMask |= magnification == Filter::LINEAR ? 0x4 : 0;
    combinedMask |= minification == Filter::LINEAR ? 0x10 : 0;

    if (useComparison)
        combinedMask |= 0x80;
    else if (filterExt == FilterExt::MIN)
        combinedMask |= 0x100;
    else if (filterExt == FilterExt::MAX)
        combinedMask |= 0x180;

    return (D3D12_FILTER)combinedMask;
}

D3D12_FILTER GetFilterAnisotropic(FilterExt filterExt, bool useComparison)
{
    if (filterExt == FilterExt::MIN)
        return D3D12_FILTER_MINIMUM_ANISOTROPIC;
    else if (filterExt == FilterExt::MAX)
        return D3D12_FILTER_MAXIMUM_ANISOTROPIC;

    return useComparison ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
}

static constexpr std::array<D3D12_QUERY_TYPE, (uint32_t)QueryType::MAX_NUM> QUERY_TYPES =
{
    D3D12_QUERY_TYPE_TIMESTAMP,                         // TIMESTAMP
    D3D12_QUERY_TYPE_OCCLUSION,                         // OCCLUSION
    D3D12_QUERY_TYPE_PIPELINE_STATISTICS                // PIPELINE_STATISTICS
};

D3D12_QUERY_TYPE GetQueryType(QueryType queryType)
{
    return QUERY_TYPES[(uint32_t)queryType];
}

static constexpr std::array<D3D12_QUERY_HEAP_TYPE, (uint32_t)QueryType::MAX_NUM> QUERY_HEAP_TYPES =
{
    D3D12_QUERY_HEAP_TYPE_TIMESTAMP,                    // TIMESTAMP
    D3D12_QUERY_HEAP_TYPE_OCCLUSION,                    // OCCLUSION
    D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS           // PIPELINE_STATISTICS
};

D3D12_QUERY_HEAP_TYPE GetQueryHeapType(QueryType queryType)
{
    return QUERY_HEAP_TYPES[(uint32_t)queryType];
}

uint32_t GetQueryElementSize(D3D12_QUERY_TYPE queryType)
{
    if (queryType == D3D12_QUERY_TYPE_TIMESTAMP)
        return sizeof(uint64_t);
    else if (queryType == D3D12_QUERY_TYPE_OCCLUSION)
        return sizeof(uint64_t);
    else if (queryType == D3D12_QUERY_TYPE_PIPELINE_STATISTICS)
        return sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);

    return 0;
}

void ConvertRects(D3D12_RECT* rectsD3D12, const Rect* rects, uint32_t rectNum)
{
    for (uint32_t i = 0; i < rectNum; i++)
    {
        rectsD3D12[i].left = rects[i].left;
        rectsD3D12[i].top = rects[i].top;
        rectsD3D12[i].right = rects[i].left + rects[i].width;
        rectsD3D12[i].bottom = rects[i].top + rects[i].height;
    }
}

static constexpr std::array<DXGI_FORMAT, (uint32_t)Format::MAX_NUM> DXGI_FORMATS = {
    DXGI_FORMAT_UNKNOWN,                                // UNKNOWN
    DXGI_FORMAT_R8_UNORM,                               // R8_UNORM,
    DXGI_FORMAT_R8_SNORM,                               // R8_SNORM,
    DXGI_FORMAT_R8_UINT,                                // R8_UINT,
    DXGI_FORMAT_R8_SINT,                                // R8_SINT,
    DXGI_FORMAT_R8G8_UNORM,                             // RG8_UNORM,
    DXGI_FORMAT_R8G8_SNORM,                             // RG8_SNORM,
    DXGI_FORMAT_R8G8_UINT,                              // RG8_UINT,
    DXGI_FORMAT_R8G8_SINT,                              // RG8_SINT,
    DXGI_FORMAT_B8G8R8A8_UNORM,                         // BGRA8_UNORM
    DXGI_FORMAT_R8G8B8A8_UNORM,                         // RGBA8_UNORM,
    DXGI_FORMAT_R8G8B8A8_SNORM,                         // RGBA8_SNORM,
    DXGI_FORMAT_R8G8B8A8_UINT,                          // RGBA8_UINT,
    DXGI_FORMAT_R8G8B8A8_SINT,                          // RGBA8_SINT,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,                    // RGBA8_SRGB,
    DXGI_FORMAT_R16_UNORM,                              // R16_UNORM,
    DXGI_FORMAT_R16_SNORM,                              // R16_SNORM,
    DXGI_FORMAT_R16_UINT,                               // R16_UINT,
    DXGI_FORMAT_R16_SINT,                               // R16_SINT,
    DXGI_FORMAT_R16_FLOAT,                              // R16_SFLOAT,
    DXGI_FORMAT_R16G16_UNORM,                           // RG16_UNORM,
    DXGI_FORMAT_R16G16_SNORM,                           // RG16_SNORM,
    DXGI_FORMAT_R16G16_UINT,                            // RG16_UINT,
    DXGI_FORMAT_R16G16_SINT,                            // RG16_SINT,
    DXGI_FORMAT_R16G16_FLOAT,                           // RG16_SFLOAT,
    DXGI_FORMAT_R16G16B16A16_UNORM,                     // RGBA16_UNORM,
    DXGI_FORMAT_R16G16B16A16_SNORM,                     // RGBA16_SNORM,
    DXGI_FORMAT_R16G16B16A16_UINT,                      // RGBA16_UINT,
    DXGI_FORMAT_R16G16B16A16_SINT,                      // RGBA16_SINT,
    DXGI_FORMAT_R16G16B16A16_FLOAT,                     // RGBA16_SFLOAT,
    DXGI_FORMAT_R32_UINT,                               // R32_UINT,
    DXGI_FORMAT_R32_SINT,                               // R32_SINT,
    DXGI_FORMAT_R32_FLOAT,                              // R32_SFLOAT,
    DXGI_FORMAT_R32G32_UINT,                            // RG32_UINT,
    DXGI_FORMAT_R32G32_SINT,                            // RG32_SINT,
    DXGI_FORMAT_R32G32_FLOAT,                           // RG32_SFLOAT,
    DXGI_FORMAT_R32G32B32_UINT,                         // RGB32_UINT,
    DXGI_FORMAT_R32G32B32_SINT,                         // RGB32_SINT,
    DXGI_FORMAT_R32G32B32_FLOAT,                        // RGB32_SFLOAT,
    DXGI_FORMAT_R32G32B32A32_UINT,                      // RGBA32_UINT,
    DXGI_FORMAT_R32G32B32A32_SINT,                      // RGBA32_SINT,
    DXGI_FORMAT_R32G32B32A32_FLOAT,                     // RGBA32_SFLOAT,
    DXGI_FORMAT_R10G10B10A2_UNORM,                      // R10_G10_B10_A2_UNORM,
    DXGI_FORMAT_R10G10B10A2_UINT,                       // R10_G10_B10_A2_UINT,
    DXGI_FORMAT_R11G11B10_FLOAT,                        // R11_G11_B10_UFLOAT,
    DXGI_FORMAT_R9G9B9E5_SHAREDEXP,                     // R9_G9_B9_E5_UFLOAT,
    DXGI_FORMAT_BC1_UNORM,                              // BC1_RGBA_UNORM,
    DXGI_FORMAT_BC1_UNORM_SRGB,                         // BC1_RGBA_SRGB,
    DXGI_FORMAT_BC2_UNORM,                              // BC2_RGBA_UNORM,
    DXGI_FORMAT_BC2_UNORM_SRGB,                         // BC2_RGBA_SRGB,
    DXGI_FORMAT_BC3_UNORM,                              // BC3_RGBA_UNORM,
    DXGI_FORMAT_BC3_UNORM_SRGB,                         // BC3_RGBA_SRGB,
    DXGI_FORMAT_BC4_UNORM,                              // BC4_R_UNORM,
    DXGI_FORMAT_BC4_SNORM,                              // BC4_R_SNORM,
    DXGI_FORMAT_BC5_UNORM,                              // BC5_RG_UNORM,
    DXGI_FORMAT_BC5_SNORM,                              // BC5_RG_SNORM,
    DXGI_FORMAT_BC6H_UF16,                              // BC6H_RGB_UFLOAT,
    DXGI_FORMAT_BC6H_SF16,                              // BC6H_RGB_SFLOAT,
    DXGI_FORMAT_BC7_UNORM,                              // BC7_RGBA_UNORM,
    DXGI_FORMAT_BC7_UNORM_SRGB,                         // BC7_RGBA_SRGB,
    DXGI_FORMAT_D16_UNORM,                              // D16_UNORM,
    DXGI_FORMAT_D24_UNORM_S8_UINT,                      // D24_UNORM_S8_UINT,
    DXGI_FORMAT_D32_FLOAT,                              // D32_SFLOAT,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT,                   // D32_SFLOAT_S8_UINT_X24_TYPELESS,
    DXGI_FORMAT_R24_UNORM_X8_TYPELESS,                  // D24_UNORM_X8_TYPELESS,
    DXGI_FORMAT_X24_TYPELESS_G8_UINT,                   // X24_TYPLESS_S8_UINT,
    DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,                // X32_TYPLESS_S8_UINT_X24_TYPELESS,
    DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS                // D32_SFLOAT_X8_TYPLESS_X24_TYPELESS,
};

DXGI_FORMAT GetFormat(Format format)
{
    return DXGI_FORMATS[(uint32_t)format];
}

static constexpr std::array<DXGI_FORMAT, (uint32_t)Format::MAX_NUM> DXGI_TYPELESS_FORMATS = {
    DXGI_FORMAT_UNKNOWN,                                    // UNKNOWN
    DXGI_FORMAT_R8_TYPELESS,                                // R8_UNORM,
    DXGI_FORMAT_R8_TYPELESS,                                // R8_SNORM,
    DXGI_FORMAT_R8_TYPELESS,                                // R8_UINT,
    DXGI_FORMAT_R8_TYPELESS,                                // R8_SINT,
    DXGI_FORMAT_R8G8_TYPELESS,                              // RG8_UNORM,
    DXGI_FORMAT_R8G8_TYPELESS,                              // RG8_SNORM,
    DXGI_FORMAT_R8G8_TYPELESS,                              // RG8_UINT,
    DXGI_FORMAT_R8G8_TYPELESS,                              // RG8_SINT,
    DXGI_FORMAT_B8G8R8A8_TYPELESS,                          // BGRA8_UNORM
    DXGI_FORMAT_R8G8B8A8_TYPELESS,                          // RGBA8_UNORM,
    DXGI_FORMAT_R8G8B8A8_TYPELESS,                          // RGBA8_SNORM,
    DXGI_FORMAT_R8G8B8A8_TYPELESS,                          // RGBA8_UINT,
    DXGI_FORMAT_R8G8B8A8_TYPELESS,                          // RGBA8_SINT,
    DXGI_FORMAT_R8G8B8A8_TYPELESS,                          // RGBA8_SRGB,
    DXGI_FORMAT_R16_TYPELESS,                               // R16_UNORM,
    DXGI_FORMAT_R16_TYPELESS,                               // R16_SNORM,
    DXGI_FORMAT_R16_TYPELESS,                               // R16_UINT,
    DXGI_FORMAT_R16_TYPELESS,                               // R16_SINT,
    DXGI_FORMAT_R16_TYPELESS,                               // R16_SFLOAT,
    DXGI_FORMAT_R16G16_TYPELESS,                            // RG16_UNORM,
    DXGI_FORMAT_R16G16_TYPELESS,                            // RG16_SNORM,
    DXGI_FORMAT_R16G16_TYPELESS,                            // RG16_UINT,
    DXGI_FORMAT_R16G16_TYPELESS,                            // RG16_SINT,
    DXGI_FORMAT_R16G16_TYPELESS,                            // RG16_SFLOAT,
    DXGI_FORMAT_R16G16B16A16_TYPELESS,                      // RGBA16_UNORM,
    DXGI_FORMAT_R16G16B16A16_TYPELESS,                      // RGBA16_SNORM,
    DXGI_FORMAT_R16G16B16A16_TYPELESS,                      // RGBA16_UINT,
    DXGI_FORMAT_R16G16B16A16_TYPELESS,                      // RGBA16_SINT,
    DXGI_FORMAT_R16G16B16A16_TYPELESS,                      // RGBA16_SFLOAT,
    DXGI_FORMAT_R32_TYPELESS,                               // R32_UINT,
    DXGI_FORMAT_R32_TYPELESS,                               // R32_SINT,
    DXGI_FORMAT_R32_TYPELESS,                               // R32_SFLOAT,
    DXGI_FORMAT_R32G32_TYPELESS,                            // RG32_UINT,
    DXGI_FORMAT_R32G32_TYPELESS,                            // RG32_SINT,
    DXGI_FORMAT_R32G32_TYPELESS,                            // RG32_SFLOAT,
    DXGI_FORMAT_R32G32B32_TYPELESS,                         // RGB32_UINT,
    DXGI_FORMAT_R32G32B32_TYPELESS,                         // RGB32_SINT,
    DXGI_FORMAT_R32G32B32_TYPELESS,                         // RGB32_SFLOAT,
    DXGI_FORMAT_R32G32B32A32_TYPELESS,                      // RGBA32_UINT,
    DXGI_FORMAT_R32G32B32A32_TYPELESS,                      // RGBA32_SINT,
    DXGI_FORMAT_R32G32B32A32_TYPELESS,                      // RGBA32_SFLOAT,
    DXGI_FORMAT_R10G10B10A2_TYPELESS,                       // R10_G10_B10_A2_UNORM,
    DXGI_FORMAT_R10G10B10A2_TYPELESS,                       // R10_G10_B10_A2_UINT,
    DXGI_FORMAT_R11G11B10_FLOAT,                            // R11_G11_B10_UFLOAT,
    DXGI_FORMAT_R9G9B9E5_SHAREDEXP,                         // R9_G9_B9_E5_UFLOAT,
    DXGI_FORMAT_BC1_TYPELESS,                               // BC1_RGBA_UNORM,
    DXGI_FORMAT_BC1_TYPELESS,                               // BC1_RGBA_SRGB,
    DXGI_FORMAT_BC2_TYPELESS,                               // BC2_RGBA_UNORM,
    DXGI_FORMAT_BC2_TYPELESS,                               // BC2_RGBA_SRGB,
    DXGI_FORMAT_BC3_TYPELESS,                               // BC3_RGBA_UNORM,
    DXGI_FORMAT_BC3_TYPELESS,                               // BC3_RGBA_SRGB,
    DXGI_FORMAT_BC4_TYPELESS,                               // BC4_R_UNORM,
    DXGI_FORMAT_BC4_TYPELESS,                               // BC4_R_SNORM,
    DXGI_FORMAT_BC5_TYPELESS,                               // BC5_RG_UNORM,
    DXGI_FORMAT_BC5_TYPELESS,                               // BC5_RG_SNORM,
    DXGI_FORMAT_BC6H_TYPELESS,                              // BC6H_RGB_UFLOAT,
    DXGI_FORMAT_BC6H_TYPELESS,                              // BC6H_RGB_SFLOAT,
    DXGI_FORMAT_BC7_TYPELESS,                               // BC7_RGBA_UNORM,
    DXGI_FORMAT_BC7_TYPELESS,                               // BC7_RGBA_SRGB,
    DXGI_FORMAT_R16_TYPELESS,                               // D16_UNORM,
    DXGI_FORMAT_R24G8_TYPELESS,                             // D24_UNORM_S8_UINT,
    DXGI_FORMAT_R32_TYPELESS,                               // D32_SFLOAT,
    DXGI_FORMAT_R32G8X24_TYPELESS,                          // D32_SFLOAT_S8_UINT_X24_TYPELESS,
    DXGI_FORMAT_R24G8_TYPELESS,                             // D24_UNORM_X8_TYPELESS,
    DXGI_FORMAT_R24G8_TYPELESS,                             // X24_TYPLESS_S8_UINT,
    DXGI_FORMAT_R32G8X24_TYPELESS,                          // X32_TYPLESS_S8_UINT_X24_TYPELESS,
    DXGI_FORMAT_R32G8X24_TYPELESS                           // D32_SFLOAT_X8_TYPLESS_X24_TYPELESS,
};

DXGI_FORMAT GetTypelessFormat(Format format)
{
    return DXGI_TYPELESS_FORMATS[(uint32_t)format];
}

static constexpr std::array<bool, (uint32_t)Format::MAX_NUM> FP_FORMATS = {
    false,  // UNKNOWN
    true,   // R8_UNORM,
    true,   // R8_SNORM,
    false,  // R8_UINT,
    false,  // R8_SINT,
    true,   // RG8_UNORM,
    true,   // RG8_SNORM,
    false,  // RG8_UINT,
    false,  // RG8_SINT,
    true,   // BGRA8_UNORM,
    true,   // RGBA8_UNORM,
    true,   // RGBA8_SNORM,
    false,  // RGBA8_UINT,
    false,  // RGBA8_SINT,
    true,   // RGBA8_SRGB,
    true,   // R16_UNORM,
    true,   // R16_SNORM,
    false,  // R16_UINT,
    false,  // R16_SINT,
    true,   // R16_SFLOAT,
    true,   // RG16_UNORM,
    true,   // RG16_SNORM,
    false,  // RG16_UINT,
    false,  // RG16_SINT,
    true,   // RG16_SFLOAT,
    true,   // RGBA16_UNORM,
    true,   // RGBA16_SNORM,
    false,  // RGBA16_UINT,
    false,  // RGBA16_SINT,
    true,   // RGBA16_SFLOAT,
    false,  // R32_UINT,
    false,  // R32_SINT,
    true,   // R32_SFLOAT,
    false,  // RG32_UINT,
    false,  // RG32_SINT,
    true,   // RG32_SFLOAT,
    false,  // RGB32_UINT,
    false,  // RGB32_SINT,
    true,   // RGB32_SFLOAT,
    false,  // RGBA32_UINT,
    false,  // RGBA32_SINT,
    true,   // RGBA32_SFLOAT,
    true,   // R10_G10_B10_A2_UNORM,
    false,  // R10_G10_B10_A2_UINT,
    true,   // R11_G11_B10_UFLOAT,
    true,   // R9_G9_B9_E5_UFLOAT,
    false,  // BC1_RGBA_UNORM,
    false,  // BC1_RGBA_SRGB,
    false,  // BC2_RGBA_UNORM,
    false,  // BC2_RGBA_SRGB,
    false,  // BC3_RGBA_UNORM,
    false,  // BC3_RGBA_SRGB,
    false,  // BC4_R_UNORM,
    false,  // BC4_R_SNORM,
    false,  // BC5_RG_UNORM,
    false,  // BC5_RG_SNORM,
    false,  // BC6H_RGB_UFLOAT,
    false,  // BC6H_RGB_SFLOAT,
    false,  // BC7_RGBA_UNORM,
    false,  // BC7_RGBA_SRGB,
    true,   // D16_UNORM,
    true,   // D24_UNORM_S8_UINT,
    true,   // D32_SFLOAT,
    true,   // D32_SFLOAT_S8_UINT_X24_TYPELESS,
    true,   // D24_UNORM_X8_TYPELESS,
    false,  // X24_TYPLESS_S8_UINT,
    false,  // X32_TYPLESS_S8_UINT_X24_TYPELESS,
    true    // D32_SFLOAT_X8_TYPLESS_X24_TYPELESS,
};

bool IsFloatingPointFormat(Format format)
{
    return FP_FORMATS[(uint32_t)format];
}

static constexpr std::array<uint32_t, (uint32_t)Format::MAX_NUM> FORMAT_BYTE_SIZES = {
    1,      // UNKNOWN
    1,      // R8_UNORM,
    1,      // R8_SNORM,
    1,      // R8_UINT,
    1,      // R8_SINT,
    2,      // RG8_UNORM,
    2,      // RG8_SNORM,
    2,      // RG8_UINT,
    2,      // RG8_SINT,
    4,      // BGRA8_UNORM,
    4,      // RGBA8_UNORM,
    4,      // RGBA8_SNORM,
    4,      // RGBA8_UINT,
    4,      // RGBA8_SINT,
    4,      // RGBA8_SRGB,
    2,      // R16_UNORM,
    2,      // R16_SNORM,
    2,      // R16_UINT,
    2,      // R16_SINT,
    2,      // R16_SFLOAT,
    4,      // RG16_UNORM,
    4,      // RG16_SNORM,
    4,      // RG16_UINT,
    4,      // RG16_SINT,
    4,      // RG16_SFLOAT,
    8,      // RGBA16_UNORM,
    8,      // RGBA16_SNORM,
    8,      // RGBA16_UINT,
    8,      // RGBA16_SINT,
    8,      // RGBA16_SFLOAT,
    4,      // R32_UINT,
    4,      // R32_SINT,
    4,      // R32_SFLOAT,
    8,      // RG32_UINT,
    8,      // RG32_SINT,
    8,      // RG32_SFLOAT,
    12,     // RGB32_UINT,
    12,     // RGB32_SINT,
    12,     // RGB32_SFLOAT,
    16,     // RGBA32_UINT,
    16,     // RGBA32_SINT,
    16,     // RGBA32_SFLOAT,
    4,      // R10_G10_B10_A2_UNORM,
    4,      // R10_G10_B10_A2_UINT,
    4,      // R11_G11_B10_UFLOAT,
    4       // R9_G9_B9_E5_UFLOAT,
};

uint32_t GetFormatByteSize(Format format)
{
    return FORMAT_BYTE_SIZES[(uint32_t)format];
}

static constexpr std::array<Format, 100> FORMATS = {
    Format::UNKNOWN,                            // DXGI_FORMAT_UNKNOWN = 0,
    Format::UNKNOWN,                            // DXGI_FORMAT_R32G32B32A32_TYPELESS = 1,
    Format::RGBA32_SFLOAT,                      // DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
    Format::RGBA32_UINT,                        // DXGI_FORMAT_R32G32B32A32_UINT = 3,
    Format::RGBA32_SINT,                        // DXGI_FORMAT_R32G32B32A32_SINT = 4,
    Format::UNKNOWN,                            // DXGI_FORMAT_R32G32B32_TYPELESS = 5,
    Format::RGB32_SFLOAT,                       // DXGI_FORMAT_R32G32B32_FLOAT = 6,
    Format::RGB32_UINT,                         // DXGI_FORMAT_R32G32B32_UINT = 7,
    Format::RGB32_SINT,                         // DXGI_FORMAT_R32G32B32_SINT = 8,
    Format::UNKNOWN,                            // DXGI_FORMAT_R16G16B16A16_TYPELESS = 9,
    Format::RGBA16_SFLOAT,                      // DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
    Format::RGBA16_UNORM,                       // DXGI_FORMAT_R16G16B16A16_UNORM = 11,
    Format::RGBA16_UINT,                        // DXGI_FORMAT_R16G16B16A16_UINT = 12,
    Format::RGBA16_SNORM,                       // DXGI_FORMAT_R16G16B16A16_SNORM = 13,
    Format::RGBA16_SINT,                        // DXGI_FORMAT_R16G16B16A16_SINT = 14,
    Format::UNKNOWN,                            // DXGI_FORMAT_R32G32_TYPELESS = 15,
    Format::RG32_SFLOAT,                        // DXGI_FORMAT_R32G32_FLOAT = 16,
    Format::RG32_UINT,                          // DXGI_FORMAT_R32G32_UINT = 17,
    Format::RGB32_SINT,                         // DXGI_FORMAT_R32G32_SINT = 18,
    Format::UNKNOWN,                            // DXGI_FORMAT_R32G8X24_TYPELESS = 19,
    Format::D32_SFLOAT_S8_UINT_X24,             // DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20,
    Format::R32_SFLOAT_X8_X24,                  // DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
    Format::X32_R8_UINT_X24,                    // DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = 22,
    Format::UNKNOWN,                            // DXGI_FORMAT_R10G10B10A2_TYPELESS = 23,
    Format::R10_G10_B10_A2_UNORM,               // DXGI_FORMAT_R10G10B10A2_UNORM = 24,
    Format::R10_G10_B10_A2_UINT,                // DXGI_FORMAT_R10G10B10A2_UINT = 25,
    Format::R11_G11_B10_UFLOAT,                 // DXGI_FORMAT_R11G11B10_FLOAT = 26,
    Format::UNKNOWN,                            // DXGI_FORMAT_R8G8B8A8_TYPELESS = 27,
    Format::RGBA8_UNORM,                        // DXGI_FORMAT_R8G8B8A8_UNORM = 28,
    Format::RGBA8_SRGB,                         // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
    Format::RGBA8_UINT,                         // DXGI_FORMAT_R8G8B8A8_UINT = 30,
    Format::RGBA8_SNORM,                        // DXGI_FORMAT_R8G8B8A8_SNORM = 31,
    Format::RGBA8_SINT,                         // DXGI_FORMAT_R8G8B8A8_SINT = 32,
    Format::UNKNOWN,                            // DXGI_FORMAT_R16G16_TYPELESS = 33,
    Format::RG16_SFLOAT,                        // DXGI_FORMAT_R16G16_FLOAT = 34,
    Format::RG16_UNORM,                         // DXGI_FORMAT_R16G16_UNORM = 35,
    Format::RG16_UINT,                          // DXGI_FORMAT_R16G16_UINT = 36,
    Format::RG16_SNORM,                         // DXGI_FORMAT_R16G16_SNORM = 37,
    Format::RG16_SINT,                          // DXGI_FORMAT_R16G16_SINT = 38,
    Format::UNKNOWN,                            // DXGI_FORMAT_R32_TYPELESS = 39,
    Format::D32_SFLOAT,                         // DXGI_FORMAT_D32_FLOAT = 40,
    Format::R32_SFLOAT,                         // DXGI_FORMAT_R32_FLOAT = 41,
    Format::R32_UINT,                           // DXGI_FORMAT_R32_UINT = 42,
    Format::R32_SINT,                           // DXGI_FORMAT_R32_SINT = 43,
    Format::UNKNOWN,                            // DXGI_FORMAT_R24G8_TYPELESS = 44,
    Format::D24_UNORM_S8_UINT,                  // DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
    Format::R24_UNORM_X8,                       // DXGI_FORMAT_R24_UNORM_X8_TYPELESS = 46,
    Format::UNKNOWN,                            // DXGI_FORMAT_X24_TYPELESS_G8_UINT = 47,
    Format::UNKNOWN,                            // DXGI_FORMAT_R8G8_TYPELESS = 48,
    Format::RG8_UNORM,                          // DXGI_FORMAT_R8G8_UNORM = 49,
    Format::RG8_UINT,                           // DXGI_FORMAT_R8G8_UINT = 50,
    Format::RG8_SNORM,                          // DXGI_FORMAT_R8G8_SNORM = 51,
    Format::RG8_SINT,                           // DXGI_FORMAT_R8G8_SINT = 52,
    Format::UNKNOWN,                            // DXGI_FORMAT_R16_TYPELESS = 53,
    Format::R16_SFLOAT,                         // DXGI_FORMAT_R16_FLOAT = 54,
    Format::D16_UNORM,                          // DXGI_FORMAT_D16_UNORM = 55,
    Format::R16_UNORM,                          // DXGI_FORMAT_R16_UNORM = 56,
    Format::R16_UINT,                           // DXGI_FORMAT_R16_UINT = 57,
    Format::R16_SNORM,                          // DXGI_FORMAT_R16_SNORM = 58,
    Format::R16_SINT,                           // DXGI_FORMAT_R16_SINT = 59,
    Format::UNKNOWN,                            // DXGI_FORMAT_R8_TYPELESS = 60,
    Format::R8_UNORM,                           // DXGI_FORMAT_R8_UNORM = 61,
    Format::R8_UINT,                            // DXGI_FORMAT_R8_UINT = 62,
    Format::R8_SNORM,                           // DXGI_FORMAT_R8_SNORM = 63,
    Format::R8_SINT,                            // DXGI_FORMAT_R8_SINT = 64,
    Format::UNKNOWN,                            // DXGI_FORMAT_A8_UNORM = 65,
    Format::UNKNOWN,                            // DXGI_FORMAT_R1_UNORM = 66,
    Format::R9_G9_B9_E5_UFLOAT,                 // DXGI_FORMAT_R9G9B9E5_SHAREDEXP = 67,
    Format::UNKNOWN,                            // DXGI_FORMAT_R8G8_B8G8_UNORM = 68,
    Format::UNKNOWN,                            // DXGI_FORMAT_G8R8_G8B8_UNORM = 69,
    Format::UNKNOWN,                            // DXGI_FORMAT_BC1_TYPELESS = 70,
    Format::BC1_RGBA_UNORM,                     // DXGI_FORMAT_BC1_UNORM = 71,
    Format::BC1_RGBA_SRGB,                      // DXGI_FORMAT_BC1_UNORM_SRGB = 72,
    Format::UNKNOWN,                            // DXGI_FORMAT_BC2_TYPELESS = 73,
    Format::BC2_RGBA_UNORM,                     // DXGI_FORMAT_BC2_UNORM = 74,
    Format::BC2_RGBA_SRGB,                      // DXGI_FORMAT_BC2_UNORM_SRGB = 75,
    Format::UNKNOWN,                            // DXGI_FORMAT_BC3_TYPELESS = 76,
    Format::BC3_RGBA_UNORM,                     // DXGI_FORMAT_BC3_UNORM = 77,
    Format::BC3_RGBA_SRGB,                      // DXGI_FORMAT_BC3_UNORM_SRGB = 78,
    Format::UNKNOWN,                            // DXGI_FORMAT_BC4_TYPELESS = 79,
    Format::BC4_R_UNORM,                        // DXGI_FORMAT_BC4_UNORM = 80,
    Format::BC4_R_SNORM,                        // DXGI_FORMAT_BC4_SNORM = 81,
    Format::UNKNOWN,                            // DXGI_FORMAT_BC5_TYPELESS = 82,
    Format::BC5_RG_UNORM,                       // DXGI_FORMAT_BC5_UNORM = 83,
    Format::BC5_RG_SNORM,                       // DXGI_FORMAT_BC5_SNORM = 84,
    Format::UNKNOWN,                            // DXGI_FORMAT_B5G6R5_UNORM = 85,
    Format::UNKNOWN,                            // DXGI_FORMAT_B5G5R5A1_UNORM = 86,
    Format::BGRA8_UNORM,                        // DXGI_FORMAT_B8G8R8A8_UNORM = 87,
    Format::UNKNOWN,                            // DXGI_FORMAT_B8G8R8X8_UNORM = 88,
    Format::UNKNOWN,                            // DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89,
    Format::UNKNOWN,                            // DXGI_FORMAT_B8G8R8A8_TYPELESS = 90,
    Format::UNKNOWN,                            // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
    Format::UNKNOWN,                            // DXGI_FORMAT_B8G8R8X8_TYPELESS = 92,
    Format::UNKNOWN,                            // DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = 93,
    Format::UNKNOWN,                            // DXGI_FORMAT_BC6H_TYPELESS = 94,
    Format::BC6H_RGB_UFLOAT,                    // DXGI_FORMAT_BC6H_UF16 = 95,
    Format::BC6H_RGB_SFLOAT,                    // DXGI_FORMAT_BC6H_SF16 = 96,
    Format::UNKNOWN,                            // DXGI_FORMAT_BC7_TYPELESS = 97,
    Format::BC7_RGBA_UNORM,                     // DXGI_FORMAT_BC7_UNORM = 98,
    Format::BC7_RGBA_SRGB                       // DXGI_FORMAT_BC7_UNORM_SRGB = 99,
};

Format GetFormat(DXGI_FORMAT format)
{
    return FORMATS[(uint32_t)format];
}

DXGI_FORMAT GetShaderFormatForDepth(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_D16_UNORM:                return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT:                return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:     return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default: return format;
    }
}

constexpr TextureType TEXTURE_TYPE_TABLE[] = {
    TextureType::MAX_NUM,     // D3D12_RESOURCE_DIMENSION_UNKNOWN = 0,
    TextureType::MAX_NUM,     // D3D12_RESOURCE_DIMENSION_BUFFER = 1,
    TextureType::TEXTURE_1D,  // D3D12_RESOURCE_DIMENSION_TEXTURE1D = 2,
    TextureType::TEXTURE_2D,  // D3D12_RESOURCE_DIMENSION_TEXTURE2D = 3,
    TextureType::TEXTURE_3D,  // D3D12_RESOURCE_DIMENSION_TEXTURE3D = 4
};
static_assert(D3D12_RESOURCE_DIMENSION_TEXTURE1D == 2, "unexpected value");
static_assert(D3D12_RESOURCE_DIMENSION_TEXTURE2D == 3, "unexpected value");
static_assert(D3D12_RESOURCE_DIMENSION_TEXTURE3D == 4, "unexpected value");

namespace nri
{
    uint64_t GetMemorySizeD3D12(const MemoryD3D12Desc& memoryD3D12Desc)
    {
        return memoryD3D12Desc.d3d12Heap->GetDesc().SizeInBytes;
    }

    void GetTextureDescD3D12(const TextureD3D12Desc& textureD3D12Desc, TextureDesc& textureDesc)
    {
        const D3D12_RESOURCE_DESC desc = textureD3D12Desc.d3d12Resource->GetDesc();

        textureDesc = {};
        textureDesc.type = TEXTURE_TYPE_TABLE[desc.Dimension];
    
        textureDesc.usageMask = (TextureUsageBits)0xffff;
        static_assert(sizeof(TextureUsageBits) == sizeof(uint16_t), "invalid sizeof");

        textureDesc.format = GetFormatDXGI(desc.Format);
        textureDesc.size[0] = (uint16_t)desc.Width;
        textureDesc.size[1] = (uint16_t)desc.Height;
        textureDesc.size[2] = textureDesc.type == TextureType::TEXTURE_3D ? desc.DepthOrArraySize : 1;
        textureDesc.mipNum = desc.MipLevels;
        textureDesc.arraySize = textureDesc.type == TextureType::TEXTURE_3D ? 1 : desc.DepthOrArraySize;
        textureDesc.sampleNum = desc.SampleDesc.Count;
        textureDesc.physicalDeviceMask = 0x1; // unsupported in D3D12
    }

    void GetBufferDescD3D12(const BufferD3D12Desc& bufferD3D12Desc, BufferDesc& bufferDesc)
    {
        const D3D12_RESOURCE_DESC desc = bufferD3D12Desc.d3d12Resource->GetDesc();

        bufferDesc = {};

        bufferDesc.usageMask = (BufferUsageBits)0xffff;
        static_assert(sizeof(BufferUsageBits) == sizeof(uint16_t), "invalid sizeof");

        bufferDesc.size = desc.Width;
        bufferDesc.structureStride = bufferD3D12Desc.structureStride;
        bufferDesc.physicalDeviceMask = 0x1; // unsupported in D3D12
    }
}

#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__

D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE GetAccelerationStructureType(AccelerationStructureType accelerationStructureType)
{
    static_assert(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL == (uint32_t)AccelerationStructureType::TOP_LEVEL, "Unsupported AccelerationStructureType.");
    static_assert(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL == (uint32_t)AccelerationStructureType::BOTTOM_LEVEL, "Unsupported AccelerationStructureType.");

    return (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE)accelerationStructureType;
}

D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS GetAccelerationStructureBuildFlags(AccelerationStructureBuildBits accelerationStructureBuildFlags)
{
    static_assert(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE == (uint32_t)AccelerationStructureBuildBits::ALLOW_UPDATE, "Unsupported AccelerationStructureBuildBits.");
    static_assert(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION == (uint32_t)AccelerationStructureBuildBits::ALLOW_COMPACTION, "Unsupported AccelerationStructureBuildBits.");
    static_assert(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE == (uint32_t)AccelerationStructureBuildBits::PREFER_FAST_TRACE, "Unsupported AccelerationStructureBuildBits.");
    static_assert(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD == (uint32_t)AccelerationStructureBuildBits::PREFER_FAST_BUILD, "Unsupported AccelerationStructureBuildBits.");
    static_assert(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY == (uint32_t)AccelerationStructureBuildBits::MINIMIZE_MEMORY, "Unsupported AccelerationStructureBuildBits.");

    return (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)accelerationStructureBuildFlags;
}

D3D12_RAYTRACING_GEOMETRY_TYPE GetGeometryType(GeometryType geometryType)
{
    static_assert(D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES == (uint32_t)GeometryType::TRIANGLES, "Unsupported GeometryType.");
    static_assert(D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS == (uint32_t)GeometryType::AABBS, "Unsupported GeometryType.");

    return (D3D12_RAYTRACING_GEOMETRY_TYPE)geometryType;
}

D3D12_RAYTRACING_GEOMETRY_FLAGS GetGeometryFlags(BottomLevelGeometryBits geometryFlagMask)
{
    static_assert(D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE == (uint32_t)BottomLevelGeometryBits::OPAQUE_GEOMETRY, "Unsupported GeometryFlagMask.");
    static_assert(D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION == (uint32_t)BottomLevelGeometryBits::NO_DUPLICATE_ANY_HIT_INVOCATION, "Unsupported GeometryFlagMask.");

    return (D3D12_RAYTRACING_GEOMETRY_FLAGS)geometryFlagMask;
}

void ConvertGeometryDescs(D3D12_RAYTRACING_GEOMETRY_DESC* geometryDescs, const GeometryObject* geometryObjects, uint32_t geometryObjectNum)
{
    for (uint32_t i = 0; i < geometryObjectNum; i++)
    {
        geometryDescs[i].Type = GetGeometryType(geometryObjects[i].type);
        geometryDescs[i].Flags = GetGeometryFlags(geometryObjects[i].flags);

        if (geometryDescs[i].Type == D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES)
        {
            const Triangles& triangles = geometryObjects[i].triangles;
            geometryDescs[i].Triangles.Transform3x4 = triangles.transformBuffer ? ((BufferD3D12*)triangles.transformBuffer)->GetPointerGPU() + triangles.transformOffset : 0;
            geometryDescs[i].Triangles.IndexFormat = triangles.indexType == IndexType::UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            geometryDescs[i].Triangles.VertexFormat = GetFormat(triangles.vertexFormat);
            geometryDescs[i].Triangles.IndexCount = triangles.indexNum;
            geometryDescs[i].Triangles.VertexCount = triangles.vertexNum;
            geometryDescs[i].Triangles.IndexBuffer = triangles.indexBuffer ? ((BufferD3D12*)triangles.indexBuffer)->GetPointerGPU() + triangles.indexOffset : 0;
            geometryDescs[i].Triangles.VertexBuffer.StartAddress = ((BufferD3D12*)triangles.vertexBuffer)->GetPointerGPU() + triangles.vertexOffset;
            geometryDescs[i].Triangles.VertexBuffer.StrideInBytes = triangles.vertexStride;
        }
        else if (geometryDescs[i].Type == D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS)
        {
            const AABBs& aabbs = geometryObjects[i].boxes;
            geometryDescs[i].AABBs.AABBCount = aabbs.boxNum;
            geometryDescs[i].AABBs.AABBs.StartAddress = ((BufferD3D12*)aabbs.buffer)->GetPointerGPU() + aabbs.offset;
            geometryDescs[i].AABBs.AABBs.StrideInBytes = aabbs.stride;
        }
    }
}

D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE GetCopyMode(CopyMode copyMode)
{
    static_assert(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE == (uint32_t)CopyMode::CLONE, "Unsupported CopyMode.");
    static_assert(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT == (uint32_t)CopyMode::COMPACT, "Unsupported CopyMode.");

    return (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE)copyMode;
}

#endif
