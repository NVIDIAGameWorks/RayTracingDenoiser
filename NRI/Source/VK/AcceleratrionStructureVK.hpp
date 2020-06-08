/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#pragma region [  RayTracingInterface  ]

static void NRI_CALL GetAccelerationStructureMemoryInfo(const AccelerationStructure& accelerationStructure, MemoryDesc& memoryDesc)
{
    ((AccelerationStructureVK&)accelerationStructure).GetMemoryInfo(memoryDesc);
}

static uint64_t NRI_CALL GetAccelerationStructureUpdateScratchBufferSize(const AccelerationStructure& accelerationStructure)
{
    return ((AccelerationStructureVK&)accelerationStructure).GetUpdateScratchBufferSize();
}

static uint64_t NRI_CALL GetAccelerationStructureBuildScratchBufferSize(const AccelerationStructure& accelerationStructure)
{
    return ((AccelerationStructureVK&)accelerationStructure).GetBuildScratchBufferSize();
}

static uint64_t NRI_CALL GetAccelerationStructureHandle(const AccelerationStructure& accelerationStructure, uint32_t physicalDeviceIndex)
{
    return ((AccelerationStructureVK&)accelerationStructure).GetNativeHandle(physicalDeviceIndex);
}

static void NRI_CALL SetAccelerationStructureDebugName(AccelerationStructure& accelerationStructure, const char* name)
{
    ((AccelerationStructureVK&)accelerationStructure).SetDebugName(name);
}

static Result NRI_CALL CreateAccelerationStructureDescriptor(const AccelerationStructure& accelerationStructure, uint32_t physicalDeviceIndex, Descriptor*& descriptor)
{
    return ((AccelerationStructureVK&)accelerationStructure).CreateDescriptor(physicalDeviceIndex, descriptor);
}

void FillFunctionTableAccelerationStructureVK(RayTracingInterface& rayTracingInterface)
{
    rayTracingInterface.GetAccelerationStructureMemoryInfo = GetAccelerationStructureMemoryInfo;
    rayTracingInterface.GetAccelerationStructureUpdateScratchBufferSize = GetAccelerationStructureUpdateScratchBufferSize;
    rayTracingInterface.GetAccelerationStructureBuildScratchBufferSize = GetAccelerationStructureBuildScratchBufferSize;
    rayTracingInterface.GetAccelerationStructureHandle = GetAccelerationStructureHandle;
    rayTracingInterface.SetAccelerationStructureDebugName = SetAccelerationStructureDebugName;
    rayTracingInterface.CreateAccelerationStructureDescriptor = CreateAccelerationStructureDescriptor;
}

#pragma endregion