/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_ReblurSpecularOcclusion(uint16_t w, uint16_t h)
{
    #define METHOD_NAME REBLUR_SpecularOcclusion

    enum class Permanent
    {
        PREV_VIEWZ_DIFFACCUMSPEED = PERMANENT_POOL_START,
        PREV_NORMAL_SPECACCUMSPEED,
        PREV_ROUGHNESS,
    };

    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );

    enum class Transient
    {
        INTERNAL_DATA = TRANSIENT_POOL_START,
        SPEC_DATA,
        SPEC_ACCUMULATED,
        SPEC_TEMP,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, REBLUR_MIP_NUM} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );

    REBLUR_SET_SHARED_CONSTANTS;

    PushPass("Hit distance reconstruction");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

        PushOutput( AsUint(Transient::SPEC_TEMP) );

        AddDispatch( REBLUR_SpecularOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_SpecularOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
    }

    for (int i = 0; i < 4; i++)
    {
        bool hasConfidenceInputs    = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isAfterPrepass         = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Temporal accumulation");
        {
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
            PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
            PushInput( AsUint(Permanent::PREV_ROUGHNESS) );
            PushInput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

            if (isAfterPrepass)
                PushInput( AsUint(Transient::SPEC_TEMP) );
            else
                PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

            if (hasConfidenceInputs)
                PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE) );

            PushOutput( AsUint(Transient::INTERNAL_DATA) );
            PushOutput( AsUint(Transient::SPEC_DATA) );
            PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

            if (hasConfidenceInputs)
            {
                AddDispatch( REBLUR_SpecularOcclusion_TemporalAccumulationWithConfidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_SpecularOcclusion_TemporalAccumulationWithConfidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_SpecularOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_SpecularOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
        }
    }

    PushPass("Mip gen");
    {
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        for( int16_t i = REBLUR_MIP_NUM - 1; i >= 1; i-- )
            PushOutput( AsUint(Transient::SPEC_ACCUMULATED), i, 1 );

        AddDispatch( REBLUR_SpecularOcclusion_MipGen, REBLUR_MIPGEN_CONSTANT_NUM, REBLUR_MIPGEN_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_SpecularOcclusion_MipGen, REBLUR_MIPGEN_CONSTANT_NUM, REBLUR_MIPGEN_GROUP_DIM, 1 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED), 1, REBLUR_MIP_NUM - 1 );

        PushOutput( AsUint(Transient::SPEC_ACCUMULATED), 0, 1 );

        AddDispatch( REBLUR_SpecularOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_SpecularOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SPEC_DATA) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        PushOutput( AsUint(Transient::SPEC_TEMP) );

        AddDispatch( REBLUR_SpecularOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_SpecularOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SPEC_DATA) );
        PushInput( AsUint(Transient::SPEC_TEMP) );

        PushOutput( AsUint(Permanent::PREV_ROUGHNESS) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );
        PushOutput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushOutput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );

        AddDispatch( REBLUR_SpecularOcclusion_PostBlur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
        AddDispatch( REBLUR_Perf_SpecularOcclusion_PostBlur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

        AddDispatch( REBLUR_SpecularOcclusion_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_GROUP_DIM, 1 );
    }

    #undef METHOD_NAME

    return sizeof(ReblurSettings);
}
