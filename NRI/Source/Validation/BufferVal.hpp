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
    ((BufferVal*)&buffer)->SetDebugName(name);
}

static void NRI_CALL GetBufferMemoryInfo(const Buffer& buffer, MemoryLocation memoryLocation, MemoryDesc& memoryDesc)
{
    ((BufferVal*)&buffer)->GetMemoryInfo(memoryLocation, memoryDesc);
}

static void* NRI_CALL MapBuffer(Buffer& buffer, uint64_t offset, uint64_t size)
{
    return ((BufferVal*)&buffer)->Map(offset, size);
}

static void NRI_CALL UnmapBuffer(Buffer& buffer)
{
    ((BufferVal*)&buffer)->Unmap();
}

void FillFunctionTableBufferVal(CoreInterface& coreInterface)
{
    coreInterface.SetBufferDebugName = SetBufferDebugName;
    coreInterface.GetBufferMemoryInfo = GetBufferMemoryInfo;
    coreInterface.MapBuffer = MapBuffer;
    coreInterface.UnmapBuffer = UnmapBuffer;
}

#pragma endregion

#pragma region [  WrapperD3D11Interface  ]

static ID3D11Resource* NRI_CALL GetBufferD3D11(const Buffer& buffer)
{
    return ((BufferVal*)&buffer)->GetBufferD3D11();
}

void FillFunctionTableBufferVal(WrapperD3D11Interface& wrapperD3D11Interface)
{
    wrapperD3D11Interface.GetBufferD3D11 = GetBufferD3D11;
}

#pragma endregion

#pragma region [  WrapperD3D12Interface  ]

static ID3D12Resource* NRI_CALL GetBufferD3D12(const Buffer& buffer)
{
    return ((BufferVal*)&buffer)->GetBufferD3D12();
}

void FillFunctionTableBufferVal(WrapperD3D12Interface& wrapperD3D12Interface)
{
    wrapperD3D12Interface.GetBufferD3D12 = GetBufferD3D12;
}

#pragma endregion
