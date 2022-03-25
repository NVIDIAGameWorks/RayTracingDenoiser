/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_SpecularReflectionMv()
{
    #define METHOD_NAME SpecularReflectionMv

    SetSharedConstants(0, 0, 0, 0);

    PushPass("Compute");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_REFLECTION_MV) );

        AddDispatch( SpecularReflectionMv_Compute, SumConstants(2, 2, 3, 3), 16, 1 );
    }

    #undef METHOD_NAME

    return sizeof(SpecularReflectionMvSettings);
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
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4(data, m_Frustum);
    AddFloat4(data, ml::float4(m_ViewDirection.x, m_ViewDirection.y, m_ViewDirection.z, 0.0f));
    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, unproject);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    ValidateConstants(data);
}
