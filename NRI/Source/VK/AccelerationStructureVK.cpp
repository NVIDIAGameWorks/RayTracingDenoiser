/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "AccelerationStructureVK.h"
#include "BufferVK.h"
#include "DescriptorVK.h"
#include "CommandQueueVK.h"
#include "DeviceVK.h"
#include "ConversionVK.h"

using namespace nri;

AccelerationStructureVK::~AccelerationStructureVK()
{
    const auto& vk = m_Device.GetDispatchTable();

    if (!m_OwnsNativeObjects)
        return;

    for (uint32_t i = 0; i < GetCountOf(m_Handles); i++)
    {
        if (m_Handles[i] != VK_NULL_HANDLE)
            vk.DestroyAccelerationStructureNV(m_Device, m_Handles[i], m_Device.GetAllocationCallbacks());
    }
}

Result AccelerationStructureVK::Create(const AccelerationStructureDesc& accelerationStructureDesc)
{
    m_OwnsNativeObjects = true;

    Vector<VkGeometryNV> geometries(m_Device.GetStdAllocator());

    const bool isBottomLevel = accelerationStructureDesc.type == AccelerationStructureType::BOTTOM_LEVEL;
    if (isBottomLevel)
    {
        geometries.resize(accelerationStructureDesc.instanceOrGeometryObjectNum);
        ConvertGeometryObjectSizesVK(geometries.data(), accelerationStructureDesc.geometryObjects, (uint32_t)geometries.size());
    }

    VkAccelerationStructureCreateInfoNV createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
    VkAccelerationStructureInfoNV& info = createInfo.info;

    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
    info.type = GetAccelerationStructureType(accelerationStructureDesc.type);
    info.flags = GetAccelerationStructureBuildFlags(accelerationStructureDesc.flags);
    info.instanceCount = isBottomLevel ? 0 : accelerationStructureDesc.instanceOrGeometryObjectNum;
    info.geometryCount = isBottomLevel ? accelerationStructureDesc.instanceOrGeometryObjectNum : 0;
    info.pGeometries = geometries.data();

    const auto& vk = m_Device.GetDispatchTable();

    uint32_t physicalDeviceMask = accelerationStructureDesc.physicalDeviceMask;
    physicalDeviceMask = (physicalDeviceMask == WHOLE_DEVICE_GROUP) ? 0xff : physicalDeviceMask;

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if ((1 << i) & physicalDeviceMask)
        {
            const VkResult result = vk.CreateAccelerationStructureNV(m_Device, &createInfo, m_Device.GetAllocationCallbacks(), &m_Handles[i]);

            RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                "Can't create an acceleration structure: CreateAccelerationStructureNV returned %d.", (int32_t)result);
        }
    }

    m_CreationFlags = info.flags;

    return Result::SUCCESS;
}

Result AccelerationStructureVK::RetrieveNativeHandle()
{
    const auto& vk = m_Device.GetDispatchTable();

    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize(); i++)
    {
        if (m_Handles[i] != VK_NULL_HANDLE)
        {
            const VkResult result = vk.GetAccelerationStructureHandleNV(m_Device, m_Handles[i],
                sizeof(m_NativeHandles[i]), &m_NativeHandles[i]);

            RETURN_ON_FAILURE(m_Device.GetLog(), result == VK_SUCCESS, GetReturnCode(result),
                "Can't get an acceleration structure handle: vkCreateAccelerationStructureNVX returned %d.", (int32_t)result);
        }
    }

    return Result::SUCCESS;
}

void AccelerationStructureVK::SetDebugName(const char* name)
{
    m_Device.SetDebugNameToDeviceGroupObject(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV, (void**)m_Handles, name);
}

void AccelerationStructureVK::GetMemoryInfo(MemoryDesc& memoryDesc) const
{
    VkMemoryRequirements2 requirements = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    GetMemoryInfo(VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV, requirements);

    memoryDesc.mustBeDedicated = false;
    memoryDesc.alignment = (uint32_t)requirements.memoryRequirements.alignment;
    memoryDesc.size = requirements.memoryRequirements.size;

    MemoryTypeUnpack unpack = {};
    const bool found = m_Device.GetMemoryType(MemoryLocation::DEVICE, requirements.memoryRequirements.memoryTypeBits, unpack.info);
    CHECK(m_Device.GetLog(), found, "Can't find suitable memory type: %d", requirements.memoryRequirements.memoryTypeBits);

    memoryDesc.type = unpack.type;
}

uint64_t AccelerationStructureVK::GetUpdateScratchBufferSize() const
{
    VkMemoryRequirements2 requirements = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    GetMemoryInfo(VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV, requirements);

    return requirements.memoryRequirements.size;
}

uint64_t AccelerationStructureVK::GetBuildScratchBufferSize() const
{
    VkMemoryRequirements2 requirements = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    GetMemoryInfo(VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV, requirements);

    return requirements.memoryRequirements.size;
}

void AccelerationStructureVK::GetMemoryInfo(VkAccelerationStructureMemoryRequirementsTypeNV type, VkMemoryRequirements2& requirements) const
{
    VkAccelerationStructureNV handle = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < m_Device.GetPhyiscalDeviceGroupSize() && handle == VK_NULL_HANDLE; i++)
        handle = m_Handles[i];

    VkAccelerationStructureMemoryRequirementsInfoNV info = {};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
    info.type = type;
    info.accelerationStructure = handle;

    const auto& vk = m_Device.GetDispatchTable();
    vk.GetAccelerationStructureMemoryRequirementsNV(m_Device, &info, &requirements);
}

Result AccelerationStructureVK::CreateDescriptor(uint32_t physicalDeviceMask, Descriptor*& descriptor) const
{
    DescriptorVK& descriptorImpl = *Allocate<DescriptorVK>(m_Device.GetStdAllocator(), m_Device);
    descriptorImpl.Create(m_Handles, physicalDeviceMask);
    descriptor = (Descriptor*)&descriptorImpl;

    return Result::SUCCESS;
}

#include "AcceleratrionStructureVK.hpp"