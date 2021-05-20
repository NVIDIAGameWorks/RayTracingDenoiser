/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "PipelineLayoutVK.h"
#include "DescriptorVK.h"
#include "DeviceVK.h"

using namespace nri;

PipelineLayoutVK::PipelineLayoutVK(DeviceVK& device) :
    m_RuntimeBindingInfo(device.GetStdAllocator()),
    m_DescriptorSetLayouts(device.GetStdAllocator()),
    m_StaticSamplers(device.GetStdAllocator()),
    m_Device(device)
{
}

PipelineLayoutVK::~PipelineLayoutVK()
{
    const auto& vk = m_Device.GetDispatchTable();

    const auto allocationCallbacks = m_Device.GetAllocationCallbacks();

    if (m_Handle != VK_NULL_HANDLE)
        vk.DestroyPipelineLayout(m_Device, m_Handle, allocationCallbacks);

    for (auto& handle : m_DescriptorSetLayouts)
        vk.DestroyDescriptorSetLayout(m_Device, handle, allocationCallbacks);

    m_StaticSamplers.clear();
}

Result PipelineLayoutVK::Create(const PipelineLayoutDesc& pipelineLayoutDesc)
{
    if (pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::ALL_GRAPHICS)
        m_PipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    if (pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::COMPUTE)
        m_PipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    if (pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::ALL_RAY_TRACING)
        m_PipelineBindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_NV;

    uint32_t bindingOffsets[(uint32_t)DescriptorType::MAX_NUM] = {};
    FillBindingOffsets(pipelineLayoutDesc.ignoreGlobalSPIRVOffsets, bindingOffsets);

    ReserveStaticSamplers(pipelineLayoutDesc);

    m_DescriptorSetLayouts.reserve(pipelineLayoutDesc.descriptorSetNum);
    for (uint32_t i = 0; i < pipelineLayoutDesc.descriptorSetNum; i++)
        CreateSetLayout(pipelineLayoutDesc.descriptorSets[i], bindingOffsets);

    VkPushConstantRange* pushConstantRanges = ALLOCATE_SCRATCH(m_Device, VkPushConstantRange, pipelineLayoutDesc.pushConstantNum);
    FillPushConstantRanges(pipelineLayoutDesc, pushConstantRanges);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutCreateInfo.setLayoutCount = pipelineLayoutDesc.descriptorSetNum;
    pipelineLayoutCreateInfo.pSetLayouts = m_DescriptorSetLayouts.data();
    pipelineLayoutCreateInfo.pushConstantRangeCount = pipelineLayoutDesc.pushConstantNum;
    pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges;

    const auto& vk = m_Device.GetDispatchTable();

    const VkResult result = vk.CreatePipelineLayout(m_Device, &pipelineLayoutCreateInfo, m_Device.GetAllocationCallbacks(), &m_Handle);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, Result::FAILURE,
        "Can't create a pipeline layout: vkCreatePipelineLayout returned %d.", (int32_t)result);

    FREE_SCRATCH(m_Device, pushConstantRanges, pipelineLayoutDesc.pushConstantNum);

    FillRuntimeBindingInfo(pipelineLayoutDesc, bindingOffsets);

    return Result::SUCCESS;
}

inline void PipelineLayoutVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, m_Handle, name);
}

void PipelineLayoutVK::FillBindingOffsets(bool ignoreGlobalSPIRVOffsets, uint32_t* bindingOffsets)
{
    SPIRVBindingOffsets spirvBindingOffsets;

    if (ignoreGlobalSPIRVOffsets)
        spirvBindingOffsets = {};
    else
        spirvBindingOffsets = m_Device.GetSPIRVBindingOffsets();

    bindingOffsets[(uint32_t)DescriptorType::SAMPLER] = spirvBindingOffsets.samplerOffset;
    bindingOffsets[(uint32_t)DescriptorType::CONSTANT_BUFFER] = spirvBindingOffsets.constantBufferOffset;
    bindingOffsets[(uint32_t)DescriptorType::TEXTURE] = spirvBindingOffsets.textureOffset;
    bindingOffsets[(uint32_t)DescriptorType::STORAGE_TEXTURE] = spirvBindingOffsets.storageTextureAndBufferOffset;
    bindingOffsets[(uint32_t)DescriptorType::BUFFER] = spirvBindingOffsets.textureOffset;
    bindingOffsets[(uint32_t)DescriptorType::STORAGE_BUFFER] = spirvBindingOffsets.storageTextureAndBufferOffset;
    bindingOffsets[(uint32_t)DescriptorType::STRUCTURED_BUFFER] = spirvBindingOffsets.textureOffset;
    bindingOffsets[(uint32_t)DescriptorType::STORAGE_STRUCTURED_BUFFER] = spirvBindingOffsets.storageTextureAndBufferOffset;
    bindingOffsets[(uint32_t)DescriptorType::ACCELERATION_STRUCTURE] = spirvBindingOffsets.textureOffset;
}

void PipelineLayoutVK::ReserveStaticSamplers(const PipelineLayoutDesc& pipelineLayoutDesc)
{
    uint32_t staticSamplerNum = 0;
    for (uint32_t i = 0; i < pipelineLayoutDesc.descriptorSetNum; i++)
        staticSamplerNum += pipelineLayoutDesc.descriptorSets[i].staticSamplerNum;

    m_StaticSamplers.reserve(staticSamplerNum);
}

void PipelineLayoutVK::CreateSetLayout(const DescriptorSetDesc& descriptorSetDesc, const uint32_t* bindingOffsets)
{
    uint32_t bindingMaxNum = descriptorSetDesc.dynamicConstantBufferNum + descriptorSetDesc.staticSamplerNum;

    for (uint32_t i = 0; i < descriptorSetDesc.rangeNum; i++)
    {
        const DescriptorRangeDesc& range = descriptorSetDesc.ranges[i];
        bindingMaxNum += range.isArray ? 1 : range.descriptorNum;
    }

    VkDescriptorSetLayoutBinding* bindings = ALLOCATE_SCRATCH(m_Device, VkDescriptorSetLayoutBinding, bindingMaxNum);
    VkDescriptorBindingFlagsEXT* bindingFlags = ALLOCATE_SCRATCH(m_Device, VkDescriptorBindingFlagsEXT, bindingMaxNum);
    VkDescriptorSetLayoutBinding* bindingsBegin = bindings;
    VkDescriptorBindingFlagsEXT* bindingFlagsBegin = bindingFlags;

    FillDescriptorBindings(descriptorSetDesc, bindingOffsets, bindings, bindingFlags);
    FillDynamicConstantBufferBindings(descriptorSetDesc, bindingOffsets, bindings, bindingFlags);
    CreateStaticSamplersAndFillSamplerBindings(descriptorSetDesc, bindingOffsets, bindings, bindingFlags);

    const uint32_t bindingNum = uint32_t(bindings - bindingsBegin);

    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlagsInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT,
        nullptr,
        bindingNum,
        bindingFlagsBegin
    };

    VkDescriptorSetLayoutCreateInfo info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        m_Device.IsDescriptorIndexingExtSupported() ? &bindingFlagsInfo : nullptr,
        (VkDescriptorSetLayoutCreateFlags)0,
        bindingNum,
        bindingsBegin
    };

    VkDescriptorSetLayout handle = VK_NULL_HANDLE;

    const auto& vk = m_Device.GetDispatchTable();
    const VkResult result = vk.CreateDescriptorSetLayout(m_Device, &info, m_Device.GetAllocationCallbacks(), &handle);

    m_DescriptorSetLayouts.push_back(handle);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, ReturnVoid(),
        "Can't create the descriptor set layout: vkCreateDescriptorSetLayout returned %d.", (int32_t)result);

    FREE_SCRATCH(m_Device, bindingsBegin, bindingMaxNum);
    FREE_SCRATCH(m_Device, bindingFlagsBegin, bindingMaxNum);
}

void PipelineLayoutVK::FillDescriptorBindings(const DescriptorSetDesc& descriptorSetDesc, const uint32_t* bindingOffsets,
    VkDescriptorSetLayoutBinding*& bindings, VkDescriptorBindingFlagsEXT*& bindingFlags) const
{
    constexpr VkDescriptorBindingFlagsEXT variableSizedArrayFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
                VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;

    for (uint32_t i = 0; i < descriptorSetDesc.rangeNum; i++)
    {
        const DescriptorRangeDesc& range = descriptorSetDesc.ranges[i];

        const uint32_t baseBindingIndex = range.baseRegisterIndex + bindingOffsets[(uint32_t)range.descriptorType];

        if (range.isArray)
        {
            *(bindingFlags++) = range.isDescriptorNumVariable ? variableSizedArrayFlags : 0;

            VkDescriptorSetLayoutBinding& descriptorBinding = *(bindings++);
            descriptorBinding = {};
            descriptorBinding.binding = baseBindingIndex;
            descriptorBinding.descriptorType = GetDescriptorType(range.descriptorType);
            descriptorBinding.descriptorCount = range.descriptorNum;
            descriptorBinding.stageFlags = GetShaderStageFlags(range.visibility);
        }
        else
        {
            for (uint32_t j = 0; j < range.descriptorNum; j++)
            {
                *(bindingFlags++) = 0;

                VkDescriptorSetLayoutBinding& descriptorBinding = *(bindings++);
                descriptorBinding = {};
                descriptorBinding.binding = baseBindingIndex + j;
                descriptorBinding.descriptorType = GetDescriptorType(range.descriptorType);
                descriptorBinding.descriptorCount = 1;
                descriptorBinding.stageFlags = GetShaderStageFlags(range.visibility);
            }
        }
    }
}

void PipelineLayoutVK::FillDynamicConstantBufferBindings(const DescriptorSetDesc& descriptorSetDesc, const uint32_t* bindingOffsets,
    VkDescriptorSetLayoutBinding*& bindings, VkDescriptorBindingFlagsEXT*& bindingFlags) const
{
    for (uint32_t i = 0; i < descriptorSetDesc.dynamicConstantBufferNum; i++)
    {
        const DynamicConstantBufferDesc& buffer = descriptorSetDesc.dynamicConstantBuffers[i];

        *(bindingFlags++) = 0;

        VkDescriptorSetLayoutBinding& descriptorBinding = *(bindings++);
        descriptorBinding = {};
        descriptorBinding.binding = buffer.registerIndex + bindingOffsets[(uint32_t)DescriptorType::CONSTANT_BUFFER];
        descriptorBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptorBinding.descriptorCount = 1;
        descriptorBinding.stageFlags = GetShaderStageFlags(buffer.visibility);
    }
}

void PipelineLayoutVK::CreateStaticSamplersAndFillSamplerBindings(const DescriptorSetDesc& descriptorSetDesc, const uint32_t* bindingOffsets,
    VkDescriptorSetLayoutBinding*& bindings, VkDescriptorBindingFlagsEXT*& bindingFlags)
{
    for (uint32_t i = 0; i < descriptorSetDesc.staticSamplerNum; i++)
    {
        const StaticSamplerDesc& sampler = descriptorSetDesc.staticSamplers[i];

        m_StaticSamplers.emplace_back(m_Device);
        DescriptorVK& descirptor = m_StaticSamplers.back();

        descirptor.Create(sampler.samplerDesc);

        *(bindingFlags++) = 0;

        VkDescriptorSetLayoutBinding& descriptorBinding = *(bindings++);
        descriptorBinding = {};
        descriptorBinding.binding = sampler.registerIndex + bindingOffsets[(uint32_t)DescriptorType::SAMPLER];
        descriptorBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        descriptorBinding.descriptorCount = 1;
        descriptorBinding.stageFlags = GetShaderStageFlags(sampler.visibility);
        descriptorBinding.pImmutableSamplers = &descirptor.GetSampler();
    }
}

void PipelineLayoutVK::FillPushConstantRanges(const PipelineLayoutDesc& pipelineLayoutDesc, VkPushConstantRange* pushConstantRanges) const
{
    uint32_t offset = 0;

    for (uint32_t i = 0; i < pipelineLayoutDesc.pushConstantNum; i++)
    {
        const PushConstantDesc& pushConstantDesc = pipelineLayoutDesc.pushConstants[i];

        VkPushConstantRange& range = pushConstantRanges[i];
        range = {};
        range.stageFlags = GetShaderStageFlags(pushConstantDesc.visibility);
        range.offset = offset;
        range.size = pushConstantDesc.size;

        offset += pushConstantDesc.size;
    }
}

void PipelineLayoutVK::FillRuntimeBindingInfo(const PipelineLayoutDesc& pipelineLayoutDesc, const uint32_t* bindingOffsets)
{
    RuntimeBindingInfo& destination = m_RuntimeBindingInfo;
    const PipelineLayoutDesc& source = pipelineLayoutDesc;

    destination.descriptorSetDescs.insert(destination.descriptorSetDescs.begin(),
        source.descriptorSets, source.descriptorSets + source.descriptorSetNum);

    destination.pushConstantDescs.insert(destination.pushConstantDescs.begin(),
        source.pushConstants, source.pushConstants + source.pushConstantNum);

    destination.pushConstantBindings.resize(source.pushConstantNum);
    for (uint32_t i = 0, offset = 0; i < source.pushConstantNum; i++)
    {
        destination.pushConstantBindings[i] = { GetShaderStageFlags(source.pushConstants[i].visibility), offset };
        offset += source.pushConstants[i].size;
    }

    size_t rangeNum = 0;
    size_t dynamicConstantBufferNum = 0;
    for (uint32_t i = 0; i < source.descriptorSetNum; i++)
    {
        rangeNum += source.descriptorSets[i].rangeNum;
        dynamicConstantBufferNum += source.descriptorSets[i].dynamicConstantBufferNum;
    }

    destination.hasVariableDescriptorNum.resize(source.descriptorSetNum);
    destination.descriptorSetRangeDescs.reserve(rangeNum);
    destination.dynamicConstantBufferDescs.reserve(dynamicConstantBufferNum);

    for (uint32_t i = 0; i < source.descriptorSetNum; i++)
    {
        const DescriptorSetDesc& descriptorSetDesc = source.descriptorSets[i];

        destination.hasVariableDescriptorNum[i] = false;

        destination.descriptorSetDescs[i].ranges =
            destination.descriptorSetRangeDescs.data() + destination.descriptorSetRangeDescs.size();

        destination.descriptorSetDescs[i].dynamicConstantBuffers =
            destination.dynamicConstantBufferDescs.data() + destination.dynamicConstantBufferDescs.size();

        // Copy descriptor range descs
        destination.descriptorSetRangeDescs.insert(destination.descriptorSetRangeDescs.end(),
            descriptorSetDesc.ranges, descriptorSetDesc.ranges + descriptorSetDesc.rangeNum);

        // Fix descriptor range binding offsets and check for variable descriptor num
        DescriptorRangeDesc* ranges = const_cast<DescriptorRangeDesc*>(destination.descriptorSetDescs[i].ranges);
        for (uint32_t j = 0; j < descriptorSetDesc.rangeNum; j++)
        {
            ranges[j].baseRegisterIndex += bindingOffsets[(uint32_t)descriptorSetDesc.ranges[j].descriptorType];

            if (m_Device.IsDescriptorIndexingExtSupported() && descriptorSetDesc.ranges[j].isDescriptorNumVariable)
                destination.hasVariableDescriptorNum[i] = true;
        }

        // Copy dynamic constant buffer descs
        destination.dynamicConstantBufferDescs.insert(destination.dynamicConstantBufferDescs.end(),
            descriptorSetDesc.dynamicConstantBuffers, descriptorSetDesc.dynamicConstantBuffers + descriptorSetDesc.dynamicConstantBufferNum);

        // Copy dynamic constant buffer binding offsets
        DynamicConstantBufferDesc* dynamicConstantBuffers = const_cast<DynamicConstantBufferDesc*>(destination.descriptorSetDescs[i].dynamicConstantBuffers);
        for (uint32_t j = 0; j < descriptorSetDesc.dynamicConstantBufferNum; j++)
            dynamicConstantBuffers[j].registerIndex += bindingOffsets[(uint32_t)DescriptorType::CONSTANT_BUFFER];
    }
}

RuntimeBindingInfo::RuntimeBindingInfo(StdAllocator<uint8_t>& allocator) :
    hasVariableDescriptorNum(allocator),
    descriptorSetRangeDescs(allocator),
    dynamicConstantBufferDescs(allocator),
    descriptorSetDescs(allocator),
    pushConstantDescs(allocator),
    pushConstantBindings(allocator)
{
}

#include "PipelineLayoutVK.hpp"