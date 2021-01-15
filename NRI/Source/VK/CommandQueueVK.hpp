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

static void NRI_CALL SetCommandQueueDebugName(CommandQueue& commandQueue, const char* name)
{
    ((CommandQueueVK&)commandQueue).SetDebugName(name);
}

static void NRI_CALL SubmitQueueWork(CommandQueue& commandQueue, const WorkSubmissionDesc& workSubmissionDesc, DeviceSemaphore* deviceSemaphore)
{
    ((CommandQueueVK&)commandQueue).Submit(workSubmissionDesc, deviceSemaphore);
}

static void NRI_CALL WaitForSemaphore(CommandQueue& commandQueue, DeviceSemaphore& deviceSemaphore)
{
    ((CommandQueueVK&)commandQueue).Wait(deviceSemaphore);
}

void FillFunctionTableCommandQueueVK(CoreInterface& coreInterface)
{
    coreInterface.SetCommandQueueDebugName = SetCommandQueueDebugName;
    coreInterface.SubmitQueueWork = SubmitQueueWork;
    coreInterface.WaitForSemaphore = WaitForSemaphore;
}

#pragma endregion

#pragma region [  HelperInterface  ]

static Result NRI_CALL ChangeResourceStatesVK(CommandQueue& commandQueue, const TransitionBarrierDesc& transitionBarriers)
{
    return ((CommandQueueVK&)commandQueue).ChangeResourceStates(transitionBarriers);
}

static nri::Result NRI_CALL UploadDataVK(CommandQueue& commandQueue, const TextureUploadDesc* textureUploadDescs, uint32_t textureUploadDescNum,
    const BufferUploadDesc* bufferUploadDescs, uint32_t bufferUploadDescNum)
{
    return ((CommandQueueVK&)commandQueue).UploadData(textureUploadDescs, textureUploadDescNum, bufferUploadDescs, bufferUploadDescNum);
}

static nri::Result NRI_CALL WaitForIdleVK(CommandQueue& commandQueue)
{
    return ((CommandQueueVK&)commandQueue).WaitForIdle();
}

void FillFunctionTableCommandQueueVK(HelperInterface& helperInterface)
{
    helperInterface.ChangeResourceStates = ChangeResourceStatesVK;
    helperInterface.UploadData = UploadDataVK;
    helperInterface.WaitForIdle = WaitForIdleVK;
}

#pragma endregion
