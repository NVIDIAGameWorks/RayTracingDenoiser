/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "DeviceBase.h"
#include "DeviceVal.h"
#include "SharedVal.h"
#include "PipelineVal.h"

using namespace nri;

PipelineVal::PipelineVal(DeviceVal& device, Pipeline& pipeline) :
    DeviceObjectVal(device, pipeline),
    m_RayTracingAPI(device.GetRayTracingInterface())
{
}

PipelineVal::PipelineVal(DeviceVal& device, Pipeline& pipeline, const GraphicsPipelineDesc& graphicsPipelineDesc) :
    DeviceObjectVal(device, pipeline),
    m_RayTracingAPI(device.GetRayTracingInterface()),
    m_PipelineLayout(graphicsPipelineDesc.pipelineLayout)
{
}

PipelineVal::PipelineVal(DeviceVal& device, Pipeline& pipeline, const ComputePipelineDesc& computePipelineDesc) :
    DeviceObjectVal(device, pipeline),
    m_RayTracingAPI(device.GetRayTracingInterface()),
    m_PipelineLayout(computePipelineDesc.pipelineLayout)
{
}

PipelineVal::PipelineVal(DeviceVal& device, Pipeline& pipeline, const RayTracingPipelineDesc& rayTracingPipelineDesc) :
    DeviceObjectVal(device, pipeline),
    m_RayTracingAPI(device.GetRayTracingInterface()),
    m_PipelineLayout(rayTracingPipelineDesc.pipelineLayout)
{
}

void PipelineVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetPipelineDebugName(m_ImplObject, name);
}

Result PipelineVal::WriteShaderGroupIdentifiers(uint32_t baseShaderGroupIndex, uint32_t shaderGroupNum, void* buffer)
{
    return m_RayTracingAPI.WriteShaderGroupIdentifiers(m_ImplObject, baseShaderGroupIndex, shaderGroupNum, buffer);
}

#include "PipelineVal.hpp"
