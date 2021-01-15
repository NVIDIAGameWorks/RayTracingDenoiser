/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "DeviceD3D12.h"

namespace nri
{
    struct DeviceD3D12;
    struct DescriptorSetD3D12;

    enum DescriptorHeapType : uint32_t
    {
        RESOURCE = 0,
        SAMPLER,
        MAX_NUM
    };

    struct DescriptorPoolD3D12
    {
        DescriptorPoolD3D12(DeviceD3D12& device);
        ~DescriptorPoolD3D12();

        DeviceD3D12& GetDevice() const;

        Result Create(const DescriptorPoolDesc& descriptorPoolDesc);
        void Bind(ID3D12GraphicsCommandList* graphicsCommandList) const;
        uint32_t AllocateDescriptors(DescriptorHeapType descriptorHeapType, uint32_t descriptorNum);
        DescriptorPointerCPU GetDescriptorPointerCPU(DescriptorHeapType descriptorHeapType, uint32_t offset) const;
        DescriptorPointerGPU GetDescriptorPointerGPU(DescriptorHeapType descriptorHeapType, uint32_t offset) const;

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);

        Result AllocateDescriptorSets(const PipelineLayout& pipelineLayout, uint32_t setIndex, DescriptorSet** const descriptorSets, uint32_t instanceNum, uint32_t variableDescriptorNum);

        void Reset();

    private:
        DeviceD3D12& m_Device;
        std::array<DescriptorHeapDesc, DescriptorHeapType::MAX_NUM> m_DescriptorHeapDescs;
        std::array<uint32_t, DescriptorHeapType::MAX_NUM> m_DescriptorNum = {};
        std::array<ID3D12DescriptorHeap*, DescriptorHeapType::MAX_NUM> m_DescriptorHeaps = {};
        uint32_t m_DescriptorHeapNum = 0;
        Vector<DescriptorSetD3D12*> m_DescriptorSets;
        uint32_t m_DescriptorSetNum = 0;
    };

    inline DeviceD3D12& DescriptorPoolD3D12::GetDevice() const
    {
        return m_Device;
    }
}
