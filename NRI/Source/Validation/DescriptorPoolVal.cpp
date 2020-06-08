/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedVal.h"
#include "DescriptorPoolVal.h"

#include "DescriptorSetVal.h"
#include "PipelineLayoutVal.h"

using namespace nri;

DescriptorPoolVal::DescriptorPoolVal(DeviceVal& device, DescriptorPool& descriptorPool) :
    DeviceObjectVal(device, descriptorPool),
    m_DescriptorSets(device.GetStdAllocator()),
    m_SkipValidation(true)
{
}

DescriptorPoolVal::DescriptorPoolVal(DeviceVal& device, DescriptorPool& descriptorPool, const DescriptorPoolDesc& descriptorPoolDesc) :
    DeviceObjectVal(device, descriptorPool),
    m_DescriptorSets(device.GetStdAllocator()),
    m_Desc(descriptorPoolDesc)
{
}

DescriptorPoolVal::~DescriptorPoolVal()
{
    for (size_t i = 0; i < m_DescriptorSets.size(); i++)
        Deallocate(m_Device.GetStdAllocator(), m_DescriptorSets[i]);
}

void DescriptorPoolVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetDescriptorPoolDebugName(m_ImplObject, name);
}

bool DescriptorPoolVal::CheckDescriptorRange(const DescriptorRangeDesc& rangeDesc, uint32_t variableDescriptorNum)
{
    const uint32_t descriptorNum = rangeDesc.isVariableDescriptorNum ? variableDescriptorNum : rangeDesc.descriptorNum;

    switch (rangeDesc.descriptorType)
    {
    case DescriptorType::SAMPLER:
        return m_SamplerNum + descriptorNum <= m_Desc.samplerMaxNum;
    case DescriptorType::CONSTANT_BUFFER:
        return m_ConstantBufferNum + descriptorNum <= m_Desc.constantBufferMaxNum;
    case DescriptorType::TEXTURE:
        return m_TextureNum + descriptorNum <= m_Desc.textureMaxNum;
    case DescriptorType::STORAGE_TEXTURE:
        return m_StorageTextureNum + descriptorNum <= m_Desc.storageTextureMaxNum;
    case DescriptorType::BUFFER:
        return m_BufferNum + descriptorNum <= m_Desc.bufferMaxNum;
    case DescriptorType::STORAGE_BUFFER:
        return m_StorageBufferNum + descriptorNum <= m_Desc.storageBufferMaxNum;
    case DescriptorType::STRUCTURED_BUFFER:
        return m_StructuredBufferNum + descriptorNum <= m_Desc.structuredBufferMaxNum;
    case DescriptorType::STORAGE_STRUCTURED_BUFFER:
        return m_StorageStructuredBufferNum + descriptorNum <= m_Desc.storageStructuredBufferMaxNum;
    case DescriptorType::ACCELERATION_STRUCTURE:
        return m_AccelerationStructureNum + descriptorNum <= m_Desc.accelerationStructureMaxNum;
    }

    REPORT_ERROR(m_Device.GetLog(), "Unknown descriptor range type: %u", (uint32_t)rangeDesc.descriptorType);
    return false;
}

void DescriptorPoolVal::IncrementDescriptorNum(const DescriptorRangeDesc& rangeDesc, uint32_t variableDescriptorNum)
{
    const uint32_t descriptorNum = rangeDesc.isVariableDescriptorNum ? variableDescriptorNum : rangeDesc.descriptorNum;

    switch (rangeDesc.descriptorType)
    {
    case DescriptorType::SAMPLER:
        m_SamplerNum += descriptorNum;
        return;
    case DescriptorType::CONSTANT_BUFFER:
        m_ConstantBufferNum += descriptorNum;
        return;
    case DescriptorType::TEXTURE:
        m_TextureNum += descriptorNum;
        return;
    case DescriptorType::STORAGE_TEXTURE:
        m_StorageTextureNum += descriptorNum;
        return;
    case DescriptorType::BUFFER:
        m_BufferNum += descriptorNum;
        return;
    case DescriptorType::STORAGE_BUFFER:
        m_StorageBufferNum += descriptorNum;
        return;
    case DescriptorType::STRUCTURED_BUFFER:
        m_StructuredBufferNum += descriptorNum;
        return;
    case DescriptorType::STORAGE_STRUCTURED_BUFFER:
        m_StorageStructuredBufferNum += descriptorNum;
        return;
    case DescriptorType::ACCELERATION_STRUCTURE:
        m_AccelerationStructureNum += descriptorNum;
        return;
    }

    REPORT_ERROR(m_Device.GetLog(), "Unknown descriptor range type: %u", (uint32_t)rangeDesc.descriptorType);
}

Result DescriptorPoolVal::AllocateDescriptorSets(const PipelineLayout& pipelineLayout, uint32_t setIndex, DescriptorSet** const descriptorSets,
    uint32_t instanceNum, uint32_t physicalDeviceMask, uint32_t variableDescriptorNum)
{
    const PipelineLayoutVal& pipelineLayoutVal = (const PipelineLayoutVal&)pipelineLayout;
    const PipelineLayoutDesc& pipelineLayoutDesc = pipelineLayoutVal.GetPipelineLayoutDesc();

    if (!m_SkipValidation)
    {
        RETURN_ON_FAILURE(m_Device.GetLog(), instanceNum != 0, Result::INVALID_ARGUMENT,
            "Can't allocate DescriptorSet: 'instanceNum' is 0.");

        RETURN_ON_FAILURE(m_Device.GetLog(), m_Device.IsPhysicalDeviceMaskValid(physicalDeviceMask), Result::INVALID_ARGUMENT,
            "Can't create DescriptorSet: 'physicalDeviceMask' is invalid.");

        RETURN_ON_FAILURE(m_Device.GetLog(), m_DescriptorSetNum + instanceNum <= m_Desc.descriptorSetMaxNum, Result::INVALID_ARGUMENT,
            "Can't allocate DescriptorSet: the maximum number of descriptor sets exceeded.");

        RETURN_ON_FAILURE(m_Device.GetLog(), setIndex < pipelineLayoutDesc.descriptorSetNum, Result::INVALID_ARGUMENT,
            "Can't allocate DescriptorSet: 'setIndex' is invalid.");

        const DescriptorSetDesc& descriptorSetDesc = pipelineLayoutDesc.descriptorSets[setIndex];

        bool enoughDescriptors;

        for (uint32_t i = 0; i < descriptorSetDesc.rangeNum; i++)
        {
            const DescriptorRangeDesc& rangeDesc = descriptorSetDesc.ranges[i];
            enoughDescriptors = CheckDescriptorRange(rangeDesc, variableDescriptorNum);

            RETURN_ON_FAILURE(m_Device.GetLog(), enoughDescriptors, Result::INVALID_ARGUMENT,
                "Can't allocate DescriptorSet: the maximum number of descriptors exceeded ('%s').", GetDescriptorTypeName(rangeDesc.descriptorType));
        }

        enoughDescriptors = m_DynamicConstantBufferNum + descriptorSetDesc.dynamicConstantBufferNum <= m_Desc.dynamicConstantBufferMaxNum;

        RETURN_ON_FAILURE(m_Device.GetLog(), enoughDescriptors, Result::INVALID_ARGUMENT,
            "Can't allocate DescriptorSet: the maximum number of descriptors exceeded ('DYNAMIC_CONSTANT_BUFFER').");

        enoughDescriptors = m_StaticSamplerNum + descriptorSetDesc.staticSamplerNum <= m_Desc.staticSamplerMaxNum;

        RETURN_ON_FAILURE(m_Device.GetLog(), enoughDescriptors, Result::INVALID_ARGUMENT,
            "Can't allocate DescriptorSet: the maximum number of descriptors exceeded ('STATIC_SAMPLER').");
    }

    PipelineLayout* pipelineLayoutImpl = NRI_GET_IMPL(PipelineLayout, &pipelineLayout);

    Result result = m_CoreAPI.AllocateDescriptorSets(m_ImplObject, *pipelineLayoutImpl, setIndex, descriptorSets, instanceNum,
        physicalDeviceMask, variableDescriptorNum);

    if (result != Result::SUCCESS)
        return result;

    const DescriptorSetDesc& descriptorSetDesc = pipelineLayoutDesc.descriptorSets[setIndex];

    if (!m_SkipValidation)
    {
        m_DescriptorSetNum += instanceNum;
        m_DynamicConstantBufferNum += descriptorSetDesc.dynamicConstantBufferNum;
        m_StaticSamplerNum += descriptorSetDesc.staticSamplerNum;
        for (uint32_t i = 0; i < descriptorSetDesc.rangeNum; i++)
            IncrementDescriptorNum(descriptorSetDesc.ranges[i], variableDescriptorNum);
    }

    for (uint32_t i = 0; i < instanceNum; i++)
    {
        DescriptorSetVal* descriptorSetVal = Allocate<DescriptorSetVal>(m_Device.GetStdAllocator(), m_Device, *descriptorSets[i], descriptorSetDesc);
        descriptorSets[i] = (DescriptorSet*)descriptorSetVal;
        m_DescriptorSets.push_back(descriptorSetVal);
    }

    return result;
}

void DescriptorPoolVal::Reset()
{
    for (uint32_t i = 0; i < m_DescriptorSets.size(); i++)
        Deallocate(m_Device.GetStdAllocator(), m_DescriptorSets[i]);
    m_DescriptorSets.clear();

    m_DescriptorSetNum = 0;
    m_SamplerNum = 0;
    m_StaticSamplerNum = 0;
    m_ConstantBufferNum = 0;
    m_DynamicConstantBufferNum = 0;
    m_TextureNum = 0;
    m_StorageTextureNum = 0;
    m_BufferNum = 0;
    m_StorageBufferNum = 0;
    m_StructuredBufferNum = 0;
    m_StorageStructuredBufferNum = 0;
    m_AccelerationStructureNum = 0;

    m_CoreAPI.ResetDescriptorPool(m_ImplObject);
}

#include "DescriptorPoolVal.hpp"
