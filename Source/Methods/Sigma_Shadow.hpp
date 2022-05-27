/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define SIGMA_SET_SHARED_CONSTANTS                    SetSharedConstants(1, 1, 9, 10)
#define SIGMA_CLASSIFY_TILES_SET_CONSTANTS            SumConstants(0, 0, 0, 0)
#define SIGMA_SMOOTH_TILES_SET_CONSTANTS              SumConstants(0, 0, 1, 0)
#define SIGMA_BLUR_SET_CONSTANTS                      SumConstants(1, 1, 0, 0)
#define SIGMA_TEMPORAL_STABILIZATION_SET_CONSTANTS    SumConstants(2, 0, 0, 1)
#define SIGMA_SPLIT_SCREEN_SET_CONSTANTS              SumConstants(0, 0, 0, 1)

size_t nrd::DenoiserImpl::AddMethod_SigmaShadow(uint16_t w, uint16_t h)
{
    #define METHOD_NAME SIGMA_Shadow

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
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );

    uint16_t tilesW = DivideUp(w, 16);
    uint16_t tilesH = DivideUp(h, 16);
    m_TransientPool.push_back( {Format::RG8_UNORM, tilesW, tilesH, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, tilesW, tilesH, 1} );

    SIGMA_SET_SHARED_CONSTANTS;

    PushPass("Classify tiles");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );

        PushOutput( AsUint(Transient::TILES) );

        AddDispatch( SIGMA_Shadow_ClassifyTiles, SIGMA_CLASSIFY_TILES_SET_CONSTANTS, 1, 16 );
    }

    PushPass("Smooth tiles");
    {
        PushInput( AsUint(Transient::TILES) );

        PushOutput( AsUint(Transient::SMOOTH_TILES) );

        AddDispatch( SIGMA_Shadow_SmoothTiles, SIGMA_SMOOTH_TILES_SET_CONSTANTS, 16, 16 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );
        PushInput( AsUint(Transient::SMOOTH_TILES) );

        PushOutput( AsUint(Transient::DATA_1) );
        PushOutput( AsUint(Transient::TEMP_1) );
        PushOutput( AsUint(Transient::HISTORY) );

        AddDispatch( SIGMA_Shadow_PrePass, SIGMA_BLUR_SET_CONSTANTS, 16, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::DATA_1) );
        PushInput( AsUint(Transient::TEMP_1) );

        PushOutput( AsUint(Transient::DATA_2) );
        PushOutput( AsUint(Transient::TEMP_2) );

        AddDispatch( SIGMA_Shadow_Blur, SIGMA_BLUR_SET_CONSTANTS, 16, 1 );
    }

    PushPass("Temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::DATA_2) );
        PushInput( AsUint(Transient::TEMP_2) );
        PushInput( AsUint(Transient::HISTORY) );
        PushInput( AsUint(Transient::SMOOTH_TILES) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_Shadow_TemporalStabilization, SIGMA_TEMPORAL_STABILIZATION_SET_CONSTANTS, 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_Shadow_SplitScreen, SIGMA_SPLIT_SCREEN_SET_CONSTANTS, 16, 1 );
    }

    #undef METHOD_NAME

    return sizeof(SigmaSettings);
}

void nrd::DenoiserImpl::UpdateMethod_SigmaShadow(const MethodData& methodData)
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

    const SigmaSettings& settings = methodData.settings.sigma;

    NRD_DECLARE_DIMS;

    uint16_t tilesW = DivideUp(rectW, 16);
    uint16_t tilesH = DivideUp(rectH, 16);

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Sigma(methodData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);

        return;
    }

    // CLASSIFY_TILES
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::CLASSIFY_TILES));
    AddSharedConstants_Sigma(methodData, settings, data);
    ValidateConstants(data);

    // SMOOTH_TILES
    data = PushDispatch(methodData, AsUint(Dispatch::SMOOTH_TILES));
    AddSharedConstants_Sigma(methodData, settings, data);
    AddUint2(data, tilesW, tilesH);
    ValidateConstants(data);

    // PRE_BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::PRE_BLUR));
    AddSharedConstants_Sigma(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[0]);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddSharedConstants_Sigma(methodData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[1]);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddSharedConstants_Sigma(methodData, settings, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);
    ValidateConstants(data);

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Sigma(methodData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);
    }
}

void nrd::DenoiserImpl::AddSharedConstants_Sigma(const MethodData& methodData, const SigmaSettings& settings, Constant*& data)
{
    NRD_DECLARE_DIMS;

    // Even with DRS keep radius, it works well for shadows
    float unproject = 1.0f / (0.5f * screenH * m_ProjectY);

    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);

    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat2(data, 1.0f / float(screenW), 1.0f / float(screenH));

    AddFloat2(data, float(screenW), float(screenH));
    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));

    AddFloat2(data, float(rectW), float(rectH));
    AddFloat2(data, float(rectWprev), float(rectHprev));

    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));
    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));

    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, unproject);

    AddFloat(data, m_CommonSettings.debug);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, settings.planeDistanceSensitivity);
    AddFloat(data, settings.blurRadiusScale);

    AddFloat(data, 0.0f);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, 0);
}
