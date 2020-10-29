/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "Timer/Timer.h"

#define NDC_DONT_CARE
#include "MathLib/MathLib.h"

struct CameraDesc
{
    cBoxf limits = {};
    float3 dLocal = float3(0.0f);
    float3 dUser = float3(0.0f);
    float dYaw = 0.0f; // deg
    float dPitch = 0.0f; // deg
    float aspectRatio = 1.0f;
    float horizontalFov = 90.0f; // deg
    float nearZ = 0.1f;
    float farZ = 10000.0f;
    float orthoRange = 0.0f;
    bool isProjectionReversed = false;
    bool isLeftHanded = true;
    bool isCustomMatrixSet = false;
    float4x4 customMatrix = float4x4::identity;
};

struct CameraState
{
    double3 globalPosition = {};
    float4x4 mViewToClip = float4x4::identity;
    float4x4 mClipToView = float4x4::identity;
    float4x4 mWorldToView = float4x4::identity;
    float4x4 mViewToWorld = float4x4::identity;
    float4x4 mWorldToClip = float4x4::identity;
    float4x4 mClipToWorld = float4x4::identity;
    float4 frustum = {};
    float3 position = {};
    float3 rotation = {};
    float2 viewportJitter = {};
    float motionScale = 0.015f;
};

class Camera
{
public:
    void Update(const CameraDesc& desc, uint32_t frameIndex);
    void Initialize(const float3& position, const float3& lookAt, bool isRelative = false);

    inline void SavePreviousState()
    {
        statePrev = state;
    }

    inline const float3 GetRelative(const double3& origin) const
    {
        double3 position = m_IsRelative ? state.globalPosition : double3(0.0);

        return ToFloat(origin - position);
    }

    inline void* GetState()
    { return &state; }

    static inline uint32_t GetStateSize()
    { return sizeof(CameraState); }

public:
    Timer m_Timer;
    CameraState state = {};
    CameraState statePrev = {};
    float m_IsOrtho = 0.0f;
private:
    bool m_IsRelative = false;
};
