/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "DescriptorPoolVK.h"
#include "DescriptorSetVK.h"
#include "PipelineLayoutVK.h"
#include "DeviceVK.h"

using namespace nri;

DescriptorPoolVK::DescriptorPoolVK(DeviceVK& device) :
    m_AllocatedSets(device.GetStdAllocator()),
    m_Device(device)
{
    const size_t initialCapacity = 64;
    m_AllocatedSets.reserve(initialCapacity);
}

DescriptorPoolVK::~DescriptorPoolVK()
{
    const auto& lowLevelAllocator = m_Device.GetStdAllocator().GetInterface();

    for (size_t i = m_UsedSets; i < m_AllocatedSets.size(); i++)
        lowLevelAllocator.Free(lowLevelAllocator.userArg, m_AllocatedSets[i]);

    const auto& vk = m_Device.GetDispatchTable();
    if (m_Handle != VK_NULL_HANDLE && m_OwnsNativeObjects)
        vk.DestroyDescriptorPool(m_Device, m_Handle, m_Device.GetAllocationCallbacks());
}

inline void AddDescriptorPoolSize(VkDescriptorPoolSize* poolSizeArray, uint32_t& poolSizeArraySize, VkDescriptorType type, uint32_t descriptorCount)
{
    if (descriptorCount == 0)
        return;

    VkDescriptorPoolSize& poolSize = poolSizeArray[poolSizeArraySize++];
    poolSize.type = type;
    poolSize.descriptorCount = descriptorCount;
}

Result DescriptorPoolVK::Create(const DescriptorPoolDesc& descriptorPoolDesc)
{
    m_OwnsNativeObjects = true;

    const auto& vk = m_Device.GetDispatchTable();

    VkDescriptorPoolSize descriptorPoolSizeArray[16] = {};
    for (uint32_t i = 0; i < GetCountOf(descriptorPoolSizeArray); i++)
        descriptorPoolSizeArray[i].type = (VkDescriptorType)i;

    const uint32_t physicalDeviceMask = GetPhysicalDeviceGroupMask(descriptorPoolDesc.physicalDeviceMask);

    uint32_t phyiscalDeviceNum = 0;
    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
        phyiscalDeviceNum += ((1 << i) & physicalDeviceMask) != 0 ? 1 : 0;

    uint32_t poolSizeCount = 0;

    const uint32_t samplerMaxNum = descriptorPoolDesc.staticSamplerMaxNum + descriptorPoolDesc.samplerMaxNum;
    AddDescriptorPoolSize(descriptorPoolSizeArray, poolSizeCount, VK_DESCRIPTOR_TYPE_SAMPLER, samplerMaxNum);

    AddDescriptorPoolSize(descriptorPoolSizeArray, poolSizeCount, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorPoolDesc.constantBufferMaxNum);
    AddDescriptorPoolSize(descriptorPoolSizeArray, poolSizeCount, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, descriptorPoolDesc.dynamicConstantBufferMaxNum);
    AddDescriptorPoolSize(descriptorPoolSizeArray, poolSizeCount, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorPoolDesc.textureMaxNum);
    AddDescriptorPoolSize(descriptorPoolSizeArray, poolSizeCount, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorPoolDesc.storageTextureMaxNum);
    AddDescriptorPoolSize(descriptorPoolSizeArray, poolSizeCount, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, descriptorPoolDesc.bufferMaxNum);
    AddDescriptorPoolSize(descriptorPoolSizeArray, poolSizeCount, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorPoolDesc.storageBufferMaxNum);
    AddDescriptorPoolSize(descriptorPoolSizeArray, poolSizeCount, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorPoolDesc.structuredBufferMaxNum + descriptorPoolDesc.storageStructuredBufferMaxNum);
    AddDescriptorPoolSize(descriptorPoolSizeArray, poolSizeCount, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, descriptorPoolDesc.accelerationStructureMaxNum);

    for (uint32_t i = 0; i < poolSizeCount; i++)
        descriptorPoolSizeArray[i].descriptorCount *= phyiscalDeviceNum;

    const VkDescriptorPoolCreateInfo info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        nullptr,
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        descriptorPoolDesc.descriptorSetMaxNum * phyiscalDeviceNum,
        poolSizeCount,
        descriptorPoolSizeArray
    };

    const VkResult result = vk.CreateDescriptorPool(m_Device, &info, m_Device.GetAllocationCallbacks(), &m_Handle);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't create a descriptor pool: vkCreateDescriptorPool returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

Result DescriptorPoolVK::Create(void* vkDescriptorPool)
{
    m_OwnsNativeObjects = false;
    m_Handle = (VkDescriptorPool)vkDescriptorPool;

    return Result::SUCCESS;
}

void DescriptorPoolVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_DESCRIPTOR_POOL, m_Handle, name);
}

Result DescriptorPoolVK::AllocateDescriptorSets(const PipelineLayout& pipelineLayout, uint32_t setIndex, DescriptorSet** const descriptorSets,
    uint32_t numberOfCopies, uint32_t physicalDeviceMask, uint32_t variableDescriptorNum)
{
    const PipelineLayoutVK& pipelineLayoutVK = (const PipelineLayoutVK&)pipelineLayout;

    const uint32_t freeSetNum = (uint32_t)m_AllocatedSets.size() - m_UsedSets;

    if (freeSetNum < numberOfCopies)
    {
        const uint32_t newSetNum = numberOfCopies - freeSetNum;
        const uint32_t prevSetNum = (uint32_t)m_AllocatedSets.size();
        m_AllocatedSets.resize(prevSetNum + newSetNum);

        const auto& lowLevelAllocator = m_Device.GetStdAllocator().GetInterface();

        for (size_t i = 0; i < newSetNum; i++)
        {
            m_AllocatedSets[prevSetNum + i] = (DescriptorSetVK*)lowLevelAllocator.Allocate(lowLevelAllocator.userArg,
                sizeof(DescriptorSetVK), alignof(DescriptorSetVK));
        }
    }

    for (size_t i = 0; i < numberOfCopies; i++)
        descriptorSets[i] = (DescriptorSet*)m_AllocatedSets[m_UsedSets + i];
    m_UsedSets += numberOfCopies;

    const VkDescriptorSetLayout setLayout = pipelineLayoutVK.GetDescriptorSetLayout(setIndex);
    const DescriptorSetDesc& setDesc = pipelineLayoutVK.GetRuntimeBindingInfo().descriptorSetDescs[setIndex];
    const bool hasVariableDescriptorNum = pipelineLayoutVK.GetRuntimeBindingInfo().hasVariableDescriptorNum[setIndex];

    VkDescriptorSetVariableDescriptorCountAllocateInfoEXT variableDescriptorCountInfo;
    variableDescriptorCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
    variableDescriptorCountInfo.pNext = nullptr;
    variableDescriptorCountInfo.descriptorSetCount = 1;
    variableDescriptorCountInfo.pDescriptorCounts = &variableDescriptorNum;

    physicalDeviceMask = GetPhysicalDeviceGroupMask(physicalDeviceMask);

    std::array<VkDescriptorSetLayout, PHYSICAL_DEVICE_GROUP_MAX_SIZE> setLayoutArray = {};
    uint32_t phyicalDeviceNum = 0;
    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
            setLayoutArray[phyicalDeviceNum++] = setLayout;
    }

    const VkDescriptorSetAllocateInfo info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        hasVariableDescriptorNum ? &variableDescriptorCountInfo : nullptr,
        m_Handle,
        phyicalDeviceNum,
        setLayoutArray.data()
    };

    const auto& vk = m_Device.GetDispatchTable();

    std::array<VkDescriptorSet, PHYSICAL_DEVICE_GROUP_MAX_SIZE> handles = {};

    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < numberOfCopies && result == VK_SUCCESS; i++)
    {
        result = vk.AllocateDescriptorSets(m_Device, &info, handles.data());
        Construct(*((DescriptorSetVK**)descriptorSets + i), 1, m_Device, handles.data(), physicalDeviceMask, setDesc);
    }

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
        "Can't allocate descriptor sets: vkAllocateDescriptorSets returned %d.", (int32_t)result);

    return Result::SUCCESS;
}

void DescriptorPoolVK::Reset()
{
    m_UsedSets = 0;

    const auto& vk = m_Device.GetDispatchTable();
    const VkResult result = vk.ResetDescriptorPool(m_Device, m_Handle, (VkDescriptorPoolResetFlags)0);

    RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, ReturnVoid(),
        "Can't reset a descriptor pool: vkResetDescriptorPool returned %d.", (int32_t)result);
}

#include "DescriptorPoolVK.hpp"