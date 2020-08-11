/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "Camera.h"

void Camera::Initialize(const float3& position, const float3& lookAt, bool isRelative)
{
    float3 dir = Normalize(lookAt - position);

    float3 rot;
    rot.x = Atan(dir.y, dir.x);
    rot.y = Asin(dir.z);
    rot.z = 0.0f;

    m_GlobalPosition = ToDouble(position);
    m_Rotation = RadToDeg(rot);
    m_IsRelative = isRelative;
}

void Camera::Update(const CameraDesc& desc, uint32_t frameIndex)
{
    m_Timer.UpdateElapsedTimeSinceLastSave();
    m_Timer.SaveCurrentTime();

    float timeScale = 0.06f * Pow( m_Timer.GetSmoothedElapsedTime(), 0.75f );

    uint32_t projFlags = desc.isProjectionReversed ? PROJ_REVERSED_Z : 0;
    projFlags |= desc.isLeftHanded ? PROJ_LEFT_HANDED : 0;

    // Previous state
    m_WorldToViewPrev = m_WorldToView;
    m_ViewToClipPrev = m_ViewToClip;
    m_ClipToViewPrev = m_ClipToView;
    m_ViewportJitterPrev = m_ViewportJitter;
    m_GlobalPositionPrev = m_GlobalPosition;

    // Position
    const float3 vRight = m_WorldToViewPrev.GetRow0().To3d();
    const float3 vUp = m_WorldToViewPrev.GetRow1().To3d();
    const float3 vForward = m_WorldToViewPrev.GetRow2().To3d();

    float linearSpeed = 5.0f * timeScale;
    float3 delta = desc.dLocal * linearSpeed;
    delta.z *= desc.isLeftHanded ? 1.0f : -1.0f;

    m_GlobalPosition += ToDouble(vRight * delta.x);
    m_GlobalPosition += ToDouble(vUp * delta.y);
    m_GlobalPosition += ToDouble(vForward * delta.z);
    m_GlobalPosition += ToDouble(desc.dUser);

    if (desc.limits.IsValid())
        m_GlobalPosition = Clamp(m_GlobalPosition, ToDouble(desc.limits.vMin), ToDouble(desc.limits.vMax));

    if (desc.isCustomMatrixSet)
        m_GlobalPosition = ToDouble( desc.customMatrix.GetRow3().To3d() );

    if (m_IsRelative)
    {
        m_Position = float3::Zero();
        m_PositionPrev = ToFloat(m_GlobalPositionPrev - m_GlobalPosition);
        m_WorldToViewPrev.PreTranslation(-m_PositionPrev);
    }
    else
    {
        m_Position = ToFloat(m_GlobalPosition);
        m_PositionPrev = ToFloat(m_GlobalPositionPrev);
    }

    // Rotation
    float angularSpeed = 0.5f * timeScale * Saturate( desc.horizontalFov * 0.5f / 90.0f );

    m_Rotation.x += desc.dYaw * angularSpeed;
    m_Rotation.y += desc.dPitch * angularSpeed;

    m_Rotation.x = Mod(m_Rotation.x, 360.0f);
    m_Rotation.y = Clamp(m_Rotation.y, -90.0f, 90.0f);

    if( desc.isCustomMatrixSet )
    {
        m_ViewToWorld = desc.customMatrix;

        m_Rotation = RadToDeg( m_ViewToWorld.GetRotationYPR() );
        m_Rotation.z = 0.0f;
    }
    else
        m_ViewToWorld.SetupByRotationYPR( DegToRad(m_Rotation.x), DegToRad(m_Rotation.y), DegToRad(m_Rotation.z) );

    m_WorldToView = m_ViewToWorld;
    m_WorldToView.WorldToView(projFlags);
    m_WorldToView.PreTranslation( -m_Position );

    // Projection
    if(desc.orthoRange > 0.0f)
    {
        float r = desc.orthoRange * Saturate( desc.horizontalFov / 180.0f );
        m_ViewToClip.SetupByOrthoProjection(-r, r, -r / desc.aspectRatio, r / desc.aspectRatio, desc.nearZ, desc.farZ);
    }
    else
    {
        if (desc.farZ == 0.0f)
            m_ViewToClip.SetupByHalfFovxInf(0.5f * DegToRad(desc.horizontalFov), desc.aspectRatio, desc.nearZ, projFlags);
        else
            m_ViewToClip.SetupByHalfFovx(0.5f * DegToRad(desc.horizontalFov), desc.aspectRatio, desc.nearZ, desc.farZ, projFlags);
    }

    // Other
    m_ViewToWorld = m_WorldToView;
    m_ViewToWorld.InvertOrtho();

    m_ViewToWorldPrev = m_WorldToViewPrev;
    m_ViewToWorldPrev.InvertOrtho();

    m_ClipToView = m_ViewToClip;
    m_ClipToView.Invert();

    m_ClipToWorld = m_WorldToClip;
    m_ClipToWorld.Invert();

    m_WorldToClip = m_ViewToClip * m_WorldToView;
    m_WorldToClipPrev = m_ViewToClipPrev * m_WorldToViewPrev;

    m_ClipToWorldPrev = m_WorldToClipPrev;
    m_ClipToWorld.Invert();

    m_ViewportJitter = Halton2D( frameIndex ) - 0.5f;

    uint32_t flags = 0;
    DecomposeProjection(NDC_D3D, NDC_D3D, m_ViewToClip, &flags, nullptr, nullptr, m_Frustum.pv, nullptr, nullptr);
    m_IsOrtho = ( flags & PROJ_ORTHO ) == 0 ? 0.0f : ( ( flags & PROJ_LEFT_HANDED ) ? 1.0f : -1.0f );
}
