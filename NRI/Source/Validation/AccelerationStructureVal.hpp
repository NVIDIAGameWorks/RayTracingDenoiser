/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

static void NRI_CALL GetAccelerationStructureMemoryInfo(const AccelerationStructure& accelerationStructure, MemoryDesc& memoryDesc)
{
    ((const AccelerationStructureVal&)accelerationStructure).GetMemoryInfo(memoryDesc);
}

static uint64_t NRI_CALL GetAccelerationStructureUpdateScratchBufferSize(const AccelerationStructure& accelerationStructure)
{
    return ((const AccelerationStructureVal&)accelerationStructure).GetUpdateScratchBufferSize();
}

static uint64_t NRI_CALL GetAccelerationStructureBuildScratchBufferSize(const AccelerationStructure& accelerationStructure)
{
    return ((const AccelerationStructureVal&)accelerationStructure).GetBuildScratchBufferSize();
}

static uint64_t NRI_CALL GetAccelerationStructureHandle(const AccelerationStructure& accelerationStructure, uint32_t physicalDeviceIndex)
{
    return ((const AccelerationStructureVal&)accelerationStructure).GetHandle(physicalDeviceIndex);
}

static Result NRI_CALL CreateAccelerationStructureDescriptor(const AccelerationStructure& accelerationStructure, uint32_t physicalDeviceIndex, Descriptor*& descriptor)
{
    return ((AccelerationStructureVal&)accelerationStructure).CreateDescriptor(physicalDeviceIndex, descriptor);
}

static void NRI_CALL SetAccelerationStructureDebugName(AccelerationStructure& accelerationStructure, const char* name)
{
    ((AccelerationStructureVal&)accelerationStructure).SetDebugName(name);
}

void FillFunctionTableAccelarationStructureVal(RayTracingInterface& rayTracingInterface)
{
    rayTracingInterface.GetAccelerationStructureMemoryInfo = GetAccelerationStructureMemoryInfo;
    rayTracingInterface.GetAccelerationStructureUpdateScratchBufferSize = GetAccelerationStructureUpdateScratchBufferSize;
    rayTracingInterface.GetAccelerationStructureBuildScratchBufferSize = GetAccelerationStructureBuildScratchBufferSize;
    rayTracingInterface.GetAccelerationStructureHandle = GetAccelerationStructureHandle;
    rayTracingInterface.CreateAccelerationStructureDescriptor = CreateAccelerationStructureDescriptor;
    rayTracingInterface.SetAccelerationStructureDebugName = SetAccelerationStructureDebugName;
}