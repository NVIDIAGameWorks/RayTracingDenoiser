/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "DescriptorPoolD3D12.h"
#include "DeviceD3D12.h"
#include "PipelineLayoutD3D12.h"

using namespace nri;

constexpr extern D3D12_DESCRIPTOR_HEAP_TYPE GetDescriptorHeapType(DescriptorType descriptorType);

DescriptorPoolD3D12::DescriptorPoolD3D12(DeviceD3D12& device)
    : m_Device(device)
    , m_DescriptorSets(device.GetStdAllocator())
{}

DescriptorPoolD3D12::~DescriptorPoolD3D12()
{
    for (size_t i = 0; i < m_DescriptorSetNum; i++)
        Deallocate(m_Device.GetStdAllocator(), m_DescriptorSets[i]);
}

Result DescriptorPoolD3D12::Create(const DescriptorPoolDesc& descriptorPoolDesc)
{
    uint32_t descriptorHeapSize[DescriptorHeapType::MAX_NUM] = {};
    descriptorHeapSize[DescriptorHeapType::RESOURCE] += descriptorPoolDesc.constantBufferMaxNum;
    descriptorHeapSize[DescriptorHeapType::RESOURCE] += descriptorPoolDesc.textureMaxNum;
    descriptorHeapSize[DescriptorHeapType::RESOURCE] += descriptorPoolDesc.storageTextureMaxNum;
    descriptorHeapSize[DescriptorHeapType::RESOURCE] += descriptorPoolDesc.bufferMaxNum;
    descriptorHeapSize[DescriptorHeapType::RESOURCE] += descriptorPoolDesc.storageBufferMaxNum;
    descriptorHeapSize[DescriptorHeapType::RESOURCE] += descriptorPoolDesc.structuredBufferMaxNum;
    descriptorHeapSize[DescriptorHeapType::RESOURCE] += descriptorPoolDesc.storageStructuredBufferMaxNum;
    descriptorHeapSize[DescriptorHeapType::RESOURCE] += descriptorPoolDesc.accelerationStructureMaxNum;
    descriptorHeapSize[DescriptorHeapType::SAMPLER] += descriptorPoolDesc.samplerMaxNum;

    for (uint32_t i = 0; i < DescriptorHeapType::MAX_NUM; i++)
    {
        if (descriptorHeapSize[i])
        {
            ComPtr<ID3D12DescriptorHeap> descriptorHeap;
            D3D12_DESCRIPTOR_HEAP_DESC desc = { (D3D12_DESCRIPTOR_HEAP_TYPE)i, descriptorHeapSize[i], D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, NRI_TEMP_NODE_MASK };
            HRESULT hr = ((ID3D12Device*)m_Device)->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap));
            if (FAILED(hr))
            {
                REPORT_ERROR(m_Device.GetLog(), "ID3D12Device::CreateDescriptorHeap() failed, return code %d.", hr);
                return Result::FAILURE;
            }

            m_DescriptorHeapDescs[i].descriptorHeap = descriptorHeap;
            m_DescriptorHeapDescs[i].descriptorPointerCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr;
            m_DescriptorHeapDescs[i].descriptorPointerGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr;
            m_DescriptorHeapDescs[i].descriptorSize = ((ID3D12Device*)m_Device)->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)i);

            m_DescriptorHeaps[m_DescriptorHeapNum] = descriptorHeap;
            m_DescriptorHeapNum++;
        }
    }

    m_DescriptorSets.resize(descriptorPoolDesc.descriptorSetMaxNum);

    return Result::SUCCESS;
}

void DescriptorPoolD3D12::Bind(ID3D12GraphicsCommandList* graphicsCommandList) const
{
    graphicsCommandList->SetDescriptorHeaps(m_DescriptorHeapNum, m_DescriptorHeaps.data());
}

uint32_t DescriptorPoolD3D12::AllocateDescriptors(DescriptorHeapType descriptorHeapType, uint32_t descriptorNum)
{
    uint32_t descriptorOffset = m_DescriptorNum[descriptorHeapType];
    m_DescriptorNum[descriptorHeapType] += descriptorNum;

    return descriptorOffset;
}

DescriptorPointerCPU DescriptorPoolD3D12::GetDescriptorPointerCPU(DescriptorHeapType descriptorHeapType, uint32_t offset) const
{
    const DescriptorHeapDesc& descriptorHeapDesc = m_DescriptorHeapDescs[descriptorHeapType];
    DescriptorPointerCPU descriptorPointer = descriptorHeapDesc.descriptorPointerCPU + offset * descriptorHeapDesc.descriptorSize;

    return descriptorPointer;
}

DescriptorPointerGPU DescriptorPoolD3D12::GetDescriptorPointerGPU(DescriptorHeapType descriptorHeapType, uint32_t offset) const
{
    const DescriptorHeapDesc& descriptorHeapDesc = m_DescriptorHeapDescs[descriptorHeapType];
    DescriptorPointerGPU descriptorPointer = descriptorHeapDesc.descriptorPointerGPU + offset * descriptorHeapDesc.descriptorSize;

    return descriptorPointer;
}

inline void DescriptorPoolD3D12::SetDebugName(const char* name)
{}

inline Result DescriptorPoolD3D12::AllocateDescriptorSets(const PipelineLayout& pipelineLayout, uint32_t setIndex, DescriptorSet** const descriptorSets, uint32_t instanceNum, uint32_t variableDescriptorNum)
{
    if (m_DescriptorSetNum + instanceNum > m_DescriptorSets.size())
        return Result::FAILURE;

    const PipelineLayoutD3D12& pipelineLayoutD3D12 = (PipelineLayoutD3D12&)pipelineLayout;
    const DescriptorSetMapping& descriptorSetMapping = pipelineLayoutD3D12.GetDescriptorSetMapping(setIndex);
    const DynamicConstantBufferMapping& dynamicConstantBufferMapping = pipelineLayoutD3D12.GetDynamicConstantBufferMapping(setIndex);

    for (uint32_t i = 0; i < instanceNum; i++)
    {
        DescriptorSetD3D12* descriptorSet = Allocate<DescriptorSetD3D12>(m_Device.GetStdAllocator(), m_Device, *this, descriptorSetMapping, dynamicConstantBufferMapping.constantNum);
        m_DescriptorSets[m_DescriptorSetNum + i] = descriptorSet;
        descriptorSets[i] = (DescriptorSet*)descriptorSet;
    }

    m_DescriptorSetNum += instanceNum;

    return Result::SUCCESS;
}

inline void DescriptorPoolD3D12::Reset()
{
    for (uint32_t i = 0; i < DescriptorHeapType::MAX_NUM; i++)
        m_DescriptorNum[i] = 0;

    for (uint32_t i = 0; i < m_DescriptorSetNum; i++)
        Deallocate(m_Device.GetStdAllocator(), m_DescriptorSets[i]);

    m_DescriptorSetNum = 0;
}

#include "DescriptorPoolD3D12.hpp"
