/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "DescriptorSetD3D12.h"

namespace nri
{
    struct DeviceD3D12;

    constexpr uint16_t ROOT_PARAMETER_UNUSED = uint16_t(-1);;

    struct DescriptorSetRootMapping
    {
        DescriptorSetRootMapping(StdAllocator<uint8_t>& allocator);

        Vector<uint16_t> rootOffsets;
    };

    struct DynamicConstantBufferMapping
    {
        uint16_t constantNum;
        uint16_t rootOffset;
    };

    struct PipelineLayoutD3D12
    {
        PipelineLayoutD3D12(DeviceD3D12& device);

        operator ID3D12RootSignature*() const;

        DeviceD3D12& GetDevice() const;
        bool IsGraphicsPipelineLayout() const;

        const DescriptorSetMapping& GetDescriptorSetMapping(uint32_t setIndex) const;
        const DescriptorSetRootMapping& GetDescriptorSetRootMapping(uint32_t setIndex) const;
        const DynamicConstantBufferMapping& GetDynamicConstantBufferMapping(uint32_t setIndex) const;
        uint32_t GetPushConstantsRootOffset(uint32_t rangeIndex) const;

        void SetDescriptorSets(ID3D12GraphicsCommandList& graphicsCommandList, bool isGraphics, uint32_t baseIndex,
            uint32_t setNum, const DescriptorSet* const* descriptorSets, const uint32_t* offsets) const;

        Result Create(const PipelineLayoutDesc& pipelineLayoutDesc);

        void SetDebugName(const char* name);

    private:
        template<bool isGraphics>
        void SetDescriptorSetsImpl(ID3D12GraphicsCommandList& graphicsCommandList, uint32_t baseIndex,
            uint32_t setNum, const DescriptorSet* const* descriptorSets, const uint32_t* offsets) const;

        ComPtr<ID3D12RootSignature> m_RootSignature;
        bool m_IsGraphicsPipelineLayout = false;
        uint32_t m_PushConstantsBaseIndex = 0;
        Vector<DescriptorSetMapping> m_DescriptorSetMappings;
        Vector<DescriptorSetRootMapping> m_DescriptorSetRootMappings;
        Vector<DynamicConstantBufferMapping> m_DynamicConstantBufferMappings;
        DeviceD3D12& m_Device;
    };

    inline PipelineLayoutD3D12::operator ID3D12RootSignature*() const
    {
        return m_RootSignature.GetInterface();
    }

    inline DeviceD3D12& PipelineLayoutD3D12::GetDevice() const
    {
        return m_Device;
    }

    inline bool PipelineLayoutD3D12::IsGraphicsPipelineLayout() const
    {
        return m_IsGraphicsPipelineLayout;
    }

    inline const DescriptorSetMapping& PipelineLayoutD3D12::GetDescriptorSetMapping(uint32_t setIndex) const
    {
        return m_DescriptorSetMappings[setIndex];
    }

    inline const DescriptorSetRootMapping& PipelineLayoutD3D12::GetDescriptorSetRootMapping(uint32_t setIndex) const
    {
        return m_DescriptorSetRootMappings[setIndex];
    }

    inline const DynamicConstantBufferMapping& PipelineLayoutD3D12::GetDynamicConstantBufferMapping(uint32_t setIndex) const
    {
        return m_DynamicConstantBufferMappings[setIndex];
    }

    inline uint32_t PipelineLayoutD3D12::GetPushConstantsRootOffset(uint32_t rangeIndex) const
    {
        return m_PushConstantsBaseIndex + rangeIndex;
    }

    template<bool isGraphics>
    inline void PipelineLayoutD3D12::SetDescriptorSetsImpl(ID3D12GraphicsCommandList& graphicsCommandList, uint32_t baseIndex,
        uint32_t setNum, const DescriptorSet* const* descriptorSets, const uint32_t* offsets) const
    {
        uint32_t dynamicConstantBufferNum = 0;

        for (uint32_t i = 0; i < setNum; i++)
        {
            const DescriptorSetD3D12* descriptorSet = (const DescriptorSetD3D12*)descriptorSets[i];

            uint32_t setIndex = baseIndex + i;
            const auto& rootOffsets = m_DescriptorSetRootMappings[setIndex].rootOffsets;

            uint32_t descriptorRangeNum = (uint32_t)rootOffsets.size();
            for (uint32_t j = 0; j < descriptorRangeNum; j++)
            {
                uint16_t rootParameterIndex = rootOffsets[j];
                if (rootParameterIndex == ROOT_PARAMETER_UNUSED)
                    continue;

                DescriptorPointerGPU descriptorPointerGPU = descriptorSet->GetPointerGPU(j, 0);

                if (isGraphics)
                    graphicsCommandList.SetGraphicsRootDescriptorTable(rootParameterIndex, { descriptorPointerGPU });
                else
                    graphicsCommandList.SetComputeRootDescriptorTable(rootParameterIndex, { descriptorPointerGPU });
            }

            const auto& dynamicConstantBufferMapping = m_DynamicConstantBufferMappings[setIndex];

            for (uint16_t j = 0; j < dynamicConstantBufferMapping.constantNum; j++)
            {
                uint16_t rootParameterIndex = dynamicConstantBufferMapping.rootOffset + j;
                DescriptorPointerGPU descriptorPointerGPU = descriptorSet->GetDynamicPointerGPU(j) + offsets[dynamicConstantBufferNum];

                if (isGraphics)
                    graphicsCommandList.SetGraphicsRootConstantBufferView(rootParameterIndex, { descriptorPointerGPU });
                else
                    graphicsCommandList.SetComputeRootConstantBufferView(rootParameterIndex, { descriptorPointerGPU });

                dynamicConstantBufferNum++;
            }
        }
    }

    inline void PipelineLayoutD3D12::SetDescriptorSets(ID3D12GraphicsCommandList& graphicsCommandList, bool isGraphics,
        uint32_t baseIndex, uint32_t setNum, const DescriptorSet* const* descriptorSets, const uint32_t* offsets) const
    {
        if (isGraphics)
            SetDescriptorSetsImpl<true>(graphicsCommandList, baseIndex, setNum, descriptorSets, offsets);
        else
            SetDescriptorSetsImpl<false>(graphicsCommandList, baseIndex, setNum, descriptorSets, offsets);
    }

    inline DescriptorSetRootMapping::DescriptorSetRootMapping(StdAllocator<uint8_t>& allocator)
        : rootOffsets(allocator)
    {}
}