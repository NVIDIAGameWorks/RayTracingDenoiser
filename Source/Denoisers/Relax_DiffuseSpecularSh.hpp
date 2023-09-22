/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::InstanceImpl::Add_RelaxDiffuseSpecularSh(DenoiserData& denoiserData)
{
    #define DENOISER_NAME RELAX_DiffuseSpecular

    denoiserData.settings.diffuseSpecularRelax = RelaxDiffuseSpecularSettings();
    denoiserData.settingsSize = sizeof(denoiserData.settings.diffuseSpecularRelax);

    uint16_t w = denoiserData.desc.renderWidth;
    uint16_t h = denoiserData.desc.renderHeight;
    uint16_t tilesW = DivideUp(w, 16);
    uint16_t tilesH = DivideUp(h, 16);

    enum class Permanent
    {
        SPEC_ILLUM_PREV = PERMANENT_POOL_START,
        SPEC_ILLUM_PREV_SH1,
        DIFF_ILLUM_PREV,
        DIFF_ILLUM_PREV_SH1,
        SPEC_ILLUM_RESPONSIVE_PREV,
        SPEC_ILLUM_RESPONSIVE_PREV_SH1,
        DIFF_ILLUM_RESPONSIVE_PREV,
        DIFF_ILLUM_RESPONSIVE_PREV_SH1,
        REFLECTION_HIT_T_CURR,
        REFLECTION_HIT_T_PREV,
        HISTORY_LENGTH_PREV,
        NORMAL_ROUGHNESS_PREV,
        MATERIAL_ID_PREV,
        VIEWZ_PREV,
    };

    AddTextureToPermanentPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::R16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::R16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::R8_UNORM, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA8_UNORM, w, h, 1} );
    AddTextureToPermanentPool( {Format::R8_UNORM, w, h, 1} );
    AddTextureToPermanentPool( {Format::R32_SFLOAT, w, h, 1} );

    enum class Transient
    {
        SPEC_ILLUM_PING = TRANSIENT_POOL_START,
        SPEC_ILLUM_PING_SH1,
        SPEC_ILLUM_PONG,
        SPEC_ILLUM_PONG_SH1,
        DIFF_ILLUM_PING,
        DIFF_ILLUM_PING_SH1,
        DIFF_ILLUM_PONG,
        DIFF_ILLUM_PONG_SH1,
        SPEC_REPROJECTION_CONFIDENCE,
        TILES,
        HISTORY_LENGTH
    };

    AddTextureToTransientPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::R8_UNORM, w, h, 1} );
    AddTextureToTransientPool( {Format::R8_UNORM, tilesW, tilesH, 1} );
    AddTextureToTransientPool( {Format::R8_UNORM, w, h, 1} );

    RELAX_SET_SHARED_CONSTANTS;

    const uint32_t halfMaxPassNum = (RELAX_MAX_ATROUS_PASS_NUM - 2 + 1) / 2;

    PushPass("Classify tiles");
    {
        PushInput(AsUint(ResourceType::IN_VIEWZ));
        PushOutput(AsUint(Transient::TILES));

        AddDispatch(RELAX_ClassifyTiles, SumConstants(0, 0, 0, 1, false), NumThreads(16, 16), 1);
    }

    PushPass("Hit distance reconstruction 3x3"); // 3x3
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(ResourceType::IN_SPEC_SH0) );
        PushInput( AsUint(ResourceType::IN_DIFF_SH0) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

        AddDispatch( RELAX_DiffuseSpecular_HitDistReconstruction, SumConstants(0, 0, 0, 0), NumThreads(8, 8), 1 );
    }

    PushPass("Hit distance reconstruction 5x5"); // 5x5
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(ResourceType::IN_SPEC_SH0) );
        PushInput( AsUint(ResourceType::IN_DIFF_SH0) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput(AsUint(Transient::SPEC_ILLUM_PING));
        PushOutput(AsUint(Transient::DIFF_ILLUM_PING));

        AddDispatch( RELAX_DiffuseSpecular_HitDistReconstruction_5x5, SumConstants(0, 0, 0, 0), NumThreads(8, 8), 1 );
    }

    PushPass("Pre-pass"); // After hit distance reconstruction
    {
        // Does preblur (if enabled) and checkerboard reconstruction (if enabled)
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(Transient::SPEC_ILLUM_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_SH1) );
        PushInput( AsUint(ResourceType::IN_DIFF_SH1) );

        PushOutput( AsUint(ResourceType::OUT_SPEC_SH0) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_SH0) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_SH1) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_SH1) );

        AddDispatch( RELAX_DiffuseSpecularSh_PrePass, SumConstants(0, 1, 0, 10), NumThreads(16, 16), 1 );
    }

    PushPass("Pre-pass"); // Without hit distance reconstruction
    {
        // Does preblur (if enabled) and checkerboard reconstruction
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(ResourceType::IN_SPEC_SH0) );
        PushInput( AsUint(ResourceType::IN_DIFF_SH0) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_SH1) );
        PushInput( AsUint(ResourceType::IN_DIFF_SH1) );

        PushOutput( AsUint(ResourceType::OUT_SPEC_SH0) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_SH0) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_SH1) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_SH1) );

        AddDispatch( RELAX_DiffuseSpecularSh_PrePass, SumConstants(0, 1, 0, 10), NumThreads(16, 16), 1 );
    }

    for (int i = 0; i < 4; i++)
    {
        // The following passes are defined here:
        // TEMPORAL_ACCUMULATION,
        // TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS,
        // TEMPORAL_ACCUMULATION_WITH_THRESHOLD_MIX,
        // TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS_WITH_THRESHOLD_MIX

        PushPass("Temporal accumulation");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::OUT_SPEC_SH0) );
            PushInput( AsUint(ResourceType::OUT_DIFF_SH0) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
            PushInput( AsUint(Permanent::SPEC_ILLUM_PREV) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
            PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
            PushInput( AsUint(Permanent::VIEWZ_PREV) );
            PushInput( AsUint(Permanent::REFLECTION_HIT_T_PREV), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_CURR) );
            PushInput( AsUint(Permanent::HISTORY_LENGTH_PREV) );
            PushInput( AsUint(Permanent::MATERIAL_ID_PREV) );

            // Confidence inputs:
            if (i == 0)
            {
                PushInput( AsUint(ResourceType::IN_VIEWZ) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(ResourceType::IN_VIEWZ) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(Permanent::HISTORY_LENGTH_PREV) ); // Bogus input that will not be fetched anyway
            }
            if (i == 1)
            {
                PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE) );
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
                PushInput( AsUint(Permanent::HISTORY_LENGTH_PREV) ); // Bogus input that will not be fetched anyway
            }
            if (i == 2)
            {
                PushInput( AsUint(ResourceType::IN_VIEWZ) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(ResourceType::IN_VIEWZ) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) );
            }
            if (i == 3)
            {
                PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE) );
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
                PushInput( AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) );
            }
            PushInput( AsUint(ResourceType::OUT_SPEC_SH1) );
            PushInput( AsUint(ResourceType::OUT_DIFF_SH1) );
            PushInput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV_SH1) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV_SH1) );
            PushInput( AsUint(Permanent::SPEC_ILLUM_PREV_SH1) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_PREV_SH1) );

            PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushOutput( AsUint(Transient::SPEC_ILLUM_PONG) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushOutput( AsUint(Permanent::REFLECTION_HIT_T_CURR), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_PREV) );
            PushOutput( AsUint(Transient::HISTORY_LENGTH) );
            PushOutput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushOutput( AsUint(Transient::SPEC_ILLUM_PING_SH1) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PING_SH1) );
            PushOutput( AsUint(Transient::SPEC_ILLUM_PONG_SH1) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PONG_SH1) );

            AddDispatch(RELAX_DiffuseSpecularSh_TemporalAccumulation, SumConstants(0, 0, 0, 13), NumThreads(8, 8), 1);
        }
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(Transient::SPEC_ILLUM_PING) ); // Normal history
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Transient::HISTORY_LENGTH) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(Transient::SPEC_ILLUM_PING_SH1) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PING_SH1) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_PONG) ); // Responsive history
        PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );
        PushOutput( AsUint(Transient::SPEC_ILLUM_PONG_SH1) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_PONG_SH1) );

        AddDispatch( RELAX_DiffuseSpecularSh_HistoryFix, SumConstants(0, 0, 0, 8), NumThreads(8, 8), 1 );
    }

    PushPass("History clamping");
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(ResourceType::OUT_SPEC_SH0) ); // Noisy input with preblur applied
        PushInput( AsUint(ResourceType::OUT_DIFF_SH0) );
        PushInput( AsUint(Transient::SPEC_ILLUM_PING) ); // Normal history
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Transient::SPEC_ILLUM_PONG) ); // Responsive history
        PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
        PushInput( AsUint(Transient::HISTORY_LENGTH) );
        PushInput( AsUint(Transient::SPEC_ILLUM_PING_SH1) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PING_SH1) );
        PushInput( AsUint(Transient::SPEC_ILLUM_PONG_SH1) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PONG_SH1) );

        PushOutput( AsUint(Permanent::SPEC_ILLUM_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushOutput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::HISTORY_LENGTH_PREV) );
        PushOutput( AsUint(Permanent::SPEC_ILLUM_PREV_SH1) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV_SH1) );
        PushOutput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV_SH1) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV_SH1) );

        AddDispatch( RELAX_DiffuseSpecularSh_HistoryClamping, SumConstants(0, 0, 0, 8), NumThreads(8, 8), 1 );
    }

    PushPass("Copy");
    {
        PushInput( AsUint(Permanent::SPEC_ILLUM_PREV) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_SH0) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_SH0) );

        AddDispatch( RELAX_DiffuseSpecularSh_Copy, SumConstants(0, 0, 0, 0), NumThreads(8, 8), 1 );
    }

    PushPass("Anti-firefly");
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(ResourceType::OUT_SPEC_SH0) );
        PushInput( AsUint(ResourceType::OUT_DIFF_SH0) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Permanent::SPEC_ILLUM_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );

        AddDispatch( RELAX_DiffuseSpecularSh_AntiFirefly, SumConstants(0, 0, 0, 0), NumThreads(16, 16), 1 );
    }

    for (int i = 0; i < 2; i++)
    {
        bool withConfidenceInputs = (i == 1);

        // A-trous (first)
        PushPass("A-trous (SMEM)");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(Permanent::SPEC_ILLUM_PREV) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
            PushInput( AsUint(Transient::HISTORY_LENGTH) );
            PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(Permanent::SPEC_ILLUM_PREV_SH1) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_PREV_SH1) );

            PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushOutput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
            PushOutput( AsUint(Permanent::MATERIAL_ID_PREV) );
            PushOutput( AsUint(Permanent::VIEWZ_PREV) );
            PushOutput( AsUint(Transient::SPEC_ILLUM_PING_SH1) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PING_SH1) );

            AddDispatch( RELAX_DiffuseSpecularSh_AtrousSmem, SumConstants(0, 0, 1, 19), NumThreads(8, 8), 1 );
        }

        // A-trous (odd)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(Transient::SPEC_ILLUM_PING) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushInput( AsUint(Transient::HISTORY_LENGTH) );
            PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(Transient::SPEC_ILLUM_PING_SH1) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PING_SH1) );

            PushOutput( AsUint(Transient::SPEC_ILLUM_PONG) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushOutput( AsUint(Transient::SPEC_ILLUM_PONG_SH1) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PONG_SH1) );

            AddDispatchRepeated( RELAX_DiffuseSpecularSh_Atrous, SumConstants(0, 0, 0, 19), NumThreads(16, 16), 1, halfMaxPassNum );
        }

        // A-trous (even)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(Transient::SPEC_ILLUM_PONG) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushInput( AsUint(Transient::HISTORY_LENGTH) );
            PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(Transient::SPEC_ILLUM_PONG_SH1) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PONG_SH1) );

            PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushOutput( AsUint(Transient::SPEC_ILLUM_PING_SH1) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PING_SH1) );

            AddDispatchRepeated( RELAX_DiffuseSpecularSh_Atrous, SumConstants(0, 0, 0, 19), NumThreads(16, 16), 1, halfMaxPassNum );
        }

        // A-trous (odd, last)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(Transient::SPEC_ILLUM_PING) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushInput( AsUint(Transient::HISTORY_LENGTH) );
            PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(Transient::SPEC_ILLUM_PING_SH1) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PING_SH1) );

            PushOutput( AsUint(ResourceType::OUT_SPEC_SH0) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_SH0) );
            PushOutput( AsUint(ResourceType::OUT_SPEC_SH1) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_SH1) );

            AddDispatch( RELAX_DiffuseSpecularSh_Atrous, SumConstants(0, 0, 0, 19), NumThreads(16, 16), 1 );
        }

        // A-trous (even, last)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(Transient::SPEC_ILLUM_PONG) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushInput( AsUint(Transient::HISTORY_LENGTH) );
            PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(Transient::SPEC_ILLUM_PONG_SH1) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PONG_SH1) );

            PushOutput( AsUint(ResourceType::OUT_SPEC_SH0) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_SH0) );
            PushOutput( AsUint(ResourceType::OUT_SPEC_SH1) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_SH1) );

            AddDispatch( RELAX_DiffuseSpecularSh_Atrous, SumConstants(0, 0, 0, 19), NumThreads(16, 16), 1 );
        }
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

        PushOutput( AsUint( ResourceType::OUT_SPEC_RADIANCE_HITDIST ) );
        PushOutput( AsUint( ResourceType::OUT_DIFF_RADIANCE_HITDIST ) );

        AddDispatch( RELAX_DiffuseSpecular_SplitScreen, SumConstants(0, 0, 0, 3), NumThreads(16, 16), 1 );
    }

    RELAX_ADD_VALIDATION_DISPATCH;

    #undef DENOISER_NAME
}

void nrd::InstanceImpl::Update_RelaxDiffuseSpecularSh(const DenoiserData& denoiserData)
{
    enum class Dispatch
    {
        CLASSIFY_TILES,
        HITDIST_RECONSTRUCTION_3x3,
        HITDIST_RECONSTRUCTION_5x5,
        PREPASS_AFTER_HITDIST_RECONSTRUCTION,
        PREPASS,
        TEMPORAL_ACCUMULATION,
        TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS,
        TEMPORAL_ACCUMULATION_WITH_THRESHOLD_MIX,
        TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS_WITH_THRESHOLD_MIX,
        HISTORY_FIX,
        HISTORY_CLAMPING,
        COPY,
        FIREFLY,
        ATROUS_SMEM,
        ATROUS_ODD,
        ATROUS_EVEN,
        ATROUS_ODD_LAST,
        ATROUS_EVEN_LAST,
        ATROUS_SMEM_WITH_CONFIDENCE_INPUTS,
        ATROUS_ODD_WITH_CONFIDENCE_INPUTS,
        ATROUS_EVEN_WITH_CONFIDENCE_INPUTS,
        ATROUS_ODD_LAST_WITH_CONFIDENCE_INPUTS,
        ATROUS_EVEN_LAST_WITH_CONFIDENCE_INPUTS,
        SPLIT_SCREEN,
        VALIDATION,
    };

    const RelaxDiffuseSpecularSettings& settings = denoiserData.settings.diffuseSpecularRelax;

    NRD_DECLARE_DIMS;

    float maxDiffuseLuminanceRelativeDifference = -ml::Log( ml::Saturate(settings.diffuseMinLuminanceWeight) );
    float maxSpecularLuminanceRelativeDifference = -ml::Log( ml::Saturate(settings.specularMinLuminanceWeight) );

    float disocclusionThreshold = m_CommonSettings.disocclusionThreshold + (1.0f + m_JitterDelta) / float(rectH);
    float disocclusionThresholdOrtho = disocclusionThreshold;
    float disocclusionThresholdAlternate = m_CommonSettings.disocclusionThresholdAlternate + (1.0f + m_JitterDelta) / float(rectH);
    float disocclusionThresholdAlternateOrtho = disocclusionThresholdAlternate;
    float depthThresholdOrtho = settings.depthThreshold;

    float tanHalfFov = 1.0f / m_ViewToClip.a00;
    float aspect = m_ViewToClip.a00 / m_ViewToClip.a11;
    ml::float3 frustumRight = m_WorldToView.GetRow0().To3d() * tanHalfFov;
    ml::float3 frustumUp = m_WorldToView.GetRow1().To3d() * tanHalfFov * aspect;
    ml::float3 frustumForward = RELAX_GetFrustumForward(m_ViewToWorld, m_Frustum);

    float prevTanHalfFov = 1.0f / m_ViewToClipPrev.a00;
    float prevAspect = m_ViewToClipPrev.a00 / m_ViewToClipPrev.a11;
    ml::float3 prevFrustumRight = m_WorldToViewPrev.GetRow0().To3d() * prevTanHalfFov;
    ml::float3 prevFrustumUp = m_WorldToViewPrev.GetRow1().To3d() * prevTanHalfFov * prevAspect;
    ml::float3 prevFrustumForward = RELAX_GetFrustumForward(m_ViewToWorldPrev, m_FrustumPrev);
    bool isCameraStatic = RELAX_IsCameraStatic(ml::float3(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z), frustumRight, frustumUp, frustumForward, prevFrustumRight, prevFrustumUp, prevFrustumForward);

    bool enableHitDistanceReconstruction = settings.hitDistanceReconstructionMode != HitDistanceReconstructionMode::OFF && settings.checkerboardMode == CheckerboardMode::OFF;

    // Checkerboard logic
    uint32_t specularCheckerboard = 2;
    uint32_t diffuseCheckerboard = 2;

    switch (settings.checkerboardMode)
    {
    case CheckerboardMode::BLACK:
        diffuseCheckerboard = 0;
        specularCheckerboard = 1;
        break;
    case CheckerboardMode::WHITE:
        diffuseCheckerboard = 1;
        specularCheckerboard = 0;
        break;
    default:
        break;
    }

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffuseCheckerboard);
        AddUint(data, specularCheckerboard);
        ValidateConstants(data);

        return;
    }

    // CLASSIFY_TILES
    Constant* data = PushDispatch(denoiserData, AsUint(Dispatch::CLASSIFY_TILES));
    AddFloat(data, m_CommonSettings.denoisingRange);
    ValidateConstants(data);

    // HITDIST_RECONSTRUCTION
    if (enableHitDistanceReconstruction)
    {
        bool is3x3 = settings.hitDistanceReconstructionMode == HitDistanceReconstructionMode::AREA_3X3;
        data = PushDispatch(denoiserData, is3x3 ? AsUint(Dispatch::HITDIST_RECONSTRUCTION_3x3) : AsUint(Dispatch::HITDIST_RECONSTRUCTION_5x5));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
        ValidateConstants(data);
    }

    // PREPASS
    data = PushDispatch(denoiserData, AsUint(enableHitDistanceReconstruction ? Dispatch::PREPASS_AFTER_HITDIST_RECONSTRUCTION : Dispatch::PREPASS));
    AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
    AddFloat4(data, m_Rotator_PrePass);
    AddUint(data, diffuseCheckerboard);
    AddUint(data, specularCheckerboard);
    AddFloat(data, settings.diffusePrepassBlurRadius);
    AddFloat(data, settings.specularPrepassBlurRadius);
    AddFloat(data, 1.0f);
    AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
    AddFloat(data, settings.diffuseLobeAngleFraction);
    AddFloat(data, settings.specularLobeAngleFraction);
    AddFloat(data, settings.specularLobeAngleSlack);
    AddFloat(data, settings.roughnessFraction);
    ValidateConstants(data);

    // TEMPORAL_ACCUMULATION
    if (!m_CommonSettings.isDisocclusionThresholdMixAvailable)
    {
        data = PushDispatch(
            denoiserData,
            AsUint(m_CommonSettings.isHistoryConfidenceAvailable ?
                Dispatch::TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS :
                Dispatch::TEMPORAL_ACCUMULATION));
    }
    else
    {
        data = PushDispatch(
            denoiserData,
            AsUint(m_CommonSettings.isHistoryConfidenceAvailable ?
                Dispatch::TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS_WITH_THRESHOLD_MIX :
                Dispatch::TEMPORAL_ACCUMULATION_WITH_THRESHOLD_MIX));
    }
    AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
    AddFloat(data, (float)settings.specularMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.specularMaxFastAccumulatedFrameNum);
    AddFloat(data, (float)settings.diffuseMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.diffuseMaxFastAccumulatedFrameNum);
    AddUint(data, diffuseCheckerboard);
    AddUint(data, specularCheckerboard);
    AddFloat(data, m_IsOrtho == 0 ? disocclusionThreshold : disocclusionThresholdOrtho);
    AddFloat(data, m_IsOrtho == 0 ? disocclusionThresholdAlternate : disocclusionThresholdAlternateOrtho);
    AddFloat(data, settings.roughnessFraction);
    AddFloat(data, settings.specularVarianceBoost);
    AddUint(data, settings.enableReprojectionTestSkippingWithoutMotion && isCameraStatic);
    AddUint(data, m_CommonSettings.isHistoryConfidenceAvailable ? 1 : 0);
    AddUint(data, m_CommonSettings.isDisocclusionThresholdMixAvailable ? 1 : 0);
    ValidateConstants(data);

    // HISTORY_FIX
    data = PushDispatch(denoiserData, AsUint(Dispatch::HISTORY_FIX));
    AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
    AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
    AddFloat(data, settings.historyFixEdgeStoppingNormalPower);
    AddFloat(data, settings.historyFixStrideBetweenSamples);
    AddFloat(data, float(settings.historyFixFrameNum));
    AddFloat(data, settings.specularLobeAngleFraction);
    AddFloat(data, ml::DegToRad(settings.specularLobeAngleSlack));
    AddFloat(data, settings.roughnessEdgeStoppingRelaxation);
    AddFloat(data, settings.normalEdgeStoppingRelaxation);
    ValidateConstants(data);


    // HISTORY_CLAMPING
    data = PushDispatch(denoiserData, AsUint(Dispatch::HISTORY_CLAMPING));
    AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
    AddFloat(data, settings.historyClampingColorBoxSigmaScale);
    AddFloat(data, float(settings.historyFixFrameNum));
    AddUint(data, settings.specularMaxFastAccumulatedFrameNum < settings.specularMaxAccumulatedFrameNum ? 1 : 0);
    AddUint(data, settings.diffuseMaxFastAccumulatedFrameNum < settings.diffuseMaxAccumulatedFrameNum ? 1 : 0);
    AddFloat(data, float(settings.antilagSettings.accelerationAmount));
    AddFloat(data, float(settings.antilagSettings.temporalSigmaScale));
    AddFloat(data, float(settings.antilagSettings.spatialSigmaScale));
    AddFloat(data, float(settings.antilagSettings.resetAmount));
    ValidateConstants(data);

    if (settings.enableAntiFirefly)
    {
        // COPY
        data = PushDispatch(denoiserData, AsUint(Dispatch::COPY));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
        ValidateConstants(data);

        // FIREFLY
        data = PushDispatch(denoiserData, AsUint(Dispatch::FIREFLY));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
        ValidateConstants(data);
    }

    // A-TROUS
    uint32_t iterationNum = ml::Clamp(settings.atrousIterationNum, 2u, RELAX_MAX_ATROUS_PASS_NUM);
    for (uint32_t i = 0; i < iterationNum; i++)
    {
        Dispatch dispatch;
        if (!m_CommonSettings.isHistoryConfidenceAvailable)
        {
            if (i == 0)
                dispatch = Dispatch::ATROUS_SMEM;
            else if (i == iterationNum - 1)
                dispatch = (i % 2 == 0) ? Dispatch::ATROUS_EVEN_LAST : Dispatch::ATROUS_ODD_LAST;
            else
                dispatch = (i % 2 == 0) ? Dispatch::ATROUS_EVEN : Dispatch::ATROUS_ODD;
        }
        else
        {
            if (i == 0)
                dispatch = Dispatch::ATROUS_SMEM_WITH_CONFIDENCE_INPUTS;
            else if (i == iterationNum - 1)
                dispatch = (i % 2 == 0) ? Dispatch::ATROUS_EVEN_LAST_WITH_CONFIDENCE_INPUTS : Dispatch::ATROUS_ODD_LAST_WITH_CONFIDENCE_INPUTS;
            else
                dispatch = (i % 2 == 0) ? Dispatch::ATROUS_EVEN_WITH_CONFIDENCE_INPUTS : Dispatch::ATROUS_ODD_WITH_CONFIDENCE_INPUTS;
        }

        data = PushDispatch(denoiserData, AsUint(dispatch));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);

        if (i == 0)
        {
            AddUint2(data, screenW, screenH); // For Atrous_shmem
            AddUint(data, settings.spatialVarianceEstimationHistoryThreshold);
        }

        AddFloat(data, settings.specularPhiLuminance);
        AddFloat(data, settings.diffusePhiLuminance);
        AddFloat(data, maxDiffuseLuminanceRelativeDifference);
        AddFloat(data, maxSpecularLuminanceRelativeDifference);
        AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
        AddFloat(data, settings.diffuseLobeAngleFraction);
        AddFloat(data, settings.roughnessFraction);
        AddFloat(data, settings.specularLobeAngleFraction);
        AddFloat(data, ml::DegToRad(settings.specularLobeAngleSlack));
        AddUint(data, 1 << i);
        AddUint(data, settings.enableRoughnessEdgeStopping);
        AddFloat(data, settings.roughnessEdgeStoppingRelaxation);
        AddFloat(data, settings.normalEdgeStoppingRelaxation);
        AddFloat(data, settings.luminanceEdgeStoppingRelaxation);
        AddUint(data, m_CommonSettings.isHistoryConfidenceAvailable ? 1 : 0);
        AddFloat(data, settings.confidenceDrivenRelaxationMultiplier);
        AddFloat(data, settings.confidenceDrivenLuminanceEdgeStoppingRelaxation);
        AddFloat(data, settings.confidenceDrivenNormalEdgeStoppingRelaxation);
        if (i != 0)
        {
            AddUint(data, (i == iterationNum - 1) ? 1 : 0);
        }
        ValidateConstants(data);
    }

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffuseCheckerboard);
        AddUint(data, specularCheckerboard);
        ValidateConstants(data);
    }

    // VALIDATION
    if (m_CommonSettings.enableValidation)
    {
        data = PushDispatch(denoiserData, AsUint(Dispatch::VALIDATION));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
        AddFloat4x4(data, m_WorldToClipPrev);
        AddFloat2(data, m_CommonSettings.cameraJitter[0], m_CommonSettings.cameraJitter[1]);
        AddFloat(data, (float)ml::Max(settings.diffuseMaxAccumulatedFrameNum, settings.specularMaxAccumulatedFrameNum));
        ValidateConstants(data);
    }
}
