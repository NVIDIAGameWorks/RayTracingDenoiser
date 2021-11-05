/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_ReblurDiffuseOcclusion(uint16_t w, uint16_t h)
{
    #define DENOISER_NAME "REBLUR::DiffuseOcclusion"
    #define MIP_NUM 5

    enum class Permanent
    {
        PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS = PERMANENT_POOL_START,
        DIFF_HISTORY_FAST_1,
        DIFF_HISTORY_FAST_2,
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );

    enum class Transient
    {
        INTERNAL_DATA = TRANSIENT_POOL_START,
        DIFF_ACCUMULATED,
        DIFF_TEMP,
        ESTIMATED_ERROR,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );

    SetSharedConstants(1, 2, 8, 20);

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_FAST_2), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_1) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_FAST_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_2) );

        AddDispatch( REBLUR_DiffuseOcclusion_TemporalAccumulation, SumConstants(3, 2, 1, 3), 16, 1 );
    }

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_FAST_2), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_1) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_FAST_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_2) );

        AddDispatch( REBLUR_DiffuseOcclusion_TemporalAccumulationWithConfidence, SumConstants(3, 2, 1, 3), 16, 1 );
    }

    PushPass("Mip generation");
    {
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

        for( uint16_t i = 1; i < MIP_NUM; i++ )
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED), i, 1 );

        AddDispatch( NRD_MipGeneration_Float2, SumConstants(0, 0, 0, 2, false), 16, 2 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 0, MIP_NUM );
        PushInput( AsUint(Permanent::DIFF_HISTORY_FAST_1), 0, 1, AsUint(Permanent::DIFF_HISTORY_FAST_2) );

        PushOutput( AsUint(Transient::DIFF_TEMP) );

        AddDispatch( REBLUR_DiffuseOcclusion_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_TEMP) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseOcclusion_Blur, SumConstants(1, 1, 0, 2), 16, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );

        AddDispatch( REBLUR_DiffuseOcclusion_PostBlur, SumConstants(1, 1, 0, 2), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );

        AddDispatch( REBLUR_DiffuseOcclusion_SplitScreen, SumConstants(0, 0, 0, 2), 16, 1 );
    }

    #undef DENOISER_NAME
    #undef MIP_NUM

    return sizeof(ReblurDiffuseSettings);
}

void nrd::DenoiserImpl::UpdateMethod_ReblurDiffuseOcclusion(const MethodData& methodData)
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

    const ReblurDiffuseSettings& settings = methodData.settings.diffuseReblur;

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

    // TEMPORAL_ACCUMULATION
    Constant* data = PushDispatch(methodData, AsUint( m_CommonSettings.isHistoryConfidenceInputsAvailable ? Dispatch::TEMPORAL_ACCUMULATION_WITH_CONFIDENCE : Dispatch::TEMPORAL_ACCUMULATION ));
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
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_FIX));
    AddSharedConstants_ReblurDiffuse(methodData, settings, data);
    AddFloat(data, settings.historyClampingColorBoxSigmaScale);
    ValidateConstants(data);

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
