/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_ReblurDiffuseSpecular(uint16_t w, uint16_t h)
{
    #define DENOISER_NAME "REBLUR::DiffuseSpecular"
    #define MIP_NUM 5
    #define DIFF_TEMP1 AsUint(Transient::DIFF_HISTORY_STABILIZED) // valid before HistoryFix
    #define SPEC_TEMP1 AsUint(Transient::SPEC_HISTORY_STABILIZED) // valid before HistoryFix
    #define DIFF_TEMP2 AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) // valid after HistoryFix
    #define SPEC_TEMP2 AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) // valid after HistoryFix

    enum class Permanent
    {
        PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS = PERMANENT_POOL_START,
        DIFF_HISTORY,
        SPEC_HISTORY,
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    enum class Transient
    {
        INTERNAL_DATA = TRANSIENT_POOL_START,
        ESTIMATED_ERROR,
        SCALED_VIEWZ,
        DIFF_ACCUMULATED,
        DIFF_HISTORY_STABILIZED,
        SPEC_ACCUMULATED,
        SPEC_HISTORY_STABILIZED,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    SetSharedConstants(1, 4, 8, 16);

    PushPass("Pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );

        PushOutput( DIFF_TEMP1 );
        PushOutput( SPEC_TEMP1 );

        AddDispatch( REBLUR_DiffuseSpecular_PreBlur, SumConstants(1, 2, 0, 5), 16, 1 );
    }

    PushPass("Pre-blur (advanced)");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_PDF) );
        PushInput( AsUint(ResourceType::IN_SPEC_DIRECTION_PDF) );

        PushOutput( DIFF_TEMP1 );
        PushOutput( SPEC_TEMP1 );

        AddDispatch( REBLUR_DiffuseSpecular_PreBlurAdvanced, SumConstants(1, 2, 0, 5), 16, 1 );
    }

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );
        PushInput( AsUint(Permanent::SPEC_HISTORY) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseSpecular_TemporalAccumulation, SumConstants(4, 2, 1, 6), 8, 1 );
    }

    PushPass("Temporal accumulation"); // after Pre-blur
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( DIFF_TEMP1 );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( SPEC_TEMP1 );
        PushInput( AsUint(Permanent::SPEC_HISTORY) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseSpecular_TemporalAccumulation, SumConstants(4, 2, 1, 6), 8, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );
        PushInput( AsUint(Permanent::SPEC_HISTORY) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
        PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseSpecular_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 6), 8, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence, after Pre-blur
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( DIFF_TEMP1 );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( SPEC_TEMP1 );
        PushInput( AsUint(Permanent::SPEC_HISTORY) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
        PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseSpecular_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 6), 8, 1 );
    }

    PushPass("Mip generation");
    {
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );

        for( uint16_t i = 1; i < MIP_NUM; i++ )
        {
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED), i, 1 );
            PushOutput( AsUint(Transient::SPEC_ACCUMULATED), i, 1 );
            PushOutput( AsUint(Transient::SCALED_VIEWZ), i, 1 );
        }

        AddDispatch( NRD_MipGeneration_Float4_Float4_Float, SumConstants(0, 0, 1, 2, false), 16, 2 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, MIP_NUM );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 1, MIP_NUM - 1 );
        PushInput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED), 1, MIP_NUM - 1 );
        PushInput( AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 0, 1 );
        PushOutput( AsUint(Transient::DIFF_HISTORY_STABILIZED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED), 0, 1 );
        PushOutput( AsUint(Transient::SPEC_HISTORY_STABILIZED) );

        AddDispatch( REBLUR_DiffuseSpecular_HistoryFix, SumConstants(0, 0, 0, 2), 16, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( DIFF_TEMP2 );
        PushOutput( SPEC_TEMP2 );

        AddDispatch( REBLUR_DiffuseSpecular_Blur, SumConstants(1, 2, 0, 4), 16, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( DIFF_TEMP2 );
        PushInput( SPEC_TEMP2 );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY) );
        PushOutput( AsUint(Permanent::SPEC_HISTORY) );

        AddDispatch( REBLUR_DiffuseSpecular_PostBlur, SumConstants(1, 2, 0, 4), 16, 1 );
    }

    PushPass("Post-blur"); // before Anti-Firefly
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( DIFF_TEMP2 );
        PushInput( SPEC_TEMP2 );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseSpecular_PostBlur, SumConstants(1, 2, 0, 4), 16, 1 );
    }

    PushPass("Anti-firefly");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        PushOutput( AsUint(Permanent::DIFF_HISTORY) );
        PushOutput( AsUint(Permanent::SPEC_HISTORY) );

        AddDispatch( REBLUR_DiffuseSpecular_AntiFirefly, SumConstants(0, 0, 0, 0), 16, 1 );
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
        PushInput( AsUint(Transient::SPEC_HISTORY_STABILIZED) );
        PushInput( AsUint(Permanent::SPEC_HISTORY) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) );

        AddDispatch( REBLUR_DiffuseSpecular_TemporalStabilization, SumConstants(2, 5, 1, 2), 8, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) );

        AddDispatch( REBLUR_DiffuseSpecular_SplitScreen, SumConstants(0, 0, 0, 3), 16, 1 );
    }

    #undef DENOISER_NAME
    #undef MIP_NUM
    #undef DIFF_TEMP1
    #undef SPEC_TEMP1
    #undef DIFF_TEMP2
    #undef SPEC_TEMP2

    return sizeof(ReblurDiffuseSpecularSettings);
}

void nrd::DenoiserImpl::UpdateMethod_ReblurDiffuseSpecular(const MethodData& methodData)
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
        BLUR,
        POST_BLUR,
        POST_BLUR_BEFORE_ANTI_FIRELY,
        ANTI_FIREFLY,
        TEMPORAL_STABILIZATION,
        SPLIT_SCREEN,
    };

    const ReblurDiffuseSpecularSettings& settings = methodData.settings.diffuseSpecularReblur;
    const ReblurDiffuseSettings& diffSettings = settings.diffuse;
    const ReblurSpecularSettings& specSettings = settings.specular;

    bool enableReferenceAccumulation = diffSettings.enableReferenceAccumulation && specSettings.enableReferenceAccumulation;
    bool enablePrePass = diffSettings.prePassMode != PrePassMode::OFF && specSettings.prePassMode != PrePassMode::OFF;
    bool skipPrePass = !enablePrePass && diffSettings.checkerboardMode == CheckerboardMode::OFF && specSettings.checkerboardMode == CheckerboardMode::OFF;
    bool enableAntiFirefly = diffSettings.enableAntiFirefly && specSettings.enableAntiFirefly;
    float normalWeightStrictness = ml::Lerp( 0.1f, 1.0f, ml::Min( diffSettings.normalWeightStrictness, specSettings.normalWeightStrictness ) );

    uint32_t diffCheckerboard = ((uint32_t)diffSettings.checkerboardMode + 2) % 3;
    ml::float4 diffAntilag1 = ml::float4(diffSettings.antilagIntensitySettings.sigmaScale / GetMinResolutionScale(), diffSettings.antilagHitDistanceSettings.sigmaScale / GetMinResolutionScale(), diffSettings.antilagIntensitySettings.sensitivityToDarkness, diffSettings.antilagHitDistanceSettings.sensitivityToDarkness);
    ml::float4 diffAntilag2 = ml::float4(diffSettings.antilagIntensitySettings.thresholdMin / GetMinResolutionScale(), diffSettings.antilagHitDistanceSettings.thresholdMin / GetMinResolutionScale(), diffSettings.antilagIntensitySettings.thresholdMax, diffSettings.antilagHitDistanceSettings.thresholdMax);
    float diffBlurRadius = enableReferenceAccumulation ? 0.0f : (diffSettings.blurRadius * GetMinResolutionScale());

    if (!diffSettings.antilagIntensitySettings.enable || enableReferenceAccumulation)
    {
        diffAntilag2.x = 99998.0f;
        diffAntilag2.z = 99999.0f;
    }

    if (!diffSettings.antilagHitDistanceSettings.enable || enableReferenceAccumulation)
    {
        diffAntilag2.y = 99998.0f;
        diffAntilag2.w = 99999.0f;
    }

    uint32_t specCheckerboard = ((uint32_t)specSettings.checkerboardMode + 2) % 3;
    ml::float4 specTrimmingParams_and_specBlurRadius = ml::float4(specSettings.lobeTrimmingParameters.A, specSettings.lobeTrimmingParameters.B, specSettings.lobeTrimmingParameters.C, enableReferenceAccumulation ? 0.0f : (specSettings.blurRadius * GetMinResolutionScale()));
    ml::float4 specAntilag1 = ml::float4(specSettings.antilagIntensitySettings.sigmaScale / GetMinResolutionScale(), specSettings.antilagHitDistanceSettings.sigmaScale / GetMinResolutionScale(), specSettings.antilagIntensitySettings.sensitivityToDarkness, specSettings.antilagHitDistanceSettings.sensitivityToDarkness);
    ml::float4 specAntilag2 = ml::float4(specSettings.antilagIntensitySettings.thresholdMin / GetMinResolutionScale(), specSettings.antilagHitDistanceSettings.thresholdMin / GetMinResolutionScale(), specSettings.antilagIntensitySettings.thresholdMax, specSettings.antilagHitDistanceSettings.thresholdMax);

    if (!specSettings.antilagIntensitySettings.enable || enableReferenceAccumulation)
    {
        specAntilag2.x = 99998.0f;
        specAntilag2.z = 99999.0f;
    }

    if (!specSettings.antilagHitDistanceSettings.enable || enableReferenceAccumulation)
    {
        specAntilag2.y = 99998.0f;
        specAntilag2.w = 99999.0f;
    }

    NRD_DECLARE_DIMS;

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);

        return;
    }

    // PRE_BLUR
    if (!skipPrePass)
    {
        uint32_t preBlurPass = AsUint(Dispatch::PRE_BLUR) + ml::Max(ml::Min((int32_t)diffSettings.prePassMode, (int32_t)specSettings.prePassMode) - 1, 0);
        Constant* data = PushDispatch(methodData, preBlurPass);
        AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
        AddFloat4x4(data, m_WorldToView);
        AddFloat4(data, m_Rotator[0]);
        AddFloat4(data, specTrimmingParams_and_specBlurRadius);
        AddUint(data, specCheckerboard);
        AddFloat(data, diffBlurRadius);
        AddUint(data, diffCheckerboard);
        AddUint(data, enablePrePass ? 1 : 0);
        AddFloat(data, normalWeightStrictness);
        ValidateConstants(data);
    }

    // TEMPORAL_ACCUMULATION
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_ACCUMULATION) + (m_CommonSettings.isHistoryConfidenceInputsAvailable ? 2 : 0) + (skipPrePass ? 0 : 1));
    AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, m_CameraDelta);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, enableReferenceAccumulation ? 0.005f : m_CommonSettings.disocclusionThreshold);
    AddFloat(data, m_JitterDelta);
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
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_FIX));
    AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
    AddFloat(data, diffSettings.historyFixStrength);
    AddFloat(data, specSettings.historyFixStrength);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[1]);
    AddFloat4(data, specTrimmingParams_and_specBlurRadius);
    AddFloat(data, specSettings.maxAdaptiveRadiusScale);
    AddFloat(data, diffBlurRadius);
    AddFloat(data, diffSettings.maxAdaptiveRadiusScale);
    AddFloat(data, normalWeightStrictness);
    ValidateConstants(data);

    // POST_BLUR
    data = PushDispatch(methodData, AsUint( enableAntiFirefly ? Dispatch::POST_BLUR_BEFORE_ANTI_FIRELY : Dispatch::POST_BLUR));
    AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[2]);
    AddFloat4(data, specTrimmingParams_and_specBlurRadius);
    AddFloat(data, specSettings.maxAdaptiveRadiusScale);
    AddFloat(data, diffBlurRadius);
    AddFloat(data, diffSettings.maxAdaptiveRadiusScale);
    AddFloat(data, normalWeightStrictness);
    ValidateConstants(data);

    // ANTI_FIREFLY
    if (enableAntiFirefly)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::ANTI_FIREFLY));
        AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
        ValidateConstants(data);
    }

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, m_CameraDelta);
    AddFloat4(data, diffAntilag1 );
    AddFloat4(data, diffAntilag2 );
    AddFloat4(data, specAntilag1 );
    AddFloat4(data, specAntilag2 );
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, settings.diffuse.stabilizationStrength);
    AddFloat(data, settings.specular.stabilizationStrength);
    ValidateConstants(data);

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);
    }
}

void nrd::DenoiserImpl::AddSharedConstants_ReblurDiffuseSpecular(const MethodData& methodData, const ReblurDiffuseSpecularSettings& settings, Constant*& data)
{
    NRD_DECLARE_DIMS;

    bool enableReferenceAccumulation = settings.diffuse.enableReferenceAccumulation && settings.specular.enableReferenceAccumulation;
    uint32_t diffMaxAccumulatedFrameNum = ml::Min(settings.diffuse.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM);
    uint32_t specMaxAccumulatedFrameNum = ml::Min(settings.specular.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM);
    float planeDistanceSensitivity = ml::Min( settings.diffuse.planeDistanceSensitivity, settings.specular.planeDistanceSensitivity );
    float residualNoiseLevel = ml::Min( settings.diffuse.residualNoiseLevel, settings.specular.residualNoiseLevel );
    ml::float4 diffHitDistParams = ml::float4(settings.diffuse.hitDistanceParameters.A, settings.diffuse.hitDistanceParameters.B, settings.diffuse.hitDistanceParameters.C, settings.diffuse.hitDistanceParameters.D);
    ml::float4 specHitDistParams = ml::float4(settings.specular.hitDistanceParameters.A, settings.specular.hitDistanceParameters.B, settings.specular.hitDistanceParameters.C, settings.specular.hitDistanceParameters.D);

    // DRS will increase reprojected values, needed for stability, compensated by blur radius adjustment
    float unproject = 1.0f / (0.5f * rectH * m_ProjectY);

    AddFloat4x4(data, m_ViewToClip);

    AddFloat4(data, m_Frustum);
    AddFloat4(data, diffHitDistParams);
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, ml::float4(m_ViewDirection.x, m_ViewDirection.y, m_ViewDirection.z, 0.0f));

    AddFloat2(data, 1.0f / float(screenW), 1.0f / float(screenH));
    AddFloat2(data, float(screenW), float(screenH));

    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat2(data, float(rectW), float(rectH));

    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));

    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);

    AddFloat(data, enableReferenceAccumulation ? 1.0f : 0.0f);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.debug);

    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, planeDistanceSensitivity);
    AddFloat(data, m_FrameRateScale);
    AddFloat(data, residualNoiseLevel);

    AddFloat(data, float( enableReferenceAccumulation ? REBLUR_MAX_HISTORY_FRAME_NUM : diffMaxAccumulatedFrameNum ) );
    AddFloat(data, float( enableReferenceAccumulation ? REBLUR_MAX_HISTORY_FRAME_NUM : specMaxAccumulatedFrameNum) );
    AddFloat(data, 0.0f);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);

    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);
    AddUint(data, settings.diffuse.materialMask);
    AddUint(data, settings.specular.materialMask);
}
