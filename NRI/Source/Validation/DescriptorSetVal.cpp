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
#include "DescriptorSetVal.h"

#include "DescriptorVal.h"

using namespace nri;

DescriptorSetVal::DescriptorSetVal(DeviceVal& device, DescriptorSet& descriptorSet, const DescriptorSetDesc& descriptorSetDesc) :
    DeviceObjectVal(device, descriptorSet),
    m_Desc(descriptorSetDesc)
{
}

void DescriptorSetVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetDescriptorSetDebugName(m_ImplObject, name);
}

void DescriptorSetVal::UpdateDescriptorRanges(uint32_t physicalDeviceMask, uint32_t rangeOffset, uint32_t rangeNum, const DescriptorRangeUpdateDesc* rangeUpdateDescs)
{
    if (rangeNum == 0)
        return;

    RETURN_ON_FAILURE(m_Device.GetLog(), m_Device.IsPhysicalDeviceMaskValid(physicalDeviceMask), ReturnVoid(),
        "Can't update descriptor ranges: 'physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(m_Device.GetLog(), rangeUpdateDescs != nullptr, ReturnVoid(),
        "Can't update descriptor ranges: 'rangeUpdateDescs' is invalid.");

    RETURN_ON_FAILURE(m_Device.GetLog(), rangeOffset < m_Desc.rangeNum, ReturnVoid(),
        "Can't update descriptor ranges: 'rangeOffset' is out of bounds. (rangeOffset=%u, rangeNum=%u)", rangeOffset, m_Desc.rangeNum);

    RETURN_ON_FAILURE(m_Device.GetLog(), rangeOffset + rangeNum <= m_Desc.rangeNum, ReturnVoid(),
        "Can't update descriptor ranges: 'rangeOffset' + 'rangeNum' is greater than the number of ranges. (rangeOffset=%u, rangeNum=%u, rangeNum=%u)",
        rangeOffset, rangeNum, m_Desc.rangeNum);

    DescriptorRangeUpdateDesc* rangeUpdateDescsImpl = STACK_ALLOC(DescriptorRangeUpdateDesc, rangeNum);
    for (uint32_t i = 0; i < rangeNum; i++)
    {
        const DescriptorRangeUpdateDesc& updateDesc = rangeUpdateDescs[i];
        const DescriptorRangeDesc& rangeDesc = m_Desc.ranges[rangeOffset + i];

        RETURN_ON_FAILURE(m_Device.GetLog(), updateDesc.descriptorNum != 0, ReturnVoid(),
            "Can't update descriptor ranges: 'rangeUpdateDescs[%u].descriptorNum' is zero.", i);

        RETURN_ON_FAILURE(m_Device.GetLog(), updateDesc.offsetInRange < rangeDesc.descriptorNum, ReturnVoid(),
            "Can't update descriptor ranges: 'rangeUpdateDescs[%u].offsetInRange' is greater than the number of descriptors. "
            "(offsetInRange=%u, rangeDescriptorNum=%u, descriptorType=%s)",
            i, updateDesc.offsetInRange, rangeDesc.descriptorNum, GetDescriptorTypeName(rangeDesc.descriptorType));

        RETURN_ON_FAILURE(m_Device.GetLog(), updateDesc.offsetInRange + updateDesc.descriptorNum <= rangeDesc.descriptorNum, ReturnVoid(),
            "Can't update descriptor ranges: 'rangeUpdateDescs[%u].offsetInRange' + 'rangeUpdateDescs[%u].descriptorNum' is greater than the number of descriptors. "
            "(offsetInRange=%u, descriptorNum=%u, rangeDescriptorNum=%u, descriptorType=%s)",
            i, i, updateDesc.offsetInRange, updateDesc.descriptorNum, rangeDesc.descriptorNum, GetDescriptorTypeName(rangeDesc.descriptorType));

        RETURN_ON_FAILURE(m_Device.GetLog(), updateDesc.descriptors != nullptr, ReturnVoid(),
            "Can't update descriptor ranges: 'rangeUpdateDescs[%u].descriptors' is invalid.", i);

        DescriptorRangeUpdateDesc& dstDesc = rangeUpdateDescsImpl[i];

        dstDesc = updateDesc;
        dstDesc.descriptors = STACK_ALLOC(Descriptor*, updateDesc.descriptorNum);
        Descriptor** descriptors = (Descriptor**)dstDesc.descriptors;

        for (uint32_t j = 0; j < updateDesc.descriptorNum; j++)
        {
            RETURN_ON_FAILURE(m_Device.GetLog(), updateDesc.descriptors[j] != nullptr, ReturnVoid(),
                "Can't update descriptor ranges: 'rangeUpdateDescs[%u].descriptors[%u]' is invalid.", i, j);

            descriptors[j] = NRI_GET_IMPL(Descriptor, updateDesc.descriptors[j]);
        }
    }

    m_CoreAPI.UpdateDescriptorRanges(m_ImplObject, physicalDeviceMask, rangeOffset, rangeNum, rangeUpdateDescsImpl);
}

void DescriptorSetVal::UpdateDynamicConstantBuffers(uint32_t physicalDeviceMask, uint32_t baseBuffer, uint32_t bufferNum, const Descriptor* const* descriptors)
{
    if (bufferNum == 0)
        return;

    RETURN_ON_FAILURE(m_Device.GetLog(), m_Device.IsPhysicalDeviceMaskValid(physicalDeviceMask), ReturnVoid(),
        "Can't update dynamic constant buffers: 'physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(m_Device.GetLog(), baseBuffer < m_Desc.dynamicConstantBufferNum, ReturnVoid(),
        "Can't update dynamic constant buffers: 'baseBuffer' is invalid. (baseBuffer=%u, dynamicConstantBufferNum=%u)",
        baseBuffer, m_Desc.dynamicConstantBufferNum);

    RETURN_ON_FAILURE(m_Device.GetLog(), baseBuffer + bufferNum <= m_Desc.dynamicConstantBufferNum, ReturnVoid(),
        "Can't update dynamic constant buffers: 'baseBuffer' + 'bufferNum' is greater than the number of buffers. "
        "(baseBuffer=%u, bufferNum=%u, dynamicConstantBufferNum=%u)",
        baseBuffer, bufferNum, m_Desc.dynamicConstantBufferNum);

    RETURN_ON_FAILURE(m_Device.GetLog(), descriptors != nullptr, ReturnVoid(),
        "Can't update dynamic constant buffers: 'descriptors' is invalid.");

    Descriptor** descriptorsImpl = STACK_ALLOC(Descriptor*, bufferNum);
    for (uint32_t i = 0; i < bufferNum; i++)
    {
        RETURN_ON_FAILURE(m_Device.GetLog(), descriptors[i] != nullptr, ReturnVoid(),
            "Can't update dynamic constant buffers: 'descriptors[%u]' is invalid.", i);

        descriptorsImpl[i] = NRI_GET_IMPL(Descriptor, descriptors[i]);
    }

    m_CoreAPI.UpdateDynamicConstantBuffers(m_ImplObject, physicalDeviceMask, baseBuffer, bufferNum, descriptorsImpl);
}

void DescriptorSetVal::Copy(const DescriptorSetCopyDesc& descriptorSetCopyDesc)
{
    RETURN_ON_FAILURE(m_Device.GetLog(), m_Device.IsPhysicalDeviceMaskValid(descriptorSetCopyDesc.physicalDeviceMask), ReturnVoid(),
        "Can't copy descriptor set: 'physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(m_Device.GetLog(), descriptorSetCopyDesc.srcDescriptorSet != nullptr, ReturnVoid(),
        "Can't copy descriptor set: 'descriptorSetCopyDesc.srcDescriptorSet' is invalid.");

    DescriptorSetVal& srcDescriptorSetVal = *(DescriptorSetVal*)descriptorSetCopyDesc.srcDescriptorSet;
    const DescriptorSetDesc& srcDesc = srcDescriptorSetVal.GetDesc();

    RETURN_ON_FAILURE(m_Device.GetLog(), descriptorSetCopyDesc.baseSrcRange < srcDesc.rangeNum, ReturnVoid(),
        "Can't copy descriptor set: 'descriptorSetCopyDesc.baseSrcRange' is invalid.");

    bool srcRangeValid = descriptorSetCopyDesc.baseSrcRange + descriptorSetCopyDesc.rangeNum < srcDesc.rangeNum;

    RETURN_ON_FAILURE(m_Device.GetLog(), srcRangeValid, ReturnVoid(),
        "Can't copy descriptor set: 'descriptorSetCopyDesc.rangeNum' is invalid.");

    RETURN_ON_FAILURE(m_Device.GetLog(), descriptorSetCopyDesc.baseDstRange < m_Desc.rangeNum, ReturnVoid(),
        "Can't copy descriptor set: 'descriptorSetCopyDesc.baseDstRange' is invalid.");

    bool dstRangeValid = descriptorSetCopyDesc.baseDstRange + descriptorSetCopyDesc.rangeNum < m_Desc.rangeNum;

    RETURN_ON_FAILURE(m_Device.GetLog(), dstRangeValid, ReturnVoid(),
        "Can't copy descriptor set: 'descriptorSetCopyDesc.rangeNum' is invalid.");

    const bool srcOffsetValid = descriptorSetCopyDesc.baseSrcDynamicConstantBuffer < srcDesc.dynamicConstantBufferNum;

    RETURN_ON_FAILURE(m_Device.GetLog(), srcOffsetValid, ReturnVoid(),
        "Can't copy descriptor set: 'descriptorSetCopyDesc.baseSrcDynamicConstantBuffer' is invalid.");

    srcRangeValid = descriptorSetCopyDesc.baseSrcDynamicConstantBuffer +
        descriptorSetCopyDesc.dynamicConstantBufferNum < srcDesc.dynamicConstantBufferNum;

    RETURN_ON_FAILURE(m_Device.GetLog(), srcRangeValid, ReturnVoid(),
        "Can't copy descriptor set: source range of dynamic constant buffers is invalid.");

    const bool dstOffsetValid = descriptorSetCopyDesc.baseDstDynamicConstantBuffer < m_Desc.dynamicConstantBufferNum;

    RETURN_ON_FAILURE(m_Device.GetLog(), dstOffsetValid, ReturnVoid(),
        "Can't copy descriptor set: 'descriptorSetCopyDesc.baseDstDynamicConstantBuffer' is invalid.");

    dstRangeValid = descriptorSetCopyDesc.baseDstDynamicConstantBuffer +
        descriptorSetCopyDesc.dynamicConstantBufferNum < m_Desc.dynamicConstantBufferNum;

    RETURN_ON_FAILURE(m_Device.GetLog(), dstRangeValid, ReturnVoid(),
        "Can't copy descriptor set: destination range of dynamic constant buffers is invalid.");

    auto descriptorSetCopyDescImpl = descriptorSetCopyDesc;
    descriptorSetCopyDescImpl.srcDescriptorSet = NRI_GET_IMPL(DescriptorSet, descriptorSetCopyDesc.srcDescriptorSet);

    m_CoreAPI.CopyDescriptorSet(m_ImplObject, descriptorSetCopyDescImpl);
}

#include "DescriptorSetVal.hpp"
