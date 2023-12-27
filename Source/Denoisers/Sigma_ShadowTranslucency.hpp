/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::InstanceImpl::Add_SigmaShadowTranslucency(nrd::DenoiserData& denoiserData)
{
    #define DENOISER_NAME SIGMA_ShadowTranslucency

    denoiserData.settings.sigma = SigmaSettings();
    denoiserData.settingsSize = sizeof(denoiserData.settings.sigma);
            
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

    AddTextureToTransientPool( {Format::RG16_SFLOAT, 1} );
    AddTextureToTransientPool( {Format::RG16_SFLOAT, 1} );
    AddTextureToTransientPool( {Format::RGBA8_UNORM, 1} );
    AddTextureToTransientPool( {Format::RGBA8_UNORM, 1} );
    AddTextureToTransientPool( {Format::RGBA8_UNORM, 1} );
    AddTextureToTransientPool( {Format::RGBA8_UNORM, 16} );
    AddTextureToTransientPool( {Format::RG8_UNORM, 16} );

    PushPass("Classify tiles");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(Transient::TILES) );

        AddDispatch( SIGMA_ShadowTranslucency_ClassifyTiles, SIGMA_ClassifyTiles, 1 );
    }

    PushPass("Smooth tiles");
    {
        PushInput( AsUint(Transient::TILES) );

        PushOutput( AsUint(Transient::SMOOTHED_TILES) );

        AddDispatch( SIGMA_Shadow_SmoothTiles, SIGMA_SmoothTiles, 16 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(Transient::SMOOTHED_TILES) );
        PushInput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(Transient::DATA_1) );
        PushOutput( AsUint(Transient::TEMP_1) );
        PushOutput( AsUint(Transient::HISTORY) );

        AddDispatch( SIGMA_ShadowTranslucency_Blur, SIGMA_Blur, USE_MAX_DIMS );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::DATA_1) );
        PushInput( AsUint(Transient::SMOOTHED_TILES) );
        PushInput( AsUint(Transient::TEMP_1) );

        PushOutput( AsUint(Transient::DATA_2) );
        PushOutput( AsUint(Transient::TEMP_2) );

        AddDispatch( SIGMA_ShadowTranslucency_PostBlur, SIGMA_Blur, 1 );
    }

    PushPass("Temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::DATA_2) );
        PushInput( AsUint(Transient::TEMP_2) );
        PushInput( AsUint(Transient::HISTORY) );
        PushInput( AsUint(Transient::SMOOTHED_TILES) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_ShadowTranslucency_TemporalStabilization, SIGMA_TemporalStabilization, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_SHADOWDATA) );
        PushInput( AsUint(ResourceType::IN_SHADOW_TRANSLUCENCY) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW_TRANSLUCENCY) );

        AddDispatch( SIGMA_ShadowTranslucency_SplitScreen, SIGMA_SplitScreen, 1 );
    }

    #undef DENOISER_NAME
}
