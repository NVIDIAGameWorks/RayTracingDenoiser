/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_ReblurDiffuse(uint16_t w, uint16_t h)
{
    #define DENOISER_NAME "REBLUR::Diffuse"
    #define MIP_NUM 5
    #define DIFF_TEMP1 AsUint(Permanent::DIFF_HISTORY_STABILIZED_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_STABILIZED_2)
    #define DIFF_TEMP2 AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST)

    enum class Permanent
    {
        PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS = PERMANENT_POOL_START,
        DIFF_HISTORY,
        DIFF_HISTORY_FAST_1,
        DIFF_HISTORY_FAST_2,
        DIFF_HISTORY_STABILIZED_1,
        DIFF_HISTORY_STABILIZED_2,
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    enum class Transient
    {
        INTERNAL_DATA = TRANSIENT_POOL_START,
        SCALED_VIEWZ,
        DIFF_ACCUMULATED,
        ESTIMATED_ERROR,
    };

    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );

    SetSharedConstants(1, 2, 8, 20);

    PushPass("Pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

        PushOutput( DIFF_TEMP1 );

        AddDispatch( REBLUR_Diffuse_PreBlur, SumConstants(1, 1, 0, 3), 16, 1 );
    }

    PushPass("Pre-blur (advanced)");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_PDF) );

        PushOutput( DIFF_TEMP1 );

        AddDispatch( REBLUR_Diffuse_PreBlurAdvanced, SumConstants(1, 1, 0, 3), 16, 1 );
    }

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_FAST_2), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_1) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_FAST_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_2) );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulation, SumConstants(3, 2, 1, 3), 16, 1 );
    }

    PushPass("Temporal accumulation"); // after Pre-blur
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( DIFF_TEMP1 );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_FAST_2), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_1) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_FAST_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_2) );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulation, SumConstants(3, 2, 1, 3), 16, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_FAST_2), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_1) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_FAST_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_2) );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulationWithConfidence, SumConstants(3, 2, 1, 3), 16, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence, after Pre-blur
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( DIFF_TEMP1 );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_FAST_2), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_1) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_FAST_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_2) );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulationWithConfidence, SumConstants(3, 2, 1, 3), 16, 1 );
    }

    PushPass("Mip generation");
    {
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );

        for( uint16_t i = 1; i < MIP_NUM; i++ )
        {
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED), i, 1 );
            PushOutput( AsUint(Transient::SCALED_VIEWZ), i, 1 );
        }

        AddDispatch( NRD_MipGeneration_Float4_Float, SumConstants(0, 0, 0, 2, false), 16, 2 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, MIP_NUM );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 0, MIP_NUM );
        PushInput( AsUint(Permanent::DIFF_HISTORY_FAST_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_2) );

        PushOutput( DIFF_TEMP1 );

        AddDispatch( REBLUR_Diffuse_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    PushPass("History fix"); // Anti-firefly enabled
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, MIP_NUM );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 0, MIP_NUM );
        PushInput( AsUint(Permanent::DIFF_HISTORY_FAST_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_2) );

        PushOutput( DIFF_TEMP2 );

        AddDispatch( REBLUR_Diffuse_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    PushPass("Anti-firefly");
    {
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( DIFF_TEMP2 );

        PushOutput( DIFF_TEMP1 );

        AddDispatch( REBLUR_Diffuse_AntiFirefly, SumConstants(0, 0, 0, 0), 16, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( DIFF_TEMP1 );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

        AddDispatch( REBLUR_Diffuse_Blur, SumConstants(1, 1, 0, 2), 16, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY) );

        AddDispatch( REBLUR_Diffuse_PostBlur, SumConstants(1, 1, 0, 2), 16, 1 );
    }

    PushPass("Temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_STABILIZED_2), 0, 1, AsUint(Permanent::DIFF_HISTORY_STABILIZED_1) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Transient::ESTIMATED_ERROR) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_STABILIZED_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_STABILIZED_2) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        AddDispatch( REBLUR_Diffuse_TemporalStabilization, SumConstants(2, 3, 1, 0), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        AddDispatch( REBLUR_Diffuse_SplitScreen, SumConstants(0, 0, 0, 2), 16, 1 );
    }

    #undef DENOISER_NAME
    #undef MIP_NUM
    #undef DIFF_TEMP1
    #undef DIFF_TEMP2

    return sizeof(ReblurDiffuseSettings);
}

void nrd::DenoiserImpl::UpdateMethod_ReblurDiffuse(const MethodData& methodData)
{
    enum class Dispatch
    {
        PRE_BLUR,
        PRE_BLUR_ADVANCED,
        TEMPORAL_ACCUMULATION,
        TEMPORAL_ACCUMULATION_AFTER_PRE_BLUR,
        TEMPORAL_ACCUMULATION_WITH_CONFIDENCE,
        TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_AFTER_PRE_BLUR,
        MIP_GENERATION,
        HISTORY_FIX,
        HISTORY_FIX_ANTI_FIREFLY_ENABLED,
        ANTI_FIREFLY,
        BLUR,
        POST_BLUR,
        TEMPORAL_STABILIZATION,
        SPLIT_SCREEN,
    };

    const ReblurDiffuseSettings& settings = methodData.settings.diffuseReblur;

    bool skipPrePass = settings.prePassMode == PrePassMode::OFF && settings.checkerboardMode == CheckerboardMode::OFF;
    float normalWeightStrictness = ml::Lerp( 0.1f, 1.0f, settings.normalWeightStrictness );

    uint32_t diffCheckerboard = ((uint32_t)settings.checkerboardMode + 2) % 3;
    ml::float4 diffAntilag1 = ml::float4(settings.antilagIntensitySettings.sigmaScale / GetMinResolutionScale(), settings.antilagHitDistanceSettings.sigmaScale / GetMinResolutionScale(), settings.antilagIntensitySettings.sensitivityToDarkness, settings.antilagHitDistanceSettings.sensitivityToDarkness);
    ml::float4 diffAntilag2 = ml::float4(settings.antilagIntensitySettings.thresholdMin / GetMinResolutionScale(), settings.antilagHitDistanceSettings.thresholdMin / GetMinResolutionScale(), settings.antilagIntensitySettings.thresholdMax, settings.antilagHitDistanceSettings.thresholdMax);

    if (!settings.antilagIntensitySettings.enable || settings.enableReferenceAccumulation)
    {
        diffAntilag2.x = 99998.0f;
        diffAntilag2.z = 99999.0f;
    }

    if (!settings.antilagHitDistanceSettings.enable || settings.enableReferenceAccumulation)
    {
        diffAntilag2.y = 99998.0f;
        diffAntilag2.w = 99999.0f;
    }

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_ReblurDiffuse(methodData, settings, data);
        AddUint(data, diffCheckerboard);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);

        return;
    }

    // PRE_BLUR
    if (!skipPrePass)
    {
        uint32_t preBlurPass = AsUint(Dispatch::PRE_BLUR) + ml::Max((int32_t)settings.prePassMode - 1, 0);
        Constant* data = PushDispatch(methodData, preBlurPass);
        AddSharedConstants_ReblurDiffuse(methodData, settings, data);
        AddFloat4x4(data, m_WorldToView);
        AddFloat4(data, m_Rotator[0]);
        AddUint(data, diffCheckerboard);
        AddUint(data, settings.prePassMode == PrePassMode::OFF ? 0 : 1);
        AddFloat(data, normalWeightStrictness);
        ValidateConstants(data);
    }

    // TEMPORAL_ACCUMULATION
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_ACCUMULATION) + (m_CommonSettings.isHistoryConfidenceInputsAvailable ? 2 : 0) + (skipPrePass ? 0 : 1));
    AddSharedConstants_ReblurDiffuse(methodData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, ml::float4(m_CameraDelta));
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, settings.enableReferenceAccumulation ? 0.005f : m_CommonSettings.disocclusionThreshold );
    AddUint(data, diffCheckerboard);
    ValidateConstants(data);

    // MIP_GENERATION
    data = PushDispatch(methodData, AsUint(Dispatch::MIP_GENERATION));
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // HISTORY_FIX
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_FIX) + (settings.enableAntiFirefly ? 1 : 0));
    AddSharedConstants_ReblurDiffuse(methodData, settings, data);
    AddFloat(data, settings.historyClampingColorBoxSigmaScale);
    ValidateConstants(data);

    // ANTI_FIREFLY
    if (settings.enableAntiFirefly)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::ANTI_FIREFLY));
        AddSharedConstants_ReblurDiffuse(methodData, settings, data);
        ValidateConstants(data);
    }

    // BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddSharedConstants_ReblurDiffuse(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[1]);
    AddFloat(data, settings.maxAdaptiveRadiusScale);
    AddFloat(data, normalWeightStrictness);
    ValidateConstants(data);

    // POST_BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::POST_BLUR));
    AddSharedConstants_ReblurDiffuse(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[2]);
    AddFloat(data, settings.maxAdaptiveRadiusScale);
    AddFloat(data, normalWeightStrictness);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddSharedConstants_ReblurDiffuse(methodData, settings, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, ml::float4(m_CameraDelta));
    AddFloat4(data, diffAntilag1 );
    AddFloat4(data, diffAntilag2 );
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    ValidateConstants(data);

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_ReblurDiffuse(methodData, settings, data);
        AddUint(data, diffCheckerboard);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);
    }
}

void nrd::DenoiserImpl::AddSharedConstants_ReblurDiffuse(const MethodData& methodData, const ReblurDiffuseSettings& settings, Constant*& data)
{
    NRD_DECLARE_DIMS;

    uint32_t maxAccumulatedFrameNum = ml::Min(settings.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM);
    float blurRadius = settings.enableReferenceAccumulation ? 0.0f : (settings.blurRadius * GetMinResolutionScale());
    float amount = settings.enableReferenceAccumulation ? 4.0f : ml::Saturate( settings.stabilizationStrength );
    float frameRateScale = ml::Max( m_FrameRateScale * amount, 2.0f / 16.0f );
    ml::float4 diffHitDistParams = ml::float4(settings.hitDistanceParameters.A * m_CommonSettings.meterToUnitsMultiplier, settings.hitDistanceParameters.B, settings.hitDistanceParameters.C, settings.hitDistanceParameters.D);

    // DRS will increase reprojected values, needed for stability, compensated by blur radius adjustment
    float unproject = 1.0f / (0.5f * rectH * m_ProjectY);

    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);
    AddFloat4(data, diffHitDistParams);

    AddFloat2(data, 1.0f / float(screenW), 1.0f / float(screenH));
    AddFloat2(data, float(screenW), float(screenH));

    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat2(data, float(rectW), float(rectH));

    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));

    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);

    AddFloat(data, settings.enableReferenceAccumulation ? 1.0f : 0.0f);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.debug);

    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, 1.0f / (m_CommonSettings.meterToUnitsMultiplier * settings.planeDistanceSensitivity));
    AddFloat(data, frameRateScale);
    AddFloat(data, blurRadius);

    AddFloat(data, float( settings.enableReferenceAccumulation ? REBLUR_MAX_HISTORY_FRAME_NUM : maxAccumulatedFrameNum ));
    AddFloat(data, float( settings.enableReferenceAccumulation ? REBLUR_MAX_HISTORY_FRAME_NUM : settings.maxFastAccumulatedFrameNum ));
    AddFloat(data, settings.residualNoiseLevel);
    AddFloat(data, m_JitterDelta);

    AddFloat(data, m_CommonSettings.meterToUnitsMultiplier);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);

    AddUint(data, m_CommonSettings.isRadianceMultipliedByExposure ? 1 : 0);
    AddUint(data, 0);
    AddUint(data, 0);
    AddUint(data, 0);
}
