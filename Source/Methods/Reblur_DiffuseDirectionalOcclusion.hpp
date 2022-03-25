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
        ESTIMATED_ERROR,
        SCALED_VIEWZ,
        DIFF_ACCUMULATED,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, REBLUR_MIP_NUM} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, REBLUR_MIP_NUM} );

    REBLUR_DECLARE_SHARED_CONSTANT_NUM;

    PushPass("Pre-pass");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );

        PushOutput( DIFF_TEMP1 );

        AddDispatch( REBLUR_Diffuse_PrePass, SumConstants(1, 2, 0, 2), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_PrePass, SumConstants(1, 2, 0, 2), 16, 1 );
    }

    PushPass("Pre-pass (advanced)");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_PDF) );

        PushOutput( DIFF_TEMP1 );

        AddDispatch( REBLUR_Diffuse_PrePassAdvanced, SumConstants(1, 2, 0, 2), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_PrePassAdvanced, SumConstants(1, 2, 0, 2), 16, 1 );
    }

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( DIFF_TEMP2 );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulation, SumConstants(4, 2, 1, 5), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulation, SumConstants(4, 2, 1, 5), 16, 1 );
    }

    PushPass("Temporal accumulation"); // after Pre-blur
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( DIFF_TEMP1 );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( DIFF_TEMP2 );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulation, SumConstants(4, 2, 1, 5), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulation, SumConstants(4, 2, 1, 5), 16, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( DIFF_TEMP2 );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 5), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 5), 16, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence, after Pre-blur
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( DIFF_TEMP1 );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( DIFF_TEMP2 );

        AddDispatch( REBLUR_Diffuse_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 5), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 5), 16, 1 );
    }

    PushPass("Mip generation");
    {
        PushInput( DIFF_TEMP2 );

        for( uint16_t i = 0; i < REBLUR_MIP_NUM; i++ )
        {
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED), i, 1 );
            PushOutput( AsUint(Transient::SCALED_VIEWZ), i, 1 );
        }

        AddDispatch( REBLUR_Diffuse_MipGen, SumConstants(0, 0, 0, 0), 8, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_MipGen, SumConstants(0, 0, 0, 0), 8, 1 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, REBLUR_MIP_NUM );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 1, REBLUR_MIP_NUM - 1 );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 0, 1 );

        AddDispatch( REBLUR_Diffuse_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( DIFF_TEMP2 );

        AddDispatch( REBLUR_Diffuse_Blur, SumConstants(1, 2, 0, 0), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_Blur, SumConstants(1, 2, 0, 0), 16, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( DIFF_TEMP2 );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY) );

        AddDispatch( REBLUR_Diffuse_PostBlur, SumConstants(1, 2, 0, 0), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_PostBlur, SumConstants(1, 2, 0, 0), 16, 1 );
    }

    PushPass("Temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::ESTIMATED_ERROR) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_STABILIZED_PING), 0, 1, AsUint(Permanent::DIFF_HISTORY_STABILIZED_PONG) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushOutput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_STABILIZED_PONG), 0, 1, AsUint(Permanent::DIFF_HISTORY_STABILIZED_PING) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        AddDispatch( REBLUR_Diffuse_TemporalStabilization, SumConstants(2, 2, 2, 1), 16, 1 );
        AddDispatch( REBLUR_Perf_Diffuse_TemporalStabilization, SumConstants(2, 2, 2, 1), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_DIRECTION_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_DIRECTION_HITDIST) );

        AddDispatch( REBLUR_Diffuse_SplitScreen, SumConstants(0, 0, 0, 3), 16, 1 );
    }

    #undef METHOD_NAME
    #undef DIFF_TEMP1
    #undef DIFF_TEMP2

    return sizeof(ReblurSettings);
}
