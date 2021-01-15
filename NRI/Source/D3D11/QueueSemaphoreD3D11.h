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

    struct QueueSemaphoreD3D11
    {
        QueueSemaphoreD3D11(DeviceD3D11& device);
        ~QueueSemaphoreD3D11();

        inline DeviceD3D11& GetDevice() const
        { return m_Device; }

        inline void Wait()
        {}

        inline void Signal()
        {}

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);

    private:
        DeviceD3D11& m_Device;
    };
}
