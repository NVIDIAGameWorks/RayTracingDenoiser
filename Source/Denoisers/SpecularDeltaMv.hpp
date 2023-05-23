/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::InstanceImpl::Add_SpecularDeltaMv(DenoiserData& denoiserData)
{
    #define DENOISER_NAME SpecularDeltaMv

    denoiserData.settings.specularDeltaMv = SpecularDeltaMvSettings();
    denoiserData.settingsSize = sizeof(denoiserData.settings.specularDeltaMv);
            
    uint16_t w = denoiserData.desc.renderWidth;
    uint16_t h = denoiserData.desc.renderHeight;

    enum class Permanent
    {
        DELTA_SECONDARY_POS_CURR = PERMANENT_POOL_START,
        DELTA_SECONDARY_POS_PREV
    };

    AddTextureToPermanentPool( {Format::RGBA32_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA32_SFLOAT, w, h, 1} );

    SetSharedConstants(0, 0, 0, 0);

    PushPass("Compute");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(ResourceType::IN_DELTA_PRIMARY_POS) );
        PushInput( AsUint(ResourceType::IN_DELTA_SECONDARY_POS) );
        PushInput( AsUint(Permanent::DELTA_SECONDARY_POS_PREV), 0, 1, AsUint(Permanent::DELTA_SECONDARY_POS_CURR) );

        PushOutput( AsUint(ResourceType::OUT_DELTA_MV) );
        PushOutput( AsUint(Permanent::DELTA_SECONDARY_POS_CURR), 0, 1, AsUint(Permanent::DELTA_SECONDARY_POS_PREV) );

        AddDispatch( SpecularDeltaMv_Compute, SumConstants(1, 1, 3, 1), NumThreads(16, 16), 1 );
    }

    #undef DENOISER_NAME
}

void nrd::InstanceImpl::Update_SpecularDeltaMv(const DenoiserData& denoiserData)
{
    enum class Dispatch
    {
        COMPUTE,
    };

    NRD_DECLARE_DIMS;

    // COMPUTE
    Constant* data = PushDispatch(denoiserData, AsUint(Dispatch::COMPUTE));
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4(data, ml::float4(m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1], m_CommonSettings.motionVectorScale[2], m_CommonSettings.debug));
    AddUint2(data, rectW, rectH);
    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    ValidateConstants(data);
}
