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
    struct DeviceD3D11;

    struct CommandAllocatorD3D11
    {
        CommandAllocatorD3D11(DeviceD3D11& device, const VersionedDevice& versionedDevice);
        ~CommandAllocatorD3D11();

        inline DeviceD3D11& GetDevice() const
        { return m_Device; }

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);
        Result CreateCommandBuffer(CommandBuffer*& commandBuffer);
        void Reset();

    private:
        const VersionedDevice& m_VersionedDevice;
        DeviceD3D11& m_Device;
    };
}

nri::Result CreateCommandBuffer(nri::DeviceD3D11& deviceImpl, ID3D11DeviceContext* precreatedContext, nri::CommandBuffer*& commandBuffer);
