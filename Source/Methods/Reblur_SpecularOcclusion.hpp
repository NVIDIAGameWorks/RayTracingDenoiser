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
    #define SPEC_TEMP1 AsUint(Transient::SPEC_TMP1)
    #define SPEC_TEMP2 AsUint(Transient::SPEC_TMP2)

    enum class Permanent
    {
        PREV_VIEWZ = PERMANENT_POOL_START,
        PREV_NORMAL_ROUGHNESS,
        PREV_ACCUMSPEEDS_MATERIALID,
    };

    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} ); // R32f doesn't make sense because .y in data is FP16
    m_PermanentPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_UINT, w, h, 1} );

    enum class Transient
    {
        DATA1 = TRANSIENT_POOL_START,
        SPEC_ACCUMULATED,
        SPEC_TMP1, // single channel for hit dist only
        SPEC_TMP2,
    };

    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );

    REBLUR_SET_SHARED_CONSTANTS;

    for (int i = 0; i < REBLUR_OCCLUSION_HITDIST_RECONSTRUCTION_PERMUTATION_NUM; i++)
    {
        bool is5x5 = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Hit distance reconstruction");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

            // Outputs
            PushOutput( SPEC_TEMP1 );

            // Shaders
            if (is5x5)
            {
                AddDispatch( REBLUR_SpecularOcclusion_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_SpecularOcclusion_HitDistReconstruction_5x5, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_SpecularOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_SpecularOcclusion_HitDistReconstruction, REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM, REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_TEMPORAL_ACCUMULATION_PERMUTATION_NUM; i++)
    {
        bool hasConfidenceInputs = ( ( ( i >> 1 ) & 0x1 ) != 0 );
        bool isAfterReconstruction  = ( ( ( i >> 0 ) & 0x1 ) != 0 );

        PushPass("Temporal accumulation");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(Permanent::PREV_VIEWZ) );
            PushInput( AsUint(Permanent::PREV_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Permanent::PREV_ACCUMSPEEDS_MATERIALID) );
            PushInput( REBLUR_DUMMY ); // TODO: should be SPEC_MIN_HITDIST, but there is no pre-pass in occlusion-only denoisers
            PushInput( hasConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : REBLUR_DUMMY );
            PushInput( isAfterReconstruction ? SPEC_TEMP1 : AsUint(ResourceType::IN_SPEC_HITDIST) );
            PushInput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

            // Outputs
            PushOutput( SPEC_TEMP2 );
            PushOutput( AsUint(Transient::DATA1) );

            // Shaders
            if (hasConfidenceInputs)
            {
                AddDispatch( REBLUR_SpecularOcclusion_TemporalAccumulation_Confidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_SpecularOcclusion_TemporalAccumulation_Confidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_SpecularOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_SpecularOcclusion_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_HISTORY_FIX_PERMUTATION_NUM; i++)
    {
        PushPass("History fix");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( SPEC_TEMP2 );

            // Outputs
            PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

            // Shaders
            AddDispatch( REBLUR_SpecularOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_SpecularOcclusion_HistoryFix, REBLUR_HISTORY_FIX_CONSTANT_NUM, REBLUR_HISTORY_FIX_GROUP_DIM, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_BLUR_PERMUTATION_NUM; i++)
    {
        PushPass("Blur");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

            // Outputs
            PushOutput( SPEC_TEMP2 );
            PushOutput( AsUint(Permanent::PREV_VIEWZ) );

            // Shaders
            AddDispatch( REBLUR_SpecularOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_SpecularOcclusion_Blur, REBLUR_BLUR_CONSTANT_NUM, REBLUR_BLUR_GROUP_DIM, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_POST_BLUR_PERMUTATION_NUM; i++)
    {
        PushPass("Post-blur");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::DATA1) );
            PushInput( SPEC_TEMP2 );

            // Outputs
            PushOutput( AsUint(Permanent::PREV_NORMAL_ROUGHNESS) );
            PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );
            PushOutput( AsUint(Permanent::PREV_ACCUMSPEEDS_MATERIALID) );

            // Shaders
            AddDispatch( REBLUR_SpecularOcclusion_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_GROUP_DIM, 1 );
            AddDispatch( REBLUR_Perf_SpecularOcclusion_PostBlur_NoTemporalStabilization, REBLUR_POST_BLUR_CONSTANT_NUM, REBLUR_POST_BLUR_GROUP_DIM, 1 );
        }
    }

    for (int i = 0; i < REBLUR_OCCLUSION_SPLIT_SCREEN_PERMUTATION_NUM; i++)
    {
        PushPass("Split screen");
        {
            // Inputs
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(ResourceType::IN_SPEC_HITDIST) );

            // Outputs
            PushOutput( AsUint(ResourceType::OUT_SPEC_HITDIST) );

            // Shaders
            AddDispatch( REBLUR_SpecularOcclusion_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_GROUP_DIM, 1 );
        }
    }

    #undef METHOD_NAME
    #undef SPEC_TEMP1
    #undef SPEC_TEMP2

    return sizeof(ReblurSettings);
}
