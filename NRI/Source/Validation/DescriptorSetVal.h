/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nri
{
    struct DescriptorSetVal : public DeviceObjectVal<DescriptorSet>
    {
        DescriptorSetVal(DeviceVal& device, DescriptorSet& descriptorSet, const DescriptorSetDesc& descriptorSetDesc);

        const DescriptorSetDesc& GetDesc() const;

        void SetDebugName(const char* name);
        void UpdateDescriptorRanges(uint32_t physicalDeviceMask, uint32_t rangeOffset, uint32_t rangeNum, const DescriptorRangeUpdateDesc* rangeUpdateDescs);
        void UpdateDynamicConstantBuffers(uint32_t physicalDeviceMask, uint32_t baseBuffer, uint32_t bufferNum, const Descriptor* const* descriptors);
        void Copy(const DescriptorSetCopyDesc& descriptorSetCopyDesc);

    private:
        const DescriptorSetDesc& m_Desc;
    };

    inline const DescriptorSetDesc& DescriptorSetVal::GetDesc() const
    {
        return m_Desc;
    }
}
