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
    struct DeviceD3D11;
    struct DescriptorD3D11;

    struct DescriptorPoolD3D11
    {
        DescriptorPoolD3D11(DeviceD3D11& device);

        inline DeviceD3D11& GetDevice() const
        { return m_Device; }

        Result Create(const DescriptorPoolDesc& descriptorPoolDesc);

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);
        Result AllocateDescriptorSets(const PipelineLayout& pipelineLayout, uint32_t setIndex, DescriptorSet** const descriptorSets,
            uint32_t instanceNum, uint32_t physicalDeviceMask, uint32_t variableDescriptorNum);
        void Reset();

    private:
        Vector<DescriptorSetD3D11> m_Sets;
        Vector<const DescriptorD3D11*> m_Pool;
        uint32_t m_DescriptorPoolOffset = 0;
        uint32_t m_DescriptorSetIndex = 0;
        DeviceD3D11& m_Device;
    };
}
