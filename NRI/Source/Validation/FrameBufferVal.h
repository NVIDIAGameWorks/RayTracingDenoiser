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
    struct FrameBufferVal : public DeviceObjectVal<FrameBuffer>
    {
        FrameBufferVal(DeviceVal& device, FrameBuffer& frameBuffer);

        void SetDebugName(const char* name);
    };
}
