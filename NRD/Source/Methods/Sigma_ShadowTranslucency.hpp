/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_SigmaShadowTranslucency(uint16_t w, uint16_t h)
{
    enum class Transient
    {
        DATA_1 = TRANSIENT_POOL_START,
        DATA_2,
        TEMP_1,
        TEMP_2,
        HISTORY
    };

    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );

    SetSharedConstants(1, 1, 9, 10);

    PushPass("SIGMA::ShadowTranscluency - pre blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(Transient::DATA_1) );
        PushOutput( AsUint(Transient::TEMP_1) );
        PushOutput( AsUint(Transient::HISTORY) );

        AddDispatch( SIGMA_ShadowTranslucency_PreBlur, SumConstants(1, 1, 0, 0), 16, 1 );
    }

    PushPass("SIGMA::ShadowTranscluency - blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::DATA_1) );
        PushInput( AsUint(Transient::TEMP_1) );

        PushOutput( AsUint(Transient::DATA_2) );
        PushOutput( AsUint(Transient::TEMP_2) );

        AddDispatch( SIGMA_ShadowTranslucency_Blur, SumConstants(1, 1, 0, 0), 16, 1 );
    }

    PushPass("SIGMA::ShadowTranscluency - temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::DATA_2) );
        PushInput( AsUint(Transient::TEMP_2) );
        PushInput( AsUint(Transient::HISTORY) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_ShadowTranslucency_TemporalStabilization, SumConstants(2, 0, 0, 0), 16, 1 );
    }

    PushPass("SIGMA::ShadowTranscluency - split screen");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_ShadowTranslucency_SplitScreen, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    return sizeof(SigmaShadowSettings);
}

void DenoiserImpl::UpdateMethod_SigmaShadowTranslucency(const MethodData& methodData)
{
    enum class Dispatch
    {
        PRE_BLUR,
        BLUR,
        TEMPORAL_STABILIZATION,
        SPLIT_SCREEN,
    };

    const SigmaShadowSettings& settings = methodData.settings.shadow;

    // PRE_BLUR
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PRE_BLUR));
    AddSharedConstants_SigmaShadowTranslucency(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[0]);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddSharedConstants_SigmaShadowTranslucency(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[1]);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddSharedConstants_SigmaShadowTranslucency(methodData, settings, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    ValidateConstants(data);

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_SigmaShadowTranslucency(methodData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);
    }
}

void DenoiserImpl::AddSharedConstants_SigmaShadowTranslucency(const MethodData& methodData, const SigmaShadowSettings& settings, Constant*& data)
{
    uint32_t screenW = methodData.desc.fullResolutionWidth;
    uint32_t screenH = methodData.desc.fullResolutionHeight;
    uint32_t rectW = uint32_t(screenW * m_CommonSettings.resolutionScale + 0.5f);
    uint32_t rectH = uint32_t(screenH * m_CommonSettings.resolutionScale + 0.5f);
    uint32_t rectWprev = uint32_t(screenW * m_ResolutionScalePrev + 0.5f);
    uint32_t rectHprev = uint32_t(screenH * m_ResolutionScalePrev + 0.5f);

    // Even with DRS keep radius, it works well for shadows
    float unproject = 1.0f / (0.5f * screenH * m_ProjectY);

    // TODO: it's needed due to history copying in PreBlur which can copy less than needed in case of DRS
    float historyCorrection = 1.0f / ml::Saturate(m_CommonSettings.resolutionScale / m_ResolutionScalePrev);

    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
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
    AddFloat(data, settings.blurRadiusScale);
    AddFloat(data, historyCorrection);
    AddUint(data, m_CommonSettings.worldSpaceMotion ? 1 : 0);
    AddUint(data, m_CommonSettings.frameIndex);
}
