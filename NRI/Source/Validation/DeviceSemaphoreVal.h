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
    struct DeviceSemaphoreVal : public DeviceObjectVal<DeviceSemaphore>
    {
        DeviceSemaphoreVal(DeviceVal& device, DeviceSemaphore& deviceSemaphore);

        void Create(bool signaled);

        bool IsUnsignaled() const;

        void Signal();
        void Wait();

        void SetDebugName(const char* name);

    private:
        uint64_t m_Value = 0;
    };

    inline bool DeviceSemaphoreVal::IsUnsignaled() const
    {
        return (m_Value & 0x1) == 0;
    }
}
