/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::DenoiserImpl::AddMethod_ReblurDiffuseOcclusion(MethodData& methodData)
{
    #define METHOD_NAME REBLUR_DiffuseOcclusion
    #define DIFF_TEMP1 AsUint(Transient::DIFF_TMP1)
    #define DIFF_TEMP2 AsUint(Transient::DIFF_TMP2)

    methodData.settings.reblur = ReblurSettings();
    methodData.settingsSize = sizeof(methodData.settings.reblur);

    uint16_t w = methodData.desc.fullResolutionWidth;
    uint16_t h = methodData.desc.fullResolutionHeight;

    enum class Permanent
    {
        PREV_VIEWZ = PERMANENT_POOL_START,
        PREV_NORMAL_ROUGHNESS,
        PREV_INTERNAL_DATA,
    };

    m_PermanentPool.push_back( {REBLUR_FORMAT_PREV_VIEWZ, w, h, 1} );
    m_PermanentPool.push_back( {REBLUR_FORMAT_PREV_NORMAL_ROUGHNESS, w, h, 1} );
    m_PermanentPool.push_back( {REBLUR_FORMAT_PREV_INTERNAL_DATA, w, h, 1} );

    enum class Transient
    {
        DATA1 = TRANSIENT_POOL_START,
        DIFF_TMP1,
        DIFF_TMP2,
    };

    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {REBLUR_FORMAT_OCCLUSION, w, h, 1} );
    m_TransientPool.push_back( {REBLUR_FORMAT_OCCLUSION, w, h, 1} );

    REBLUR_SET_SHARED_CONSTANTS;

    for (int i = 0; i < REBLUR_OCCLUSION_HITDIST_RECONSTRUCTION_PERMUTATION_NUM; i++)
    {
        bool is5x5 = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Hit distance reconstruction");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );

            // Outputs
            PushOutput( DIFF_TEMP1 );

            // Shaders
            if (is5x5)
            {
                AddDispatch( REBLUR_DiffuseOcclusion_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
            }
            else
            {
                AddDispatch( REBLUR_DiffuseOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_TEMPORAL_ACCUMULATION_PERMUTATION_NUM; i++)
    {
        bool hasDisocclusionThresholdMix = ( ( ( i >> 2 ) & 0x1 ) != 0 );
        bool hasConfidenceInputs = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isAfterReconstruction = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Temporal accumulation");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );
            PushInput( AsUint(Permanent::PREV_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Permanent::PREV_INTERNAL_DATA) );
            PushInput( hasDisocclusionThresholdMix ? AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) : REBLUR_DUMMY );
            PushInput( hasConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : REBLUR_DUMMY );
            PushInput( isAfterReconstruction ? DIFF_TEMP1 : AsUint(ResourceType::IN_DIFF_HITDIST) );
            PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );

            // Outputs
            PushOutput( DIFF_TEMP2 );
            PushOutput( AsUint(Transient::DATA1) );

            // Shaders
            AddDispatch( REBLUR_DiffuseOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_HISTORY_FIX_PERMUTATION_NUM; i++)
    {
        PushPass("History fix");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( DIFF_TEMP2 );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );

            // Outputs
            PushOutput( DIFF_TEMP1 );

            // Shaders
            AddDispatch( REBLUR_DiffuseOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_BLUR_PERMUTATION_NUM; i++)
    {
        PushPass("Blur");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( DIFF_TEMP1 );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );

            // Outputs
            PushOutput( DIFF_TEMP2 );
            PushOutput( AsUint(Permanent::PREV_VIEWZ) );

            // Shaders
            AddDispatch( REBLUR_DiffuseOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_POST_BLUR_PERMUTATION_NUM; i++)
    {
        PushPass("Post-blur");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( DIFF_TEMP2 );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );

            // Outputs
            PushOutput( AsUint(Permanent::PREV_NORMAL_ROUGHNESS) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
            PushOutput( AsUint(Permanent::PREV_INTERNAL_DATA) );

            // Shaders
            AddDispatch( REBLUR_DiffuseOcclusion_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseOcclusion_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_SPLIT_SCREEN_PERMUTATION_NUM; i++)
    {
        PushPass("Split screen");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );

            // Outputs
            PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );

            // Shaders
            AddDispatch( REBLUR_Diffuse_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_NUM_THREADS, 1 );
        }
    }

    REBLUR_ADD_VALIDATION_DISPATCH( Transient::DATA1, ResourceType::IN_DIFF_HITDIST, ResourceType::IN_DIFF_HITDIST );

    #undef METHOD_NAME
    #undef DIFF_TEMP1
    #undef DIFF_TEMP2
}

void nrd::DenoiserImpl::UpdateMethod_ReblurOcclusion(const MethodData& methodData)
{
    enum class Dispatch
    {
        HITDIST_RECONSTRUCTION,
        TEMPORAL_ACCUMULATION   = HITDIST_RECONSTRUCTION + REBLUR_OCCLUSION_HITDIST_RECONSTRUCTION_PERMUTATION_NUM * 2,
        HISTORY_FIX             = TEMPORAL_ACCUMULATION + REBLUR_OCCLUSION_TEMPORAL_ACCUMULATION_PERMUTATION_NUM * 2,
        BLUR                    = HISTORY_FIX + REBLUR_OCCLUSION_HISTORY_FIX_PERMUTATION_NUM * 2, // non perf mode is used for anti-firefly
        POST_BLUR               = BLUR + REBLUR_OCCLUSION_BLUR_PERMUTATION_NUM * 2,
        SPLIT_SCREEN            = POST_BLUR + REBLUR_OCCLUSION_POST_BLUR_PERMUTATION_NUM * 2,
        VALIDATION              = SPLIT_SCREEN + REBLUR_SPLIT_SCREEN_PERMUTATION_NUM * 1, // no perf mode for split screen
    };

    NRD_DECLARE_DIMS;

    const ReblurSettings& settings = methodData.settings.reblur;
    const ReblurProps& props = g_ReblurProps[ size_t(methodData.desc.method) - size_t(Method::REBLUR_DIFFUSE) ];

    bool enableHitDistanceReconstruction = settings.hitDistanceReconstructionMode != HitDistanceReconstructionMode::OFF && settings.checkerboardMode == CheckerboardMode::OFF;

    float disocclusionThresholdBonus = (1.0f + m_JitterDelta) / float(rectH);
    float disocclusionThreshold = m_CommonSettings.disocclusionThreshold + disocclusionThresholdBonus;
    float disocclusionThresholdAlternate = m_CommonSettings.disocclusionThresholdAlternate + disocclusionThresholdBonus;

    uint32_t specCheckerboard = 2;
    uint32_t diffCheckerboard = 2;

    switch (settings.checkerboardMode)
    {
        case CheckerboardMode::BLACK:
            diffCheckerboard = 0;
            specCheckerboard = 1;
            break;
        case CheckerboardMode::WHITE:
            diffCheckerboard = 1;
            specCheckerboard = 0;
            break;
        default:
            break;
    }

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
    if (enableHitDistanceReconstruction)
    {
        uint32_t passIndex = AsUint(Dispatch::HITDIST_RECONSTRUCTION) + (settings.hitDistanceReconstructionMode == HitDistanceReconstructionMode::AREA_5X5 ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
        Constant* data = PushDispatch(methodData, passIndex);
        AddSharedConstants_Reblur(methodData, settings, data);
        ValidateConstants(data);
    }

    // TEMPORAL_ACCUMULATION
    uint32_t passIndex = AsUint(Dispatch::TEMPORAL_ACCUMULATION) + (m_CommonSettings.isDisocclusionThresholdMixAvailable ? 8 : 0) +
        (m_CommonSettings.isHistoryConfidenceInputsAvailable ? 4 : 0) + (enableHitDistanceReconstruction ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
    Constant* data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldPrevToWorld);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, disocclusionThreshold));
    AddFloat(data, disocclusionThresholdAlternate);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddUint(data, diffCheckerboard);
    AddUint(data, specCheckerboard);
    AddUint(data, 0);
    AddUint(data, m_CommonSettings.isHistoryConfidenceInputsAvailable);
    AddUint(data, m_CommonSettings.isDisocclusionThresholdMixAvailable);
    ValidateConstants(data);

    // HISTORY_FIX
    passIndex = AsUint(Dispatch::HISTORY_FIX) + (!settings.enableAntiFirefly ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat(data, settings.historyFixStrideBetweenSamples);
    ValidateConstants(data);

    // BLUR
    passIndex = AsUint(Dispatch::BLUR) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4(data, m_Rotator_Blur);
    ValidateConstants(data);

    // POST_BLUR
    passIndex = AsUint(Dispatch::POST_BLUR) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4(data, m_Rotator_PostBlur);
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

    // VALIDATION
    if (m_CommonSettings.enableValidation)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::VALIDATION));
        AddSharedConstants_Reblur(methodData, settings, data);
        AddFloat4x4(data, m_WorldToClipPrev);
        AddFloat2(data, m_CommonSettings.cameraJitter[0], m_CommonSettings.cameraJitter[1]);
        AddUint(data, props.hasDiffuse ? 1 : 0);
        AddUint(data, props.hasSpecular ? 1 : 0);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }
}
