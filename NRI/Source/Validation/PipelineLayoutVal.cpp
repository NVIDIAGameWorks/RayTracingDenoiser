/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

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
#include "PipelineLayoutVal.h"

using namespace nri;

PipelineLayoutVal::PipelineLayoutVal(DeviceVal& device, PipelineLayout& pipelineLayout, const PipelineLayoutDesc& pipelineLayoutDesc) :
    DeviceObjectVal(device, pipelineLayout),
    m_DescriptorSets(device.GetStdAllocator()),
    m_PushConstants(device.GetStdAllocator()),
    m_DescriptorRangeDescs(device.GetStdAllocator()),
    m_StaticSamplerDescs(device.GetStdAllocator()),
    m_DynamicConstantBufferDescs(device.GetStdAllocator())
{
    uint32_t descriptorRangeDescNum = 0;
    uint32_t staticSamplerDescNum = 0;
    uint32_t dynamicConstantBufferDescNum = 0;

    for (uint32_t i = 0; i < pipelineLayoutDesc.descriptorSetNum; i++)
    {
        descriptorRangeDescNum += pipelineLayoutDesc.descriptorSets[i].rangeNum;
        staticSamplerDescNum += pipelineLayoutDesc.descriptorSets[i].staticSamplerNum;
        dynamicConstantBufferDescNum += pipelineLayoutDesc.descriptorSets[i].dynamicConstantBufferNum;
    }

    m_DescriptorSets.insert(m_DescriptorSets.begin(), pipelineLayoutDesc.descriptorSets,
        pipelineLayoutDesc.descriptorSets + pipelineLayoutDesc.descriptorSetNum);

    m_PushConstants.insert(m_PushConstants.begin(), pipelineLayoutDesc.pushConstants,
        pipelineLayoutDesc.pushConstants + pipelineLayoutDesc.pushConstantNum);

    m_DescriptorRangeDescs.reserve(descriptorRangeDescNum);
    m_StaticSamplerDescs.reserve(staticSamplerDescNum);
    m_DynamicConstantBufferDescs.reserve(dynamicConstantBufferDescNum);

    for (uint32_t i = 0; i < pipelineLayoutDesc.descriptorSetNum; i++)
    {
        const DescriptorSetDesc& descriptorSetDesc = pipelineLayoutDesc.descriptorSets[i];

        m_DescriptorSets[i].ranges = m_DescriptorRangeDescs.data() + m_DescriptorRangeDescs.size();
        m_DescriptorSets[i].staticSamplers = m_StaticSamplerDescs.data() + m_StaticSamplerDescs.size();
        m_DescriptorSets[i].dynamicConstantBuffers = m_DynamicConstantBufferDescs.data() + m_DynamicConstantBufferDescs.size();

        m_DescriptorRangeDescs.insert(m_DescriptorRangeDescs.end(), descriptorSetDesc.ranges,
            descriptorSetDesc.ranges + descriptorSetDesc.rangeNum);

        m_StaticSamplerDescs.insert(m_StaticSamplerDescs.end(), descriptorSetDesc.staticSamplers,
            descriptorSetDesc.staticSamplers + descriptorSetDesc.staticSamplerNum);

        m_DynamicConstantBufferDescs.insert(m_DynamicConstantBufferDescs.end(), descriptorSetDesc.dynamicConstantBuffers,
            descriptorSetDesc.dynamicConstantBuffers + descriptorSetDesc.dynamicConstantBufferNum);
    }

    m_PipelineLayoutDesc = pipelineLayoutDesc;

    m_PipelineLayoutDesc.descriptorSets = m_DescriptorSets.data();
    m_PipelineLayoutDesc.pushConstants = m_PushConstants.data();
}

void PipelineLayoutVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetPipelineLayoutDebugName(m_ImplObject, name);
}

#include "PipelineLayoutVal.hpp"
