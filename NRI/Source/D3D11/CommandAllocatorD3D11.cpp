/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedD3D11.h"
#include "CommandAllocatorD3D11.h"

#include "CommandBufferD3D11.h"
#include "CommandBufferEmuD3D11.h"

using namespace nri;

Result CreateCommandBuffer(DeviceD3D11& deviceImpl, ID3D11DeviceContext* precreatedContext, CommandBuffer*& commandBuffer)
{
    void* impl;
    if (deviceImpl.GetDevice().isDeferredContextsEmulated)
        impl = Allocate<CommandBufferEmuD3D11>(deviceImpl.GetStdAllocator(), deviceImpl);
    else
        impl = Allocate<CommandBufferD3D11>(deviceImpl.GetStdAllocator(), deviceImpl);

    const nri::Result result = ((CommandBufferHelper*)impl)->Create(precreatedContext);

    if (result == nri::Result::SUCCESS)
    {
        commandBuffer = (CommandBuffer*)impl;
        return nri::Result::SUCCESS;
    }

    if (deviceImpl.GetDevice().isDeferredContextsEmulated)
        Deallocate(deviceImpl.GetStdAllocator(), (CommandBufferEmuD3D11*)impl);
    else
        Deallocate(deviceImpl.GetStdAllocator(), (CommandBufferD3D11*)impl);

    return result;
}

CommandAllocatorD3D11::CommandAllocatorD3D11(DeviceD3D11& device, const VersionedDevice& versionedDevice) :
    m_VersionedDevice(versionedDevice),
    m_Device(device)
{
}

CommandAllocatorD3D11::~CommandAllocatorD3D11()
{
}

inline void CommandAllocatorD3D11::SetDebugName(const char* name)
{
}

inline Result CommandAllocatorD3D11::CreateCommandBuffer(CommandBuffer*& commandBuffer)
{
    return ::CreateCommandBuffer(m_Device, nullptr, commandBuffer);
}

inline void CommandAllocatorD3D11::Reset()
{
}

#include "CommandAllocatorD3D11.hpp"
