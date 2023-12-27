/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "../Shaders/Resources/SpecularDeltaMv_Compute.resources.hlsli"

void nrd::InstanceImpl::Add_SpecularDeltaMv(DenoiserData& denoiserData)
{
    #define DENOISER_NAME SpecularDeltaMv

    denoiserData.settings.specularDeltaMv = SpecularDeltaMvSettings();
    denoiserData.settingsSize = sizeof(denoiserData.settings.specularDeltaMv);

    enum class Permanent
    {
        DELTA_SECONDARY_POS_CURR = PERMANENT_POOL_START,
        DELTA_SECONDARY_POS_PREV
    };

    AddTextureToPermanentPool( {Format::RGBA32_SFLOAT, 1} );
    AddTextureToPermanentPool( {Format::RGBA32_SFLOAT, 1} );

    PushPass("Compute");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(ResourceType::IN_DELTA_PRIMARY_POS) );
        PushInput( AsUint(ResourceType::IN_DELTA_SECONDARY_POS) );
        PushInput( AsUint(Permanent::DELTA_SECONDARY_POS_PREV), AsUint(Permanent::DELTA_SECONDARY_POS_CURR) );

        PushOutput( AsUint(ResourceType::OUT_DELTA_MV) );
        PushOutput( AsUint(Permanent::DELTA_SECONDARY_POS_CURR), AsUint(Permanent::DELTA_SECONDARY_POS_PREV) );

        AddDispatch( SpecularDeltaMv_Compute, SpecularDeltaMv_Compute, 1 );
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

    { // COMPUTE
        SpecularDeltaMv_ComputeConstants* consts = (SpecularDeltaMv_ComputeConstants*)PushDispatch(denoiserData, AsUint(Dispatch::COMPUTE));
        consts->gWorldToClipPrev    = m_WorldToClipPrev;
        consts->gMvScale            = float4(m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1], m_CommonSettings.motionVectorScale[2], m_CommonSettings.isMotionVectorInWorldSpace ? 1.0f : 0.0f);
        consts->gRectOrigin         = uint2(m_CommonSettings.rectOrigin[0], m_CommonSettings.rectOrigin[1]);
        consts->gRectSize           = uint2(rectW, rectH);
        consts->gRectSizeInv        = float2(1.0f / float(rectW), 1.0f / float(rectH));
        consts->gDebug              = m_CommonSettings.debug;
    }
}
