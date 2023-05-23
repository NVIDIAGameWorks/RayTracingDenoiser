/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::InstanceImpl::Add_ReblurDiffuseSpecularOcclusion(DenoiserData& denoiserData)
{
    #define DENOISER_NAME REBLUR_DiffuseSpecularOcclusion
    #define DIFF_TEMP1 AsUint(Transient::DIFF_TMP1)
    #define DIFF_TEMP2 AsUint(Transient::DIFF_TMP2)
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
        DIFF_FAST_HISTORY_PING,
        DIFF_FAST_HISTORY_PONG,
        SPEC_FAST_HISTORY_PING,
        SPEC_FAST_HISTORY_PONG,
    };

    AddTextureToPermanentPool( {REBLUR_FORMAT_PREV_VIEWZ, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_PREV_NORMAL_ROUGHNESS, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_PREV_INTERNAL_DATA, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_DIFF_FAST_HISTORY, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_DIFF_FAST_HISTORY, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_SPEC_FAST_HISTORY, w, h, 1} );
    AddTextureToPermanentPool( {REBLUR_FORMAT_SPEC_FAST_HISTORY, w, h, 1} );

    enum class Transient
    {
        DATA1 = TRANSIENT_POOL_START,
        DIFF_TMP1,
        DIFF_TMP2,
        SPEC_TMP1,
        SPEC_TMP2,
        TILES,
    };

    AddTextureToTransientPool( {Format::RGBA8_UNORM, w, h, 1} );
    AddTextureToTransientPool( {REBLUR_FORMAT_OCCLUSION, w, h, 1} );
    AddTextureToTransientPool( {REBLUR_FORMAT_OCCLUSION, w, h, 1} );
    AddTextureToTransientPool( {REBLUR_FORMAT_OCCLUSION, w, h, 1} );
    AddTextureToTransientPool( {REBLUR_FORMAT_OCCLUSION, w, h, 1} );
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

    for (int i = 0; i < REBLUR_OCCLUSION_HITDIST_RECONSTRUCTION_PERMUTATION_NUM; i++)
    {
        bool is5x5 = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Hit distance reconstruction");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
            PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

            // Outputs
            PushOutput( DIFF_TEMP1 );
            PushOutput( SPEC_TEMP1 );

            // Shaders
            if (is5x5)
            {
                AddDispatch( REBLUR_DiffuseSpecularOcclusion_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
            }
            else
            {
                AddDispatch( REBLUR_DiffuseSpecularOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_TEMPORAL_ACCUMULATION_PERMUTATION_NUM; i++)
    {
        bool hasDisocclusionThresholdMix = ( ( ( i >> 2 ) & 0x1 ) != 0 );
        bool hasConfidenceInputs = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isAfterReconstruction = ( ( ( i >> 0 ) & 0x1 ) != 0 );

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
            PushInput( hasConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : REBLUR_DUMMY );
            PushInput( hasConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : REBLUR_DUMMY );
            PushInput( isAfterReconstruction ? DIFF_TEMP1 : AsUint(ResourceType::IN_DIFF_HITDIST) );
            PushInput( isAfterReconstruction ? SPEC_TEMP1 : AsUint(ResourceType::IN_SPEC_HITDIST) );
            PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
            PushInput( AsUint(ResourceType::OUT_SPEC_HITDIST) );
            PushInput( AsUint(Permanent::DIFF_FAST_HISTORY_PING), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_PONG) );
            PushInput( AsUint(Permanent::SPEC_FAST_HISTORY_PING), 0, 1, AsUint(Permanent::SPEC_FAST_HISTORY_PONG) );

            // Outputs
            PushOutput( DIFF_TEMP2 );
            PushOutput( SPEC_TEMP2 );
            PushOutput( AsUint(Transient::DATA1) );
            PushOutput( AsUint(Permanent::DIFF_FAST_HISTORY_PONG), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_PING) );
            PushOutput( AsUint(Permanent::SPEC_FAST_HISTORY_PONG), 0, 1, AsUint(Permanent::SPEC_FAST_HISTORY_PING) );

            // Shaders
            AddDispatch( REBLUR_DiffuseSpecularOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_HISTORY_FIX_PERMUTATION_NUM; i++)
    {
        PushPass("History fix");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( DIFF_TEMP2 );
            PushInput( SPEC_TEMP2 );
            PushInput( AsUint(Permanent::DIFF_FAST_HISTORY_PONG), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_PING) );
            PushInput( AsUint(Permanent::SPEC_FAST_HISTORY_PONG), 0, 1, AsUint(Permanent::SPEC_FAST_HISTORY_PING) );

            // Outputs
            PushOutput( DIFF_TEMP1 );
            PushOutput( SPEC_TEMP1 );

            // Shaders
            AddDispatch( REBLUR_DiffuseSpecularOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_BLUR_PERMUTATION_NUM; i++)
    {
        PushPass("Blur");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( DIFF_TEMP1 );
            PushInput( SPEC_TEMP1 );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );

            // Outputs
            PushOutput( DIFF_TEMP2 );
            PushOutput( SPEC_TEMP2 );
            PushOutput( AsUint(Permanent::PREV_VIEWZ) );

            // Shaders
            AddDispatch( REBLUR_DiffuseSpecularOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_POST_BLUR_PERMUTATION_NUM; i++)
    {
        PushPass("Post-blur");
        {
            // Inputs
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( DIFF_TEMP2 );
            PushInput( SPEC_TEMP2 );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );

            // Outputs
            PushOutput( AsUint(Permanent::PREV_NORMAL_ROUGHNESS) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
            PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );
            PushOutput( AsUint(Permanent::PREV_INTERNAL_DATA) );

            // Shaders
            AddDispatch( REBLUR_DiffuseSpecularOcclusion_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_SPLIT_SCREEN_PERMUTATION_NUM; i++)
    {
        PushPass("Split screen");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
            PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

            // Outputs
            PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
            PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

            // Shaders
            AddDispatch( REBLUR_DiffuseSpecular_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_NUM_THREADS, 1 );
        }
    }

    REBLUR_ADD_VALIDATION_DISPATCH( Transient::DATA1, ResourceType::IN_DIFF_HITDIST, ResourceType::IN_SPEC_HITDIST );

    #undef DENOISER_NAME
    #undef DIFF_TEMP1
    #undef SPEC_TEMP1
    #undef DIFF_TEMP2
    #undef SPEC_TEMP2
}
