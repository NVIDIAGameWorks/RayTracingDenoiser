/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "../Shaders/Resources/SpecularReflectionMv_Compute.resources.hlsli"

void nrd::InstanceImpl::Add_SpecularReflectionMv(DenoiserData& denoiserData)
{
    #define DENOISER_NAME SpecularReflectionMv

    denoiserData.settings.specularReflectionMv = SpecularReflectionMvSettings();
    denoiserData.settingsSize = sizeof(denoiserData.settings.specularReflectionMv);

    PushPass("Compute");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_REFLECTION_MV) );

        AddDispatch( SpecularReflectionMv_Compute, SpecularReflectionMv_Compute, 1 );
    }

    #undef DENOISER_NAME
}

void nrd::InstanceImpl::Update_SpecularReflectionMv(const DenoiserData& denoiserData)
{
    enum class Dispatch
    {
        COMPUTE,
    };

    NRD_DECLARE_DIMS;

    float unproject = 1.0f / (0.5f * rectH * m_ProjectY);

    { // COMPUTE
    SpecularReflectionMv_ComputeConstants* consts = (SpecularReflectionMv_ComputeConstants*)PushDispatch(denoiserData, AsUint(Dispatch::COMPUTE));
    consts->gViewToWorld            = m_ViewToWorld;
    consts->gWorldToClip            = m_WorldToClip;
    consts->gWorldToClipPrev        = m_WorldToClipPrev;
    consts->gWorldToViewPrev        = m_WorldToViewPrev;
    consts->gFrustumPrev            = m_FrustumPrev;
    consts->gFrustum                = m_Frustum;
    consts->gViewVectorWorld        = float4(m_ViewDirection.x, m_ViewDirection.y, m_ViewDirection.z, 0.0f);
    consts->gCameraDelta            = float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, 0.0f);
    consts->gMvScale                = float4(m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1], m_CommonSettings.motionVectorScale[2], m_CommonSettings.isMotionVectorInWorldSpace ? 1.0f : 0.0f);
    consts->gRectSize               = float2(float(rectW), float(rectH));
    consts->gRectSizeInv            = float2(1.0f / float(rectW), 1.0f / float(rectH));
    consts->gRectOffset             = float2(float(m_CommonSettings.rectOrigin[0]) / float(resourceW), float(m_CommonSettings.rectOrigin[1]) / float(resourceH));
    consts->gResolutionScale        = float2(float(rectW) / float(resourceW), float(rectH) / float(resourceH));
    consts->gRectOrigin             = uint2(m_CommonSettings.rectOrigin[0], m_CommonSettings.rectOrigin[1]);
    consts->gDenoisingRange         = m_CommonSettings.denoisingRange;
    consts->gOrthoMode              = m_OrthoMode;
    consts->gUnproject              = unproject;
    consts->gDebug                  = m_CommonSettings.debug;
    }
}
