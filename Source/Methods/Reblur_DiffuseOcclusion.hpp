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
        DIFF_DATA,
        DIFF_ACCUMULATED,
        DIFF_TEMP,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, REBLUR_MIP_NUM} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );

    REBLUR_SET_SHARED_CONSTANTS;

    PushPass("Hit distance reconstruction");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );

        PushOutput( AsUint(Transient::DIFF_TEMP) );

        AddDispatch( REBLUR_DiffuseOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
    }

    for (int i = 0; i < 4; i++)
    {
        bool hasConfidenceInputs    = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isAfterPrepass         = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Temporal accumulation");
        {
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
            PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
            PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );

            if (isAfterPrepass)
                PushInput( AsUint(Transient::DIFF_TEMP) );
            else
                PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );

            if (hasConfidenceInputs)
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

            PushOutput( AsUint(Transient::INTERNAL_DATA) );
            PushOutput( AsUint(Transient::DIFF_DATA) );
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

            if (hasConfidenceInputs)
            {
                AddDispatch( REBLUR_DiffuseOcclusion_TemporalAccumulationWithConfidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_DiffuseOcclusion_TemporalAccumulationWithConfidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_DiffuseOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_DiffuseOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
        }
    }

    PushPass("Mip gen");
    {
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

        for( int16_t i = REBLUR_MIP_NUM - 1; i >= 1; i-- )
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED), i, 1 );

        AddDispatch( REBLUR_DiffuseOcclusion_MipGen, REBLUR_MIPGEN_CONSTANT_NUM, REBLUR_MIPGEN_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_MipGen, REBLUR_MIPGEN_CONSTANT_NUM, REBLUR_MIPGEN_GROUP_DIM, 1 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 1, REBLUR_MIP_NUM - 1 );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 0, 1 );

        AddDispatch( REBLUR_DiffuseOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

        PushOutput( AsUint(Transient::DIFF_TEMP) );

        AddDispatch( REBLUR_DiffuseOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_DATA) );
        PushInput( AsUint(Transient::DIFF_TEMP) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushOutput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushOutput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );

        AddDispatch( REBLUR_DiffuseOcclusion_PostBlur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_DiffuseOcclusion_PostBlur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );

        AddDispatch( REBLUR_DiffuseOcclusion_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_GROUP_DIM, 1 );
    }

    #undef METHOD_NAME

    return sizeof(ReblurSettings);
}

void nrd::DenoiserImpl::UpdateMethod_ReblurOcclusion(const MethodData& methodData)
{
    enum class Dispatch
    {
        HITDIST_RECONSTRUCTION,
        TEMPORAL_ACCUMULATION   = HITDIST_RECONSTRUCTION + 1 * 2,
        MIP_GEN                 = TEMPORAL_ACCUMULATION + 4 * 2,
        HISTORY_FIX             = MIP_GEN + 1 * 2,
        BLUR                    = HISTORY_FIX + 1 * 2,
        POST_BLUR               = BLUR + 1 * 2,
        SPLIT_SCREEN            = POST_BLUR + 1 * 2,
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

    float disocclusionThreshold = m_CommonSettings.disocclusionThreshold + (1.0f + m_JitterDelta) / float(rectH);

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

    // HITDIST_RECONSTRUCTION
    if (settings.enableHitDistanceReconstruction)
    {
        uint32_t passIndex = AsUint(Dispatch::HITDIST_RECONSTRUCTION) + (settings.enablePerformanceMode ? 1 : 0);
        Constant* data = PushDispatch(methodData, passIndex);
        AddSharedConstants_Reblur(methodData, settings, data);
        ValidateConstants(data);
    }

    // TEMPORAL_ACCUMULATION
    uint32_t passIndex = AsUint(Dispatch::TEMPORAL_ACCUMULATION) + (m_CommonSettings.isHistoryConfidenceInputsAvailable ? 4 : 0) + (settings.enableHitDistanceReconstruction ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
    Constant* data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldPrevToWorld);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, 0.0f));
    AddFloat4(data, m_Rotator[0]);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, disocclusionThreshold );
    AddUint(data, diffCheckerboard);
    AddUint(data, specCheckerboard);
    AddUint(data, 0);
    ValidateConstants(data);

    // MIP_GEN
    passIndex = AsUint(Dispatch::MIP_GEN) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    ValidateConstants(data);

    // HISTORY_FIX
    passIndex = AsUint(Dispatch::HISTORY_FIX) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat(data, settings.historyFixStrength);
    ValidateConstants(data);

    // BLUR
    passIndex = AsUint(Dispatch::BLUR) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4(data, m_Rotator[1]);
    AddFloat4(data, ml::float4(settings.specularLobeTrimmingParameters.A, settings.specularLobeTrimmingParameters.B, settings.specularLobeTrimmingParameters.C, settings.maxAdaptiveRadiusScale));
    ValidateConstants(data);

    // POST_BLUR
    passIndex = AsUint(Dispatch::POST_BLUR) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
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
