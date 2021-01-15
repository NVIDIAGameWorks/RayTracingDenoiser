/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static void NRI_CALL SetBufferDebugName(Buffer& buffer, const char* name)
{
    ((BufferD3D12&)buffer).SetDebugName(name);
}

static void NRI_CALL GetBufferMemoryInfo(const Buffer& buffer, MemoryLocation memoryLocation, MemoryDesc& memoryDesc)
{
    ((BufferD3D12&)buffer).GetMemoryInfo(memoryLocation, memoryDesc);
}

static void* NRI_CALL MapBuffer(Buffer& buffer, uint64_t offset, uint64_t size)
{
    return ((BufferD3D12&)buffer).Map(offset, size);
}

static void NRI_CALL UnmapBuffer(Buffer& buffer)
{
    ((BufferD3D12&)buffer).Unmap();
}

void FillFunctionTableBufferD3D12(CoreInterface& coreInterface)
{
    coreInterface.SetBufferDebugName = SetBufferDebugName;
    coreInterface.GetBufferMemoryInfo = GetBufferMemoryInfo;
    coreInterface.MapBuffer = MapBuffer;
    coreInterface.UnmapBuffer = UnmapBuffer;
}

#pragma endregion
