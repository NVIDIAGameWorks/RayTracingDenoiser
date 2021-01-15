/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static void NRI_CALL SetDescriptorPoolDebugName(DescriptorPool& descriptorPool, const char* name)
{
    ((DescriptorPoolD3D12&)descriptorPool).SetDebugName(name);
}

static Result NRI_CALL AllocateDescriptorSets(DescriptorPool& descriptorPool, const PipelineLayout& pipelineLayout, uint32_t setIndex,
    DescriptorSet** const descriptorSets, uint32_t instanceNum, uint32_t physicalDeviceMask, uint32_t variableDescriptorNum)
{
    // TODO: use physicalDeviceMask

    return ((DescriptorPoolD3D12&)descriptorPool).AllocateDescriptorSets(pipelineLayout, setIndex, descriptorSets, instanceNum, variableDescriptorNum);
}

static void NRI_CALL ResetDescriptorPool(DescriptorPool& descriptorPool)
{
    ((DescriptorPoolD3D12&)descriptorPool).Reset();
}

void FillFunctionTableDescriptorPoolD3D12(CoreInterface& coreInterface)
{
    coreInterface.SetDescriptorPoolDebugName = SetDescriptorPoolDebugName;
    coreInterface.AllocateDescriptorSets = AllocateDescriptorSets;
    coreInterface.ResetDescriptorPool = ResetDescriptorPool;
}

#pragma endregion
