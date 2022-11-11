/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::DenoiserImpl::AddMethod_Reference(MethodData& methodData)
{
    #define METHOD_NAME Reference

    methodData.settings.reference = ReferenceSettings();
    methodData.settingsSize = sizeof(methodData.settings.reference);
            
    uint16_t w = methodData.desc.fullResolutionWidth;
    uint16_t h = methodData.desc.fullResolutionHeight;

    enum class Permanent
    {
        HISTORY = PERMANENT_POOL_START,
    };

    m_PermanentPool.push_back( {Format::RGBA32_SFLOAT, w, h, 1} );

    SetSharedConstants(0, 0, 0, 0);

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_RADIANCE) );

        PushOutput( AsUint(Permanent::HISTORY) );

        AddDispatch( REFERENCE_TemporalAccumulation, SumConstants(0, 0, 2, 3), NumThreads(16, 16), 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(Permanent::HISTORY) );

        PushOutput( AsUint(ResourceType::OUT_RADIANCE) );

        AddDispatch( REFERENCE_SplitScreen, SumConstants(0, 0, 0, 0), NumThreads(16, 16), 1 );
    }

    #undef METHOD_NAME
}

void nrd::DenoiserImpl::UpdateMethod_Reference(const MethodData& methodData)
{
    enum class Dispatch
    {
        ACCUMULATE,
        COPY,
    };

    const ReferenceSettings& settings = methodData.settings.reference;

    if (m_WorldToClip != m_WorldToClipPrev || m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE)
        m_AccumulatedFrameNum = 0;
    else
        m_AccumulatedFrameNum = ml::Min(m_AccumulatedFrameNum + 1, settings.maxAccumulatedFrameNum);

    NRD_DECLARE_DIMS;

    // ACCUMULATE
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::ACCUMULATE));
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat(data, m_CommonSettings.splitScreen);
    AddFloat(data, 1.0f / (1.0f + float(m_AccumulatedFrameNum)));
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // COPY
    data = PushDispatch(methodData, AsUint(Dispatch::COPY));
    ValidateConstants(data);
}
