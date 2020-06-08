/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <d3d12.h>

#include "SharedExternal.h"
#include "DeviceBase.h"

#define NRI_TEMP_NODE_MASK 0x1
#define SET_D3D_DEBUG_OBJECT_NAME(obj, name) if (obj) obj->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)std::strlen(name), name)
