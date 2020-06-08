/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static void NRI_CALL SetCommandQueueDebugName(CommandQueue& commandQueue, const char* name)
{
    ((CommandQueueD3D12&)commandQueue).SetDebugName(name);
}

static void NRI_CALL SubmitQueueWork(CommandQueue& commandQueue, const WorkSubmissionDesc& workSubmissionDesc, DeviceSemaphore* deviceSemaphore)
{
    ((CommandQueueD3D12&)commandQueue).Submit(workSubmissionDesc, deviceSemaphore);
}

static void NRI_CALL WaitForSemaphore(CommandQueue& commandQueue, DeviceSemaphore& deviceSemaphore)
{
    ((CommandQueueD3D12&)commandQueue).Wait(deviceSemaphore);
}

void FillFunctionTableCommandQueueD3D12(CoreInterface& coreInterface)
{
    coreInterface.SetCommandQueueDebugName = SetCommandQueueDebugName;
    coreInterface.SubmitQueueWork = SubmitQueueWork;
    coreInterface.WaitForSemaphore = WaitForSemaphore;
}

#pragma endregion
