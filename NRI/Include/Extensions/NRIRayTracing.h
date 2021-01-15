/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nri
{
    struct AccelerationStructure;

    enum class GeometryType
    {
        TRIANGLES,
        AABBS,
        MAX_NUM
    };

    enum class AccelerationStructureType
    {
        TOP_LEVEL,
        BOTTOM_LEVEL,
        MAX_NUM
    };

    enum class CopyMode
    {
        CLONE = 0,
        COMPACT = 1,
        MAX_NUM
    };

    NRI_DECLARE_ENUM_CLASS_WITH_OPS(BottomLevelGeometryBits, uint32_t)
    {
        NONE                                = 0,
        OPAQUE_GEOMETRY                     = SetBit(0),
        NO_DUPLICATE_ANY_HIT_INVOCATION     = SetBit(1)
    };

    NRI_DECLARE_ENUM_CLASS_WITH_OPS(TopLevelInstanceBits, uint32_t)
    {
        NONE                                = 0,
        TRIANGLE_CULL_DISABLE               = SetBit(0),
        TRIANGLE_FRONT_COUNTERCLOCKWISE     = SetBit(1),
        FORCE_OPAQUE                        = SetBit(2),
        FORCE_NON_OPAQUE                    = SetBit(3)
    };

    NRI_DECLARE_ENUM_CLASS_WITH_OPS(AccelerationStructureBuildBits, uint32_t)
    {
        NONE                                = 0,
        ALLOW_UPDATE                        = SetBit(0),
        ALLOW_COMPACTION                    = SetBit(1),
        PREFER_FAST_TRACE                   = SetBit(2),
        PREFER_FAST_BUILD                   = SetBit(3),
        MINIMIZE_MEMORY                     = SetBit(4)
    };

    struct ShaderLibrary
    {
        const ShaderDesc* shaderDescs;
        uint32_t shaderNum;
    };

    struct ShaderGroupDesc
    {
        uint32_t shaderIndices[3];
    };

    struct RayTracingPipelineDesc
    {
        const PipelineLayout* pipelineLayout;
        const ShaderLibrary* shaderLibrary;
        const ShaderGroupDesc* shaderGroupDescs; // TODO: move to ShaderLibrary
        uint32_t shaderGroupDescNum;
        uint32_t recursionDepthMax;
        uint32_t payloadAttributeSizeMax;
        uint32_t intersectionAttributeSizeMax;
    };

    struct Triangles
    {
        Buffer* vertexBuffer;
        uint64_t vertexOffset;
        uint32_t vertexNum;
        uint64_t vertexStride;
        Format vertexFormat;
        Buffer* indexBuffer;
        uint64_t indexOffset;
        uint32_t indexNum;
        IndexType indexType;
        Buffer* transformBuffer;
        uint64_t transformOffset;
    };

    struct AABBs
    {
        Buffer* buffer;
        uint32_t boxNum;
        uint32_t stride;
        uint64_t offset;
    };

    struct GeometryObject
    {
        GeometryType type;
        BottomLevelGeometryBits flags;
        union
        {
            Triangles triangles;
            AABBs boxes;
        };
    };

    struct GeometryObjectInstance
    {
        float transform[3][4];
        uint32_t instanceId : 24;
        uint32_t mask : 8;
        uint32_t shaderBindingTableLocalOffset : 24;
        TopLevelInstanceBits flags : 8;
        uint64_t accelerationStructureHandle;
    };

    struct AccelerationStructureDesc
    {
        AccelerationStructureType type;
        AccelerationStructureBuildBits flags;
        uint32_t physicalDeviceMask;
        uint32_t instanceOrGeometryObjectNum;
        const GeometryObject* geometryObjects;
    };

    struct AccelerationStructureMemoryBindingDesc
    {
        Memory* memory;
        AccelerationStructure* accelerationStructure;
        uint64_t offset;
        uint32_t physicalDeviceMask;
    };

    struct StridedBufferRegion
    {
        const Buffer* buffer;
        uint64_t offset;
        uint64_t size;
        uint64_t stride;
    };

    struct DispatchRaysDesc
    {
        StridedBufferRegion raygenShader;
        StridedBufferRegion missShaders;
        StridedBufferRegion hitShaderGroups;
        StridedBufferRegion callableShaders;
        uint32_t width;
        uint32_t height;
        uint32_t depth;
    };

    struct RayTracingInterface
    {
        Result (NRI_CALL *CreateRayTracingPipeline)(Device& device, const RayTracingPipelineDesc& rayTracingPipelineDesc, Pipeline*& pipeline);
        Result (NRI_CALL *CreateAccelerationStructure)(Device& device, const AccelerationStructureDesc& accelerationStructureDesc, AccelerationStructure*& accelerationStructure);
        Result (NRI_CALL *BindAccelerationStructureMemory)(Device& device, const AccelerationStructureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum);

        Result (NRI_CALL *CreateAccelerationStructureDescriptor)(const AccelerationStructure& accelerationStructure, uint32_t physicalDeviceMask, Descriptor*& descriptor);
        void (NRI_CALL *SetAccelerationStructureDebugName)(AccelerationStructure& accelerationStructure, const char* name);
        void (NRI_CALL *DestroyAccelerationStructure)(AccelerationStructure& accelerationStructure);

        void (NRI_CALL *GetAccelerationStructureMemoryInfo)(const AccelerationStructure& accelerationStructure, MemoryDesc& memoryDesc);
        uint64_t (NRI_CALL *GetAccelerationStructureUpdateScratchBufferSize)(const AccelerationStructure& accelerationStructure);
        uint64_t (NRI_CALL *GetAccelerationStructureBuildScratchBufferSize)(const AccelerationStructure& accelerationStructure);
        uint64_t (NRI_CALL *GetAccelerationStructureHandle)(const AccelerationStructure& accelerationStructure, uint32_t physicalDeviceIndex);
        Result (NRI_CALL *WriteShaderGroupIdentifiers)(const Pipeline& pipeline, uint32_t baseShaderGroupIndex, uint32_t shaderGroupNum, void* buffer); // TODO: add stride

        void (NRI_CALL *CmdBuildTopLevelAccelerationStructure)(CommandBuffer& commandBuffer, uint32_t instanceNum, const Buffer& buffer, uint64_t bufferOffset,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch, uint64_t scratchOffset);
        void (NRI_CALL *CmdBuildBottomLevelAccelerationStructure)(CommandBuffer& commandBuffer, uint32_t geometryObjectNum, const GeometryObject* geometryObjects,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch, uint64_t scratchOffset);
        void (NRI_CALL *CmdUpdateTopLevelAccelerationStructure)(CommandBuffer& commandBuffer, uint32_t instanceNum, const Buffer& buffer, uint64_t bufferOffset,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src, Buffer& scratch, uint64_t scratchOffset);
        void (NRI_CALL *CmdUpdateBottomLevelAccelerationStructure)(CommandBuffer& commandBuffer, uint32_t geometryObjectNum, const GeometryObject* geometryObjects,
            AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src, Buffer& scratch, uint64_t scratchOffset);

        void (NRI_CALL *CmdCopyAccelerationStructure)(CommandBuffer& commandBuffer, AccelerationStructure& dst, AccelerationStructure& src, CopyMode copyMode);
        void (NRI_CALL *CmdWriteAccelerationStructureSize)(CommandBuffer& commandBuffer, const AccelerationStructure* const* accelerationStructures, uint32_t accelerationStructureNum, QueryPool& queryPool, uint32_t queryPoolOffset);

        void (NRI_CALL *CmdDispatchRays)(CommandBuffer& commandBuffer, const DispatchRaysDesc& dispatchRaysDesc);
    };
}
