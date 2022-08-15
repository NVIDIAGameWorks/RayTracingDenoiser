/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_ReblurDiffuseDirectionalOcclusion(uint16_t w, uint16_t h)
{
    #define METHOD_NAME REBLUR_DirectionalOcclusion
    #define DIFF_TEMP1 AsUint(Permanent::DIFF_HISTORY_STABILIZED_PONG), 0, 1, AsUint(Permanent::DIFF_HISTORY_STABILIZED_PING)
    #define DIFF_TEMP2 AsUint(ResourceType::OUT_DIFF_DIRECTION_HITDIST)

    enum class Permanent
    {
        PREV_VIEWZ = PERMANENT_POOL_START,
        PREV_NORMAL_ROUGHNESS,
        PREV_ACCUMSPEEDS_MATERIALID,
        DIFF_HISTORY,
        DIFF_HISTORY_STABILIZED_PING,
        DIFF_HISTORY_STABILIZED_PONG,
        DIFF_FAST_HISTORY_PING,
        DIFF_FAST_HISTORY_PONG,
    };

    m_PermanentPool.push_back( {Format::R32_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );

    enum class Transient
    {
        DATA1 = TRANSIENT_POOL_START,
        DATA2,
        DIFF_ACCUMULATED,
    };

    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

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
                AddDispatch( REBLUR_Diffuse_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_Diffuse_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_PREPASS_PERMUTATION_NUM; i++)
    {
        bool isAdvanced = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isAfterReconstruction = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Pre-pass");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( isAfterReconstruction ? DIFF_TEMP2 : AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );

            if (isAdvanced)
                PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_PDF) );

            // Outputs
            PushOutput( DIFF_TEMP1 );

            // Shaders
            if (isAdvanced)
            {
                AddDispatch( REBLUR_Diffuse_PrePass_Advanced, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_PrePass_Advanced, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_Diffuse_PrePass, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_PrePass, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_GROUP_DIM, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_TEMPORAL_ACCUMULATION_PERMUTATION_NUM; i++)
    {
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
            PushInput( AsUint(Permanent::PREV_ACCUMSPEEDS_MATERIALID) );
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
            AddDispatch( REBLUR_Diffuse_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
        }
    }

    for (int i = 0; i < REBLUR_HISTORY_FIX_PERMUTATION_NUM; i++)
    {
        PushPass("History fix");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( DIFF_TEMP2 );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(Permanent::DIFF_FAST_HISTORY_PONG), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_PING) );

            // Outputs
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

            // Shaders
            AddDispatch( REBLUR_Diffuse_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_Diffuse_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
        }
    }

    for (int i = 0; i < REBLUR_BLUR_PERMUTATION_NUM; i++)
    {
        PushPass("Blur");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );

            // Outputs
            PushOutput( DIFF_TEMP2 );
            PushOutput( AsUint(Permanent::PREV_VIEWZ) );

            // Shaders
            AddDispatch( REBLUR_Diffuse_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_Diffuse_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
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
                PushOutput( AsUint(Permanent::PREV_ACCUMSPEEDS_MATERIALID) );
            }

            // Shaders
            if (isTemporalStabilization)
            {
                AddDispatch( REBLUR_Diffuse_PostBlur, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_PostBlur, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_Diffuse_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_GROUP_DIM, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_TEMPORAL_STABILIZATION_PERMUTATION_NUM; i++)
    {
        PushPass("Temporal stabilization");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( AsUint(Transient::DATA2) );
            PushInput( AsUint(Permanent::DIFF_HISTORY) );
            PushInput( AsUint(Permanent::DIFF_HISTORY_STABILIZED_PING), 0, 1, AsUint(Permanent::DIFF_HISTORY_STABILIZED_PONG) );

            // Outputs
            PushOutput( AsUint(Permanent::PREV_ACCUMSPEEDS_MATERIALID) );
            PushOutput( AsUint(Permanent::DIFF_HISTORY_STABILIZED_PONG), 0, 1, AsUint(Permanent::DIFF_HISTORY_STABILIZED_PING) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_DIRECTION_HITDIST) );

            // Shaders
            AddDispatch( REBLUR_Diffuse_TemporalStabilization, REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM, REBLUR_TEMPORAL_STABILIZATION_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_Diffuse_TemporalStabilization, REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM, REBLUR_TEMPORAL_STABILIZATION_GROUP_DIM, 1 );
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
            AddDispatch( REBLUR_Diffuse_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_GROUP_DIM, 1 );
        }
    }

    #undef METHOD_NAME
    #undef DIFF_TEMP1
    #undef DIFF_TEMP2

    return sizeof(ReblurSettings);
}
