/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::InstanceImpl::Add_SigmaShadow(DenoiserData& denoiserData)
{
    #define DENOISER_NAME SIGMA_Shadow

    denoiserData.settings.sigma = SigmaSettings();
    denoiserData.settingsSize = sizeof(denoiserData.settings.sigma);

    uint16_t w = denoiserData.desc.renderWidth;
    uint16_t h = denoiserData.desc.renderHeight;
    uint16_t tilesW = DivideUp(w, 16);
    uint16_t tilesH = DivideUp(h, 16);

    enum class Transient
    {
        DATA_1 = TRANSIENT_POOL_START,
        DATA_2,
        TEMP_1,
        TEMP_2,
        HISTORY,
        TILES,
        SMOOTHED_TILES,
    };

    AddTextureToTransientPool( {Format::RG16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::RG16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::R8_UNORM, w, h, 1} );
    AddTextureToTransientPool( {Format::R8_UNORM, w, h, 1} );
    AddTextureToTransientPool( {Format::R8_UNORM, w, h, 1} );
    AddTextureToTransientPool( {Format::RGBA8_UNORM, tilesW, tilesH, 1} );
    AddTextureToTransientPool( {Format::RG8_UNORM, tilesW, tilesH, 1} );

    SIGMA_SET_SHARED_CONSTANTS;

    PushPass("Classify tiles");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );

        PushOutput( AsUint(Transient::TILES) );

        AddDispatch( SIGMA_Shadow_ClassifyTiles, SIGMA_CLASSIFY_TILES_SET_CONSTANTS, SIGMA_CLASSIFY_TILES_NUM_THREADS, 16 );
    }

    PushPass("Smooth tiles");
    {
        PushInput( AsUint(Transient::TILES) );

        PushOutput( AsUint(Transient::SMOOTHED_TILES) );

        AddDispatch( SIGMA_Shadow_SmoothTiles, SIGMA_SMOOTH_TILES_SET_CONSTANTS, SIGMA_SMOOTH_TILES_NUM_THREADS, 16 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(Transient::SMOOTHED_TILES) );
        PushInput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(Transient::DATA_1) );
        PushOutput( AsUint(Transient::TEMP_1) );
        PushOutput( AsUint(Transient::HISTORY) );

        AddDispatch( SIGMA_Shadow_Blur, SIGMA_BLUR_SET_CONSTANTS, SIGMA_BLUR_NUM_THREADS, USE_MAX_DIMS );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::DATA_1) );
        PushInput( AsUint(Transient::SMOOTHED_TILES) );
        PushInput( AsUint(Transient::TEMP_1) );

        PushOutput( AsUint(Transient::DATA_2) );
        PushOutput( AsUint(Transient::TEMP_2) );

        AddDispatch( SIGMA_Shadow_PostBlur, SIGMA_BLUR_SET_CONSTANTS, SIGMA_BLUR_NUM_THREADS, 1 );
    }

    PushPass("Temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::DATA_2) );
        PushInput( AsUint(Transient::TEMP_2) );
        PushInput( AsUint(Transient::HISTORY) );
        PushInput( AsUint(Transient::SMOOTHED_TILES) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_Shadow_TemporalStabilization, SIGMA_TEMPORAL_STABILIZATION_SET_CONSTANTS, SIGMA_TEMPORAL_STABILIZATION_NUM_THREADS, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_Shadow_SplitScreen, SIGMA_SPLIT_SCREEN_SET_CONSTANTS, SIGMA_SPLIT_SCREEN_NUM_THREADS, 1 );
    }

    #undef DENOISER_NAME
}
