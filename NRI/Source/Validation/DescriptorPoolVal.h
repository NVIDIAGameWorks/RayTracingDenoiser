/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nri
{
    struct DescriptorSetVal;

    struct DescriptorPoolVal : public DeviceObjectVal<DescriptorPool>
    {
        DescriptorPoolVal(DeviceVal& device, DescriptorPool& descriptorPool);
        DescriptorPoolVal(DeviceVal& device, DescriptorPool& descriptorPool, const DescriptorPoolDesc& descriptorPoolDesc);
        ~DescriptorPoolVal();

        void SetDebugName(const char* name);

        Result AllocateDescriptorSets(const PipelineLayout& pipelineLayout, uint32_t setIndex, DescriptorSet** const descriptorSets,
            uint32_t instanceNum, uint32_t physicalDeviceMask, uint32_t variableDescriptorNum);

        void Reset();

    private:
        bool CheckDescriptorRange(const DescriptorRangeDesc& rangeDesc, uint32_t variableDescriptorNum);
        void IncrementDescriptorNum(const DescriptorRangeDesc& rangeDesc, uint32_t variableDescriptorNum);

        Vector<DescriptorSetVal*> m_DescriptorSets;
        DescriptorPoolDesc m_Desc = {};
        uint32_t m_DescriptorSetNum = 0;
        uint32_t m_SamplerNum = 0;
        uint32_t m_StaticSamplerNum = 0;
        uint32_t m_ConstantBufferNum = 0;
        uint32_t m_DynamicConstantBufferNum = 0;
        uint32_t m_TextureNum = 0;
        uint32_t m_StorageTextureNum = 0;
        uint32_t m_BufferNum = 0;
        uint32_t m_StorageBufferNum = 0;
        uint32_t m_StructuredBufferNum = 0;
        uint32_t m_StorageStructuredBufferNum = 0;
        uint32_t m_AccelerationStructureNum = 0;
        bool m_SkipValidation = false;
    };
}
