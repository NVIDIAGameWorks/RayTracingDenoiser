/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_Reference(uint16_t w, uint16_t h)
{
    #define DENOISER_NAME "REFERENCE"

    enum class Permanent
    {
        HISTORY = PERMANENT_POOL_START,
    };

    m_PermanentPool.push_back( {Format::RGBA32_SFLOAT, w, h, 1} );

    SetSharedConstants(0, 0, 0, 0);

    PushPass("Accumulate");
    {
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

        PushOutput( AsUint(Permanent::HISTORY) );

        AddDispatch( REFERENCE_Accumulate, SumConstants(0, 0, 2, 2), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(Permanent::HISTORY) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        AddDispatch( REFERENCE_SplitScreen, SumConstants(0, 0, 0, 0), 16, 1 );
    }

    #undef DENOISER_NAME

    return sizeof(ReferenceSettings);
}

void nrd::DenoiserImpl::UpdateMethod_Reference(const MethodData& methodData)
{
    enum class Dispatch
    {
        ACCUMULATE,
        COPY,
    };

    const ReferenceSettings& settings = methodData.settings.reference;

    if (m_WorldToClip != m_WorldToClipPrev || m_CommonSettings.accumulationMode != nrd::AccumulationMode::CONTINUE)
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
    ValidateConstants(data);

    // COPY
    data = PushDispatch(methodData, AsUint(Dispatch::COPY));
    ValidateConstants(data);
}
