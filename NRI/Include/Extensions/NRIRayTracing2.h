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
    struct AccelerationStructure2;
    struct RayTracingShaderLibrary2;

    typedef void* VirtualAddressGPU2;

    enum class GeometryType2
    {
        TRIANGLES,
        AABBS,
        MAX_NUM
    };

    enum class AccelerationStructureType2
    {
        TOP_LEVEL,
        BOTTOM_LEVEL,
        MAX_NUM
    };

    NRI_DECLARE_ENUM_CLASS_WITH_OPS(GeometryFlagBits2, uint32_t)
    {
        NONE                                = 0,
        OPAQUE_GEOMETRY                     = SetBit(0),
        NO_DUPLICATE_ANY_HIT_INVOCATION     = SetBit(1)
    };

    NRI_DECLARE_ENUM_CLASS_WITH_OPS(BottomLevelInstanceFlagBits2, uint8_t)
    {
        NONE                                = 0,
        TRIANGLE_CULL_DISABLE               = SetBit(0),
        TRIANGLE_FRONT_COUNTERCLOCKWISE     = SetBit(1),
        FORCE_OPAQUE                        = SetBit(2),
        FORCE_NON_OPAQUE                    = SetBit(3)
    };

    NRI_DECLARE_ENUM_CLASS_WITH_OPS(AccelerationStructureBuildFlagBits2, uint32_t)
    {
        NONE                                = 0,
        ALLOW_UPDATE                        = SetBit(0),
        ALLOW_COMPACTION                    = SetBit(1),
        PREFER_FAST_TRACE                   = SetBit(2),
        PREFER_FAST_BUILD                   = SetBit(3),
        LOW_MEMORY                          = SetBit(4)
    };

    struct RayTracingShaderLibraryDesc2
    {
        const ShaderDesc* shaderDescs;
        uint32_t shaderNum;
    };

    struct ShaderGroupDesc2
    {
        uint32_t shaderIndices[3];
    };

    struct RayTracingPipelineDesc2
    {
        const PipelineLayout* pipelineLayout;
        const RayTracingShaderLibrary2* shaderLibraries;
        uint32_t shaderLibraryNum;
        const ShaderGroupDesc2* shaderGroupDescs;
        uint32_t shaderGroupDescNum;
        uint32_t recursionDepthMax;
        uint32_t payloadAttributeSizeMax;
        uint32_t intersectionAttributeSizeMax;
        uint32_t callableAttributeSizeMax;
    };

    struct Triangles2
    {
        VirtualAddressGPU2 vertices;
        Format vertexFormat;
        uint32_t vertexStride;
        VirtualAddressGPU2 indices;
        IndexType indexType;
        VirtualAddressGPU2 transforms;
    };

    struct AABBs2
    {
        VirtualAddressGPU2 boxes;
        uint32_t stride;
    };

    struct GeometryObject2
    {
        GeometryType2 type;
        GeometryFlagBits2 flags;
        union
        {
            Triangles2 triangles;
            AABBs2 boxes;
        };
    };

    struct AABB2
    {
        float minX;
        float minY;
        float minZ;
        float maxX;
        float maxY;
        float maxZ;
    };

    struct BottomLeveLInstance2
    {
        float transform[3][4];
        uint32_t instanceCustomIndex:24;
        uint32_t mask:8;
        uint32_t instanceShaderBindingTableRecordOffset:24;
        BottomLevelInstanceFlagBits2 flags:8;
        VirtualAddressGPU2 accelerationStructure;
    };

    struct AccelerationStructureDesc2
    {
        AccelerationStructureType2 type;
        AccelerationStructureBuildFlagBits2 flags;
        uint32_t physicalDeviceMask;
        uint32_t instanceOrGeometryObjectNum;
    };

    struct AccelerationStructureMemoryBindingDesc2
    {
        Memory* memory;
        AccelerationStructure2* accelerationStructure;
        uint64_t offset;
        uint32_t physicalDeviceMask;
    };

    struct StridedBufferRegion2
    {
        const Buffer* buffer;
        uint64_t offset;
        uint64_t size;
        uint64_t stride;
    };

    struct DispatchRaysDesc2
    {
        StridedBufferRegion2 raygenShader;
        StridedBufferRegion2 missShaders;
        StridedBufferRegion2 hitShaderGroups;
        StridedBufferRegion2 callableShaders;
        uint32_t width;
        uint32_t height;
        uint32_t depth;
    };

    struct TopLevelAccelerationStructureBuildDesc
    {
        AccelerationStructure2* accelerationStructure;
        VirtualAddressGPU2 scratchBuffer;
        VirtualAddressGPU2 instances;
        uint32_t instanceNum;
        bool isArrayOfPointers;
    };

    struct BottomLevelAccelerationStructureBuildDesc
    {
        AccelerationStructure2* accelerationStructure;
        VirtualAddressGPU2 scratchBuffer;
        GeometryObject2* geometries;
        uint32_t geometryNum;
        bool isArrayOfPointers;
    };

    struct RayTracingInterface2
    {
        Result (NRI_CALL *CreateRayTracingShaderLibrary)(Device& device, const RayTracingShaderLibraryDesc2& shaderLibraryDesc, RayTracingShaderLibrary2*& shaderLibrary);

        Result (NRI_CALL *CreateRayTracingPipeline)(Device& device, const RayTracingPipelineDesc2& rayTracingPipelineDesc, Pipeline*& pipeline);
        Result (NRI_CALL *CreateAccelerationStructure)(Device& device, const AccelerationStructureDesc2& accelerationStructureDesc, AccelerationStructure2*& accelerationStructure);
        Result (NRI_CALL *BindAccelerationStructureMemory)(Device& device, const AccelerationStructureMemoryBindingDesc2* memoryBindingDescs, uint32_t memoryBindingDescNum);

        Result (NRI_CALL *CreateAccelerationStructureDescriptor)(const AccelerationStructure2& accelerationStructure, uint32_t physicalDeviceMask, Descriptor*& descriptor);
        void (NRI_CALL *SetAccelerationStructureDebugName)(AccelerationStructure2& accelerationStructure, const char* name);
        void (NRI_CALL *DestroyAccelerationStructure)(AccelerationStructure2& accelerationStructure);

        void (NRI_CALL *GetAccelerationStructureMemoryInfo)(const AccelerationStructure2& accelerationStructure, MemoryDesc& memoryDesc);
        uint64_t (NRI_CALL *GetAccelerationStructureUpdateScratchBufferSize)(const AccelerationStructure2& accelerationStructure);
        uint64_t (NRI_CALL *GetAccelerationStructureBuildScratchBufferSize)(const AccelerationStructure2& accelerationStructure);
        Result (NRI_CALL *WriteShaderGroupIdentifiers)(const Pipeline& pipeline, uint32_t baseShaderGroupIndex, uint32_t shaderGroupNum, void* buffer); // TODO: add stride

        VirtualAddressGPU2 (NRI_CALL *GetBufferAddress)(const AccelerationStructure2& accelerationStructure, uint32_t physicalDeviceIndex);
        VirtualAddressGPU2 (NRI_CALL *GetAccelerationStructureAddress)(const AccelerationStructure2& accelerationStructure, uint32_t physicalDeviceIndex);

        void (NRI_CALL *CmdBuildTopLevelAccelerationStructure)(CommandBuffer& commandBuffer, const TopLevelAccelerationStructureBuildDesc& desc);
        void (NRI_CALL *CmdBuildBottomLevelAccelerationStructure)(CommandBuffer& commandBuffer, const BottomLevelAccelerationStructureBuildDesc& desc);
        void (NRI_CALL *CmdUpdateTopLevelAccelerationStructure)(CommandBuffer& commandBuffer, const TopLevelAccelerationStructureBuildDesc& desc);
        void (NRI_CALL *CmdUpdateBottomLevelAccelerationStructure)(CommandBuffer& commandBuffer, const BottomLevelAccelerationStructureBuildDesc& desc);

        void (NRI_CALL *CmdCopyAccelerationStructure)(CommandBuffer& commandBuffer, AccelerationStructure2& dst, AccelerationStructure2& src);
        void (NRI_CALL *CmdCompactAccelerationStructure)(CommandBuffer& commandBuffer, AccelerationStructure2& dst, AccelerationStructure2& src);
        void (NRI_CALL *CmdWriteAccelerationStructureSize)(CommandBuffer& commandBuffer, const AccelerationStructure2* const* accelerationStructures, uint32_t accelerationStructureNum, QueryPool& queryPool, uint32_t queryPoolOffset);

        void (NRI_CALL *CmdDispatchRays)(CommandBuffer& commandBuffer, const DispatchRaysDesc2& dispatchRaysDesc);
    };
}
