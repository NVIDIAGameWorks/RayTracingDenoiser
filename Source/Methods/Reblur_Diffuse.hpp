/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define REBLUR_DECLARE_SHARED_CONSTANT_NUM      SetSharedConstants(1, 3, 9, 22)
#define REBLUR_MIP_NUM                          4

size_t nrd::DenoiserImpl::AddMethod_ReblurDiffuse(uint16_t w, uint16_t h)
{
    #define METHOD_NAME REBLUR_Diffuse
    #define DIFF_TEMP1 AsUint(Transient::DIFF_HISTORY_STABILIZED) // valid before HistoryFix
    #define DIFF_TEMP2 AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) // valid after HistoryFix

    enum class Permanent
    {
        PREV_VIEWZ_DIFFACCUMSPEED = PERMANENT_POOL_START,
        PREV_NORMAL_SPECACCUMSPEED,
        DIFF_HISTORY,
    };

    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    enum class Transient
    {
        INTERNAL_DATA = TRANSIENT_POOL_START,
        ESTIMATED_ERROR,
        SCALED_VIEWZ,
        DIFF_ACCUMULATED,
        DIFF_HISTORY_STABILIZED,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, REBLUR_MIP_NUM} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, REBLUR_MIP_NUM} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    REBLUR_DECLARE_SHARED_CONSTANT_NUM;

    PushPass("Pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

        PushOutput( DIFF_TEMP1 );

        AddDispatch( REBLUR_Diffuse_PreBlur, SumConstants(1, 2, 0, 2), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_PreBlur, SumConstants(1, 2, 0, 2), 16, 1 );
    }

    PushPass("Pre-blur (advanced)");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_PDF) );

        PushOutput( DIFF_TEMP1 );

        AddDispatch( REBLUR_Diffuse_PreBlurAdvanced, SumConstants(1, 2, 0, 2), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_PreBlurAdvanced, SumConstants(1, 2, 0, 2), 16, 1 );
    }

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulation, SumConstants(4, 2, 1, 5), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulation, SumConstants(4, 2, 1, 5), 16, 1 );
    }

    PushPass("Temporal accumulation"); // after Pre-blur
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( DIFF_TEMP1 );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulation, SumConstants(4, 2, 1, 5), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulation, SumConstants(4, 2, 1, 5), 16, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 5), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 5), 16, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence, after Pre-blur
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( DIFF_TEMP1 );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 5), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 5), 16, 1 );
    }

    PushPass("Mip generation");
    {
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );

        for( uint16_t i = 1; i < REBLUR_MIP_NUM; i++ )
        {
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED), i, 1 );
            PushOutput( AsUint(Transient::SCALED_VIEWZ), i, 1 );
        }

        AddDispatch( NRD_MipGeneration_Float4_Float, SumConstants(0, 0, 1, 2, false), 16, 2 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, REBLUR_MIP_NUM );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 1, REBLUR_MIP_NUM - 1 );
        PushInput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 0, 1 );
        PushOutput( AsUint(Transient::DIFF_HISTORY_STABILIZED) );

        AddDispatch( REBLUR_Diffuse_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( DIFF_TEMP2 );

        AddDispatch( REBLUR_Diffuse_Blur, SumConstants(1, 2, 0, 0), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_Blur, SumConstants(1, 2, 0, 0), 16, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( DIFF_TEMP2 );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY) );

        AddDispatch( REBLUR_Diffuse_PostBlur, SumConstants(1, 2, 0, 0), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_PostBlur, SumConstants(1, 2, 0, 0), 16, 1 );
    }

    PushPass("Post-blur"); // before Anti-Firefly
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( DIFF_TEMP2 );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

        AddDispatch( REBLUR_Diffuse_PostBlur, SumConstants(1, 2, 0, 0), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_PostBlur, SumConstants(1, 2, 0, 0), 16, 1 );
    }

    PushPass("Anti-firefly");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

        PushOutput( AsUint(Permanent::DIFF_HISTORY) );

        AddDispatch( REBLUR_Diffuse_AntiFirefly, SumConstants(0, 0, 0, 0), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_AntiFirefly, SumConstants(0, 0, 0, 0), 16, 1 );
    }

    PushPass("Temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::ESTIMATED_ERROR) );
        PushInput( AsUint(Transient::DIFF_HISTORY_STABILIZED) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushOutput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        AddDispatch( REBLUR_Diffuse_TemporalStabilization, SumConstants(2, 2, 2, 1), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalStabilization, SumConstants(2, 2, 2, 1), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        AddDispatch( REBLUR_Diffuse_SplitScreen, SumConstants(0, 0, 0, 3), 16, 1 );
    }

    #undef METHOD_NAME
    #undef DIFF_TEMP1
    #undef DIFF_TEMP2

    return sizeof(ReblurSettings);
}

void nrd::DenoiserImpl::UpdateMethod_Reblur(const MethodData& methodData)
{
    enum class Dispatch
    {
        PRE_BLUR,
        PERF_PRE_BLUR,
        PRE_BLUR_ADVANCED,
        PERF_PRE_BLUR_ADVANCED,
        TEMPORAL_ACCUMULATION,
        PERF_TEMPORAL_ACCUMULATION,
        TEMPORAL_ACCUMULATION_AFTER_PRE_BLUR,
        PERF_TEMPORAL_ACCUMULATION_AFTER_PRE_BLUR,
        TEMPORAL_ACCUMULATION_WITH_CONFIDENCE,
        PERF_TEMPORAL_ACCUMULATION_WITH_CONFIDENCE,
        TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_AFTER_PRE_BLUR,
        PERF_TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_AFTER_PRE_BLUR,
        MIP_GENERATION,
        HISTORY_FIX,
        PERF_HISTORY_FIX,
        BLUR,
        PERF_BLUR,
        POST_BLUR,
        PERF_POST_BLUR,
        POST_BLUR_BEFORE_ANTI_FIRELY,
        PERF_POST_BLUR_BEFORE_ANTI_FIRELY,
        ANTI_FIREFLY,
        PERF_ANTI_FIREFLY,
        TEMPORAL_STABILIZATION,
        PERF_TEMPORAL_STABILIZATION,
        SPLIT_SCREEN,
    };

    const ReblurSettings& settings = methodData.settings.reblur;

    bool skipPrePass = settings.prePassMode == PrePassMode::OFF && settings.checkerboardMode == CheckerboardMode::OFF;
    ml::float4 antilagMinMaxThreshold = ml::float4(settings.antilagIntensitySettings.thresholdMin, settings.antilagHitDistanceSettings.thresholdMin, settings.antilagIntensitySettings.thresholdMax, settings.antilagHitDistanceSettings.thresholdMax);

    if (!settings.antilagIntensitySettings.enable || settings.enableReferenceAccumulation)
    {
        antilagMinMaxThreshold.x = 99998.0f;
        antilagMinMaxThreshold.z = 99999.0f;
    }

    if (!settings.antilagHitDistanceSettings.enable || settings.enableReferenceAccumulation)
    {
        antilagMinMaxThreshold.y = 99998.0f;
        antilagMinMaxThreshold.w = 99999.0f;
    }

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

    // PRE_BLUR
    if (!skipPrePass)
    {
        uint32_t passIndex = AsUint(Dispatch::PRE_BLUR) + ml::Max((int32_t)settings.prePassMode - 1, 0) * 2 + (settings.enablePerformanceMode ? 1 : 0);
        Constant* data = PushDispatch(methodData, passIndex);
        AddSharedConstants_Reblur(methodData, settings, data);
        AddFloat4x4(data, m_WorldToView);
        AddFloat4(data, m_Rotator[0]);
        AddFloat4(data, ml::float4(settings.specularLobeTrimmingParameters.A, settings.specularLobeTrimmingParameters.B, settings.specularLobeTrimmingParameters.C, settings.prePassMode == PrePassMode::OFF ? 0.0f : 1.0f));
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }

    // TEMPORAL_ACCUMULATION
    uint32_t passIndex = AsUint(Dispatch::TEMPORAL_ACCUMULATION) + ((m_CommonSettings.isHistoryConfidenceInputsAvailable ? 2 : 0) + (skipPrePass ? 0 : 1)) * 2 + (settings.enablePerformanceMode ? 1 : 0);
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
    AddFloat(data, settings.enableReferenceAccumulation ? 0.005f : m_CommonSettings.disocclusionThreshold );
    AddUint(data, diffCheckerboard);
    AddUint(data, specCheckerboard);
    AddUint(data, skipPrePass ? 0 : 1);
    ValidateConstants(data);

    // MIP_GENERATION
    data = PushDispatch(methodData, AsUint(Dispatch::MIP_GENERATION));
    AddUint2(data, rectW, rectH);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, m_CommonSettings.debug);
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
    uint32_t postBlur = AsUint( settings.enableAntiFirefly ?
        ( settings.enablePerformanceMode ? Dispatch::PERF_POST_BLUR_BEFORE_ANTI_FIRELY : Dispatch::POST_BLUR_BEFORE_ANTI_FIRELY ) :
        ( settings.enablePerformanceMode ? Dispatch::PERF_POST_BLUR : Dispatch::POST_BLUR ));
    data = PushDispatch(methodData, postBlur);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[2]);
    AddFloat4(data, ml::float4(settings.specularLobeTrimmingParameters.A, settings.specularLobeTrimmingParameters.B, settings.specularLobeTrimmingParameters.C, settings.maxAdaptiveRadiusScale));
    ValidateConstants(data);

    // ANTI_FIREFLY
    if (settings.enableAntiFirefly)
    {
        data = PushDispatch(methodData, AsUint( settings.enablePerformanceMode ? Dispatch::PERF_ANTI_FIREFLY : Dispatch::ANTI_FIREFLY ));
        AddSharedConstants_Reblur(methodData, settings, data);
        ValidateConstants(data);
    }

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint( settings.enablePerformanceMode ? Dispatch::PERF_TEMPORAL_STABILIZATION : Dispatch::TEMPORAL_STABILIZATION ));
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, m_CameraDelta);
    AddFloat4(data, antilagMinMaxThreshold );
    AddFloat2(data, settings.antilagIntensitySettings.sigmaScale, settings.antilagHitDistanceSettings.sigmaScale );
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, settings.stabilizationStrength);
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

void nrd::DenoiserImpl::AddSharedConstants_Reblur(const MethodData& methodData, const ReblurSettings& settings, Constant*& data)
{
    NRD_DECLARE_DIMS;

    uint32_t maxAccumulatedFrameNum = ml::Min(settings.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM);
    float blurRadius = settings.enableReferenceAccumulation ? 0.0f : settings.blurRadius;
    ml::float4 hitDistParams = ml::float4(settings.hitDistanceParameters.A, settings.hitDistanceParameters.B, settings.hitDistanceParameters.C, settings.hitDistanceParameters.D);

    // DRS will increase reprojected values, needed for stability, compensated by blur radius adjustment
    float unproject = 1.0f / (0.5f * rectH * m_ProjectY);

    AddFloat4x4(data, m_ViewToClip);

    AddFloat4(data, m_Frustum);
    AddFloat4(data, hitDistParams);
    AddFloat4(data, ml::float4(m_ViewDirection.x, m_ViewDirection.y, m_ViewDirection.z, 0.0f));

    AddFloat2(data, 1.0f / float(screenW), 1.0f / float(screenH));
    AddFloat2(data, float(screenW), float(screenH));

    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat2(data, float(rectW), float(rectH));

    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));

    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));
    AddFloat2(data, settings.antilagIntensitySettings.sensitivityToDarkness, settings.antilagHitDistanceSettings.sensitivityToDarkness);

    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddFloat(data, settings.enableReferenceAccumulation ? 1.0f : 0.0f);
    AddFloat(data, m_IsOrtho);

    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.debug);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, settings.planeDistanceSensitivity);

    AddFloat(data, m_FrameRateScale);
    AddFloat(data, blurRadius);
    AddFloat(data, float( settings.enableReferenceAccumulation ? REBLUR_MAX_HISTORY_FRAME_NUM : maxAccumulatedFrameNum ));
    AddFloat(data, settings.residualNoiseLevel);

    AddFloat(data, m_JitterDelta);
    AddFloat(data, settings.inputMix);
    AddFloat(data, settings.minConvergedStateBaseRadiusScale);
    AddFloat(data, settings.lobeAngleFraction);

    AddFloat(data, settings.roughnessFraction);
    AddFloat(data, settings.responsiveAccumulationRoughnessThreshold);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    AddUint(data, m_CommonSettings.frameIndex);

    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);
    AddUint(data, settings.enableMaterialTestForDiffuse ? 1 : 0);
    AddUint(data, settings.enableMaterialTestForSpecular ? 1 : 0);
    AddUint(data, 0);
}
