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
    bool isCustomMatrixSet = false;
    float4x4 customMatrix = float4x4::identity;
};

class Camera
{
public:
    void Update(const CameraDesc& desc, uint32_t frameIndex);
    void Initialize(const float3& position, const float3& lookAt, bool isRelative = false, bool isLeftHanded = false);

    inline const float3 GetRelative(const double3& origin) const
    {
        double3 position = m_IsRelative ? m_GlobalPosition : double3(0.0);

        return ToFloat(origin - position);
    }

    inline void* GetDataPtr()
    { return &m_GlobalPosition; }

    inline uint32_t GetDataSize() const
    { return sizeof(Camera) - sizeof(Timer) - sizeof(bool) * 2; }

public:
    Timer m_Timer;
    double3 m_GlobalPosition = {};
    double3 m_GlobalPositionPrev = {};
    float4x4 m_ViewToClip = float4x4::identity;
    float4x4 m_ViewToClipPrev = float4x4::identity;
    float4x4 m_ClipToView = float4x4::identity;
    float4x4 m_ClipToViewPrev = float4x4::identity;
    float4x4 m_WorldToView = float4x4::identity;
    float4x4 m_WorldToViewPrev = float4x4::identity;
    float4x4 m_ViewToWorld = float4x4::identity;
    float4x4 m_ViewToWorldPrev = float4x4::identity;
    float4x4 m_WorldToClip = float4x4::identity;
    float4x4 m_WorldToClipPrev = float4x4::identity;
    float4x4 m_ClipToWorld = float4x4::identity;
    float4x4 m_ClipToWorldPrev = float4x4::identity;
    float3 m_Position = {};
    float3 m_PositionPrev = {};
    float3 m_Rotation = {};
    float4 m_Frustum = {};
    float2 m_ViewportJitter = {};
    float2 m_ViewportJitterPrev = {};
    float m_MotionScale = 0.015f;
    float m_IsOrtho = 0.0f;
private:
    bool m_IsRelative = false;
    bool m_IsLeftHanded = false;
};
