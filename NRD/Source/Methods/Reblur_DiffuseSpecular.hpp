/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_ReblurDiffuseSpecular(uint16_t w, uint16_t h)
{
    enum class Permanent
    {
        PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS = PERMANENT_POOL_START,
        DIFF_HISTORY,
        DIFF_FAST_HISTORY_1,
        DIFF_FAST_HISTORY_2,
        DIFF_STABILIZED_HISTORY_1,
        DIFF_STABILIZED_HISTORY_2,
        SPEC_HISTORY,
        SPEC_FAST_HISTORY_1,
        SPEC_FAST_HISTORY_2,
        SPEC_STABILIZED_HISTORY_1,
        SPEC_STABILIZED_HISTORY_2,
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
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
        SPEC_ACCUMULATED,
    };

    #define MIP_NUM 4

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, MIP_NUM} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, MIP_NUM} );

   SetSharedConstants(1, 1, 8, 12);

    // Tricks to save memory
    #define DIFF_TEMP AsUint(Permanent::DIFF_STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::DIFF_STABILIZED_HISTORY_2)
    #define SPEC_TEMP AsUint(Permanent::SPEC_STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::SPEC_STABILIZED_HISTORY_2)

    PushPass("REBLUR::DiffuseSpecular - Copy viewZ");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );

        AddDispatch( REBLUR_CopyViewZ, SumConstants(0, 0, 0, 1, false), 16, 1 );
    }

    PushPass("REBLUR::DiffuseSpecular - pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HIT) );
        PushInput( AsUint(ResourceType::IN_SPEC_HIT) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( DIFF_TEMP );
        PushOutput( SPEC_TEMP );

        AddDispatch( REBLUR_DiffuseSpecular_PreBlur, SumConstants(1, 4, 0, 3), 16, 1 );
    }

    PushPass("REBLUR::DiffuseSpecular - temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::SPEC_HISTORY) );
        PushInput( AsUint(Permanent::DIFF_FAST_HISTORY_2), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_1) );
        PushInput( AsUint(Permanent::SPEC_FAST_HISTORY_2), 0, 1, AsUint(Permanent::SPEC_FAST_HISTORY_1) );
        PushInput( DIFF_TEMP );
        PushInput( SPEC_TEMP );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );
        PushOutput( AsUint(Permanent::DIFF_FAST_HISTORY_1), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_2) );
        PushOutput( AsUint(Permanent::SPEC_FAST_HISTORY_1), 0, 1, AsUint(Permanent::SPEC_FAST_HISTORY_2) );

        AddDispatch( REBLUR_DiffuseSpecular_TemporalAccumulation, SumConstants(4, 2, 1, 6), 8, 1 );
    }

    PushPass("REBLUR::DiffuseSpecular - temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::SPEC_HISTORY) );
        PushInput( AsUint(Permanent::DIFF_FAST_HISTORY_2), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_1) );
        PushInput( AsUint(Permanent::SPEC_FAST_HISTORY_2), 0, 1, AsUint(Permanent::SPEC_FAST_HISTORY_1) );
        PushInput( AsUint(ResourceType::IN_DIFF_HIT) );
        PushInput( AsUint(ResourceType::IN_SPEC_HIT) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );
        PushOutput( AsUint(Permanent::DIFF_FAST_HISTORY_1), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_2) );
        PushOutput( AsUint(Permanent::SPEC_FAST_HISTORY_1), 0, 1, AsUint(Permanent::SPEC_FAST_HISTORY_2) );

        AddDispatch( REBLUR_DiffuseSpecular_TemporalAccumulation, SumConstants(4, 2, 1, 6), 8, 1 );
    }

    PushPass("REBLUR::DiffuseSpecular - mip generation");
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

        AddDispatch( NRD_MipGeneration_Float4_Float4_Float, SumConstants(0, 0, 0, 2, false), 16, 2 );
    }

    PushPass("REBLUR::DiffuseSpecular - history fix");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, MIP_NUM );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 1, MIP_NUM - 1 );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED), 1, MIP_NUM - 1 );
        PushInput( AsUint(Permanent::DIFF_FAST_HISTORY_1), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_2) );
        PushInput( AsUint(Permanent::SPEC_FAST_HISTORY_1), 0, 1, AsUint(Permanent::SPEC_FAST_HISTORY_2) );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseSpecular_HistoryFix, SumConstants(0, 0, 1, 4), 16, 1 );
    }

    PushPass("REBLUR::DiffuseSpecular - blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        PushOutput( DIFF_TEMP );
        PushOutput( SPEC_TEMP );

        AddDispatch( REBLUR_DiffuseSpecular_Blur, SumConstants(1, 4, 0, 1), 16, 1 );
    }

    PushPass("REBLUR::DiffuseSpecular - post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( DIFF_TEMP );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( SPEC_TEMP );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        PushOutput( AsUint(Permanent::DIFF_HISTORY) );
        PushOutput( AsUint(Permanent::SPEC_HISTORY) );

        AddDispatch( REBLUR_DiffuseSpecular_PostBlur, SumConstants(1, 4, 0, 3), 16, 1 );
    }

    PushPass("REBLUR::DiffuseSpecular - temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Permanent::DIFF_STABILIZED_HISTORY_2), 0, 1, AsUint(Permanent::DIFF_STABILIZED_HISTORY_1) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::SPEC_STABILIZED_HISTORY_2), 0, 1, AsUint(Permanent::SPEC_STABILIZED_HISTORY_1) );
        PushInput( AsUint(Permanent::SPEC_HISTORY) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushOutput( AsUint(Permanent::DIFF_STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::DIFF_STABILIZED_HISTORY_2) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_HIT) );
        PushOutput( AsUint(Permanent::SPEC_STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::SPEC_STABILIZED_HISTORY_2) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_HIT) );

        AddDispatch( REBLUR_DiffuseSpecular_TemporalStabilization, SumConstants(2, 7, 1, 0), 8, 1 );
    }

    PushPass("REBLUR::DiffuseSpecular - split screen");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HIT) );
        PushInput( AsUint(ResourceType::IN_SPEC_HIT) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_HIT) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_HIT) );

        AddDispatch( REBLUR_DiffuseSpecular_SplitScreen, SumConstants(0, 2, 0, 3), 16, 1 );
    }

    #undef DIFF_TEMP
    #undef SPEC_TEMP

    return sizeof(ReblurDiffuseSpecularSettings);
}

void DenoiserImpl::UpdateMethod_ReblurDiffuseSpecular(const MethodData& methodData)
{
    enum class Dispatch
    {
        COPY_VIEWZ,
        PRE_BLUR,
        TEMPORAL_ACCUMULATION,
        TEMPORAL_ACCUMULATION_WITHOUT_PRE_BLUR,
        MIP_GENERATION,
        HISTORY_FIX,
        BLUR,
        POST_BLUR,
        TEMPORAL_STABILIZATION,
        SPLIT_SCREEN,
    };

    const ReblurDiffuseSpecularSettings& settings = methodData.settings.diffuseSpecular;
    const ReblurDiffuseSettings& diffSettings = settings.diffuseSettings;
    const ReblurSpecularSettings& specSettings = settings.specularSettings;

    bool skipPreBlur = diffSettings.skipPreBlur && diffSettings.checkerboardMode == CheckerboardMode::OFF && specSettings.skipPreBlur && specSettings.checkerboardMode == CheckerboardMode::OFF;

    uint32_t diffCheckerboard = ((uint32_t)diffSettings.checkerboardMode + 2) % 3;
    float4 diffHitDistParams = float4(&diffSettings.hitDistanceParameters.A);    
    float4 diffAntilag1 = float4(diffSettings.antilagIntensitySettings.sigmaScale / m_CommonSettings.resolutionScale, diffSettings.antilagHitDistanceSettings.sigmaScale / m_CommonSettings.resolutionScale, diffSettings.antilagIntensitySettings.sensitivityToDarkness, diffSettings.antilagHitDistanceSettings.sensitivityToDarkness);
    float4 diffAntilag2 = float4(diffSettings.antilagIntensitySettings.thresholdMin / m_CommonSettings.resolutionScale, diffSettings.antilagHitDistanceSettings.thresholdMin / m_CommonSettings.resolutionScale, diffSettings.antilagIntensitySettings.thresholdMax, diffSettings.antilagHitDistanceSettings.thresholdMax);
    float diffBlurRadius = diffSettings.blurRadius * m_CommonSettings.resolutionScale;

    if (!diffSettings.antilagIntensitySettings.enable)
    {
        diffAntilag2.x = 99998.0f;
        diffAntilag2.z = 99999.0f;
    }

    if (!diffSettings.antilagHitDistanceSettings.enable)
    {
        diffAntilag2.y = 99998.0f;
        diffAntilag2.w = 99999.0f;
    }

    uint32_t specCheckerboard = ((uint32_t)specSettings.checkerboardMode + 2) % 3;
    float4 specHitDistParams = float4(&specSettings.hitDistanceParameters.A);
    float4 specTrimmingParams_and_specBlurRadius = float4(specSettings.lobeTrimmingParameters.A, specSettings.lobeTrimmingParameters.B, specSettings.lobeTrimmingParameters.C, specSettings.blurRadius * m_CommonSettings.resolutionScale);
    float4 specAntilag1 = float4(specSettings.antilagIntensitySettings.sigmaScale / m_CommonSettings.resolutionScale, specSettings.antilagHitDistanceSettings.sigmaScale / m_CommonSettings.resolutionScale, specSettings.antilagIntensitySettings.sensitivityToDarkness, specSettings.antilagHitDistanceSettings.sensitivityToDarkness);
    float4 specAntilag2 = float4(specSettings.antilagIntensitySettings.thresholdMin / m_CommonSettings.resolutionScale, specSettings.antilagHitDistanceSettings.thresholdMin / m_CommonSettings.resolutionScale, specSettings.antilagIntensitySettings.thresholdMax, specSettings.antilagHitDistanceSettings.thresholdMax);

    if (!specSettings.antilagIntensitySettings.enable)
    {
        specAntilag2.x = 99998.0f;
        specAntilag2.z = 99999.0f;
    }

    if (!specSettings.antilagHitDistanceSettings.enable)
    {
        specAntilag2.y = 99998.0f;
        specAntilag2.w = 99999.0f;
    }

    // PRE_BLUR
    Constant* data = PushDispatch(methodData, AsUint(skipPreBlur ? Dispatch::COPY_VIEWZ : Dispatch::PRE_BLUR));
    if (skipPreBlur)
        AddFloat(data, m_CommonSettings.debug);
    else
    {
        AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
        AddFloat4x4(data, m_WorldToView);
        AddFloat4(data, m_Rotator[0]);
        AddFloat4(data, diffHitDistParams);
        AddFloat4(data, specHitDistParams);
        AddFloat4(data, specTrimmingParams_and_specBlurRadius);
        AddUint(data, specCheckerboard);
        AddFloat(data, diffBlurRadius);
        AddUint(data, diffCheckerboard);
    }
    ValidateConstants(data);

    // TEMPORAL_ACCUMULATION
    data = PushDispatch(methodData, AsUint(skipPreBlur ? Dispatch::TEMPORAL_ACCUMULATION_WITHOUT_PRE_BLUR : Dispatch::TEMPORAL_ACCUMULATION));
    AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4(data, float4(m_CameraDeltaSmoothed));
    AddFloat4(data, specHitDistParams);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, m_CommonSettings.disocclusionThreshold);
    AddFloat(data, float(diffSettings.maxFastAccumulatedFrameNum));
    AddUint(data, diffCheckerboard);
    AddFloat(data, float(specSettings.maxFastAccumulatedFrameNum));
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
    AddUint2(data, methodData.desc.fullResolutionWidth, methodData.desc.fullResolutionHeight);
    AddFloat(data, float(diffSettings.maxFastAccumulatedFrameNum));
    AddFloat(data, float(specSettings.maxFastAccumulatedFrameNum));
    AddUint(data, diffSettings.antifirefly ? 1 : 0);
    AddUint(data, specSettings.antifirefly ? 1 : 0);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[1]);
    AddFloat4(data, diffHitDistParams);
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, specTrimmingParams_and_specBlurRadius);
    AddFloat(data, diffBlurRadius);
    ValidateConstants(data);

    // POST_BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::POST_BLUR));
    AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[2]);
    AddFloat4(data, diffHitDistParams);
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, specTrimmingParams_and_specBlurRadius);
    AddFloat(data, specSettings.maxAdaptiveRadiusScale);
    AddFloat(data, diffBlurRadius);
    AddFloat(data, diffSettings.maxAdaptiveRadiusScale);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, float4(m_CameraDeltaSmoothed));
    AddFloat4(data, diffHitDistParams);
    AddFloat4(data, diffAntilag1 );
    AddFloat4(data, diffAntilag2 );
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, specAntilag1 );
    AddFloat4(data, specAntilag2 );
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    ValidateConstants(data);

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_ReblurDiffuseSpecular(methodData, settings, data);
        AddFloat4(data, diffHitDistParams);
        AddFloat4(data, specHitDistParams);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);
    }
}

void DenoiserImpl::AddSharedConstants_ReblurDiffuseSpecular(const MethodData& methodData, const ReblurDiffuseSpecularSettings& settings, Constant*& data)
{
    uint32_t screenW = methodData.desc.fullResolutionWidth;
    uint32_t screenH = methodData.desc.fullResolutionHeight;
    uint32_t rectW = uint32_t(screenW * m_CommonSettings.resolutionScale + 0.5f);
    uint32_t rectH = uint32_t(screenH * m_CommonSettings.resolutionScale + 0.5f);
    uint32_t rectWprev = uint32_t(screenW * m_ResolutionScalePrev + 0.5f);
    uint32_t rectHprev = uint32_t(screenH * m_ResolutionScalePrev + 0.5f);
    float planeDistanceSensitivity = Min( settings.diffuseSettings.planeDistanceSensitivity, settings.specularSettings.planeDistanceSensitivity );
    float diffMaxAccumulatedFrameNum = float( Min(settings.diffuseSettings.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM) );
    float specMaxAccumulatedFrameNum = float( Min(settings.specularSettings.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM) );

    // DRS will increase reprojected values, needed for stability, compensated by blur radius adjustment
    float unproject = 1.0f / (0.5f * rectH * m_ProjectY);

    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);
    AddFloat2(data, 1.0f / float(screenW), 1.0f / float(screenH));
    AddFloat2(data, float(screenW), float(screenH));
    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat2(data, float(rectW), float(rectH));
    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));
    AddFloat2(data, float(m_CommonSettings.inputDataOrigin[0]) / float(screenW), float(m_CommonSettings.inputDataOrigin[1]) / float(screenH));
    AddUint2(data, m_CommonSettings.inputDataOrigin[0], m_CommonSettings.inputDataOrigin[1]);
    AddFloat(data, m_CommonSettings.forceReferenceAccumulation ? 1.0f : 0.0f);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.debug);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, 1.0f / planeDistanceSensitivity);
    AddFloat(data, m_FrameRateScale);
    AddFloat(data, m_JitterDelta);
    AddFloat(data, diffMaxAccumulatedFrameNum);
    AddFloat(data, specMaxAccumulatedFrameNum);
    AddUint(data, m_CommonSettings.worldSpaceMotion ? 1 : 0);
    AddUint(data, m_CommonSettings.frameIndex);
}
