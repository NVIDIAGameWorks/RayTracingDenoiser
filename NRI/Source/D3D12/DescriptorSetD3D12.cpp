/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "DescriptorSetD3D12.h"
#include "DeviceD3D12.h"
#include "DescriptorPoolD3D12.h"
#include "DescriptorD3D12.h"

using namespace nri;

constexpr extern D3D12_DESCRIPTOR_HEAP_TYPE GetDescriptorHeapType(DescriptorType descriptorType);

DescriptorSetD3D12::DescriptorSetD3D12(DeviceD3D12& device, DescriptorPoolD3D12& descriptorPoolD3D12, const DescriptorSetMapping& descriptorSetMapping, uint16_t dynamicConstantBufferNum)
    : m_Device(device)
    , m_DescriptorPoolD3D12(descriptorPoolD3D12)
    , m_DescriptorSetMapping(descriptorSetMapping)
    , m_DynamicConstantBuffers(device.GetStdAllocator())
{
    uint32_t heapOffset[DescriptorHeapType::MAX_NUM] = {};

    for (uint32_t i = 0; i < DescriptorHeapType::MAX_NUM; i++)
    {
        if (m_DescriptorSetMapping.descriptorNum[i])
            heapOffset[i] = m_DescriptorPoolD3D12.AllocateDescriptors((DescriptorHeapType)i, m_DescriptorSetMapping.descriptorNum[i]);
    }

    for (uint32_t i = 0; i < (uint32_t)m_DescriptorSetMapping.descriptorRangeMappings.size(); i++)
    {
        DescriptorHeapType descriptorHeapType = m_DescriptorSetMapping.descriptorRangeMappings[i].descriptorHeapType;
        m_DescriptorSetMapping.descriptorRangeMappings[i].heapOffset += heapOffset[descriptorHeapType];
    }

    m_DynamicConstantBuffers.resize(dynamicConstantBufferNum, 0);
}

void DescriptorSetD3D12::BuildDescriptorSetMapping(const DescriptorSetDesc& descriptorSetDesc, DescriptorSetMapping& descriptorSetMapping)
{
    descriptorSetMapping.descriptorRangeMappings.resize(descriptorSetDesc.rangeNum);
    for (uint32_t i = 0; i < descriptorSetDesc.rangeNum; i++)
    {
        D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType = GetDescriptorHeapType(descriptorSetDesc.ranges[i].descriptorType);
        descriptorSetMapping.descriptorRangeMappings[i].descriptorHeapType = (DescriptorHeapType)descriptorHeapType;
        descriptorSetMapping.descriptorRangeMappings[i].heapOffset = descriptorSetMapping.descriptorNum[descriptorHeapType];
        descriptorSetMapping.descriptorRangeMappings[i].descriptorNum = descriptorSetDesc.ranges[i].descriptorNum;

        descriptorSetMapping.descriptorNum[descriptorHeapType] += descriptorSetDesc.ranges[i].descriptorNum;
    }
}

DescriptorPointerCPU DescriptorSetD3D12::GetPointerCPU(uint32_t rangeIndex, uint32_t rangeOffset) const
{
    const DescriptorHeapType& descriptorHeapType = m_DescriptorSetMapping.descriptorRangeMappings[rangeIndex].descriptorHeapType;
    uint32_t offset = m_DescriptorSetMapping.descriptorRangeMappings[rangeIndex].heapOffset + rangeOffset;
    DescriptorPointerCPU descriptorPointerCPU = m_DescriptorPoolD3D12.GetDescriptorPointerCPU(descriptorHeapType, offset);

    return descriptorPointerCPU;
}

DescriptorPointerGPU DescriptorSetD3D12::GetPointerGPU(uint32_t rangeIndex, uint32_t rangeOffset) const
{
    const DescriptorHeapType& descriptorHeapType = m_DescriptorSetMapping.descriptorRangeMappings[rangeIndex].descriptorHeapType;
    uint32_t offset = m_DescriptorSetMapping.descriptorRangeMappings[rangeIndex].heapOffset + rangeOffset;
    DescriptorPointerGPU descriptorPointerGPU = m_DescriptorPoolD3D12.GetDescriptorPointerGPU(descriptorHeapType, offset);

    return descriptorPointerGPU;
}

DescriptorPointerGPU DescriptorSetD3D12::GetDynamicPointerGPU(uint32_t dynamicConstantBufferIndex) const
{
    return m_DynamicConstantBuffers[dynamicConstantBufferIndex];
}

inline void DescriptorSetD3D12::SetDebugName(const char* name)
{}

inline void DescriptorSetD3D12::UpdateDescriptorRanges(uint32_t rangeOffset, uint32_t rangeNum, const DescriptorRangeUpdateDesc* rangeUpdateDescs)
{
    for (uint32_t i = 0; i < rangeNum; i++)
    {
        const DescriptorRangeMapping& rangeMapping = m_DescriptorSetMapping.descriptorRangeMappings[rangeOffset + i];
        const uint32_t baseOffset = rangeMapping.heapOffset + rangeUpdateDescs[i].offsetInRange;
        for (uint32_t j = 0; j < rangeUpdateDescs[i].descriptorNum; j++)
        {
            DescriptorPointerCPU dstPointer = m_DescriptorPoolD3D12.GetDescriptorPointerCPU(rangeMapping.descriptorHeapType, baseOffset + j);
            DescriptorPointerCPU srcPointer = ((DescriptorD3D12*)rangeUpdateDescs[i].descriptors[j])->GetPointerCPU();
            D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType = (D3D12_DESCRIPTOR_HEAP_TYPE)rangeMapping.descriptorHeapType;

            ((ID3D12Device*)m_Device)->CopyDescriptorsSimple(1, { dstPointer }, { srcPointer }, descriptorHeapType);
        }
    }
}

inline void DescriptorSetD3D12::UpdateDynamicConstantBuffers(uint32_t baseBuffer, uint32_t bufferNum, const Descriptor* const* descriptors)
{
    for (uint32_t i = 0; i < bufferNum; i++)
        m_DynamicConstantBuffers[baseBuffer + i] = ((DescriptorD3D12*)descriptors[i])->GetBufferLocation();
}

inline void DescriptorSetD3D12::Copy(const DescriptorSetCopyDesc& descriptorSetCopyDesc)
{
    const DescriptorSetD3D12* srcDescriptorSet = (DescriptorSetD3D12*)descriptorSetCopyDesc.srcDescriptorSet;

    for (uint32_t i = 0; i < descriptorSetCopyDesc.rangeNum; i++)
    {
        DescriptorPointerCPU dstPointer = GetPointerCPU(descriptorSetCopyDesc.baseDstRange + i, 0);
        DescriptorPointerCPU srcPointer = srcDescriptorSet->GetPointerCPU(descriptorSetCopyDesc.baseSrcRange + i, 0);

        uint32_t descriptorNum = m_DescriptorSetMapping.descriptorRangeMappings[i].descriptorNum;
        D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType = (D3D12_DESCRIPTOR_HEAP_TYPE)m_DescriptorSetMapping.descriptorRangeMappings[i].descriptorHeapType;

        ((ID3D12Device*)m_Device)->CopyDescriptorsSimple(descriptorNum, { dstPointer }, { srcPointer }, descriptorHeapType);
    }

    for (uint32_t i = 0; i < descriptorSetCopyDesc.dynamicConstantBufferNum; i++)
    {
        DescriptorPointerGPU descriptorPointerGPU = srcDescriptorSet->GetDynamicPointerGPU(descriptorSetCopyDesc.baseSrcDynamicConstantBuffer + i);
        m_DynamicConstantBuffers[descriptorSetCopyDesc.baseDstDynamicConstantBuffer + i] = descriptorPointerGPU;
    }
}

#include "DescriptorSetD3D12.hpp"
