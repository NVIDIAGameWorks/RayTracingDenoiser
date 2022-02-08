/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_ReblurSpecularOcclusion(uint16_t w, uint16_t h)
{
    #define DENOISER_NAME "REBLUR::SpecularOcclusion"
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
        SPEC_ACCUMULATED,
        SPEC_TEMP,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );

    SetSharedConstants(1, 3, 8, 16);

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_SpecularOcclusion_TemporalAccumulation, SumConstants(4, 2, 1, 3), 8, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_SPEC_HITDIST) );
        PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_SpecularOcclusion_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 3), 8, 1 );
    }

    PushPass("Mip generation");
    {
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        for( uint16_t i = 1; i < MIP_NUM; i++ )
            PushOutput( AsUint(Transient::SPEC_ACCUMULATED), i, 1 );

        AddDispatch( NRD_MipGeneration_Float2, SumConstants(0, 0, 1, 2, false), 16, 2 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED), 1, MIP_NUM - 1 );

        PushOutput( AsUint(Transient::SPEC_ACCUMULATED), 0, 1 );

        AddDispatch( REBLUR_SpecularOcclusion_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::SPEC_TEMP) );

        AddDispatch( REBLUR_SpecularOcclusion_Blur, SumConstants(1, 2, 0, 2), 16, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SPEC_TEMP) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

        AddDispatch( REBLUR_SpecularOcclusion_PostBlur, SumConstants(1, 2, 0, 2), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

        AddDispatch( REBLUR_SpecularOcclusion_SplitScreen, SumConstants(0, 0, 0, 2), 16, 1 );
    }

    #undef DENOISER_NAME
    #undef MIP_NUM

    return sizeof(ReblurSpecularSettings);
}

void nrd::DenoiserImpl::UpdateMethod_ReblurSpecularOcclusion(const MethodData& methodData)
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

    const ReblurSpecularSettings& settings = methodData.settings.specularReblur;

    float normalWeightStrictness = ml::Lerp( 0.1f, 1.0f, settings.normalWeightStrictness );

    uint32_t specCheckerboard = ((uint32_t)settings.checkerboardMode + 2) % 3;
    ml::float4 specTrimmingParams = ml::float4(settings.lobeTrimmingParameters.A, settings.lobeTrimmingParameters.B, settings.lobeTrimmingParameters.C, 0.0f);
    ml::float4 specAntilag1 = ml::float4(settings.antilagIntensitySettings.sigmaScale / GetMinResolutionScale(), settings.antilagHitDistanceSettings.sigmaScale / GetMinResolutionScale(), settings.antilagIntensitySettings.sensitivityToDarkness, settings.antilagHitDistanceSettings.sensitivityToDarkness);
    ml::float4 specAntilag2 = ml::float4(settings.antilagIntensitySettings.thresholdMin / GetMinResolutionScale(), settings.antilagHitDistanceSettings.thresholdMin / GetMinResolutionScale(), settings.antilagIntensitySettings.thresholdMax, settings.antilagHitDistanceSettings.thresholdMax);

    if (!settings.antilagIntensitySettings.enable || settings.enableReferenceAccumulation)
    {
        specAntilag2.x = 99998.0f;
        specAntilag2.z = 99999.0f;
    }

    if (!settings.antilagHitDistanceSettings.enable || settings.enableReferenceAccumulation)
    {
        specAntilag2.y = 99998.0f;
        specAntilag2.w = 99999.0f;
    }

    NRD_DECLARE_DIMS;

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_ReblurSpecular(methodData, settings, data);
        AddUint(data, specCheckerboard);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);

        return;
    }

    // TEMPORAL_ACCUMULATION
    Constant* data = PushDispatch(methodData, AsUint( m_CommonSettings.isHistoryConfidenceInputsAvailable ? Dispatch::TEMPORAL_ACCUMULATION_WITH_CONFIDENCE : Dispatch::TEMPORAL_ACCUMULATION ));
    AddSharedConstants_ReblurSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, m_CameraDelta);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, settings.enableReferenceAccumulation ? 0.005f : m_CommonSettings.disocclusionThreshold);
    AddUint(data, specCheckerboard);
    ValidateConstants(data);

    // MIP_GENERATION
    data = PushDispatch(methodData, AsUint(Dispatch::MIP_GENERATION));
    AddUint2(data, rectW, rectH);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // HISTORY_FIX
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_FIX));
    AddSharedConstants_ReblurSpecular(methodData, settings, data);
    AddFloat(data, settings.historyFixStrength);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddSharedConstants_ReblurSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[1]);
    AddFloat4(data, specTrimmingParams);
    AddFloat(data, settings.maxAdaptiveRadiusScale);
    AddFloat(data, normalWeightStrictness);
    ValidateConstants(data);

    // POST_BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::POST_BLUR));
    AddSharedConstants_ReblurSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[2]);
    AddFloat4(data, specTrimmingParams);
    AddFloat(data, settings.maxAdaptiveRadiusScale);
    AddFloat(data, normalWeightStrictness);
    ValidateConstants(data);

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_ReblurSpecular(methodData, settings, data);
        AddUint(data, specCheckerboard);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);
    }
}
