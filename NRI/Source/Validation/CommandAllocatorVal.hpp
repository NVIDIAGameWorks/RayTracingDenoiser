/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static void NRI_CALL SetCommandAllocatorDebugName(CommandAllocator& commandAllocator, const char* name)
{
    ((CommandAllocatorVal*)&commandAllocator)->SetDebugName(name);
}

static Result NRI_CALL CreateCommandBuffer(CommandAllocator& commandAllocator, CommandBuffer*& commandBuffer)
{
    return ((CommandAllocatorVal*)&commandAllocator)->CreateCommandBuffer(commandBuffer);
}

static void NRI_CALL ResetCommandAllocator(CommandAllocator& commandAllocator)
{
    ((CommandAllocatorVal*)&commandAllocator)->Reset();
}

void FillFunctionTableCommandAllocatorVal(CoreInterface& coreInterface)
{
    coreInterface.SetCommandAllocatorDebugName = SetCommandAllocatorDebugName;
    coreInterface.CreateCommandBuffer = CreateCommandBuffer;
    coreInterface.ResetCommandAllocator = ResetCommandAllocator;
}

#pragma endregion
