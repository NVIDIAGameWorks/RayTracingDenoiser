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
    struct PipelineLayoutVal : public DeviceObjectVal<PipelineLayout>
    {
        PipelineLayoutVal(DeviceVal& device, PipelineLayout& pipelineLayout, const PipelineLayoutDesc& pipelineLayoutDesc);

        const PipelineLayoutDesc& GetPipelineLayoutDesc() const;

        void SetDebugName(const char* name);

    private:
        PipelineLayoutDesc m_PipelineLayoutDesc;
        Vector<DescriptorSetDesc> m_DescriptorSets;
        Vector<PushConstantDesc> m_PushConstants;
        Vector<DescriptorRangeDesc> m_DescriptorRangeDescs;
        Vector<StaticSamplerDesc> m_StaticSamplerDescs;
        Vector<DynamicConstantBufferDesc> m_DynamicConstantBufferDescs;
    };

    inline const PipelineLayoutDesc& PipelineLayoutVal::GetPipelineLayoutDesc() const
    {
        return m_PipelineLayoutDesc;
    }
}
