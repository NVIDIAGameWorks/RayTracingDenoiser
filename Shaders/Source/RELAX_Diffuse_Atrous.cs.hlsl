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

#define RELAX_DIFFUSE

#include "../Include/RELAX/RELAX_Config.hlsli"
#include "../Resources/RELAX_DiffuseSpecular_Atrous.resources.hlsli"

#include "../Include/Common.hlsli"
#include "../Include/RELAX/RELAX_Common.hlsli"
#include "../Include/RELAX/RELAX_DiffuseSpecular_Atrous.hlsli"
