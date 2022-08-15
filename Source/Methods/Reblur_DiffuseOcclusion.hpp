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
    #define DIFF_TEMP1 AsUint(Transient::DIFF_TMP1)
    #define DIFF_TEMP2 AsUint(Transient::DIFF_TMP2)

    enum class Permanent
    {
        PREV_VIEWZ = PERMANENT_POOL_START,
        PREV_NORMAL_ROUGHNESS,
        PREV_ACCUMSPEEDS_MATERIALID,
    };

    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} ); // R32f doesn't make sense because .y in data is FP16
    m_PermanentPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_UINT, w, h, 1} );

    enum class Transient
    {
        DATA1 = TRANSIENT_POOL_START,
        DIFF_ACCUMULATED,
        DIFF_TMP1, // single channel for hit dist only
        DIFF_TMP2,
    };

    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );

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
                AddDispatch( REBLUR_DiffuseOcclusion_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_DiffuseOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_TEMPORAL_ACCUMULATION_PERMUTATION_NUM; i++)
    {
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
            PushInput( AsUint(Permanent::PREV_ACCUMSPEEDS_MATERIALID) );
            PushInput( hasConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : REBLUR_DUMMY );
            PushInput( isAfterReconstruction ? DIFF_TEMP1 : AsUint(ResourceType::IN_DIFF_HITDIST) );
            PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );

            // Outputs
            PushOutput( DIFF_TEMP2 );
            PushOutput( AsUint(Transient::DATA1) );

            // Shaders
            if (hasConfidenceInputs)
            {
                AddDispatch( REBLUR_DiffuseOcclusion_TemporalAccumulation_Confidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_DiffuseOcclusion_TemporalAccumulation_Confidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_DiffuseOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_DiffuseOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
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

            // Outputs
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

            // Shaders
            AddDispatch( REBLUR_DiffuseOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_DiffuseOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_BLUR_PERMUTATION_NUM; i++)
    {
        PushPass("Blur");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

            // Outputs
            PushOutput( DIFF_TEMP2 );
            PushOutput( AsUint(Permanent::PREV_VIEWZ) );

            // Shaders
            AddDispatch( REBLUR_DiffuseOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_DiffuseOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
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

            // Outputs
            PushOutput( AsUint(Permanent::PREV_NORMAL_ROUGHNESS) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
            PushOutput( AsUint(Permanent::PREV_ACCUMSPEEDS_MATERIALID) );

            // Shaders
            AddDispatch( REBLUR_DiffuseOcclusion_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_DiffuseOcclusion_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_GROUP_DIM, 1 );
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
            AddDispatch( REBLUR_DiffuseOcclusion_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_GROUP_DIM, 1 );
        }
    }

    #undef METHOD_NAME
    #undef DIFF_TEMP1
    #undef DIFF_TEMP2

    return sizeof(ReblurSettings);
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
    };

    const ReblurSettings& settings = methodData.settings.reblur;

    bool enableHitDistanceReconstruction = settings.hitDistanceReconstructionMode != HitDistanceReconstructionMode::OFF && settings.checkerboardMode == CheckerboardMode::OFF;

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
    if (enableHitDistanceReconstruction)
    {
        uint32_t passIndex = AsUint(Dispatch::HITDIST_RECONSTRUCTION) + (settings.hitDistanceReconstructionMode == nrd::HitDistanceReconstructionMode::AREA_5X5 ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
        Constant* data = PushDispatch(methodData, passIndex);
        AddSharedConstants_Reblur(methodData, settings, data);
        ValidateConstants(data);
    }

    // TEMPORAL_ACCUMULATION
    uint32_t passIndex = AsUint(Dispatch::TEMPORAL_ACCUMULATION) + (m_CommonSettings.isHistoryConfidenceInputsAvailable ? 4 : 0) + (enableHitDistanceReconstruction ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
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

    // HISTORY_FIX
    passIndex = AsUint(Dispatch::HISTORY_FIX) + (!settings.enableAntiFirefly ? 1 : 0);
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
