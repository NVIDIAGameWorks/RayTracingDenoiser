/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma region [  CoreInterface  ]

static void NRI_CALL SetQueryPoolDebugName(QueryPool& queryPool, const char* name)
{
    ((QueryPoolVal*)&queryPool)->SetDebugName(name);
}

static uint32_t NRI_CALL GetQuerySize(const QueryPool& queryPool)
{
    return ((QueryPoolVal*)&queryPool)->GetQuerySize();
}

void FillFunctionTableQueryPoolVal(CoreInterface& coreInterface)
{
    coreInterface.SetQueryPoolDebugName = SetQueryPoolDebugName;
    coreInterface.GetQuerySize = GetQuerySize;
}

#pragma endregion
