/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "../Shaders/Resources/REFERENCE_TemporalAccumulation.resources.hlsli"
#include "../Shaders/Resources/REFERENCE_SplitScreen.resources.hlsli"

void nrd::InstanceImpl::Add_Reference(DenoiserData& denoiserData)
{
    #define DENOISER_NAME Reference

    denoiserData.settings.reference = ReferenceSettings();
    denoiserData.settingsSize = sizeof(denoiserData.settings.reference);

    enum class Permanent
    {
        HISTORY = PERMANENT_POOL_START,
    };

    AddTextureToPermanentPool( {Format::RGBA32_SFLOAT, 1} );

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_SIGNAL) );

        PushOutput( AsUint(Permanent::HISTORY) );

        AddDispatch( REFERENCE_TemporalAccumulation, REFERENCE_TemporalAccumulation, 1 );
    }

    PushPass("Copy");
    {
        PushInput( AsUint(Permanent::HISTORY) );

        PushOutput( AsUint(ResourceType::OUT_SIGNAL) );

        AddDispatch( REFERENCE_SplitScreen, REFERENCE_SplitScreen, 1 ); // TODO: rename to Copy
    }

    #undef DENOISER_NAME
}

void nrd::InstanceImpl::Update_Reference(const DenoiserData& denoiserData)
{
    enum class Dispatch
    {
        ACCUMULATE,
        COPY,
    };

    const ReferenceSettings& settings = denoiserData.settings.reference;

    if (m_WorldToClip != m_WorldToClipPrev || m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE)
        m_AccumulatedFrameNum = 0;
    else
        m_AccumulatedFrameNum = min(m_AccumulatedFrameNum + 1, settings.maxAccumulatedFrameNum);

    NRD_DECLARE_DIMS;

    { // ACCUMULATE
        REFERENCE_TemporalAccumulationConstants* consts = (REFERENCE_TemporalAccumulationConstants*)PushDispatch(denoiserData, AsUint(Dispatch::ACCUMULATE));
        consts->gRectOrigin     = uint2(m_CommonSettings.rectOrigin[0], m_CommonSettings.rectOrigin[1]);
        consts->gRectSizeInv    = float2(1.0f / float(rectW), 1.0f / float(rectH));
        consts->gSplitScreen    = m_CommonSettings.splitScreen;
        consts->gAccumSpeed     = 1.0f / (1.0f + float(m_AccumulatedFrameNum));
        consts->gDebug          = m_CommonSettings.debug;
    }

    { // COPY
        PushDispatch(denoiserData, AsUint(Dispatch::COPY));
    }
}
