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
    struct QueueSemaphoreVal : public DeviceObjectVal<QueueSemaphore>
    {
        QueueSemaphoreVal(DeviceVal& device, QueueSemaphore& queueSemaphore);

        void Signal();
        void Wait();

        void SetDebugName(const char* name);

    private:
        bool m_isSignaled = false;
    };
}
