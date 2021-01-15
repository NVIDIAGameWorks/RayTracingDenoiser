/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#pragma region [  CoreInterface  ]

static void NRI_CALL SetDescriptorSetDebugName(DescriptorSet& descriptorSet, const char* name)
{
    ((DescriptorSetVK&)descriptorSet).SetDebugName(name);
}

static void NRI_CALL UpdateDescriptorRanges(DescriptorSet& descriptorSet, uint32_t physicalDeviceMask, uint32_t baseRange, uint32_t rangeNum, const DescriptorRangeUpdateDesc* rangeUpdateDescs)
{
    ((DescriptorSetVK&)descriptorSet).UpdateDescriptorRanges(physicalDeviceMask, baseRange, rangeNum, rangeUpdateDescs);
}

static void NRI_CALL UpdateDynamicConstantBuffers(DescriptorSet& descriptorSet, uint32_t physicalDeviceMask, uint32_t baseBuffer, uint32_t bufferNum, const Descriptor* const* descriptors)
{
    ((DescriptorSetVK&)descriptorSet).UpdateDynamicConstantBuffers(physicalDeviceMask, baseBuffer, bufferNum, descriptors);
}

static void NRI_CALL CopyDescriptorSet(DescriptorSet& descriptorSet, const DescriptorSetCopyDesc& descriptorSetCopyDesc)
{
    ((DescriptorSetVK&)descriptorSet).Copy(descriptorSetCopyDesc);
}

void FillFunctionTableDescriptorSetVK(CoreInterface& coreInterface)
{
    coreInterface.SetDescriptorSetDebugName = SetDescriptorSetDebugName;
    coreInterface.UpdateDescriptorRanges = UpdateDescriptorRanges;
    coreInterface.UpdateDynamicConstantBuffers = UpdateDynamicConstantBuffers;
    coreInterface.CopyDescriptorSet = CopyDescriptorSet;
}

#pragma endregion
