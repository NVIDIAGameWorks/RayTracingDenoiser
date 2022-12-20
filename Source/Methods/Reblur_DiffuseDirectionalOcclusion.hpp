/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::DenoiserImpl::AddMethod_ReblurDiffuseDirectionalOcclusion(MethodData& methodData)
{
    #define METHOD_NAME REBLUR_DirectionalOcclusion
    #define DIFF_TEMP1 AsUint(Transient::DIFF_TMP1)
    #define DIFF_TEMP2 AsUint(Transient::DIFF_TMP2)

    methodData.settings.reblur = ReblurSettings();
    methodData.settingsSize = sizeof(methodData.settings.reblur);
            
    uint16_t w = methodData.desc.fullResolutionWidth;
    uint16_t h = methodData.desc.fullResolutionHeight;

    // IMPORTANT: uses SNORM / UNORM 16-bit textures to maximize bits utilization and uniformity

    enum class Permanent
    {
        PREV_VIEWZ = PERMANENT_POOL_START,
        PREV_NORMAL_ROUGHNESS,
        PREV_INTERNAL_DATA,
        DIFF_HISTORY,
        DIFF_FAST_HISTORY_PING,
        DIFF_FAST_HISTORY_PONG,
    };

    m_PermanentPool.push_back( {REBLUR_FORMAT_PREV_VIEWZ, w, h, 1} );
    m_PermanentPool.push_back( {REBLUR_FORMAT_PREV_NORMAL_ROUGHNESS, w, h, 1} );
    m_PermanentPool.push_back( {REBLUR_FORMAT_PREV_INTERNAL_DATA, w, h, 1} );
    m_PermanentPool.push_back( {REBLUR_FORMAT_DIRECTIONAL_OCCLUSION, w, h, 1} );
    m_PermanentPool.push_back( {REBLUR_FORMAT_DIRECTIONAL_OCCLUSION_FAST_HISTORY, w, h, 1} );
    m_PermanentPool.push_back( {REBLUR_FORMAT_DIRECTIONAL_OCCLUSION_FAST_HISTORY, w, h, 1} );

    enum class Transient
    {
        DATA1 = TRANSIENT_POOL_START,
        DATA2,
        DIFF_TMP1,
        DIFF_TMP2
    };

    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {REBLUR_FORMAT_DIRECTIONAL_OCCLUSION, w, h, 1} );
    m_TransientPool.push_back( {REBLUR_FORMAT_DIRECTIONAL_OCCLUSION, w, h, 1} );

    REBLUR_SET_SHARED_CONSTANTS;

    for (int i = 0; i < REBLUR_HITDIST_RECONSTRUCTION_PERMUTATION_NUM; i++)
    {
        bool is5x5 = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isPrepassEnabled = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Hit distance reconstruction");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );

            // Outputs
            PushOutput( isPrepassEnabled ? DIFF_TEMP2 : DIFF_TEMP1 );

            // Shaders
            if (is5x5)
            {
                AddDispatch( REBLUR_Diffuse_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
            }
            else
            {
                AddDispatch( REBLUR_Diffuse_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_PREPASS_PERMUTATION_NUM; i++)
    {
        bool isAfterReconstruction = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Pre-pass");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( isAfterReconstruction ? DIFF_TEMP2 : AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );

            // Outputs
            PushOutput( DIFF_TEMP1 );

            // Shaders
            AddDispatch( REBLUR_DiffuseDirectionalOcclusion_PrePass, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseDirectionalOcclusion_PrePass, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_NUM_THREADS, 1 );
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
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );
            PushInput( AsUint(Permanent::PREV_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Permanent::PREV_INTERNAL_DATA) );
            PushInput( hasDisocclusionThresholdMix ? AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) : REBLUR_DUMMY );
            PushInput( hasConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : REBLUR_DUMMY );
            PushInput( isAfterPrepass ? DIFF_TEMP1 : AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );
            PushInput( isTemporalStabilization ? AsUint(Permanent::DIFF_HISTORY) : AsUint(ResourceType::OUT_DIFF_DIRECTION_HITDIST) );
            PushInput( AsUint(Permanent::DIFF_FAST_HISTORY_PING), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_PONG) );
            
            // Outputs
            PushOutput( DIFF_TEMP2 );
            PushOutput( AsUint(Transient::DATA1) );
            PushOutput( AsUint(Permanent::DIFF_FAST_HISTORY_PONG), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_PING) );
            PushOutput( AsUint(Transient::DATA2) );

            // Shaders
            AddDispatch( REBLUR_DiffuseDirectionalOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseDirectionalOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_HISTORY_FIX_PERMUTATION_NUM; i++)
    {
        PushPass("History fix");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( DIFF_TEMP2 );
            PushInput( AsUint(Permanent::DIFF_FAST_HISTORY_PONG), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_PING) );

            // Outputs
            PushOutput( DIFF_TEMP1 );

            // Shaders
            AddDispatch( REBLUR_DiffuseDirectionalOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseDirectionalOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_BLUR_PERMUTATION_NUM; i++)
    {
        PushPass("Blur");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( DIFF_TEMP1 );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );

            // Outputs
            PushOutput( DIFF_TEMP2 );
            PushOutput( AsUint(Permanent::PREV_VIEWZ) );

            // Shaders
            AddDispatch( REBLUR_DiffuseDirectionalOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseDirectionalOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_POST_BLUR_PERMUTATION_NUM; i++)
    {
        bool isTemporalStabilization = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Post-blur");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( DIFF_TEMP2 );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );

            // Outputs
            PushOutput( AsUint(Permanent::PREV_NORMAL_ROUGHNESS) );

            if (isTemporalStabilization)
                PushOutput( AsUint(Permanent::DIFF_HISTORY) );
            else
            {
                PushOutput( AsUint(ResourceType::OUT_DIFF_DIRECTION_HITDIST) );
                PushOutput( AsUint(Permanent::PREV_INTERNAL_DATA) );
            }

            // Shaders
            if (isTemporalStabilization)
            {
                AddDispatch( REBLUR_DiffuseDirectionalOcclusion_PostBlur, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_DiffuseDirectionalOcclusion_PostBlur, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
            }
            else
            {
                AddDispatch( REBLUR_DiffuseDirectionalOcclusion_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
                AddDispatch( REBLUR_Perf_DiffuseDirectionalOcclusion_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_NUM_THREADS, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_COPY_STABILIZED_HISTORY_PERMUTATION_NUM; i++)
    {
        PushPass("Copy stabilized history");
        {
            // Inputs
            PushInput( AsUint(ResourceType::OUT_DIFF_DIRECTION_HITDIST) );

            // Outputs
            PushOutput( DIFF_TEMP2 );

            // Shaders
            AddDispatch( REBLUR_Diffuse_CopyStabilizedHistory, REBLUR_COPY_STABILIZED_HISTORY_CONSTANT_NUM, REBLUR_COPY_STABILIZED_HISTORY_NUM_THREADS, USE_MAX_DIMS );
        }
    }

    for (int i = 0; i < REBLUR_TEMPORAL_STABILIZATION_PERMUTATION_NUM; i++)
    {
        PushPass("Temporal stabilization");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( AsUint(Transient::DATA2) );
            PushInput( AsUint(Permanent::DIFF_HISTORY) );
            PushInput( DIFF_TEMP2 );

            // Outputs
            PushOutput( AsUint(ResourceType::IN_MV) );
            PushOutput( AsUint(Permanent::PREV_INTERNAL_DATA) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_DIRECTION_HITDIST) );

            // Shaders
            AddDispatch( REBLUR_DiffuseDirectionalOcclusion_TemporalStabilization, REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM, REBLUR_TEMPORAL_STABILIZATION_NUM_THREADS, 1 );
            AddDispatch( REBLUR_Perf_DiffuseDirectionalOcclusion_TemporalStabilization, REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM, REBLUR_TEMPORAL_STABILIZATION_NUM_THREADS, 1 );
        }
    }

    for (int i = 0; i < REBLUR_SPLIT_SCREEN_PERMUTATION_NUM; i++)
    {
        PushPass("Split screen");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );

            // Outputs
            PushOutput( AsUint(ResourceType::OUT_DIFF_DIRECTION_HITDIST) );

            // Shaders
            AddDispatch( REBLUR_Diffuse_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_NUM_THREADS, 1 );
        }
    }

    REBLUR_ADD_VALIDATION_DISPATCH( Transient::DATA2, ResourceType::IN_DIFF_DIRECTION_HITDIST, ResourceType::IN_DIFF_DIRECTION_HITDIST );

    #undef METHOD_NAME
    #undef DIFF_TEMP1
    #undef DIFF_TEMP2
}
