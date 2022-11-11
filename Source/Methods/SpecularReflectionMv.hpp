/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::DenoiserImpl::AddMethod_SpecularReflectionMv(MethodData& methodData)
{
    #define METHOD_NAME SpecularReflectionMv

    methodData.settings.specularReflectionMv = SpecularReflectionMvSettings();
    methodData.settingsSize = sizeof(methodData.settings.specularReflectionMv);
            
    SetSharedConstants(0, 0, 0, 0);

    PushPass("Compute");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_REFLECTION_MV) );

        AddDispatch( SpecularReflectionMv_Compute, SumConstants(4, 5, 4, 2), NumThreads(16, 16), 1 );
    }

    #undef METHOD_NAME
}

void nrd::DenoiserImpl::UpdateMethod_SpecularReflectionMv(const MethodData& methodData)
{
    enum class Dispatch
    {
        COMPUTE,
    };

    NRD_DECLARE_DIMS;

    // DRS will increase reprojected values, needed for stability, compensated by blur radius adjustment
    float unproject = 1.0f / (0.5f * rectH * m_ProjectY);

    // COMPUTE
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::COMPUTE));
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, m_Frustum);
    AddFloat4(data, ml::float4(m_ViewDirection.x, m_ViewDirection.y, m_ViewDirection.z, m_IsOrtho));
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, unproject));
    AddFloat4(data, ml::float4(m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1], m_CommonSettings.motionVectorScale[2], m_CommonSettings.debug));
    AddFloat2(data, float(rectW), float(rectH));
    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    ValidateConstants(data);
}
