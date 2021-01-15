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

#include "PipelineLayoutD3D11.h"

using namespace nri;

DescriptorSetD3D11::DescriptorSetD3D11(DeviceD3D11& device) :
    m_Ranges(device.GetStdAllocator()),
    m_DynamicConstantBuffers(device.GetStdAllocator())
{
}

uint32_t DescriptorSetD3D11::Initialize(const PipelineLayoutD3D11& pipelineLayout, uint32_t setIndex, const DescriptorD3D11** descriptors)
{
    const BindingSet& bindingSet = pipelineLayout.GetBindingSet(setIndex);

    for (uint32_t i = bindingSet.rangeStart; i < bindingSet.rangeEnd; i++)
    {
        const BindingRange& bindingRange = pipelineLayout.GetBindingRange(i);

        OffsetNum offsetNum = {};
        offsetNum.descriptorOffset = bindingRange.descriptorOffset;
        offsetNum.descriptorNum = bindingRange.descriptorNum;

        if (bindingRange.descriptorType == DescriptorTypeDX11::DYNAMIC_CONSTANT)
            m_DynamicConstantBuffers.push_back(offsetNum);
        else
            m_Ranges.push_back(offsetNum);
    }

    m_Descriptors = descriptors;

    return bindingSet.descriptorNum;
}

inline void DescriptorSetD3D11::SetDebugName(const char* name)
{
}

inline void DescriptorSetD3D11::UpdateDescriptorRanges(uint32_t rangeOffset, uint32_t rangeNum, const DescriptorRangeUpdateDesc* rangeUpdateDescs)
{
    for (uint32_t i = 0; i < rangeNum; i++)
    {
        const DescriptorRangeUpdateDesc& range = rangeUpdateDescs[i];

        uint32_t descriptorOffset = m_Ranges[rangeOffset + i].descriptorOffset;
        descriptorOffset += range.offsetInRange;

        const DescriptorD3D11** dstDescriptors = m_Descriptors + descriptorOffset;
        const DescriptorD3D11** srcDescriptors = (const DescriptorD3D11**)range.descriptors;

        memcpy(dstDescriptors, srcDescriptors, range.descriptorNum * sizeof(DescriptorD3D11*));
    }
}

inline void DescriptorSetD3D11::UpdateDynamicConstantBuffers(uint32_t baseBuffer, uint32_t bufferNum, const Descriptor* const* descriptors)
{
    const DescriptorD3D11** srcDescriptors = (const DescriptorD3D11**)descriptors;

    for (uint32_t i = 0; i < bufferNum; i++)
    {
        uint32_t descriptorOffset = m_DynamicConstantBuffers[baseBuffer + i].descriptorOffset;
        m_Descriptors[descriptorOffset] = srcDescriptors[i];
    }
}

inline void DescriptorSetD3D11::Copy(const DescriptorSetCopyDesc& descriptorSetCopyDesc)
{
    DescriptorSetD3D11& srcSet = (DescriptorSetD3D11&)descriptorSetCopyDesc.srcDescriptorSet;

    for (uint32_t i = 0; i < descriptorSetCopyDesc.rangeNum; i++)
    {
        const OffsetNum& dst = m_Ranges[descriptorSetCopyDesc.baseDstRange + i];
        const DescriptorD3D11** dstDescriptors = m_Descriptors + dst.descriptorOffset;

        const OffsetNum& src = srcSet.m_Ranges[descriptorSetCopyDesc.baseSrcRange + i];
        const DescriptorD3D11** srcDescriptors = srcSet.m_Descriptors + src.descriptorOffset;

        memcpy(dstDescriptors, srcDescriptors, dst.descriptorNum * sizeof(DescriptorD3D11*));
    }

    for (uint32_t i = 0; i < descriptorSetCopyDesc.dynamicConstantBufferNum; i++)
    {
        const OffsetNum& dst = m_DynamicConstantBuffers[descriptorSetCopyDesc.baseDstDynamicConstantBuffer + i];
        const OffsetNum& src = srcSet.m_DynamicConstantBuffers[descriptorSetCopyDesc.baseSrcDynamicConstantBuffer + i];

        m_Descriptors[dst.descriptorOffset] = srcSet.m_Descriptors[src.descriptorOffset];
    }
}

#include "DescriptorSetD3D11.hpp"
