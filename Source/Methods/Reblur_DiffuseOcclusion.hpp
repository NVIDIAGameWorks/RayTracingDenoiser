/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_ReblurDiffuseOcclusion(uint16_t w, uint16_t h)
{
    #define METHOD_NAME REBLUR_DiffuseOcclusion

    enum class Permanent
    {
        PREV_VIEWZ_DIFFACCUMSPEED = PERMANENT_POOL_START,
        PREV_NORMAL_SPECACCUMSPEED,
    };

    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );

    enum class Transient
    {
        INTERNAL_DATA = TRANSIENT_POOL_START,
        ESTIMATED_ERROR,
        DIFF_ACCUMULATED,
        DIFF_TEMP,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, REBLUR_MIP_NUM} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );

    REBLUR_DECLARE_SHARED_CONSTANT_NUM;

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseOcclusion_TemporalAccumulation, SumConstants(4, 2, 1, 4), 16, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_TemporalAccumulation, SumConstants(4, 2, 1, 4), 16, 1 );
    }

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseOcclusion_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 4), 16, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 4), 16, 1 );
    }

    PushPass("Mip generation");
    {
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

        for( uint16_t i = 1; i < REBLUR_MIP_NUM; i++ )
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED), i, 1 );

        AddDispatch( REBLUR_DiffuseOcclusion_MipGen, SumConstants(0, 0, 0, 0), 8, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_MipGen, SumConstants(0, 0, 0, 0), 8, 1 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 1, REBLUR_MIP_NUM - 1 );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 0, 1 );

        AddDispatch( REBLUR_DiffuseOcclusion_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_TEMP) );

        AddDispatch( REBLUR_DiffuseOcclusion_Blur, SumConstants(1, 2, 0, 0), 16, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_Blur, SumConstants(1, 2, 0, 0), 16, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_TEMP) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushOutput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );

        AddDispatch( REBLUR_DiffuseOcclusion_PostBlur, SumConstants(1, 2, 0, 0), 16, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_PostBlur, SumConstants(1, 2, 0, 0), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );

        AddDispatch( REBLUR_DiffuseOcclusion_SplitScreen, SumConstants(0, 0, 0, 3), 16, 1 );
    }

    #undef METHOD_NAME

    return sizeof(ReblurSettings);
}

void nrd::DenoiserImpl::UpdateMethod_ReblurOcclusion(const MethodData& methodData)
{
    enum class Dispatch
    {
        TEMPORAL_ACCUMULATION,
        PERF_TEMPORAL_ACCUMULATION,
        TEMPORAL_ACCUMULATION_WITH_CONFIDENCE,
        PERF_TEMPORAL_ACCUMULATION_WITH_CONFIDENCE,
        MIP_GENERATION,
        PERF_MIP_GENERATION,
        HISTORY_FIX,
        PERF_HISTORY_FIX,
        BLUR,
        PERF_BLUR,
        POST_BLUR,
        PERF_POST_BLUR,
        SPLIT_SCREEN,
    };

    const ReblurSettings& settings = methodData.settings.reblur;

    uint32_t specCheckerboard = 2;
    uint32_t diffCheckerboard = 2;

    switch (settings.checkerboardMode)
    {
        case nrd::CheckerboardMode::BLACK:
            diffCheckerboard = 0;
            specCheckerboard = 1;
            break;
        case nrd::CheckerboardMode::WHITE:
            diffCheckerboard = 1;
            specCheckerboard = 0;
            break;
        default:
            break;
    }

    NRD_DECLARE_DIMS;

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Reblur(methodData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);

        return;
    }

    // TEMPORAL_ACCUMULATION
    uint32_t passIndex = AsUint(Dispatch::TEMPORAL_ACCUMULATION) + (m_CommonSettings.isHistoryConfidenceInputsAvailable ? 1 : 0) * 2 + (settings.enablePerformanceMode ? 1 : 0);
    Constant* data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, m_CameraDelta);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, m_CommonSettings.disocclusionThreshold );
    AddUint(data, diffCheckerboard);
    AddUint(data, specCheckerboard);
    ValidateConstants(data);

    // MIP_GENERATION
    data = PushDispatch(methodData, AsUint( settings.enablePerformanceMode ? Dispatch::PERF_MIP_GENERATION : Dispatch::MIP_GENERATION ));
    AddSharedConstants_Reblur(methodData, settings, data);
    ValidateConstants(data);

    // HISTORY_FIX
    data = PushDispatch(methodData, AsUint( settings.enablePerformanceMode ? Dispatch::PERF_HISTORY_FIX : Dispatch::HISTORY_FIX ));
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat(data, settings.historyFixStrength);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(methodData, AsUint( settings.enablePerformanceMode ? Dispatch::PERF_BLUR : Dispatch::BLUR ));
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[1]);
    AddFloat4(data, ml::float4(settings.specularLobeTrimmingParameters.A, settings.specularLobeTrimmingParameters.B, settings.specularLobeTrimmingParameters.C, settings.maxAdaptiveRadiusScale));
    ValidateConstants(data);

    // POST_BLUR
    data = PushDispatch(methodData, AsUint( settings.enablePerformanceMode ? Dispatch::PERF_POST_BLUR : Dispatch::POST_BLUR ));
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[2]);
    AddFloat4(data, ml::float4(settings.specularLobeTrimmingParameters.A, settings.specularLobeTrimmingParameters.B, settings.specularLobeTrimmingParameters.C, settings.maxAdaptiveRadiusScale));
    ValidateConstants(data);

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Reblur(methodData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }
}
