/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static void NRI_CALL SetPipelineDebugName(Pipeline& pipeline, const char* name)
{
    ((PipelineVal*)&pipeline)->SetDebugName(name);
}

void FillFunctionTablePipelineVal(CoreInterface& coreInterface)
{
    coreInterface.SetPipelineDebugName = SetPipelineDebugName;
}

static Result NRI_CALL WriteShaderGroupIdentifiers(const Pipeline& pipeline, uint32_t baseShaderGroupIndex, uint32_t shaderGroupNum, void* buffer)
{
    return ((PipelineVal&)pipeline).WriteShaderGroupIdentifiers(baseShaderGroupIndex, shaderGroupNum, buffer);
}

void FillFunctionTablePipelineVal(RayTracingInterface& rayTracingInterface)
{
    rayTracingInterface.WriteShaderGroupIdentifiers = WriteShaderGroupIdentifiers;
}

#pragma endregion
