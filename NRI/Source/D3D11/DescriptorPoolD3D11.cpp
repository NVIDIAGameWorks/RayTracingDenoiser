/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedD3D11.h"
#include "DescriptorSetD3D11.h"
#include "DescriptorPoolD3D11.h"

#include "PipelineLayoutD3D11.h"

using namespace nri;

DescriptorPoolD3D11::DescriptorPoolD3D11(DeviceD3D11& device) :
    m_Device(device),
    m_Sets(device.GetStdAllocator()),
    m_Pool(device.GetStdAllocator())
{
}

Result DescriptorPoolD3D11::Create(const DescriptorPoolDesc& descriptorPoolDesc)
{
    uint32_t descriptorNum = descriptorPoolDesc.samplerMaxNum;
    descriptorNum += descriptorPoolDesc.samplerMaxNum;
    descriptorNum += descriptorPoolDesc.constantBufferMaxNum;
    descriptorNum += descriptorPoolDesc.dynamicConstantBufferMaxNum;
    descriptorNum += descriptorPoolDesc.textureMaxNum;
    descriptorNum += descriptorPoolDesc.storageTextureMaxNum;
    descriptorNum += descriptorPoolDesc.bufferMaxNum;
    descriptorNum += descriptorPoolDesc.storageBufferMaxNum;
    descriptorNum += descriptorPoolDesc.structuredBufferMaxNum;
    descriptorNum += descriptorPoolDesc.storageStructuredBufferMaxNum;

    m_Pool.resize(descriptorNum, nullptr);
    m_Sets.resize(descriptorPoolDesc.descriptorSetMaxNum, DescriptorSetD3D11(m_Device));

    return Result::SUCCESS;
}

inline void DescriptorPoolD3D11::SetDebugName(const char* name)
{
}

inline Result DescriptorPoolD3D11::AllocateDescriptorSets(const PipelineLayout& pipelineLayout, uint32_t setIndex, DescriptorSet** const descriptorSets,
    uint32_t instanceNum, uint32_t physicalDeviceMask, uint32_t variableDescriptorNum)
{
    // TODO: use physicalDeviceMask

    const PipelineLayoutD3D11& pipelineLayoutD3D11 = (PipelineLayoutD3D11&)pipelineLayout;

    for (uint32_t i = 0; i < instanceNum; i++)
    {
        const DescriptorD3D11** descriptors = m_Pool.data() + m_DescriptorPoolOffset;
        DescriptorSetD3D11* descriptorSet = &m_Sets[m_DescriptorSetIndex];
        uint32_t descriptorNum = descriptorSet->Initialize(pipelineLayoutD3D11, setIndex, descriptors);
        descriptorSets[i] = (DescriptorSet*)descriptorSet;

        m_DescriptorPoolOffset += descriptorNum;
        m_DescriptorSetIndex++;
    }

    return Result::SUCCESS;
}

inline void DescriptorPoolD3D11::Reset()
{
    m_DescriptorPoolOffset = 0;
    m_DescriptorSetIndex = 0;
}

#include "DescriptorPoolD3D11.hpp"
