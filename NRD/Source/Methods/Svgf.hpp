/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_Svgf(uint16_t w, uint16_t h)
{
    DispatchDesc desc = {};

    enum class Permanent
    {
        PREV_VIEWZ_NORMAL = PERMANENT_POOL_START,
        HISTORY_SIGNAL,
        HISTORY_MOMENTS_1,
        HISTORY_MOMENTS_2,
        HISTORY_LENGTH_1,
        HISTORY_LENGTH_2,
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );

    enum class Transient
    {
        SIGNAL_1 = TRANSIENT_POOL_START,
        SIGNAL_2,
        VARIANCE_2
    };

    // It's a nice trick to save memory. While one of HISTORY_MOMENTS is the history this frame, the opposite is free to be reused
    #define VARIANCE_1 AsUint(Permanent::HISTORY_MOMENTS_2), 0, 1, AsUint(Permanent::HISTORY_MOMENTS_1)

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );

    PushPass("SVGF - Reproject");
    {
        PushInput( AsUint(ResourceType::IN_SVGF) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL) );
        PushInput( AsUint(Permanent::HISTORY_SIGNAL) );
        PushInput( AsUint(Permanent::HISTORY_MOMENTS_2), 0, 1, AsUint(Permanent::HISTORY_MOMENTS_1) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_2), 0, 1, AsUint(Permanent::HISTORY_LENGTH_1) );

        PushOutput( AsUint(Transient::SIGNAL_2) );
        PushOutput( AsUint(Permanent::HISTORY_MOMENTS_1), 0, 1, AsUint(Permanent::HISTORY_MOMENTS_2) );
        PushOutput( AsUint(Permanent::HISTORY_LENGTH_1), 0, 1, AsUint(Permanent::HISTORY_LENGTH_2) );

        desc.constantBufferDataSize = SumConstants(3, 1, 3, 9, false);

        AddDispatch(desc, SVGF_Reproject, w, h);
    }

    PushPass("SVGF - Filter moments");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(Transient::SIGNAL_2) );
        PushInput( AsUint(Permanent::HISTORY_MOMENTS_1), 0, 1, AsUint(Permanent::HISTORY_MOMENTS_2) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_1), 0, 1, AsUint(Permanent::HISTORY_LENGTH_2) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL) );
        PushOutput( AsUint(Transient::SIGNAL_1) );
        PushOutput( VARIANCE_1 );

        desc.constantBufferDataSize = SumConstants(0, 0, 1, 4, false);

        AddDispatch(desc, SVGF_FilterMoments, w, h);
    }

    PushPass("SVGF - A-trous 1");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_1), 0, 1, AsUint(Permanent::HISTORY_LENGTH_2) );
        PushInput( AsUint(Transient::SIGNAL_1) );
        PushInput( VARIANCE_1 );

        PushOutput( AsUint(Transient::SIGNAL_2) );
        PushOutput( AsUint(Transient::VARIANCE_2) );

        desc.constantBufferDataSize = SumConstants(0, 0, 2, 6, false);

        AddDispatch(desc, SVGF_Atrous, w, h);
    }

    PushPass("SVGF - A-trous 2");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_1), 0, 1, AsUint(Permanent::HISTORY_LENGTH_2) );
        PushInput( AsUint(Transient::SIGNAL_2) );
        PushInput( AsUint(Transient::VARIANCE_2) );

        PushOutput( AsUint(Permanent::HISTORY_SIGNAL) );
        PushOutput( VARIANCE_1 );

        desc.constantBufferDataSize = SumConstants(0, 0, 2, 6, false);

        AddDispatch(desc, SVGF_Atrous, w, h);
    }

    PushPass("SVGF - A-trous 3");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_1), 0, 1, AsUint(Permanent::HISTORY_LENGTH_2) );
        PushInput( AsUint(Permanent::HISTORY_SIGNAL) );
        PushInput( VARIANCE_1 );

        PushOutput( AsUint(Transient::SIGNAL_2) );
        PushOutput( AsUint(Transient::VARIANCE_2) );

        desc.constantBufferDataSize = SumConstants(0, 0, 2, 6, false);

        AddDispatch(desc, SVGF_Atrous, w, h);
    }

    PushPass("SVGF - A-trous 4");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_1), 0, 1, AsUint(Permanent::HISTORY_LENGTH_2) );
        PushInput( AsUint(Transient::SIGNAL_2) );
        PushInput( AsUint(Transient::VARIANCE_2) );

        PushOutput( AsUint(ResourceType::OUT_SVGF) );
        PushOutput( VARIANCE_1 );

        desc.constantBufferDataSize = SumConstants(0, 0, 2, 6, false);

        AddDispatch(desc, SVGF_Atrous, w, h);
    }

    #undef VARIANCE_1

    return sizeof(SvgfSettings);
}

void DenoiserImpl::UpdateMethod_Svgf(const MethodData& methodData)
{
    enum class Dispatch
    {
        REPROJECT,
        FILTER_MOMENTS,
        ATROUS_1,
        ATROUS_2,
        ATROUS_3,
        ATROUS_4,
    };

    const SvgfSettings& settings = methodData.settings.svgf;

    float w = float(methodData.desc.fullResolutionWidth);
    float h = float(methodData.desc.fullResolutionHeight);
    float maxAccumulatedFrameNum = float( Min( settings.maxAccumulatedFrameNum, SVGF_MAX_HISTORY_FRAME_NUM ) );
    float momentsMaxAccumulatedFrameNum = float( Min( settings.momentsMaxAccumulatedFrameNum, SVGF_MAX_HISTORY_FRAME_NUM ) );

    if( m_CommonSettings.frameIndex == 0 )
        momentsMaxAccumulatedFrameNum = 0;

    // REPROJECT
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::REPROJECT));
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, m_Frustum);
    AddFloat2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, settings.disocclusionThreshold);
    AddFloat(data, m_JitterDelta );
    AddFloat(data, settings.isDiffuse ? 1.0f : 0.0f);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, maxAccumulatedFrameNum);
    AddFloat(data, momentsMaxAccumulatedFrameNum);
    AddUint(data, m_CommonSettings.worldSpaceMotion ? 1 : 0);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // FILTER_MOMENTS
    data = PushDispatch(methodData, AsUint(Dispatch::FILTER_MOMENTS));
    AddFloat2(data, w, h);
    AddFloat(data, settings.zDeltaScale);
    AddFloat(data, settings.isDiffuse ? 1.0f : 0.0f);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // ATROUS_1
    data = PushDispatch(methodData, AsUint(Dispatch::ATROUS_1));
    AddFloat2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.zDeltaScale);
    AddFloat(data, settings.varianceScale);
    AddFloat(data, settings.isDiffuse ? 1.0f : 0.0f);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddUint(data, 0);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // ATROUS_2
    data = PushDispatch(methodData, AsUint(Dispatch::ATROUS_2));
    AddFloat2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.zDeltaScale);
    AddFloat(data, settings.varianceScale);
    AddFloat(data, settings.isDiffuse ? 1.0f : 0.0f);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddUint(data, 1);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // ATROUS_3
    data = PushDispatch(methodData, AsUint(Dispatch::ATROUS_3));
    AddFloat2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.zDeltaScale);
    AddFloat(data, settings.varianceScale);
    AddFloat(data, settings.isDiffuse ? 1.0f : 0.0f);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddUint(data, 2);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // ATROUS_4
    data = PushDispatch(methodData, AsUint(Dispatch::ATROUS_4));
    AddFloat2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.zDeltaScale);
    AddFloat(data, settings.varianceScale);
    AddFloat(data, settings.isDiffuse ? 1.0f : 0.0f);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddUint(data, 3);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);
}
