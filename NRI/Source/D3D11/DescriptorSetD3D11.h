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
    struct DescriptorD3D11;
    struct PipelineLayoutD3D11;

    struct OffsetNum
    {
        uint32_t descriptorOffset;
        uint32_t descriptorNum;
    };

    struct DescriptorSetD3D11
    {
        DescriptorSetD3D11(DeviceD3D11& device);

        inline const DescriptorD3D11* GetDescriptor(uint32_t i) const
        { return m_Descriptors[i]; }

        uint32_t Initialize(const PipelineLayoutD3D11& pipelineLayout, uint32_t setIndex, const DescriptorD3D11** descriptors);

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);
        void UpdateDescriptorRanges(uint32_t rangeOffset, uint32_t rangeNum, const DescriptorRangeUpdateDesc* rangeUpdateDescs);
        void UpdateDynamicConstantBuffers(uint32_t baseBuffer, uint32_t bufferNum, const Descriptor* const* descriptors);
        void Copy(const DescriptorSetCopyDesc& descriptorSetCopyDesc);

    private:
        const DescriptorD3D11** m_Descriptors = nullptr;
        Vector<OffsetNum> m_Ranges;
        Vector<OffsetNum> m_DynamicConstantBuffers;
    };
}
