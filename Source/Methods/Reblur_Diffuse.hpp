/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define REBLUR_MIP_NUM                                          2
#define REBLUR_SET_SHARED_CONSTANTS                             SetSharedConstants(2, 4, 9, 22)

#define REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM              SumConstants(0, 0, 0, 0)
#define REBLUR_HITDIST_RECONSTRUCTION_GROUP_DIM                 8

#define REBLUR_PREPASS_CONSTANT_NUM                             SumConstants(0, 2, 0, 2)
#define REBLUR_PREPASS_GROUP_DIM                                16

#define REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM               SumConstants(4, 3, 1, 5)
#define REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM                  8

#define REBLUR_MIPGEN_CONSTANT_NUM                              SumConstants(0, 0, 0, 0)
#define REBLUR_MIPGEN_GROUP_DIM                                 16

#define REBLUR_HISTORY_FIX_CONSTANT_NUM                         SumConstants(0, 0, 0, 1)
#define REBLUR_HISTORY_FIX_GROUP_DIM                            8 // TODO: is 16 better for occlusion-only?

#define REBLUR_BLUR_CONSTANT_NUM                                SumConstants(0, 2, 0, 0)
#define REBLUR_BLUR_GROUP_DIM                                   8

#define REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM              SumConstants(2, 2, 2, 1)
#define REBLUR_TEMPORAL_STABILIZATION_GROUP_DIM                 8

#define REBLUR_SPLIT_SCREEN_CONSTANT_NUM                        SumConstants(0, 0, 0, 3)
#define REBLUR_SPLIT_SCREEN_GROUP_DIM                           16

size_t nrd::DenoiserImpl::AddMethod_ReblurDiffuse(uint16_t w, uint16_t h)
{
    #define METHOD_NAME REBLUR_Diffuse
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
            PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

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
                PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

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
                PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

            if (hasConfidenceInputs)
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

            PushOutput( AsUint(Transient::INTERNAL_DATA) );
            PushOutput( AsUint(Transient::DIFF_DATA) );

            if (isAntifireflyEnabled)
                PushOutput( DIFF_TEMP2 );
            else
                PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );

            PushOutput( AsUint(Transient::SCALED_VIEWZ) );

            if (hasConfidenceInputs)
            {
                AddDispatch( REBLUR_Diffuse_TemporalAccumulationWithConfidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulationWithConfidence, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
            else
            {
                AddDispatch( REBLUR_Diffuse_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
                AddDispatch( REBLUR_Perf_Diffuse_TemporalAccumulation, REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM, REBLUR_TEMPORAL_ACCUMULATION_GROUP_DIM, 1 );
            }
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
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        AddDispatch( REBLUR_Diffuse_SplitScreen, REBLUR_SPLIT_SCREEN_CONSTANT_NUM, REBLUR_SPLIT_SCREEN_GROUP_DIM, 1 );
    }

    #undef METHOD_NAME
    #undef DIFF_TEMP1
    #undef DIFF_TEMP2

    return sizeof(ReblurSettings);
}

void nrd::DenoiserImpl::UpdateMethod_Reblur(const MethodData& methodData)
{
    enum class Dispatch
    {
        HITDIST_RECONSTRUCTION,
        PREPASS                 = HITDIST_RECONSTRUCTION + 4 * 2,
        TEMPORAL_ACCUMULATION   = PREPASS + 4 * 2,
        MIP_GEN                 = TEMPORAL_ACCUMULATION + 8 * 2,
        HISTORY_FIX             = MIP_GEN + 1 * 2,
        BLUR                    = HISTORY_FIX + 1 * 2,
        POST_BLUR               = BLUR + 1 * 2,
        TEMPORAL_STABILIZATION  = POST_BLUR + 1 * 2,
        SPLIT_SCREEN            = TEMPORAL_STABILIZATION + 1 * 2,
    };

    const ReblurSettings& settings = methodData.settings.reblur;

    bool skipPrePass = settings.diffusePrepassBlurRadius == 0.0f && settings.specularPrepassBlurRadius == 0.0f && settings.checkerboardMode == CheckerboardMode::OFF;
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
        AddFloat4(data, m_Rotator[0]);
        AddFloat4(data, ml::float4(settings.specularLobeTrimmingParameters.A, settings.specularLobeTrimmingParameters.B, settings.specularLobeTrimmingParameters.C, 0.0f));
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }

    // TEMPORAL_ACCUMULATION
    uint32_t passIndex = AsUint(Dispatch::TEMPORAL_ACCUMULATION) + (settings.enableAntiFirefly ? 8 : 0) + (m_CommonSettings.isHistoryConfidenceInputsAvailable ? 4 : 0) + ((!skipPrePass || enableHitDistanceReconstruction) ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
    Constant* data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldPrevToWorld);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, 0.0f));
    AddFloat4(data, m_Rotator[0]);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, disocclusionThreshold );
    AddUint(data, diffCheckerboard);
    AddUint(data, specCheckerboard);
    AddUint(data, skipPrePass ? 0 : 1);
    ValidateConstants(data);

    // MIP_GEN
    passIndex = AsUint(Dispatch::MIP_GEN) + ((settings.enablePerformanceMode || !settings.enableAntiFirefly) ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    ValidateConstants(data);

    // HISTORY_FIX
    passIndex = AsUint(Dispatch::HISTORY_FIX) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat(data, settings.historyFixStrength);
    ValidateConstants(data);

    // BLUR
    passIndex = AsUint(Dispatch::BLUR) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4(data, m_Rotator[1]);
    AddFloat4(data, ml::float4(settings.specularLobeTrimmingParameters.A, settings.specularLobeTrimmingParameters.B, settings.specularLobeTrimmingParameters.C, settings.maxAdaptiveRadiusScale));
    ValidateConstants(data);

    // POST_BLUR
    passIndex = AsUint(Dispatch::POST_BLUR) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4(data, m_Rotator[2]);
    AddFloat4(data, ml::float4(settings.specularLobeTrimmingParameters.A, settings.specularLobeTrimmingParameters.B, settings.specularLobeTrimmingParameters.C, settings.maxAdaptiveRadiusScale));
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    passIndex = AsUint(Dispatch::TEMPORAL_STABILIZATION) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(methodData, passIndex);
    AddSharedConstants_Reblur(methodData, settings, data);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, 0.0f));
    AddFloat4(data, antilagMinMaxThreshold );
    AddFloat2(data, settings.antilagIntensitySettings.sigmaScale, settings.antilagHitDistanceSettings.sigmaScale );
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, settings.stabilizationStrength);
    ValidateConstants(data);

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
    AddFloat2(data, settings.antilagIntensitySettings.sensitivityToDarkness, settings.antilagHitDistanceSettings.sensitivityToDarkness);

    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddFloat(data, settings.enableReferenceAccumulation ? 1.0f : 0.0f);
    AddFloat(data, m_IsOrtho);

    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.debug);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, settings.planeDistanceSensitivity);

    AddFloat(data, m_FrameRateScale);
    AddFloat(data, settings.enableReferenceAccumulation ? 0.0f : settings.blurRadius);
    AddFloat(data, float( maxAccumulatedFrameNum ));
    AddFloat(data, 0.0f); // TODO: unused

    AddFloat(data, settings.inputMix);
    AddFloat(data, settings.minConvergedStateBaseRadiusScale);
    AddFloat(data, settings.lobeAngleFraction);
    AddFloat(data, settings.roughnessFraction);

    AddFloat(data, settings.responsiveAccumulationRoughnessThreshold);
    AddFloat(data, settings.diffusePrepassBlurRadius);
    AddFloat(data, settings.specularPrepassBlurRadius);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);
    AddUint(data, settings.enableMaterialTestForDiffuse ? 1 : 0);
    AddUint(data, settings.enableMaterialTestForSpecular ? 1 : 0);
}
