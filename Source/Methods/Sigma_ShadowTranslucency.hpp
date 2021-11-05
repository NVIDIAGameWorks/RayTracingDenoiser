/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_SigmaShadowTranslucency(uint16_t w, uint16_t h)
{
    #define DENOISER_NAME "SIGMA::ShadowTranscluency"

    enum class Transient
    {
        DATA_1 = TRANSIENT_POOL_START,
        DATA_2,
        TEMP_1,
        TEMP_2,
        HISTORY,
        TILES,
        SMOOTH_TILES,
    };

    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );

    uint16_t tilesW = DivideUp(w, 16);
    uint16_t tilesH = DivideUp(h, 16);
    m_TransientPool.push_back( {Format::RG8_UNORM, tilesW, tilesH, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, tilesW, tilesH, 1} );

    SetSharedConstants(1, 1, 9, 14);

    PushPass("Classify tiles");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(Transient::TILES) );

        AddDispatch( SIGMA_ShadowTranslucency_ClassifyTiles, SumConstants(0, 0, 0, 0), 1, 16 );
    }

    PushPass("Smooth tiles");
    {
        PushInput( AsUint(Transient::TILES) );

        PushOutput( AsUint(Transient::SMOOTH_TILES) );

        AddDispatch( SIGMA_Shadow_SmoothTiles, SumConstants(0, 0, 1, 0), 16, 16 );
    }

    PushPass("Pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );
        PushInput( AsUint(Transient::SMOOTH_TILES) );
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(Transient::DATA_1) );
        PushOutput( AsUint(Transient::TEMP_1) );
        PushOutput( AsUint(Transient::HISTORY) );

        AddDispatch( SIGMA_ShadowTranslucency_PreBlur, SumConstants(1, 1, 0, 0), 16, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::DATA_1) );
        PushInput( AsUint(Transient::TEMP_1) );

        PushOutput( AsUint(Transient::DATA_2) );
        PushOutput( AsUint(Transient::TEMP_2) );

        AddDispatch( SIGMA_ShadowTranslucency_Blur, SumConstants(1, 1, 0, 0), 16, 1 );
    }

    PushPass("Temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::DATA_2) );
        PushInput( AsUint(Transient::TEMP_2) );
        PushInput( AsUint(Transient::HISTORY) );
        PushInput( AsUint(Transient::SMOOTH_TILES) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_ShadowTranslucency_TemporalStabilization, SumConstants(2, 0, 0, 1), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_ShadowTranslucency_SplitScreen, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    #undef DENOISER_NAME

    return sizeof(SigmaShadowSettings);
}

void nrd::DenoiserImpl::UpdateMethod_SigmaShadowTranslucency(const MethodData& methodData)
{
    enum class Dispatch
    {
        CLASSIFY_TILES,
        SMOOTH_TILES,
        PRE_BLUR,
        BLUR,
        TEMPORAL_STABILIZATION,
        SPLIT_SCREEN,
    };

    const SigmaShadowSettings& settings = methodData.settings.shadowSigma;

    NRD_DECLARE_DIMS;

    uint16_t tilesW = DivideUp(rectW, 16);
    uint16_t tilesH = DivideUp(rectH, 16);

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_SigmaShadow(methodData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);

        return;
    }

    // CLASSIFY_TILES
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::CLASSIFY_TILES));
    AddSharedConstants_SigmaShadow(methodData, settings, data);
    ValidateConstants(data);

    // SMOOTH_TILES
    data = PushDispatch(methodData, AsUint(Dispatch::SMOOTH_TILES));
    AddSharedConstants_SigmaShadow(methodData, settings, data);
    AddUint2(data, tilesW, tilesH);
    ValidateConstants(data);

    // PRE_BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::PRE_BLUR));
    AddSharedConstants_SigmaShadow(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[0]);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddSharedConstants_SigmaShadow(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[1]);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddSharedConstants_SigmaShadow(methodData, settings, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);
    ValidateConstants(data);

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_SigmaShadow(methodData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);
    }
}
