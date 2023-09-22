/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::InstanceImpl::Add_ReblurSpecular(DenoiserData& denoiserData)
{
    #define DENOISER_NAME REBLUR_Specular
    #define SPEC_TEMP1 AsUint(Transient::SPEC_TMP1)
    #define SPEC_TEMP2 AsUint(Transient::SPEC_TMP2)

    denoiserData.settings.reblur = ReblurSettings();
    denoiserData.settingsSize = sizeof(denoiserData.settings.reblur);

    uint16_t w = denoiserData.desc.renderWidth;
    uint16_t h = denoiserData.desc.renderHeight;
    uint16_t tilesW = DivideUp(w, 16);
    uint16_t tilesH = DivideUp(h, 16);

    enum class Permanent
    {
        PREV_VIEWZ = PERMANENT_POOL_START,
        PREV_NORMAL_ROUGHNESS,
        PREV_INTERNAL_DATA,
        SPEC_HISTORY,
        SPEC_FAST_HISTORY,
        SPEC_HITDIST_FOR_TRACKING_PING,
        SPEC_HITDIST_FOR_TRACKING_PONG,
    };

    AddTextureToPermanentPool( {REBLUR_FORMAT_PREV_VIEWZ, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_PREV_NORMAL_ROUGHNESS, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_PREV_INTERNAL_DATA, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_FAST_HISTORY, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_HITDIST_FOR_TRACKING, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_HITDIST_FOR_TRACKING, w, h, 1} );

    enum class Transient
    {
        DATA1 = TRANSIENT_POOL_START,
        DATA2,
        SPEC_HITDIST_FOR_TRACKING,
        SPEC_TMP1,
        SPEC_TMP2,
        SPEC_FAST_HISTORY,
        TILES,
    };

    AddTextureToTransientPool( {Format::RG8_UNORM, w, h, 1} );
    AddTextureToTransientPool( {Format::R32_UINT, w, h, 1} );
    AddTextureToTransientPool( {REBLUR_FORMAT_HITDIST_FOR_TRACKING, w, h, 1} );
    AddTextureToTransientPool( {REBLUR_FORMAT, w, h, 1} );
    AddTextureToTransientPool( {REBLUR_FORMAT, w, h, 1} );
    AddTextureToTransientPool( {REBLUR_FORMAT_FAST_HISTORY, w, h, 1} );
    AddTextureToTransientPool( {Format::R8_UNORM, tilesW, tilesH, 1} );

    REBLUR_SET_SHARED_CONSTANTS;

    for (int i = 0; i < REBLUR_CLASSIFY_TILES_PERMUTATION_NUM; i++)
    {
        PushPass("Classify tiles");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_VIEWZ) );

            // Outputs
            PushOutput( AsUint(Transient::TILES) );

            // Shaders
            AddDispatch( REBLUR_ClassifyTiles, REBLUR_CLASSIFY_TILES_CONSTANT_NUM, REBLUR_CLASSIFY_TILES_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_HITDIST_RECONSTRUCTION_PERMUTATION_NUM; i++)
    {
        bool is5x5 = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isPrepassEnabled = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Hit distance reconstruction");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );

            // Outputs
            PushOutput( isPrepassEnabled ? SPEC_TEMP2 : SPEC_TEMP1 );

            // Shaders
            if (is5x5)
            {
                AddDispatch( REBLUR_Specular_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_Specular_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
            }
            else
            {
                AddDispatch( REBLUR_Specular_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_Specular_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_PREPASS_PERMUTATION_NUM; i++)
    {
        bool isAfterReconstruction  = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Pre-pass");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( isAfterReconstruction ? SPEC_TEMP2 : AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );

            // Outputs
            PushOutput( SPEC_TEMP1 );
            PushOutput( AsUint(Transient::SPEC_HITDIST_FOR_TRACKING) );

            // Shaders
            AddDispatch( REBLUR_Specular_PrePass, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_Specular_PrePass, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_TEMPORAL_ACCUMULATION_PERMUTATION_NUM; i++)
    {
        bool hasDisocclusionThresholdMix = ( ( ( i >> 3 ) & 0x1 ) != 0 );
        bool isTemporalStabilization = ( ( ( i >> 2 ) & 0x1 ) != 0 );
        bool hasConfidenceInputs = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isAfterPrepass = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Temporal accumulation");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );
            PushInput( AsUint(Permanent::PREV_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Permanent::PREV_INTERNAL_DATA) );
            PushInput( hasDisocclusionThresholdMix ? AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) : REBLUR_DUMMY );
            PushInput( hasConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : REBLUR_DUMMY );
            PushInput( isAfterPrepass ? SPEC_TEMP1 : AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );
            PushInput( isTemporalStabilization ? AsUint(Permanent::SPEC_HISTORY) : AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) );
            PushInput( AsUint(Permanent::SPEC_FAST_HISTORY) );
            PushInput( AsUint(Permanent::SPEC_HITDIST_FOR_TRACKING_PING), 0, 1, AsUint(Permanent::SPEC_HITDIST_FOR_TRACKING_PONG) );
            PushInput( AsUint(Transient::SPEC_HITDIST_FOR_TRACKING) );

            // Outputs
            PushOutput( SPEC_TEMP2 );
            PushOutput( AsUint(Transient::SPEC_FAST_HISTORY) );
            PushOutput( AsUint(Permanent::SPEC_HITDIST_FOR_TRACKING_PONG), 0, 1, AsUint(Permanent::SPEC_HITDIST_FOR_TRACKING_PING) );
            PushOutput( AsUint(Transient::DATA1) );
            PushOutput( AsUint(Transient::DATA2) );

            // Shaders
            AddDispatch( REBLUR_Specular_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_Specular_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_HISTORY_FIX_PERMUTATION_NUM; i++)
    {
        PushPass("History fix");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( SPEC_TEMP2 );
            PushInput( AsUint(Transient::SPEC_FAST_HISTORY) );

            // Outputs
            PushOutput( SPEC_TEMP1 );
            PushOutput( AsUint(Permanent::SPEC_FAST_HISTORY) );

            AddDispatch( REBLUR_Specular_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_Specular_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_BLUR_PERMUTATION_NUM; i++)
    {
        PushPass("Blur");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( SPEC_TEMP1 );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );

            // Outputs
            PushOutput( SPEC_TEMP2 );
            PushOutput( AsUint(Permanent::PREV_VIEWZ) );

            // Shaders
            AddDispatch( REBLUR_Specular_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_Specular_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_POST_BLUR_PERMUTATION_NUM; i++)
    {
        bool isTemporalStabilization = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Post-blur");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( SPEC_TEMP2 );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );

            // Outputs
            PushOutput( AsUint(Permanent::PREV_NORMAL_ROUGHNESS) );

            if (isTemporalStabilization)
                PushOutput( AsUint(Permanent::SPEC_HISTORY) );
            else
            {
                PushOutput( AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) );
                PushOutput( AsUint(Permanent::PREV_INTERNAL_DATA) );
            }

            // Shaders
            if (isTemporalStabilization)
            {
                AddDispatch( REBLUR_Specular_PostBlur, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_Specular_PostBlur, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
            }
            else
            {
                AddDispatch( REBLUR_Specular_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_Specular_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_COPY_STABILIZED_HISTORY_PERMUTATION_NUM; i++)
    {
        PushPass("Copy stabilized history");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) );

            // Outputs
            PushOutput( SPEC_TEMP2 );

            // Shaders
            AddDispatch( REBLUR_Specular_CopyStabilizedHistory, REBLUR_COPY_STABILIZED_HISTORY_CONSTANT_NUM, REBLUR_COPY_STABILIZED_HISTORY_NUM_THREADS, USE_MAX_DIMS );
        }
    }

    for (int i = 0; i < REBLUR_TEMPORAL_STABILIZATION_PERMUTATION_NUM; i++)
    {
        bool hasRf0AndMetalness = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Temporal stabilization");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( hasRf0AndMetalness ? AsUint(ResourceType::IN_BASECOLOR_METALNESS) : REBLUR_DUMMY );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( AsUint(Transient::DATA2) );
            PushInput( AsUint(Permanent::SPEC_HISTORY) );
            PushInput( SPEC_TEMP2 );
            PushInput( AsUint(Permanent::SPEC_HITDIST_FOR_TRACKING_PONG), 0, 1, AsUint(Permanent::SPEC_HITDIST_FOR_TRACKING_PING) );

            // Outputs
            PushOutput( AsUint(ResourceType::IN_MV) );
            PushOutput( AsUint(Permanent::PREV_INTERNAL_DATA) );
            PushOutput( AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) );

            // Shaders
            AddDispatch( REBLUR_Specular_TemporalStabilization, REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM, REBLUR_TEMPORAL_STABILIZATION_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_Specular_TemporalStabilization, REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM, REBLUR_TEMPORAL_STABILIZATION_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_SPLIT_SCREEN_PERMUTATION_NUM; i++)
    {
        PushPass("Split screen");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );

            // Outputs
            PushOutput( AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) );

            // Shaders
            AddDispatch( REBLUR_Specular_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_NUM_THREADS, 1 );
        }
    }

    REBLUR_ADD_VALIDATION_DISPATCH( Transient::DATA2, ResourceType::IN_SPEC_RADIANCE_HITDIST, ResourceType::IN_SPEC_RADIANCE_HITDIST );

    #undef DENOISER_NAME
    #undef SPEC_TEMP1
    #undef SPEC_TEMP2
}
