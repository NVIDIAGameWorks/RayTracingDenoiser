/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static void NRI_CALL SetPipelineDebugName(Pipeline& pipeline, const char* name)
{
    ((PipelineD3D11&)pipeline).SetDebugName(name);
}

void FillFunctionTablePipelineD3D11(CoreInterface& coreInterface)
{
    coreInterface.SetPipelineDebugName = SetPipelineDebugName;
}

#pragma endregion
