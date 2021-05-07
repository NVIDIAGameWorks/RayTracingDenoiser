/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_ReblurDiffuse(uint16_t w, uint16_t h)
{
    enum class Permanent
    {
        PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS = PERMANENT_POOL_START,
        HISTORY,
        FAST_HISTORY_1,
        FAST_HISTORY_2,
        STABILIZED_HISTORY_1,
        STABILIZED_HISTORY_2,
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
        ACCUMULATED,
        ERROR,
    };

    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );

    SetSharedConstants(1, 2, 8, 12);

    // Tricks to save memory
    #define TEMP AsUint(Permanent::STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_2)

    PushPass("REBLUR::DiffuseSpecular - copy viewZ");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );

        AddDispatch( REBLUR_CopyViewZ, SumConstants(0, 0, 0, 1, false), 16, 1 );
    }

    PushPass("REBLUR::Diffuse - pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HIT) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( TEMP );

        AddDispatch( REBLUR_Diffuse_PreBlur, SumConstants(1, 1, 0, 3), 16, 1 );
    }

    // Temporal accumulation after pre-blur
    PushPass("REBLUR::Diffuse - temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(Permanent::HISTORY) );
        PushInput( AsUint(Permanent::FAST_HISTORY_2), 0, 1, AsUint(Permanent::FAST_HISTORY_1) );
        PushInput( TEMP );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ERROR) );
        PushOutput( AsUint(Transient::ACCUMULATED) );
        PushOutput( AsUint(Permanent::FAST_HISTORY_1), 0, 1, AsUint(Permanent::FAST_HISTORY_2) );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulation, SumConstants(3, 1, 1, 4), 16, 1 );
    }

    // Temporal accumulation after copy viewZ
    PushPass("REBLUR::Diffuse - temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(Permanent::HISTORY) );
        PushInput( AsUint(Permanent::FAST_HISTORY_2), 0, 1, AsUint(Permanent::FAST_HISTORY_1) );
        PushInput( AsUint(ResourceType::IN_DIFF_HIT) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ERROR) );
        PushOutput( AsUint(Transient::ACCUMULATED) );
        PushOutput( AsUint(Permanent::FAST_HISTORY_1), 0, 1, AsUint(Permanent::FAST_HISTORY_2) );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulation, SumConstants(3, 1, 1, 4), 16, 1 );
    }

    PushPass("REBLUR::Diffuse - history fix");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::ACCUMULATED) );
        PushInput( AsUint(Permanent::FAST_HISTORY_1), 0, 1, AsUint(Permanent::FAST_HISTORY_2) );

        PushOutput( TEMP );

        AddDispatch( REBLUR_Diffuse_HistoryFix, SumConstants(1, 1, 0, 2), 16, 1 );
    }

    PushPass("REBLUR::Diffuse - blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( TEMP );

        PushOutput( AsUint(Transient::ERROR) );
        PushOutput( AsUint(Transient::ACCUMULATED) );

        AddDispatch( REBLUR_Diffuse_Blur, SumConstants(1, 1, 0, 2), 16, 1 );
    }

    PushPass("REBLUR::Diffuse - post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::ACCUMULATED) );

        PushOutput( AsUint(Transient::ERROR) );
        PushOutput( AsUint(Permanent::HISTORY) );

        AddDispatch( REBLUR_Diffuse_PostBlur, SumConstants(1, 1, 0, 2), 16, 1 );
    }

    PushPass("REBLUR::Diffuse - temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Permanent::STABILIZED_HISTORY_2), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_1) );
        PushInput( AsUint(Permanent::HISTORY) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushOutput( AsUint(Permanent::STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_2) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_HIT) );

        AddDispatch( REBLUR_Diffuse_TemporalStabilization, SumConstants(2, 3, 1, 0), 16, 1 );
    }

    PushPass("REBLUR::Diffuse - split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HIT) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_HIT) );

        AddDispatch( REBLUR_Diffuse_SplitScreen, SumConstants(0, 0, 0, 2), 16, 1 );
    }

    #undef TEMP

    return sizeof(ReblurDiffuseSettings);
}

void DenoiserImpl::UpdateMethod_ReblurDiffuse(const MethodData& methodData)
{
    enum class Dispatch
    {
        COPY_VIEWZ,
        PRE_BLUR,
        TEMPORAL_ACCUMULATION,
        TEMPORAL_ACCUMULATION_WITHOUT_PRE_BLUR,
        HISTORY_FIX,
        BLUR,
        POST_BLUR,
        TEMPORAL_STABILIZATION,
        SPLIT_SCREEN,
    };

    const ReblurDiffuseSettings& settings = methodData.settings.diffuse;

    bool useCopyViewZ = !settings.usePrePass && settings.checkerboardMode == CheckerboardMode::OFF;
    float normalWeightStrictness = Lerp( 0.1f, 1.0f, settings.normalWeightStrictness );

    uint32_t diffCheckerboard = ((uint32_t)settings.checkerboardMode + 2) % 3;
    float4 diffAntilag1 = float4(settings.antilagIntensitySettings.sigmaScale / m_CommonSettings.resolutionScale, settings.antilagHitDistanceSettings.sigmaScale / m_CommonSettings.resolutionScale, settings.antilagIntensitySettings.sensitivityToDarkness, settings.antilagHitDistanceSettings.sensitivityToDarkness);
    float4 diffAntilag2 = float4(settings.antilagIntensitySettings.thresholdMin / m_CommonSettings.resolutionScale, settings.antilagHitDistanceSettings.thresholdMin / m_CommonSettings.resolutionScale, settings.antilagIntensitySettings.thresholdMax, settings.antilagHitDistanceSettings.thresholdMax);

    if (!settings.antilagIntensitySettings.enable)
    {
        diffAntilag2.x = 99998.0f;
        diffAntilag2.z = 99999.0f;
    }

    if (!settings.antilagHitDistanceSettings.enable)
    {
        diffAntilag2.y = 99998.0f;
        diffAntilag2.w = 99999.0f;
    }

    // PRE_BLUR
    Constant* data = PushDispatch(methodData, AsUint(useCopyViewZ ? Dispatch::COPY_VIEWZ : Dispatch::PRE_BLUR));
    if (useCopyViewZ)
        AddFloat(data, m_CommonSettings.debug);
    else
    {
        AddSharedConstants_ReblurDiffuse(methodData, settings, data);
        AddFloat4x4(data, m_WorldToView);
        AddFloat4(data, m_Rotator[0]);
        AddUint(data, diffCheckerboard);
        AddUint(data, settings.usePrePass ? 1 : 0);
        AddFloat(data, normalWeightStrictness);
    }
    ValidateConstants(data);

    // TEMPORAL_ACCUMULATION
    data = PushDispatch(methodData, AsUint(useCopyViewZ ? Dispatch::TEMPORAL_ACCUMULATION_WITHOUT_PRE_BLUR : Dispatch::TEMPORAL_ACCUMULATION));
    AddSharedConstants_ReblurDiffuse(methodData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, float4(m_CameraDeltaSmoothed));
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_JitterDelta );
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, m_CommonSettings.disocclusionThreshold );
    AddUint(data, diffCheckerboard);
    ValidateConstants(data);

    // HISTORY_FIX
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_FIX));
    AddSharedConstants_ReblurDiffuse(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[0]);
    AddFloat(data, settings.historyClampingColorBoxSigmaScale);
    AddUint(data, settings.antifirefly ? 1 : 0);
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

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddSharedConstants_ReblurDiffuse(methodData, settings, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, float4(m_CameraDeltaSmoothed));
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

void DenoiserImpl::AddSharedConstants_ReblurDiffuse(const MethodData& methodData, const ReblurDiffuseSettings& settings, Constant*& data)
{
    uint32_t screenW = methodData.desc.fullResolutionWidth;
    uint32_t screenH = methodData.desc.fullResolutionHeight;
    uint32_t rectW = uint32_t(screenW * m_CommonSettings.resolutionScale + 0.5f);
    uint32_t rectH = uint32_t(screenH * m_CommonSettings.resolutionScale + 0.5f);
    uint32_t rectWprev = uint32_t(screenW * m_ResolutionScalePrev + 0.5f);
    uint32_t rectHprev = uint32_t(screenH * m_ResolutionScalePrev + 0.5f);
    float maxAccumulatedFrameNum = float( Min(settings.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM) );
    float blurRadius = settings.blurRadius * m_CommonSettings.resolutionScale;
    float amount = m_CommonSettings.forceReferenceAccumulation ? 4.0f : Saturate( settings.stabilizationStrength );
    float frameRateScale = Max( m_FrameRateScale * amount, 2.0f / 16.0f );
    float4 diffHitDistParams = float4(&settings.hitDistanceParameters.A);    

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
    AddFloat2(data, float(m_CommonSettings.inputDataOrigin[0]) / float(screenW), float(m_CommonSettings.inputDataOrigin[1]) / float(screenH));
    AddUint2(data, m_CommonSettings.inputDataOrigin[0], m_CommonSettings.inputDataOrigin[1]);
    AddFloat(data, m_CommonSettings.forceReferenceAccumulation ? 1.0f : 0.0f);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.debug);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, 1.0f / settings.planeDistanceSensitivity);
    AddFloat(data, frameRateScale);
    AddFloat(data, blurRadius);
    AddFloat(data, maxAccumulatedFrameNum);
    AddFloat(data, float( settings.maxFastAccumulatedFrameNum ));
    AddUint(data, m_CommonSettings.worldSpaceMotion ? 1 : 0);
    AddUint(data, m_CommonSettings.frameIndex);
}
