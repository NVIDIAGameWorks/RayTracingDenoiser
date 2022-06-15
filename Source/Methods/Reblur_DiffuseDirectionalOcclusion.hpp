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
    #define DIFF_TEMP2 AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST)

    enum class Permanent
    {
        PREV_VIEWZ_DIFFACCUMSPEED = PERMANENT_POOL_START,
        PREV_NORMAL_SPECACCUMSPEED,
        DIFF_HISTORY,
        DIFF_HISTORY_STABILIZED_PING,
        DIFF_HISTORY_STABILIZED_PONG,
    };

    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    enum class Transient
    {
        INTERNAL_DATA = TRANSIENT_POOL_START,
        SCALED_VIEWZ,
        DIFF_DATA,
        DIFF_ACCUMULATED,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, REBLUR_MIP_NUM} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, REBLUR_MIP_NUM} );

    REBLUR_SET_SHARED_CONSTANTS;

    for (int i = 0; i < 4; i++)
    {
        bool is5x5                  = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isPrepassEnabled       = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Hit distance reconstruction");
        {
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );

            if (isPrepassEnabled)
                PushOutput( DIFF_TEMP2 );
            else
                PushOutput( DIFF_TEMP1 );

            if (is5x5)
            {
                AddDispatch( REBLUR_Diffuse_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_Diffuse_HitDistReconstruction_3x3, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_HitDistReconstruction_3x3, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
            }
        }
    }

    for (int i = 0; i < 4; i++)
    {
        bool isAdvanced             = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isAfterReconstruction  = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Pre-pass");
        {
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );

            if (isAfterReconstruction)
                PushInput( DIFF_TEMP2 );
            else
                PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );

            if (isAdvanced)
                PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_PDF) );

            PushOutput( DIFF_TEMP1 );

            if (isAdvanced)
            {
                AddDispatch( REBLUR_Diffuse_PrePassAdvanced, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_PrePassAdvanced, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_Diffuse_PrePass, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_PrePass, REBLUR_PREPASS_CONSTANT_NUM, REBLUR_PREPASS_GROUP_DIM, 1 );
            }
        }
    }

    for (int i = 0; i < 8; i++)
    {
        bool isAntifireflyEnabled   = ( ( ( i >> 2 ) & 0x1 ) != 0 );
        bool hasConfidenceInputs    = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isAfterPrepass         = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Temporal accumulation");
        {
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
            PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
            PushInput( AsUint(Permanent::DIFF_HISTORY) );

            if (isAfterPrepass)
                PushInput( DIFF_TEMP1 );
            else
                PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );
            
            if (hasConfidenceInputs)
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

            PushOutput( AsUint(Transient::INTERNAL_DATA) );
            PushOutput( AsUint(Transient::DIFF_DATA) );

            if (isAntifireflyEnabled)
                PushOutput( DIFF_TEMP2 );
            else
                PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

            PushOutput( AsUint(Transient::SCALED_VIEWZ) );

            AddDispatch( REBLUR_Diffuse_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
        }
    }

    PushPass("Mip gen");
    {
        PushInput( DIFF_TEMP2 );

        for( int16_t i = REBLUR_MIP_NUM - 1; i >= 0; i-- )
        {
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED), i, 1 );
            PushOutput( AsUint(Transient::SCALED_VIEWZ), i, 1 );
        }

        AddDispatch( REBLUR_Diffuse_MipGen, REBLUR_MIPGEN_CONSTANT_NUM, REBLUR_MIPGEN_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_MipGen, REBLUR_MIPGEN_CONSTANT_NUM, REBLUR_MIPGEN_GROUP_DIM, 1 ); // fast path, but no anti-firefly support
    }

    PushPass("History fix");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 1, REBLUR_MIP_NUM - 1 );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, REBLUR_MIP_NUM );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 0, 1 );

        AddDispatch( REBLUR_Diffuse_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );

        PushOutput( DIFF_TEMP2 );

        AddDispatch( REBLUR_Diffuse_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_DATA) );
        PushInput( DIFF_TEMP2 );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );

        PushOutput( AsUint(Permanent::DIFF_HISTORY) );

        AddDispatch( REBLUR_Diffuse_PostBlur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_PostBlur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
    }

    PushPass("Temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_STABILIZED_PING), 0, 1, AsUint(Permanent::DIFF_HISTORY_STABILIZED_PONG) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushOutput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_STABILIZED_PONG), 0, 1, AsUint(Permanent::DIFF_HISTORY_STABILIZED_PING) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        AddDispatch( REBLUR_Diffuse_TemporalStabilization, REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM, REBLUR_TEMPORAL_STABILIZATION_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalStabilization, REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM, REBLUR_TEMPORAL_STABILIZATION_GROUP_DIM, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_DIRECTION_HITDIST) );

        AddDispatch( REBLUR_Diffuse_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_GROUP_DIM, 1 );
    }

    #undef METHOD_NAME
    #undef DIFF_TEMP1
    #undef DIFF_TEMP2

    return sizeof(ReblurSettings);
}
