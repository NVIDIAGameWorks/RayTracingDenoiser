/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedVal.h"
#include "FrameBufferVal.h"

using namespace nri;

FrameBufferVal::FrameBufferVal(DeviceVal& device, FrameBuffer& frameBuffer) :
    DeviceObjectVal(device, frameBuffer)
{
}

void FrameBufferVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetFrameBufferDebugName(m_ImplObject, name);
}

#include "FrameBufferVal.hpp"
