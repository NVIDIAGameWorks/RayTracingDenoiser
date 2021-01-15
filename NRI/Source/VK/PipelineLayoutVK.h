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
    struct DeviceVK;
    struct DescriptorVK;

    struct PushConstantRangeBindingDesc
    {
        VkShaderStageFlags flags;
        uint32_t offset;
    };

    struct RuntimeBindingInfo
    {
        RuntimeBindingInfo(StdAllocator<uint8_t>& allocator);

        Vector<bool> hasVariableDescriptorNum;
        Vector<DescriptorRangeDesc> descriptorSetRangeDescs;
        Vector<DynamicConstantBufferDesc> dynamicConstantBufferDescs;
        Vector<DescriptorSetDesc> descriptorSetDescs;
        Vector<PushConstantDesc> pushConstantDescs;
        Vector<PushConstantRangeBindingDesc> pushConstantBindings;
    };

    struct PipelineLayoutVK
    {
        PipelineLayoutVK(DeviceVK& device);
        ~PipelineLayoutVK();

        operator VkPipelineLayout() const;
        DeviceVK& GetDevice() const;

        const RuntimeBindingInfo& GetRuntimeBindingInfo() const;
        VkDescriptorSetLayout GetDescriptorSetLayout(uint32_t index) const;
        VkPipelineBindPoint GetPipelineBindPoint() const;

        Result Create(const PipelineLayoutDesc& pipelineLayoutDesc);

        void SetDebugName(const char* name);

    private:
        void FillBindingOffsets(bool ignoreGlobalSPIRVOffsets, uint32_t* bindingOffsets);
        void ReserveStaticSamplers(const PipelineLayoutDesc& pipelineLayoutDesc);
        void CreateSetLayout(const DescriptorSetDesc& descriptorSetDesc, const uint32_t* bindingOffsets);

        void FillDescriptorBindings(const DescriptorSetDesc& descriptorSetDesc, const uint32_t* bindingOffsets,
            VkDescriptorSetLayoutBinding*& bindings, VkDescriptorBindingFlagsEXT*& bindingFlags) const;
        void FillDynamicConstantBufferBindings(const DescriptorSetDesc& descriptorSetDesc, const uint32_t* bindingOffsets,
            VkDescriptorSetLayoutBinding*& bindings, VkDescriptorBindingFlagsEXT*& bindingFlags) const;
        void CreateStaticSamplersAndFillSamplerBindings(const DescriptorSetDesc& descriptorSetDesc, const uint32_t* bindingOffsets,
            VkDescriptorSetLayoutBinding*& bindings, VkDescriptorBindingFlagsEXT*& bindingFlags);

        void FillPushConstantRanges(const PipelineLayoutDesc& pipelineLayoutDesc, VkPushConstantRange* pushConstantRanges) const;
        void FillRuntimeBindingInfo(const PipelineLayoutDesc& pipelineLayoutDesc, const uint32_t* bindingOffsets);

        VkPipelineLayout m_Handle = VK_NULL_HANDLE;
        VkPipelineBindPoint m_PipelineBindPoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;
        RuntimeBindingInfo m_RuntimeBindingInfo;
        Vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;
        Vector<DescriptorVK> m_StaticSamplers;
        DeviceVK& m_Device;
    };

    inline PipelineLayoutVK::operator VkPipelineLayout() const
    {
        return m_Handle;
    }

    inline DeviceVK& PipelineLayoutVK::GetDevice() const
    {
        return m_Device;
    }

    inline const RuntimeBindingInfo& PipelineLayoutVK::GetRuntimeBindingInfo() const
    {
        return m_RuntimeBindingInfo;
    }

    inline VkDescriptorSetLayout PipelineLayoutVK::GetDescriptorSetLayout(uint32_t index) const
    {
        return m_DescriptorSetLayouts[index];
    }

    inline VkPipelineBindPoint PipelineLayoutVK::GetPipelineBindPoint() const
    {
        return m_PipelineBindPoint;
    }
}