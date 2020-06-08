/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#pragma region [  CoreInterface  ]

static void NRI_CALL SetBufferDebugName(Buffer& buffer, const char* name)
{
    ((BufferVK&)buffer).SetDebugName(name);
}

static void NRI_CALL GetBufferMemoryInfo(const Buffer& buffer, MemoryLocation memoryLocation, MemoryDesc& memoryDesc)
{
    ((BufferVK&)buffer).GetMemoryInfo(memoryLocation, memoryDesc);
}

static void* NRI_CALL MapBuffer(Buffer& buffer, uint64_t offset, uint64_t size)
{
    return ((BufferVK&)buffer).Map(offset, size);
}

static void NRI_CALL UnmapBuffer(Buffer& buffer)
{
    ((BufferVK&)buffer).Unmap();
}

void FillFunctionTableBufferVK(CoreInterface& coreInterface)
{
    coreInterface.SetBufferDebugName = SetBufferDebugName;
    coreInterface.GetBufferMemoryInfo = GetBufferMemoryInfo;
    coreInterface.MapBuffer = MapBuffer;
    coreInterface.UnmapBuffer = UnmapBuffer;
}

#pragma endregion
