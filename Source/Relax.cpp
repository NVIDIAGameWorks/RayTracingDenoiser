/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "DenoiserImpl.h"

constexpr uint32_t RELAX_MAX_ATROUS_PASS_NUM = 8;

#define RELAX_SET_SHARED_CONSTANTS SetSharedConstants(5, 8, 7, 14)

#define RELAX_ADD_VALIDATION_DISPATCH \
    PushPass("Validation"); \
    { \
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) ); \
        PushInput( AsUint(ResourceType::IN_VIEWZ) ); \
        PushInput( AsUint(ResourceType::IN_MV) ); \
        PushInput( AsUint(Permanent::HISTORY_LENGTH_CURR) ); \
        PushOutput( AsUint(ResourceType::OUT_VALIDATION) ); \
        AddDispatch( RELAX_Validation, SumConstants(1, 0, 1, 1), NumThreads(16, 16), IGNORE_RS ); \
    }

inline ml::float3 RELAX_GetFrustumForward(const ml::float4x4& viewToWorld, const ml::float4& frustum)
{
    // Note: this vector is not normalized for non-symmetric projections but that's correct.
    // It has to have .z coordinate equal to 1.0 to correctly reconstruct world position in shaders.
    ml::float4 frustumForwardView = ml::float4(0.5f, 0.5f, 1.0f, 0.0f) * ml::float4(frustum.z, frustum.w, 1.0f, 0.0f) + ml::float4(frustum.x, frustum.y, 0.0f, 0.0f);
    ml::float3 frustumForwardWorld = (viewToWorld * frustumForwardView).To3d();
    return frustumForwardWorld;
}

inline bool RELAX_IsCameraStatic
(
    const ml::float3& cameraDelta,
    const ml::float3& frustumRight, const ml::float3& frustumUp, const ml::float3& frustumForward,
    const ml::float3& prevFrustumRight, const ml::float3& prevFrustumUp, const ml::float3& prevFrustumForward, float eps = ml::c_fEps
)
{
    return ml::Length(cameraDelta) < eps && ml::Length(frustumRight - prevFrustumRight) < eps && ml::Length(frustumUp - prevFrustumUp) < eps && ml::Length(frustumForward - prevFrustumForward) < eps;
}

void nrd::DenoiserImpl::AddSharedConstants_Relax(const MethodData& methodData, Constant*& data, Method method)
{
    NRD_DECLARE_DIMS;

    // Calculate camera right and up vectors in worldspace scaled according to frustum extents,
    // and unit forward vector, for fast worldspace position reconstruction in shaders
    float tanHalfFov = 1.0f / m_ViewToClip.a00;
    float aspect = m_ViewToClip.a00 / m_ViewToClip.a11;
    ml::float3 frustumRight = m_WorldToView.GetRow0().To3d() * tanHalfFov;
    ml::float3 frustumUp = m_WorldToView.GetRow1().To3d() * tanHalfFov * aspect;
    ml::float3 frustumForward = RELAX_GetFrustumForward(m_ViewToWorld, m_Frustum);

    float prevTanHalfFov = 1.0f / m_ViewToClipPrev.a00;
    float prevAspect = m_ViewToClipPrev.a00 / m_ViewToClipPrev.a11;
    ml::float3 prevFrustumRight = m_WorldToViewPrev.GetRow0().To3d() * prevTanHalfFov;
    ml::float3 prevFrustumUp = m_WorldToViewPrev.GetRow1().To3d() * prevTanHalfFov * prevAspect;
    ml::float3 prevFrustumForward = RELAX_GetFrustumForward(m_ViewToWorldPrev, m_FrustumPrev);

    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldPrevToWorld);
    AddFloat4x4(data, m_ViewToWorld);

    AddFloat4(data, ml::float4(frustumRight.x, frustumRight.y, frustumRight.z, 0));
    AddFloat4(data, ml::float4(frustumUp.x, frustumUp.y, frustumUp.z, 0));
    AddFloat4(data, ml::float4(frustumForward.x, frustumForward.y, frustumForward.z, 0));
    AddFloat4(data, ml::float4(prevFrustumRight.x, prevFrustumRight.y, prevFrustumRight.z, 0));
    AddFloat4(data, ml::float4(prevFrustumUp.x, prevFrustumUp.y, prevFrustumUp.z, 0));
    AddFloat4(data, ml::float4(prevFrustumForward.x, prevFrustumForward.y, prevFrustumForward.z, 0));
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, 0.0f));
    AddFloat4(data, ml::float4(m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1], m_CommonSettings.motionVectorScale[2], m_CommonSettings.debug));

    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);

    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));
    AddUint2(data, rectW, rectH);

    AddFloat2(data, 1.0f / screenW, 1.0f / screenH);
    AddFloat2(data, 1.0f / rectW, 1.0f / rectH);

    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    AddFloat(data, m_IsOrtho);

    AddFloat(data, 1.0f / (0.5f * rectH * m_ProjectY));
    AddUint(data, m_CommonSettings.frameIndex);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, ml::Clamp(16.66f / m_TimeDelta, 0.25f, 4.0f)); // Normalizing to 60 FPS

    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, m_JitterDelta);
    switch (method)
    {
    case Method::RELAX_DIFFUSE:
    case Method::RELAX_DIFFUSE_SH:
        AddUint(data, methodData.settings.diffuseRelax.enableMaterialTest ? 1 : 0);
        AddUint(data, 0);
        break;
    case Method::RELAX_SPECULAR:
    case Method::RELAX_SPECULAR_SH:
        AddUint(data, 0);
        AddUint(data, methodData.settings.specularRelax.enableMaterialTest ? 1 : 0);
        break;
    case Method::RELAX_DIFFUSE_SPECULAR:
    case Method::RELAX_DIFFUSE_SPECULAR_SH:
        AddUint(data, methodData.settings.diffuseSpecularRelax.enableMaterialTestForDiffuse ? 1 : 0);
        AddUint(data, methodData.settings.diffuseSpecularRelax.enableMaterialTestForSpecular ? 1 : 0);
        break;
    default:
        // Should never get here
        AddUint(data, 0);
        AddUint(data, 0);
        break;
    }

    // 1 if m_WorldPrevToWorld should be used in shader, otherwise we can skip multiplication
    AddUint(data, (m_WorldPrevToWorld != ml::float4x4::Identity()) ? 1 : 0);
    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);
    AddUint(data, 0);
    AddUint(data, 0);
}

#ifdef NRD_USE_PRECOMPILED_SHADERS

    // RELAX_DIFFUSE
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "RELAX_Diffuse_HitDistReconstruction.cs.dxbc.h"
        #include "RELAX_Diffuse_HitDistReconstruction.cs.dxil.h"
        #include "RELAX_Diffuse_HitDistReconstruction_5x5.cs.dxbc.h"
        #include "RELAX_Diffuse_HitDistReconstruction_5x5.cs.dxil.h"
        #include "RELAX_Diffuse_PrePass.cs.dxbc.h"
        #include "RELAX_Diffuse_PrePass.cs.dxil.h"
        #include "RELAX_Diffuse_TemporalAccumulation.cs.dxbc.h"
        #include "RELAX_Diffuse_TemporalAccumulation.cs.dxil.h"
        #include "RELAX_Diffuse_HistoryFix.cs.dxbc.h"
        #include "RELAX_Diffuse_HistoryFix.cs.dxil.h"
        #include "RELAX_Diffuse_HistoryClamping.cs.dxbc.h"
        #include "RELAX_Diffuse_HistoryClamping.cs.dxil.h"
        #include "RELAX_Diffuse_AntiFirefly.cs.dxbc.h"
        #include "RELAX_Diffuse_AntiFirefly.cs.dxil.h"
        #include "RELAX_Diffuse_AtrousSmem.cs.dxbc.h"
        #include "RELAX_Diffuse_AtrousSmem.cs.dxil.h"
        #include "RELAX_Diffuse_Atrous.cs.dxbc.h"
        #include "RELAX_Diffuse_Atrous.cs.dxil.h"
        #include "RELAX_Diffuse_SplitScreen.cs.dxbc.h"
        #include "RELAX_Diffuse_SplitScreen.cs.dxil.h"
        #include "RELAX_Validation.cs.dxbc.h"
        #include "RELAX_Validation.cs.dxil.h"
    #endif

    #include "RELAX_Diffuse_HitDistReconstruction.cs.spirv.h"
    #include "RELAX_Diffuse_HitDistReconstruction_5x5.cs.spirv.h"
    #include "RELAX_Diffuse_PrePass.cs.spirv.h"
    #include "RELAX_Diffuse_TemporalAccumulation.cs.spirv.h"
    #include "RELAX_Diffuse_HistoryFix.cs.spirv.h"
    #include "RELAX_Diffuse_HistoryClamping.cs.spirv.h"
    #include "RELAX_Diffuse_AntiFirefly.cs.spirv.h"
    #include "RELAX_Diffuse_AtrousSmem.cs.spirv.h"
    #include "RELAX_Diffuse_Atrous.cs.spirv.h"
    #include "RELAX_Diffuse_SplitScreen.cs.spirv.h"
    #include "RELAX_Validation.cs.spirv.h"

    // RELAX_DIFFUSE_SH
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "RELAX_DiffuseSh_PrePass.cs.dxbc.h"
        #include "RELAX_DiffuseSh_PrePass.cs.dxil.h"
        #include "RELAX_DiffuseSh_TemporalAccumulation.cs.dxbc.h"
        #include "RELAX_DiffuseSh_TemporalAccumulation.cs.dxil.h"
        #include "RELAX_DiffuseSh_HistoryFix.cs.dxbc.h"
        #include "RELAX_DiffuseSh_HistoryFix.cs.dxil.h"
        #include "RELAX_DiffuseSh_HistoryClamping.cs.dxbc.h"
        #include "RELAX_DiffuseSh_HistoryClamping.cs.dxil.h"
        #include "RELAX_DiffuseSh_AntiFirefly.cs.dxbc.h"
        #include "RELAX_DiffuseSh_AntiFirefly.cs.dxil.h"
        #include "RELAX_DiffuseSh_AtrousSmem.cs.dxbc.h"
        #include "RELAX_DiffuseSh_AtrousSmem.cs.dxil.h"
        #include "RELAX_DiffuseSh_Atrous.cs.dxbc.h"
        #include "RELAX_DiffuseSh_Atrous.cs.dxil.h"
        #include "RELAX_DiffuseSh_SplitScreen.cs.dxbc.h"
        #include "RELAX_DiffuseSh_SplitScreen.cs.dxil.h"
    #endif

    #include "RELAX_DiffuseSh_PrePass.cs.spirv.h"
    #include "RELAX_DiffuseSh_TemporalAccumulation.cs.spirv.h"
    #include "RELAX_DiffuseSh_HistoryFix.cs.spirv.h"
    #include "RELAX_DiffuseSh_HistoryClamping.cs.spirv.h"
    #include "RELAX_DiffuseSh_AntiFirefly.cs.spirv.h"
    #include "RELAX_DiffuseSh_AtrousSmem.cs.spirv.h"
    #include "RELAX_DiffuseSh_Atrous.cs.spirv.h"
    #include "RELAX_DiffuseSh_SplitScreen.cs.spirv.h"

    // RELAX_SPECULAR
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "RELAX_Specular_HitDistReconstruction.cs.dxbc.h"
        #include "RELAX_Specular_HitDistReconstruction.cs.dxil.h"
        #include "RELAX_Specular_HitDistReconstruction_5x5.cs.dxbc.h"
        #include "RELAX_Specular_HitDistReconstruction_5x5.cs.dxil.h"
        #include "RELAX_Specular_PrePass.cs.dxbc.h"
        #include "RELAX_Specular_PrePass.cs.dxil.h"
        #include "RELAX_Specular_TemporalAccumulation.cs.dxbc.h"
        #include "RELAX_Specular_TemporalAccumulation.cs.dxil.h"
        #include "RELAX_Specular_HistoryFix.cs.dxbc.h"
        #include "RELAX_Specular_HistoryFix.cs.dxil.h"
        #include "RELAX_Specular_HistoryClamping.cs.dxbc.h"
        #include "RELAX_Specular_HistoryClamping.cs.dxil.h"
        #include "RELAX_Specular_AntiFirefly.cs.dxbc.h"
        #include "RELAX_Specular_AntiFirefly.cs.dxil.h"
        #include "RELAX_Specular_AtrousSmem.cs.dxbc.h"
        #include "RELAX_Specular_AtrousSmem.cs.dxil.h"
        #include "RELAX_Specular_Atrous.cs.dxbc.h"
        #include "RELAX_Specular_Atrous.cs.dxil.h"
        #include "RELAX_Specular_SplitScreen.cs.dxbc.h"
        #include "RELAX_Specular_SplitScreen.cs.dxil.h"
    #endif

    #include "RELAX_Specular_HitDistReconstruction.cs.spirv.h"
    #include "RELAX_Specular_HitDistReconstruction_5x5.cs.spirv.h"
    #include "RELAX_Specular_PrePass.cs.spirv.h"
    #include "RELAX_Specular_TemporalAccumulation.cs.spirv.h"
    #include "RELAX_Specular_HistoryFix.cs.spirv.h"
    #include "RELAX_Specular_HistoryClamping.cs.spirv.h"
    #include "RELAX_Specular_AntiFirefly.cs.spirv.h"
    #include "RELAX_Specular_AtrousSmem.cs.spirv.h"
    #include "RELAX_Specular_Atrous.cs.spirv.h"
    #include "RELAX_Specular_SplitScreen.cs.spirv.h"

    // RELAX_SPECULAR_SH
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "RELAX_SpecularSh_PrePass.cs.dxbc.h"
        #include "RELAX_SpecularSh_PrePass.cs.dxil.h"
        #include "RELAX_SpecularSh_TemporalAccumulation.cs.dxbc.h"
        #include "RELAX_SpecularSh_TemporalAccumulation.cs.dxil.h"
        #include "RELAX_SpecularSh_HistoryFix.cs.dxbc.h"
        #include "RELAX_SpecularSh_HistoryFix.cs.dxil.h"
        #include "RELAX_SpecularSh_HistoryClamping.cs.dxbc.h"
        #include "RELAX_SpecularSh_HistoryClamping.cs.dxil.h"
        #include "RELAX_SpecularSh_AntiFirefly.cs.dxbc.h"
        #include "RELAX_SpecularSh_AntiFirefly.cs.dxil.h"
        #include "RELAX_SpecularSh_AtrousSmem.cs.dxbc.h"
        #include "RELAX_SpecularSh_AtrousSmem.cs.dxil.h"
        #include "RELAX_SpecularSh_Atrous.cs.dxbc.h"
        #include "RELAX_SpecularSh_Atrous.cs.dxil.h"
        #include "RELAX_SpecularSh_SplitScreen.cs.dxbc.h"
        #include "RELAX_SpecularSh_SplitScreen.cs.dxil.h"
    #endif

    #include "RELAX_SpecularSh_PrePass.cs.spirv.h"
    #include "RELAX_SpecularSh_TemporalAccumulation.cs.spirv.h"
    #include "RELAX_SpecularSh_HistoryFix.cs.spirv.h"
    #include "RELAX_SpecularSh_HistoryClamping.cs.spirv.h"
    #include "RELAX_SpecularSh_AntiFirefly.cs.spirv.h"
    #include "RELAX_SpecularSh_AtrousSmem.cs.spirv.h"
    #include "RELAX_SpecularSh_Atrous.cs.spirv.h"
    #include "RELAX_SpecularSh_SplitScreen.cs.spirv.h"

    // RELAX_DIFFUSE_SPECULAR
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "RELAX_DiffuseSpecular_HitDistReconstruction.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_HitDistReconstruction.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_HitDistReconstruction_5x5.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_HitDistReconstruction_5x5.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_PrePass.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_PrePass.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_TemporalAccumulation.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_TemporalAccumulation.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_HistoryFix.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_HistoryFix.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_HistoryClamping.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_HistoryClamping.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_AntiFirefly.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_AntiFirefly.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_AtrousSmem.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_AtrousSmem.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_Atrous.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_Atrous.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_SplitScreen.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_SplitScreen.cs.dxil.h"
    #endif

    #include "RELAX_DiffuseSpecular_HitDistReconstruction.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_HitDistReconstruction_5x5.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_PrePass.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_TemporalAccumulation.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_HistoryFix.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_HistoryClamping.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_AntiFirefly.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_AtrousSmem.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_Atrous.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_SplitScreen.cs.spirv.h"

    // RELAX_DIFFUSE_SPECULAR_SH
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "RELAX_DiffuseSpecularSh_PrePass.cs.dxbc.h"
        #include "RELAX_DiffuseSpecularSh_PrePass.cs.dxil.h"
        #include "RELAX_DiffuseSpecularSh_TemporalAccumulation.cs.dxbc.h"
        #include "RELAX_DiffuseSpecularSh_TemporalAccumulation.cs.dxil.h"
        #include "RELAX_DiffuseSpecularSh_HistoryFix.cs.dxbc.h"
        #include "RELAX_DiffuseSpecularSh_HistoryFix.cs.dxil.h"
        #include "RELAX_DiffuseSpecularSh_HistoryClamping.cs.dxbc.h"
        #include "RELAX_DiffuseSpecularSh_HistoryClamping.cs.dxil.h"
        #include "RELAX_DiffuseSpecularSh_AntiFirefly.cs.dxbc.h"
        #include "RELAX_DiffuseSpecularSh_AntiFirefly.cs.dxil.h"
        #include "RELAX_DiffuseSpecularSh_AtrousSmem.cs.dxbc.h"
        #include "RELAX_DiffuseSpecularSh_AtrousSmem.cs.dxil.h"
        #include "RELAX_DiffuseSpecularSh_Atrous.cs.dxbc.h"
        #include "RELAX_DiffuseSpecularSh_Atrous.cs.dxil.h"
        #include "RELAX_DiffuseSpecularSh_SplitScreen.cs.dxbc.h"
        #include "RELAX_DiffuseSpecularSh_SplitScreen.cs.dxil.h"
    #endif

    #include "RELAX_DiffuseSpecularSh_PrePass.cs.spirv.h"
    #include "RELAX_DiffuseSpecularSh_TemporalAccumulation.cs.spirv.h"
    #include "RELAX_DiffuseSpecularSh_HistoryFix.cs.spirv.h"
    #include "RELAX_DiffuseSpecularSh_HistoryClamping.cs.spirv.h"
    #include "RELAX_DiffuseSpecularSh_AntiFirefly.cs.spirv.h"
    #include "RELAX_DiffuseSpecularSh_AtrousSmem.cs.spirv.h"
    #include "RELAX_DiffuseSpecularSh_Atrous.cs.spirv.h"
    #include "RELAX_DiffuseSpecularSh_SplitScreen.cs.spirv.h"

#endif

#include "Methods/Relax_Diffuse.hpp"
#include "Methods/Relax_DiffuseSh.hpp"
#include "Methods/Relax_Specular.hpp"
#include "Methods/Relax_SpecularSh.hpp"
#include "Methods/Relax_DiffuseSpecular.hpp"
#include "Methods/Relax_DiffuseSpecularSh.hpp"
