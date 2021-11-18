/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_ReblurDiffuseSpecularOcclusion(uint16_t w, uint16_t h)
{
    #define DENOISER_NAME "REBLUR::DiffuseSpecularOcclusion"
    #define MIP_NUM 5

    enum class Permanent
    {
        PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS = PERMANENT_POOL_START,
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );

    enum class Transient
    {
        INTERNAL_DATA = TRANSIENT_POOL_START,
        ESTIMATED_ERROR,
        DIFF_ACCUMULATED,
        SPEC_ACCUMULATED,
        DIFF_TEMP,
        SPEC_TEMP,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );

    SetSharedConstants(1, 4, 8, 16);

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_TemporalAccumulation, SumConstants(4, 2, 1, 5), 8, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_SPEC_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
        PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 5), 8, 1 );
    }

    PushPass("Mip generation");
    {
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        for( uint16_t i = 1; i < MIP_NUM; i++ )
        {
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED), i, 1 );
            PushOutput( AsUint(Transient::SPEC_ACCUMULATED), i, 1 );
        }

        AddDispatch( NRD_MipGeneration_Float2_Float2, SumConstants(0, 0, 0, 2, false), 16, 2 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 1, MIP_NUM - 1 );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED), 1, MIP_NUM - 1 );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 0, 1 );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED), 0, 1 );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_HistoryFix, SumConstants(0, 0, 0, 2), 16, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_TEMP) );
        PushOutput( AsUint(Transient::SPEC_TEMP) );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_Blur, SumConstants(1, 2, 0, 4), 16, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_TEMP) );
        PushInput( AsUint(Transient::SPEC_TEMP) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_PostBlur, SumConstants(1, 2, 0, 4), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_SplitScreen, SumConstants(0, 0, 0, 3), 16, 1 );
    }

    #undef DENOISER_NAME
    #undef MIP_NUM

    return sizeof(ReblurDiffuseSpecularSettings);
}

void nrd::DenoiserImpl::UpdateMethod_ReblurDiffuseSpecularOcclusion(const MethodData& methodData)
{
    enum class Dispatch
    {
        TEMPORAL_ACCUMULATION,
        TEMPORAL_ACCUMULATION_WITH_CONFIDENCE,
        MIP_GENERATION,
        HISTORY_FIX,
        BLUR,
        POST_BLUR,
        SPLIT_SCREEN,
    };

    const ReblurDiffuseSpecularSettings& settings = methodData.settings.diffuseSpecularReblur;
    const ReblurDiffuseSettings& diffSettings = settings.diffuseSettings;
    const ReblurSpecularSettings& specSettings = settings.specularSettings;

    bool enableReferenceAccumulation = diffSettings.enableReferenceAccumulation && specSettings.enableReferenceAccumulation;
    float normalWeightStrictness = ml::Lerp( 0.1f, 1.0f, ml::Max( diffSettings.normalWeightStrictness, specSettings.normalWeightStrictness ) );

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

    // TEMPORAL_ACCUMULATION
    Constant* data = PushDispatch(methodData, AsUint( m_CommonSettings.isHistoryConfidenceInputsAvailable ? Dispatch::TEMPORAL_ACCUMULATION_WITH_CONFIDENCE : Dispatch::TEMPORAL_ACCUMULATION ));
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
    ValidateConstants(data);

    // MIP_GENERATION
    data = PushDispatch(methodData, AsUint(Dispatch::MIP_GENERATION));
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
    data = PushDispatch(methodData, AsUint(Dispatch::POST_BLUR));
    AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[2]);
    AddFloat4(data, specTrimmingParams_and_specBlurRadius);
    AddFloat(data, specSettings.maxAdaptiveRadiusScale);
    AddFloat(data, diffBlurRadius);
    AddFloat(data, diffSettings.maxAdaptiveRadiusScale);
    AddFloat(data, normalWeightStrictness);
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
