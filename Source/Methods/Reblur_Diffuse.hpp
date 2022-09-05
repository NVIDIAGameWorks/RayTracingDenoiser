/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// Constants
#define REBLUR_SET_SHARED_CONSTANTS                                 SetSharedConstants(2, 4, 9, 22)

#define REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM                  SumConstants(0, 0, 0, 0)
#define REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM                     8

#define REBLUR_PREPASS_CONSTANT_NUM                                 SumConstants(0, 1, 0, 2)
#define REBLUR_PREPASS_GROUP_DIM                                    16

#define REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM                   SumConstants(4, 2, 1, 4)
#define REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM                      8

#define REBLUR_HISTORY_FIX_CONSTANT_NUM                             SumConstants(0, 1, 0, 1)
#define REBLUR_HISTORY_FIX_GROUP_DIM                                16

#define REBLUR_BLUR_CONSTANT_NUM                                    SumConstants(0, 1, 0, 1)
#define REBLUR_BLUR_GROUP_DIM                                       8

#define REBLUR_POST_BLUR_CONSTANT_NUM                               SumConstants(0, 1, 0, 1)
#define REBLUR_POST_BLUR_GROUP_DIM                                  8

#define REBLUR_COPY_STABILIZED_HISTORY_CONSTANT_NUM                 SumConstants(0, 0, 0, 0, false)
#define REBLUR_COPY_STABILIZED_HISTORY_GROUP_DIM                    16

#define REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM                  SumConstants(2, 2, 2, 0)
#define REBLUR_TEMPORAL_STABILIZATION_GROUP_DIM                     8

#define REBLUR_SPLIT_SCREEN_CONSTANT_NUM                            SumConstants(0, 0, 0, 3)
#define REBLUR_SPLIT_SCREEN_GROUP_DIM                               16

// Permutations
#define REBLUR_HITDIST_RECONSTRUCTION_PERMUTATION_NUM               4
#define REBLUR_PREPASS_PERMUTATION_NUM                              4
#define REBLUR_TEMPORAL_ACCUMULATION_PERMUTATION_NUM                8
#define REBLUR_HISTORY_FIX_PERMUTATION_NUM                          1
#define REBLUR_BLUR_PERMUTATION_NUM                                 1
#define REBLUR_POST_BLUR_PERMUTATION_NUM                            2
#define REBLUR_COPY_STABILIZED_HISTORY_PERMUTATION_NUM              1
#define REBLUR_TEMPORAL_STABILIZATION_PERMUTATION_NUM               1
#define REBLUR_SPLIT_SCREEN_PERMUTATION_NUM                         1

#define REBLUR_OCCLUSION_HITDIST_RECONSTRUCTION_PERMUTATION_NUM     2
#define REBLUR_OCCLUSION_TEMPORAL_ACCUMULATION_PERMUTATION_NUM      4
#define REBLUR_OCCLUSION_HISTORY_FIX_PERMUTATION_NUM                1
#define REBLUR_OCCLUSION_BLUR_PERMUTATION_NUM                       1
#define REBLUR_OCCLUSION_POST_BLUR_PERMUTATION_NUM                  1
#define REBLUR_OCCLUSION_SPLIT_SCREEN_PERMUTATION_NUM               1

// Formats
#define REBLUR_FORMAT                                               Format::RGBA16_SFLOAT
#define REBLUR_FORMAT_FAST_HISTORY                                  Format::R16_SFLOAT

#define REBLUR_FORMAT_OCCLUSION                                     Format::R16_UNORM

#define REBLUR_FORMAT_DIRECTIONAL_OCCLUSION                         Format::RGBA16_SNORM
#define REBLUR_FORMAT_DIRECTIONAL_OCCLUSION_FAST_HISTORY            REBLUR_FORMAT_OCCLUSION

#define REBLUR_FORMAT_PREV_VIEWZ                                    Format::R32_SFLOAT
#define REBLUR_FORMAT_PREV_NORMAL_ROUGHNESS                         Format::RGBA8_UNORM
#define REBLUR_FORMAT_PREV_INTERNAL_DATA                            Format::R16_UINT

#define REBLUR_FORMAT_MIN_HITDIST                                   REBLUR_FORMAT_OCCLUSION

// Other
#define REBLUR_DUMMY                                                AsUint(ResourceType::IN_VIEWZ)

size_t nrd::DenoiserImpl::AddMethod_ReblurDiffuse(uint16_t w, uint16_t h)
{
    #define METHOD_NAME REBLUR_Diffuse
    #define DIFF_TEMP1 AsUint(Transient::DIFF_TMP1)
    #define DIFF_TEMP2 AsUint(Transient::DIFF_TMP2)

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
    m_PermanentPool.push_back( {REBLUR_FORMAT, w, h, 1} );
    m_PermanentPool.push_back( {REBLUR_FORMAT_FAST_HISTORY, w, h, 1} );
    m_PermanentPool.push_back( {REBLUR_FORMAT_FAST_HISTORY, w, h, 1} );

    enum class Transient
    {
        DATA1 = TRANSIENT_POOL_START,
        DATA2,
        DIFF_TMP1,
        DIFF_TMP2
    };

    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {REBLUR_FORMAT, w, h, 1} );
    m_TransientPool.push_back( {REBLUR_FORMAT, w, h, 1} );

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
            PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

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
            PushInput( isAfterReconstruction ? DIFF_TEMP2 : AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

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
            PushInput( AsUint(Permanent::PREV_INTERNAL_DATA) );
            PushInput( hasConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : REBLUR_DUMMY );
            PushInput( isAfterPrepass ? DIFF_TEMP1 : AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
            PushInput( isTemporalStabilization ? AsUint(Permanent::DIFF_HISTORY) : AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );
            PushInput( AsUint(Permanent::DIFF_FAST_HISTORY_PING), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_PONG) );

            // Outputs
            PushOutput( DIFF_TEMP2 );
            PushOutput( AsUint(Transient::DATA1) );
            PushOutput( AsUint(Permanent::DIFF_FAST_HISTORY_PONG), 0, 1, AsUint(Permanent::DIFF_FAST_HISTORY_PING) );
            PushOutput( AsUint(Transient::DATA2) );

            // Shaders
            if (hasConfidenceInputs)
            {
                AddDispatch( REBLUR_Diffuse_TemporalAccumulation_Confidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulation_Confidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_Diffuse_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
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
            PushOutput( DIFF_TEMP1 );

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
            PushInput( DIFF_TEMP1 );
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
                PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );
                PushOutput( AsUint(Permanent::PREV_INTERNAL_DATA) );
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

    for (int i = 0; i < REBLUR_COPY_STABILIZED_HISTORY_PERMUTATION_NUM; i++)
    {
        PushPass("Copy stabilized history");
        {
            // Inputs
            PushInput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

            // Outputs
            PushOutput( DIFF_TEMP2 );

            // Shaders
            AddDispatch( REBLUR_Diffuse_CopyStabilizedHistory, REBLUR_COPY_STABILIZED_HISTORY_CONSTANT_NUM, REBLUR_COPY_STABILIZED_HISTORY_GROUP_DIM, USE_MAX_DIMS );
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
            PushInput( DIFF_TEMP2 );

            // Outputs
            PushOutput( AsUint(Permanent::PREV_INTERNAL_DATA) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

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
            PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

            // Outputs
            PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

            // Shaders
            AddDispatch( REBLUR_Diffuse_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_GROUP_DIM, 1 );
        }
    }

    #undef METHOD_NAME
    #undef DIFF_TEMP1
    #undef DIFF_TEMP2

    return sizeof(ReblurSettings);
}

void nrd::DenoiserImpl::UpdateMethod_Reblur(const MethodData& methodData, bool isDiffuse, bool isSpecular)
{
    enum class Dispatch
    {
        HITDIST_RECONSTRUCTION,
        PREPASS                 = HITDIST_RECONSTRUCTION + REBLUR_HITDIST_RECONSTRUCTION_PERMUTATION_NUM * 2,
        TEMPORAL_ACCUMULATION   = PREPASS + REBLUR_PREPASS_PERMUTATION_NUM * 2,
        HISTORY_FIX             = TEMPORAL_ACCUMULATION + REBLUR_TEMPORAL_ACCUMULATION_PERMUTATION_NUM * 2,
        BLUR                    = HISTORY_FIX + REBLUR_HISTORY_FIX_PERMUTATION_NUM * 2,
        POST_BLUR               = BLUR + REBLUR_BLUR_PERMUTATION_NUM * 2,
        COPY_STABILIZED_HISTORY = POST_BLUR + REBLUR_POST_BLUR_PERMUTATION_NUM * 2,
        TEMPORAL_STABILIZATION  = COPY_STABILIZED_HISTORY + REBLUR_COPY_STABILIZED_HISTORY_PERMUTATION_NUM * 1, // no perf mode for copy
        SPLIT_SCREEN            = TEMPORAL_STABILIZATION + REBLUR_TEMPORAL_STABILIZATION_PERMUTATION_NUM * 2,
    };

    const ReblurSettings& settings = methodData.settings.reblur;

    bool skipTemporalStabilization = settings.stabilizationStrength == 0.0f;
    bool skipPrePass = (settings.diffusePrepassBlurRadius == 0.0f || !isDiffuse) &&
        (settings.specularPrepassBlurRadius == 0.0f || !isSpecular) &&
        settings.checkerboardMode == CheckerboardMode::OFF;

    bool enableHitDistanceReconstruction = settings.hitDistanceReconstructionMode != HitDistanceReconstructionMode::OFF && settings.checkerboardMode == CheckerboardMode::OFF;
    ml::float4 antilagMinMaxThreshold = ml::float4(settings.antilagIntensitySettings.thresholdMin, settings.antilagHitDistanceSettings.thresholdMin, settings.antilagIntensitySettings.thresholdMax, settings.antilagHitDistanceSettings.thresholdMax);

    if (!settings.antilagIntensitySettings.enable || settings.enableReferenceAccumulation)
    {
        antilagMinMaxThreshold.x = 99998.0f;
        antilagMinMaxThreshold.z = 99999.0f;
    }

    if (!settings.antilagHitDistanceSettings.enable || settings.enableReferenceAccumulation)
    {
        antilagMinMaxThreshold.y = 99998.0f;
        antilagMinMaxThreshold.w = 99999.0f;
    }

    uint32_t specCheckerboard = 2;
    uint32_t diffCheckerboard = 2;

    switch (settings.checkerboardMode)
    {
        case nrd::CheckerboardMode::BLACK:
            diffCheckerboard = 0;
            specCheckerboard = 1;
            break;
        case nrd::CheckerboardMode::WHITE:
            diffCheckerboard = 1;
            specCheckerboard = 0;
            break;
        default:
            break;
    }

    NRD_DECLARE_DIMS;

    float disocclusionThreshold = m_CommonSettings.disocclusionThreshold + (1.0f + m_JitterDelta) / float(rectH);

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Reblur(methodData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);

        return;
    }

    // HITDIST_RECONSTRUCTION
    if (enableHitDistanceReconstruction)
    {
        uint32_t passIndex = AsUint(Dispatch::HITDIST_RECONSTRUCTION) + (settings.hitDistanceReconstructionMode == nrd::HitDistanceReconstructionMode::AREA_5X5 ? 4 : 0) + (!skipPrePass ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
        Constant* data = PushDispatch(methodData, passIndex);
        AddSharedConstants_Reblur(methodData, settings, data);
        ValidateConstants(data);
    }

    // PREPASS
    if (!skipPrePass)
    {
        uint32_t passIndex = AsUint(Dispatch::PREPASS) + (settings.enableAdvancedPrepass ? 4 : 0) + (enableHitDistanceReconstruction ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
        Constant* data = PushDispatch(methodData, passIndex);
        AddSharedConstants_Reblur(methodData, settings, data);
        AddFloat4(data, m_Rotator_PrePass);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }

    // TEMPORAL_ACCUMULATION
    uint32_t passIndex = AsUint(Dispatch::TEMPORAL_ACCUMULATION) + (skipTemporalStabilization ? 0 : 8) + (m_CommonSettings.isHistoryConfidenceInputsAvailable ? 4 : 0) + ((!skipPrePass || enableHitDistanceReconstruction) ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
    Constant* data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldPrevToWorld);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, disocclusionThreshold));
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddUint(data, diffCheckerboard);
    AddUint(data, specCheckerboard);
    AddUint(data, skipPrePass ? 0 : 1);
    ValidateConstants(data);

    // HISTORY_FIX
    passIndex = AsUint(Dispatch::HISTORY_FIX) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4(data, m_Rotator_HistoryFix);
    AddFloat(data, settings.historyFixStrideBetweenSamples);
    ValidateConstants(data);

    // BLUR
    passIndex = AsUint(Dispatch::BLUR) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4(data, m_Rotator_Blur);
    AddFloat(data, settings.maxAdaptiveRadiusScale);
    ValidateConstants(data);

    // POST_BLUR
    passIndex = AsUint(Dispatch::POST_BLUR) + (skipTemporalStabilization ? 0 : 2) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4(data, m_Rotator_PostBlur);
    AddFloat(data, settings.maxAdaptiveRadiusScale);
    ValidateConstants(data);

    // COPY_STABILIZED_HISTORY
    if (!skipTemporalStabilization)
    {
        passIndex = AsUint(Dispatch::COPY_STABILIZED_HISTORY);
        data = PushDispatch(methodData, passIndex);
        ValidateConstants(data);
    }

    // TEMPORAL_STABILIZATION
    if (!skipTemporalStabilization)
    {
        passIndex = AsUint(Dispatch::TEMPORAL_STABILIZATION) + (settings.enablePerformanceMode ? 1 : 0);
        data = PushDispatch(methodData, passIndex);
        AddSharedConstants_Reblur(methodData, settings, data);
        AddFloat4x4(data, m_WorldToClip);
        AddFloat4x4(data, m_WorldToClipPrev);
        AddFloat4(data, antilagMinMaxThreshold );
        AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, settings.stabilizationStrength));
        AddFloat2(data, settings.antilagIntensitySettings.sigmaScale, settings.antilagHitDistanceSettings.sigmaScale);
        AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
        ValidateConstants(data);
    }

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Reblur(methodData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }
}

void nrd::DenoiserImpl::AddSharedConstants_Reblur(const MethodData& methodData, const ReblurSettings& settings, Constant*& data)
{
    NRD_DECLARE_DIMS;

    uint32_t maxAccumulatedFrameNum = ml::Min(settings.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM);
    ml::float4 hitDistParams = ml::float4(settings.hitDistanceParameters.A, settings.hitDistanceParameters.B, settings.hitDistanceParameters.C, settings.hitDistanceParameters.D);

    // DRS will increase reprojected values, needed for stability, compensated by blur radius adjustment
    float unproject = 1.0f / (0.5f * rectH * m_ProjectY);

    AddFloat4x4(data, m_ViewToClip);
    AddFloat4x4(data, m_ViewToWorld);

    AddFloat4(data, m_Frustum);
    AddFloat4(data, hitDistParams);
    AddFloat4(data, ml::float4(m_ViewDirection.x, m_ViewDirection.y, m_ViewDirection.z, 0.0f));
    AddFloat4(data, ml::float4(m_ViewDirectionPrev.x, m_ViewDirectionPrev.y, m_ViewDirectionPrev.z, 0.0f));

    AddFloat2(data, 1.0f / float(screenW), 1.0f / float(screenH));
    AddFloat2(data, float(screenW), float(screenH));

    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat2(data, float(rectW), float(rectH));

    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));

    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));
    AddFloat2(data, settings.antilagIntensitySettings.sensitivityToDarkness + 1e-6f, settings.antilagHitDistanceSettings.sensitivityToDarkness + 1e-6f);

    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddFloat(data, settings.enableReferenceAccumulation ? 1.0f : 0.0f);
    AddFloat(data, m_IsOrtho);

    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.debug);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, settings.planeDistanceSensitivity);

    AddFloat(data, m_FrameRateScale);
    AddFloat(data, settings.enableReferenceAccumulation ? 0.0f : settings.blurRadius);
    AddFloat(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 0 : float(maxAccumulatedFrameNum));
    AddFloat(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 0 : float(settings.maxFastAccumulatedFrameNum));

    AddFloat(data, settings.enableAntiFirefly ? 1.0f : 0.0f);
    AddFloat(data, settings.minConvergedStateBaseRadiusScale);
    AddFloat(data, settings.lobeAngleFraction);
    AddFloat(data, settings.roughnessFraction);

    AddFloat(data, settings.responsiveAccumulationRoughnessThreshold);
    AddFloat(data, settings.diffusePrepassBlurRadius);
    AddFloat(data, settings.specularPrepassBlurRadius);
    AddFloat(data, (float)ml::Max(settings.historyFixFrameNum, 1u));

    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, settings.enableMaterialTestForDiffuse ? 1 : 0);
    AddUint(data, settings.enableMaterialTestForSpecular ? 1 : 0);
}
