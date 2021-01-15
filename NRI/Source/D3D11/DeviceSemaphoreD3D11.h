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
    struct DeviceD3D11;

    struct DeviceSemaphoreD3D11
    {
        DeviceSemaphoreD3D11(DeviceD3D11& device, const VersionedDevice& versionedDevice);
        ~DeviceSemaphoreD3D11();

        inline DeviceD3D11& GetDevice() const
        { return m_Device; }

        Result Create(bool signaled);
        void Signal(const VersionedContext& immediateContext);
        void Wait(const VersionedContext& immediateContext);

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);

    private:
        ComPtr<ID3D11Query1> m_Query;
        ComPtr<ID3D11Fence> m_Fence;
        void* m_Handle = nullptr;
        const VersionedDevice& m_VersionedDevice;
        uint64_t m_Value = 0;
		bool m_IgnoreWait = false;
        DeviceD3D11& m_Device;
    };
}
