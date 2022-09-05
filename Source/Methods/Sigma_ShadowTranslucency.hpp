/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_SigmaShadowTranslucency(uint16_t w, uint16_t h)
{
    #define METHOD_NAME SIGMA_ShadowTranslucency

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

    SIGMA_SET_SHARED_CONSTANTS;

    PushPass("Classify tiles");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(Transient::TILES) );

        AddDispatch( SIGMA_ShadowTranslucency_ClassifyTiles, SIGMA_CLASSIFY_TILES_SET_CONSTANTS, 1, 16 );
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
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(Transient::DATA_1) );
        PushOutput( AsUint(Transient::TEMP_1) );
        PushOutput( AsUint(Transient::HISTORY) );

        AddDispatch( SIGMA_ShadowTranslucency_Blur, SIGMA_BLUR_SET_CONSTANTS, 16, USE_MAX_DIMS );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::DATA_1) );
        PushInput( AsUint(Transient::TEMP_1) );

        PushOutput( AsUint(Transient::DATA_2) );
        PushOutput( AsUint(Transient::TEMP_2) );

        AddDispatch( SIGMA_ShadowTranslucency_PostBlur, SIGMA_BLUR_SET_CONSTANTS, 16, 1 );
    }

    PushPass("Temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::DATA_2) );
        PushInput( AsUint(Transient::TEMP_2) );
        PushInput( AsUint(Transient::HISTORY) );
        PushInput( AsUint(Transient::SMOOTH_TILES) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_ShadowTranslucency_TemporalStabilization, SIGMA_TEMPORAL_STABILIZATION_SET_CONSTANTS, 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_ShadowTranslucency_SplitScreen, SIGMA_SPLIT_SCREEN_SET_CONSTANTS, 16, 1 );
    }

    #undef METHOD_NAME

    return sizeof(SigmaSettings);
}
