/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_ReblurDiffuseSpecularOcclusion(uint16_t w, uint16_t h)
{
    #define METHOD_NAME REBLUR_DiffuseSpecularOcclusion

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
        ESTIMATED_ERROR,
        DIFF_ACCUMULATED,
        SPEC_ACCUMULATED,
        DIFF_TEMP,
        SPEC_TEMP,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, REBLUR_MIP_NUM} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, REBLUR_MIP_NUM} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );

    REBLUR_DECLARE_SHARED_CONSTANT_NUM;

    PushPass("Temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_TemporalAccumulation, SumConstants(4, 2, 1, 4), 8, 1 );
        AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_TemporalAccumulation, SumConstants(4, 2, 1, 4), 8, 1 );
    }

    PushPass("Temporal accumulation"); // with confidence
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushInput( AsUint(Permanent::PREV_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );
        PushInput( AsUint(ResourceType::OUT_SPEC_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
        PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE) );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 4), 8, 1 );
        AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_TemporalAccumulationWithConfidence, SumConstants(4, 2, 1, 4), 8, 1 );
    }

    PushPass("Mip generation");
    {
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        for( uint16_t i = 1; i < REBLUR_MIP_NUM; i++ )
        {
            PushOutput( AsUint(Transient::DIFF_ACCUMULATED), i, 1 );
            PushOutput( AsUint(Transient::SPEC_ACCUMULATED), i, 1 );
        }

        AddDispatch( NRD_MipGeneration_Float2_Float2, SumConstants(0, 0, 1, 2, false), 16, 2 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 1, REBLUR_MIP_NUM - 1 );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED), 1, REBLUR_MIP_NUM - 1 );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 0, 1 );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED), 0, 1 );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
        AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_HistoryFix, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    PushPass("Blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Transient::DIFF_TEMP) );
        PushOutput( AsUint(Transient::SPEC_TEMP) );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_Blur, SumConstants(1, 2, 0, 0), 16, 1 );
        AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_Blur, SumConstants(1, 2, 0, 0), 16, 1 );
    }

    PushPass("Post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::DIFF_TEMP) );
        PushInput( AsUint(Transient::SPEC_TEMP) );

        PushOutput( AsUint(Transient::ESTIMATED_ERROR) );
        PushOutput( AsUint(Permanent::PREV_VIEWZ_DIFFACCUMSPEED) );
        PushOutput( AsUint(Permanent::PREV_NORMAL_SPECACCUMSPEED) );
        PushOutput( AsUint(Permanent::PREV_ROUGHNESS) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_PostBlur, SumConstants(1, 2, 0, 0), 16, 1 );
        AddDispatch( REBLUR_Perf_DiffuseSpecularOcclusion_PostBlur, SumConstants(1, 2, 0, 0), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HITDIST) );
        PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_HITDIST) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

        AddDispatch( REBLUR_DiffuseSpecularOcclusion_SplitScreen, SumConstants(0, 0, 0, 3), 16, 1 );
    }

    #undef METHOD_NAME

    return sizeof(ReblurSettings);
}
