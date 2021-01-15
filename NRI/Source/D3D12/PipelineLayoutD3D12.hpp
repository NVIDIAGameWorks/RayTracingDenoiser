/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static void NRI_CALL SetPipelineLayoutDebugName(PipelineLayout& pipelineLayout, const char* name)
{
    ((PipelineLayoutD3D12&)pipelineLayout).SetDebugName(name);
}

void FillFunctionTablePipelineLayoutD3D12(CoreInterface& coreInterface)
{
    coreInterface.SetPipelineLayoutDebugName = SetPipelineLayoutDebugName;
}

#pragma endregion
