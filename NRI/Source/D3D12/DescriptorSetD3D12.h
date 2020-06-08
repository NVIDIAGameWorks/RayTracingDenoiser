/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "DescriptorPoolD3D12.h"

namespace nri
{
    struct DeviceD3D12;

    struct DescriptorRangeMapping
    {
        DescriptorHeapType descriptorHeapType;
        uint32_t heapOffset;
        uint32_t descriptorNum;
    };

    struct DescriptorSetMapping
    {
        DescriptorSetMapping(StdAllocator<uint8_t>& allocator);

        uint32_t descriptorNum[DescriptorHeapType::MAX_NUM] = {};
        Vector<DescriptorRangeMapping> descriptorRangeMappings;
    };

    struct DescriptorSetD3D12
    {
        DescriptorSetD3D12(DeviceD3D12& device, DescriptorPoolD3D12& desriptorPoolD3D12, const DescriptorSetMapping& descriptorSetMapping, uint16_t dynamicConstantBufferNum);
        ~DescriptorSetD3D12();

        DeviceD3D12& GetDevice() const;

        static void BuildDescriptorSetMapping(const DescriptorSetDesc& descriptorSetDesc, DescriptorSetMapping& descriptorSetMapping);
        DescriptorPointerCPU GetPointerCPU(uint32_t rangeIndex, uint32_t rangeOffset) const;
        DescriptorPointerGPU GetPointerGPU(uint32_t rangeIndex, uint32_t rangeOffset) const;
        DescriptorPointerGPU GetDynamicPointerGPU(uint32_t dynamicConstantBufferIndex) const;

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);
        void UpdateDescriptorRanges(uint32_t rangeOffset, uint32_t rangeNum, const DescriptorRangeUpdateDesc* rangeUpdateDescs);
        void UpdateDynamicConstantBuffers(uint32_t baseBuffer, uint32_t bufferNum, const Descriptor* const* descriptors);
        void Copy(const DescriptorSetCopyDesc& descriptorSetCopyDesc);

    private:
        DeviceD3D12& m_Device;
        DescriptorPoolD3D12& m_DescriptorPoolD3D12;
        DescriptorSetMapping m_DescriptorSetMapping;
        Vector<DescriptorPointerGPU> m_DynamicConstantBuffers;
    };

    inline DescriptorSetD3D12::~DescriptorSetD3D12()
    {}

    inline DeviceD3D12& DescriptorSetD3D12::GetDevice() const
    {
        return m_Device;
    }

    inline DescriptorSetMapping::DescriptorSetMapping(StdAllocator<uint8_t>& allocator)
        : descriptorRangeMappings(allocator)
    {}
}
