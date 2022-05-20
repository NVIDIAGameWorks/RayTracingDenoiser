/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "STL.hlsli"
#include "../Include/NRD.hlsli"

#define SIGMA_TRANSLUCENT

#include "../Include/SIGMA/SIGMA_Config.hlsli"
#include "../Resources/SIGMA_Shadow_SplitScreen.resources.hlsli"

#include "../Include/Common.hlsli"
#include "../Include/SIGMA/SIGMA_Common.hlsli"
#include "../Include/SIGMA/SIGMA_Shadow_SplitScreen.hlsli"