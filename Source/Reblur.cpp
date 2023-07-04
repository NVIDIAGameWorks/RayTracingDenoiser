/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "InstanceImpl.h"

#include <array>

// Constants
#define REBLUR_SET_SHARED_CONSTANTS                                 SetSharedConstants(2, 5, 10, 24)

#define REBLUR_CLASSIFY_TILES_CONSTANT_NUM                          SumConstants(0, 0, 0, 1, false)
#define REBLUR_CLASSIFY_TILES_NUM_THREADS                           NumThreads(16, 16)

#define REBLUR_HITDIST_RECONSTRUCTION_CONSTANT_NUM                  SumConstants(0, 0, 0, 0)
#define REBLUR_HITDIST_RECONSTRUCTION_NUM_THREADS                   NumThreads(8, 8)

#define REBLUR_PREPASS_CONSTANT_NUM                                 SumConstants(0, 1, 0, 2)
#define REBLUR_PREPASS_NUM_THREADS                                  NumThreads(16, 16)

#define REBLUR_TEMPORAL_ACCUMULATION_CONSTANT_NUM                   SumConstants(4, 2, 0, 7)
#define REBLUR_TEMPORAL_ACCUMULATION_NUM_THREADS                    NumThreads(8, 8)

#define REBLUR_HISTORY_FIX_CONSTANT_NUM                             SumConstants(0, 0, 0, 1)
#define REBLUR_HISTORY_FIX_NUM_THREADS                              NumThreads(16, 16)

#define REBLUR_BLUR_CONSTANT_NUM                                    SumConstants(0, 1, 0, 0)
#define REBLUR_BLUR_NUM_THREADS                                     NumThreads(8, 8)

#define REBLUR_POST_BLUR_CONSTANT_NUM                               SumConstants(0, 1, 0, 0)
#define REBLUR_POST_BLUR_NUM_THREADS                                NumThreads(8, 8)

#define REBLUR_COPY_STABILIZED_HISTORY_CONSTANT_NUM                 SumConstants(0, 0, 0, 1, false)
#define REBLUR_COPY_STABILIZED_HISTORY_NUM_THREADS                  NumThreads(16, 16)

#define REBLUR_TEMPORAL_STABILIZATION_CONSTANT_NUM                  SumConstants(3, 3, 2, 1)
#define REBLUR_TEMPORAL_STABILIZATION_NUM_THREADS                   NumThreads(8, 8)

#define REBLUR_SPLIT_SCREEN_CONSTANT_NUM                            SumConstants(0, 0, 0, 3)
#define REBLUR_SPLIT_SCREEN_NUM_THREADS                             NumThreads(16, 16)

// Permutations
#define REBLUR_CLASSIFY_TILES_PERMUTATION_NUM                       1
#define REBLUR_HITDIST_RECONSTRUCTION_PERMUTATION_NUM               4
#define REBLUR_PREPASS_PERMUTATION_NUM                              2
#define REBLUR_TEMPORAL_ACCUMULATION_PERMUTATION_NUM                16
#define REBLUR_HISTORY_FIX_PERMUTATION_NUM                          1
#define REBLUR_BLUR_PERMUTATION_NUM                                 1
#define REBLUR_POST_BLUR_PERMUTATION_NUM                            2
#define REBLUR_COPY_STABILIZED_HISTORY_PERMUTATION_NUM              1
#define REBLUR_TEMPORAL_STABILIZATION_PERMUTATION_NUM               2
#define REBLUR_SPLIT_SCREEN_PERMUTATION_NUM                         1

#define REBLUR_OCCLUSION_HITDIST_RECONSTRUCTION_PERMUTATION_NUM     2
#define REBLUR_OCCLUSION_TEMPORAL_ACCUMULATION_PERMUTATION_NUM      8
#define REBLUR_OCCLUSION_HISTORY_FIX_PERMUTATION_NUM                1
#define REBLUR_OCCLUSION_BLUR_PERMUTATION_NUM                       1
#define REBLUR_OCCLUSION_POST_BLUR_PERMUTATION_NUM                  1
#define REBLUR_OCCLUSION_SPLIT_SCREEN_PERMUTATION_NUM               1

// Formats
#define REBLUR_FORMAT                                               Format::RGBA16_SFLOAT

#define REBLUR_FORMAT_DIFF_FAST_HISTORY                             Format::R16_SFLOAT
#define REBLUR_FORMAT_SPEC_FAST_HISTORY                             Format::RG16_SFLOAT // .y = hit distance for tracking

#define REBLUR_FORMAT_SPEC_HITDIST_FOR_TRACKING                     Format::R16_UNORM // use R16_SFLOAT if pre-pass outputs unnormalized hit distance

#define REBLUR_FORMAT_OCCLUSION                                     Format::R16_UNORM

#define REBLUR_FORMAT_DIRECTIONAL_OCCLUSION                         Format::RGBA16_SNORM
#define REBLUR_FORMAT_DIRECTIONAL_OCCLUSION_FAST_HISTORY            REBLUR_FORMAT_OCCLUSION

#define REBLUR_FORMAT_PREV_VIEWZ                                    Format::R32_SFLOAT
#define REBLUR_FORMAT_PREV_NORMAL_ROUGHNESS                         Format::RGBA8_UNORM
#define REBLUR_FORMAT_PREV_INTERNAL_DATA                            Format::R16_UINT

// Other
#define REBLUR_DUMMY                                                AsUint(ResourceType::IN_VIEWZ)

#define REBLUR_ADD_VALIDATION_DISPATCH( data2, diff, spec ) \
    PushPass("Validation"); \
    { \
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) ); \
        PushInput( AsUint(ResourceType::IN_VIEWZ) ); \
        PushInput( AsUint(ResourceType::IN_MV) ); \
        PushInput( AsUint(Transient::DATA1) ); \
        PushInput( AsUint(data2) ); \
        PushInput( AsUint(diff) ); \
        PushInput( AsUint(spec) ); \
        PushOutput( AsUint(ResourceType::OUT_VALIDATION) ); \
        AddDispatch( REBLUR_Validation, SumConstants(1, 0, 1, 4), NumThreads(16, 16), IGNORE_RS ); \
    }

struct ReblurProps
{
    bool hasDiffuse;
    bool hasSpecular;
};

constexpr std::array<ReblurProps, 10> g_ReblurProps =
{{
    {true, false},      // REBLUR_DIFFUSE
    {true, false},      // REBLUR_DIFFUSE_OCCLUSION
    {true, false},      // REBLUR_DIFFUSE_SH
    {false, true},      // REBLUR_SPECULAR
    {false, true},      // REBLUR_SPECULAR_OCCLUSION
    {false, true},      // REBLUR_SPECULAR_SH
    {true, true},       // REBLUR_DIFFUSE_SPECULAR
    {true, true},       // REBLUR_DIFFUSE_SPECULAR_OCCLUSION
    {true, true},       // REBLUR_DIFFUSE_SPECULAR_SH
    {true, false},      // REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION
}};

void nrd::InstanceImpl::Update_Reblur(const DenoiserData& denoiserData)
{
    enum class Dispatch
    {
        CLASSIFY_TILES,
        HITDIST_RECONSTRUCTION  = CLASSIFY_TILES + REBLUR_CLASSIFY_TILES_PERMUTATION_NUM * 1, // CLASSIFY_TILES doesn't have perf mode
        PREPASS                 = HITDIST_RECONSTRUCTION + REBLUR_HITDIST_RECONSTRUCTION_PERMUTATION_NUM * 2,
        TEMPORAL_ACCUMULATION   = PREPASS + REBLUR_PREPASS_PERMUTATION_NUM * 2,
        HISTORY_FIX             = TEMPORAL_ACCUMULATION + REBLUR_TEMPORAL_ACCUMULATION_PERMUTATION_NUM * 2,
        BLUR                    = HISTORY_FIX + REBLUR_HISTORY_FIX_PERMUTATION_NUM * 2,
        POST_BLUR               = BLUR + REBLUR_BLUR_PERMUTATION_NUM * 2,
        COPY_STABILIZED_HISTORY = POST_BLUR + REBLUR_POST_BLUR_PERMUTATION_NUM * 2,
        TEMPORAL_STABILIZATION  = COPY_STABILIZED_HISTORY + REBLUR_COPY_STABILIZED_HISTORY_PERMUTATION_NUM * 1, // COPY_STABILIZED_HISTORY doesn't have perf mode
        SPLIT_SCREEN            = TEMPORAL_STABILIZATION + REBLUR_TEMPORAL_STABILIZATION_PERMUTATION_NUM * 2,
        VALIDATION              = SPLIT_SCREEN + REBLUR_SPLIT_SCREEN_PERMUTATION_NUM * 1, // SPLIT_SCREEN doesn't have perf mode
    };

    NRD_DECLARE_DIMS;

    const ReblurSettings& settings = denoiserData.settings.reblur;
    const ReblurProps& props = g_ReblurProps[ size_t(denoiserData.desc.denoiser) - size_t(Denoiser::REBLUR_DIFFUSE) ];

    bool isRectChanged = rectW != rectWprev || rectH != rectHprev;
    bool enableHitDistanceReconstruction = settings.hitDistanceReconstructionMode != HitDistanceReconstructionMode::OFF && settings.checkerboardMode == CheckerboardMode::OFF;
    bool skipTemporalStabilization = settings.stabilizationStrength == 0.0f;
    bool skipPrePass = (settings.diffusePrepassBlurRadius == 0.0f || !props.hasDiffuse) &&
        (settings.specularPrepassBlurRadius == 0.0f || !props.hasSpecular) &&
        settings.checkerboardMode == CheckerboardMode::OFF;

    float disocclusionThresholdBonus = (1.0f + m_JitterDelta) / float(rectH);
    float disocclusionThreshold = m_CommonSettings.disocclusionThreshold + disocclusionThresholdBonus;
    float disocclusionThresholdAlternate = m_CommonSettings.disocclusionThresholdAlternate + disocclusionThresholdBonus;

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
        case CheckerboardMode::BLACK:
            diffCheckerboard = 0;
            specCheckerboard = 1;
            break;
        case CheckerboardMode::WHITE:
            diffCheckerboard = 1;
            specCheckerboard = 0;
            break;
        default:
            break;
    }

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Reblur(denoiserData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);

        return;
    }

    // CLASSIFY_TILES
    Constant* data = PushDispatch(denoiserData, AsUint(Dispatch::CLASSIFY_TILES));
    AddFloat(data, m_CommonSettings.denoisingRange);
    ValidateConstants(data);

    // HITDIST_RECONSTRUCTION
    if (enableHitDistanceReconstruction)
    {
        uint32_t passIndex = AsUint(Dispatch::HITDIST_RECONSTRUCTION) + (settings.hitDistanceReconstructionMode == HitDistanceReconstructionMode::AREA_5X5 ? 4 : 0) + (!skipPrePass ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
        data = PushDispatch(denoiserData, passIndex);
        AddSharedConstants_Reblur(denoiserData, settings, data);
        ValidateConstants(data);
    }

    // PREPASS
    if (!skipPrePass)
    {
        uint32_t passIndex = AsUint(Dispatch::PREPASS) + (enableHitDistanceReconstruction ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
        data = PushDispatch(denoiserData, passIndex);
        AddSharedConstants_Reblur(denoiserData, settings, data);
        AddFloat4(data, m_Rotator_PrePass);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }

    // TEMPORAL_ACCUMULATION
    uint32_t passIndex = AsUint(Dispatch::TEMPORAL_ACCUMULATION) + (m_CommonSettings.isDisocclusionThresholdMixAvailable ? 16 : 0) +
        (!skipTemporalStabilization ? 8 : 0) + (m_CommonSettings.isHistoryConfidenceAvailable ? 4 : 0) +
        ((!skipPrePass || enableHitDistanceReconstruction) ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(denoiserData, passIndex);
    AddSharedConstants_Reblur(denoiserData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldPrevToWorld);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, disocclusionThreshold));
    AddFloat(data, disocclusionThresholdAlternate);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddUint(data, diffCheckerboard);
    AddUint(data, specCheckerboard);
    AddUint(data, skipPrePass ? 0 : 1);
    AddUint(data, m_CommonSettings.isHistoryConfidenceAvailable ? 1 : 0);
    AddUint(data, m_CommonSettings.isDisocclusionThresholdMixAvailable ? 1 : 0);
    ValidateConstants(data);

    // HISTORY_FIX
    passIndex = AsUint(Dispatch::HISTORY_FIX) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(denoiserData, passIndex);
    AddSharedConstants_Reblur(denoiserData, settings, data);
    AddFloat(data, settings.historyFixStrideBetweenSamples);
    ValidateConstants(data);

    // BLUR
    passIndex = AsUint(Dispatch::BLUR) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(denoiserData, passIndex);
    AddSharedConstants_Reblur(denoiserData, settings, data);
    AddFloat4(data, m_Rotator_Blur);
    ValidateConstants(data);

    // POST_BLUR
    passIndex = AsUint(Dispatch::POST_BLUR) + (skipTemporalStabilization ? 0 : 2) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(denoiserData, passIndex);
    AddSharedConstants_Reblur(denoiserData, settings, data);
    AddFloat4(data, m_Rotator_PostBlur);
    ValidateConstants(data);

    // COPY_STABILIZED_HISTORY
    if (!skipTemporalStabilization)
    {
        passIndex = AsUint(Dispatch::COPY_STABILIZED_HISTORY);
        data = PushDispatch(denoiserData, passIndex);
        AddUint(data, isRectChanged ? 1 : 0);
        ValidateConstants(data);
    }

    // TEMPORAL_STABILIZATION
    if (!skipTemporalStabilization)
    {
        passIndex = AsUint(Dispatch::TEMPORAL_STABILIZATION) + (m_CommonSettings.isBaseColorMetalnessAvailable ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
        data = PushDispatch(denoiserData, passIndex);
        AddSharedConstants_Reblur(denoiserData, settings, data);
        AddFloat4x4(data, m_WorldToClip);
        AddFloat4x4(data, m_WorldToClipPrev);
        AddFloat4x4(data, m_WorldToViewPrev);
        AddFloat4(data, m_FrustumPrev);
        AddFloat4(data, antilagMinMaxThreshold );
        AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, settings.stabilizationStrength));
        AddFloat2(data, settings.antilagIntensitySettings.sigmaScale, settings.antilagHitDistanceSettings.sigmaScale);
        if (m_CommonSettings.isBaseColorMetalnessAvailable)
            AddFloat2(data, settings.specularProbabilityThresholdsForMvModification[0], settings.specularProbabilityThresholdsForMvModification[1]);
        else
            AddFloat2(data, 2.0f, 3.0f);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);
    }

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Reblur(denoiserData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }

    // VALIDATION
    if (m_CommonSettings.enableValidation)
    {
        data = PushDispatch(denoiserData, AsUint(Dispatch::VALIDATION));
        AddSharedConstants_Reblur(denoiserData, settings, data);
        AddFloat4x4(data, m_WorldToClipPrev);
        AddFloat2(data, m_CommonSettings.cameraJitter[0], m_CommonSettings.cameraJitter[1]);
        AddUint(data, props.hasDiffuse ? 1 : 0);
        AddUint(data, props.hasSpecular ? 1 : 0);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }
}

void nrd::InstanceImpl::Update_ReblurOcclusion(const DenoiserData& denoiserData)
{
    enum class Dispatch
    {
        CLASSIFY_TILES,
        HITDIST_RECONSTRUCTION  = CLASSIFY_TILES + REBLUR_CLASSIFY_TILES_PERMUTATION_NUM * 1, // CLASSIFY_TILES doesn't have perf mode
        TEMPORAL_ACCUMULATION   = HITDIST_RECONSTRUCTION + REBLUR_OCCLUSION_HITDIST_RECONSTRUCTION_PERMUTATION_NUM * 2,
        HISTORY_FIX             = TEMPORAL_ACCUMULATION + REBLUR_OCCLUSION_TEMPORAL_ACCUMULATION_PERMUTATION_NUM * 2,
        BLUR                    = HISTORY_FIX + REBLUR_OCCLUSION_HISTORY_FIX_PERMUTATION_NUM * 2,
        POST_BLUR               = BLUR + REBLUR_OCCLUSION_BLUR_PERMUTATION_NUM * 2,
        SPLIT_SCREEN            = POST_BLUR + REBLUR_OCCLUSION_POST_BLUR_PERMUTATION_NUM * 2,
        VALIDATION              = SPLIT_SCREEN + REBLUR_SPLIT_SCREEN_PERMUTATION_NUM * 1, // SPLIT_SCREEN doesn't have perf mode
    };

    NRD_DECLARE_DIMS;

    const ReblurSettings& settings = denoiserData.settings.reblur;
    const ReblurProps& props = g_ReblurProps[ size_t(denoiserData.desc.denoiser) - size_t(Denoiser::REBLUR_DIFFUSE) ];

    bool enableHitDistanceReconstruction = settings.hitDistanceReconstructionMode != HitDistanceReconstructionMode::OFF && settings.checkerboardMode == CheckerboardMode::OFF;

    float disocclusionThresholdBonus = (1.0f + m_JitterDelta) / float(rectH);
    float disocclusionThreshold = m_CommonSettings.disocclusionThreshold + disocclusionThresholdBonus;
    float disocclusionThresholdAlternate = m_CommonSettings.disocclusionThresholdAlternate + disocclusionThresholdBonus;

    uint32_t specCheckerboard = 2;
    uint32_t diffCheckerboard = 2;

    switch (settings.checkerboardMode)
    {
        case CheckerboardMode::BLACK:
            diffCheckerboard = 0;
            specCheckerboard = 1;
            break;
        case CheckerboardMode::WHITE:
            diffCheckerboard = 1;
            specCheckerboard = 0;
            break;
        default:
            break;
    }

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Reblur(denoiserData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);

        return;
    }

    // CLASSIFY_TILES
    Constant* data = PushDispatch(denoiserData, AsUint(Dispatch::CLASSIFY_TILES));
    AddFloat(data, m_CommonSettings.denoisingRange);
    ValidateConstants(data);

    // HITDIST_RECONSTRUCTION
    if (enableHitDistanceReconstruction)
    {
        uint32_t passIndex = AsUint(Dispatch::HITDIST_RECONSTRUCTION) + (settings.hitDistanceReconstructionMode == HitDistanceReconstructionMode::AREA_5X5 ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
        data = PushDispatch(denoiserData, passIndex);
        AddSharedConstants_Reblur(denoiserData, settings, data);
        ValidateConstants(data);
    }

    // TEMPORAL_ACCUMULATION
    uint32_t passIndex = AsUint(Dispatch::TEMPORAL_ACCUMULATION) + (m_CommonSettings.isDisocclusionThresholdMixAvailable ? 8 : 0) +
        (m_CommonSettings.isHistoryConfidenceAvailable ? 4 : 0) + (enableHitDistanceReconstruction ? 2 : 0) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(denoiserData, passIndex);
    AddSharedConstants_Reblur(denoiserData, settings, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldPrevToWorld);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, disocclusionThreshold));
    AddFloat(data, disocclusionThresholdAlternate);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddUint(data, diffCheckerboard);
    AddUint(data, specCheckerboard);
    AddUint(data, 0);
    AddUint(data, m_CommonSettings.isHistoryConfidenceAvailable);
    AddUint(data, m_CommonSettings.isDisocclusionThresholdMixAvailable);
    ValidateConstants(data);

    // HISTORY_FIX
    passIndex = AsUint(Dispatch::HISTORY_FIX) + (!settings.enableAntiFirefly ? 1 : 0);
    data = PushDispatch(denoiserData, passIndex);
    AddSharedConstants_Reblur(denoiserData, settings, data);
    AddFloat(data, settings.historyFixStrideBetweenSamples);
    ValidateConstants(data);

    // BLUR
    passIndex = AsUint(Dispatch::BLUR) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(denoiserData, passIndex);
    AddSharedConstants_Reblur(denoiserData, settings, data);
    AddFloat4(data, m_Rotator_Blur);
    ValidateConstants(data);

    // POST_BLUR
    passIndex = AsUint(Dispatch::POST_BLUR) + (settings.enablePerformanceMode ? 1 : 0);
    data = PushDispatch(denoiserData, passIndex);
    AddSharedConstants_Reblur(denoiserData, settings, data);
    AddFloat4(data, m_Rotator_PostBlur);
    ValidateConstants(data);

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Reblur(denoiserData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }

    // VALIDATION
    if (m_CommonSettings.enableValidation)
    {
        data = PushDispatch(denoiserData, AsUint(Dispatch::VALIDATION));
        AddSharedConstants_Reblur(denoiserData, settings, data);
        AddFloat4x4(data, m_WorldToClipPrev);
        AddFloat2(data, m_CommonSettings.cameraJitter[0], m_CommonSettings.cameraJitter[1]);
        AddUint(data, props.hasDiffuse ? 1 : 0);
        AddUint(data, props.hasSpecular ? 1 : 0);
        AddUint(data, diffCheckerboard);
        AddUint(data, specCheckerboard);
        ValidateConstants(data);
    }
}

void nrd::InstanceImpl::AddSharedConstants_Reblur(const DenoiserData& denoiserData, const ReblurSettings& settings, Constant*& data)
{
    NRD_DECLARE_DIMS;

    bool isHistoryReset = m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE;
    float unproject = 1.0f / (0.5f * rectH * m_ProjectY);
    uint32_t maxAccumulatedFrameNum = ml::Min(settings.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM);

    AddFloat4x4(data, m_ViewToClip);
    AddFloat4x4(data, m_ViewToWorld);

    AddFloat4(data, m_Frustum);
    AddFloat4(data, ml::float4(settings.hitDistanceParameters.A, settings.hitDistanceParameters.B, settings.hitDistanceParameters.C, settings.hitDistanceParameters.D));
    AddFloat4(data, ml::float4(m_ViewDirection.x, m_ViewDirection.y, m_ViewDirection.z, 0.0f));
    AddFloat4(data, ml::float4(m_ViewDirectionPrev.x, m_ViewDirectionPrev.y, m_ViewDirectionPrev.z, 0.0f));
    AddFloat4(data, ml::float4(m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1], m_CommonSettings.motionVectorScale[2], m_CommonSettings.debug));

    AddFloat2(data, 1.0f / float(screenW), 1.0f / float(screenH));
    AddFloat2(data, float(screenW), float(screenH));

    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat2(data, float(rectW), float(rectH));

    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));
    AddFloat2(data, float(rectWprev) / float(screenW), float(rectHprev) / float(screenH));

    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddFloat2(data, settings.antilagIntensitySettings.sensitivityToDarkness + 1e-6f, settings.antilagHitDistanceSettings.sensitivityToDarkness + 1e-6f);

    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);

    AddFloat(data, settings.enableReferenceAccumulation ? 0.0f : 1.0f);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.denoisingRange);

    AddFloat(data, settings.planeDistanceSensitivity);
    AddFloat(data, m_FrameRateScale);
    AddFloat(data, settings.enableReferenceAccumulation ? 0.0f : settings.blurRadius);
    AddFloat(data, isHistoryReset ? 0 : float(maxAccumulatedFrameNum));

    AddFloat(data, float(settings.maxFastAccumulatedFrameNum));
    AddFloat(data, settings.enableAntiFirefly ? 1.0f : 0.0f);
    AddFloat(data, settings.lobeAngleFraction);
    AddFloat(data, settings.roughnessFraction);

    AddFloat(data, settings.responsiveAccumulationRoughnessThreshold);
    AddFloat(data, settings.diffusePrepassBlurRadius);
    AddFloat(data, settings.specularPrepassBlurRadius);
    AddFloat(data, (float)settings.historyFixFrameNum);

    AddFloat(data, (float)ml::Min(rectW, rectH) * unproject);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, settings.enableMaterialTestForDiffuse ? 1 : 0);

    AddUint(data, settings.enableMaterialTestForSpecular ? 1 : 0);
    AddUint(data, isHistoryReset ? 1 : 0);
    AddUint(data, 0);
    AddUint(data, 0);
}

// REBLUR_SHARED
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_ClassifyTiles.cs.dxbc.h"
    #include "REBLUR_Validation.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_ClassifyTiles.cs.dxil.h"
    #include "REBLUR_Validation.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_ClassifyTiles.cs.spirv.h"
    #include "REBLUR_Validation.cs.spirv.h"
#endif

// REBLUR_DIFFUSE
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_Diffuse_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_Diffuse_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_Diffuse_PrePass.cs.dxbc.h"
    #include "REBLUR_Diffuse_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Diffuse_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Diffuse_Blur.cs.dxbc.h"
    #include "REBLUR_Diffuse_PostBlur.cs.dxbc.h"
    #include "REBLUR_Diffuse_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Diffuse_CopyStabilizedHistory.cs.dxbc.h"
    #include "REBLUR_Diffuse_TemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Diffuse_SplitScreen.cs.dxbc.h"

    #include "REBLUR_Perf_Diffuse_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_Perf_Diffuse_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_Perf_Diffuse_PrePass.cs.dxbc.h"
    #include "REBLUR_Perf_Diffuse_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Perf_Diffuse_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Perf_Diffuse_Blur.cs.dxbc.h"
    #include "REBLUR_Perf_Diffuse_PostBlur.cs.dxbc.h"
    #include "REBLUR_Perf_Diffuse_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Perf_Diffuse_TemporalStabilization.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_Diffuse_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_Diffuse_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_Diffuse_PrePass.cs.dxil.h"
    #include "REBLUR_Diffuse_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Diffuse_HistoryFix.cs.dxil.h"
    #include "REBLUR_Diffuse_Blur.cs.dxil.h"
    #include "REBLUR_Diffuse_PostBlur.cs.dxil.h"
    #include "REBLUR_Diffuse_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_Diffuse_CopyStabilizedHistory.cs.dxil.h"
    #include "REBLUR_Diffuse_TemporalStabilization.cs.dxil.h"
    #include "REBLUR_Diffuse_SplitScreen.cs.dxil.h"

    #include "REBLUR_Perf_Diffuse_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_Perf_Diffuse_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_Perf_Diffuse_PrePass.cs.dxil.h"
    #include "REBLUR_Perf_Diffuse_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Perf_Diffuse_HistoryFix.cs.dxil.h"
    #include "REBLUR_Perf_Diffuse_Blur.cs.dxil.h"
    #include "REBLUR_Perf_Diffuse_PostBlur.cs.dxil.h"
    #include "REBLUR_Perf_Diffuse_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_Perf_Diffuse_TemporalStabilization.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_Diffuse_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_Diffuse_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_Diffuse_PrePass.cs.spirv.h"
    #include "REBLUR_Diffuse_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Diffuse_HistoryFix.cs.spirv.h"
    #include "REBLUR_Diffuse_Blur.cs.spirv.h"
    #include "REBLUR_Diffuse_CopyStabilizedHistory.cs.spirv.h"
    #include "REBLUR_Diffuse_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Diffuse_PostBlur.cs.spirv.h"
    #include "REBLUR_Diffuse_PostBlur_NoTemporalStabilization.cs.spirv.h"
    #include "REBLUR_Diffuse_SplitScreen.cs.spirv.h"

    #include "REBLUR_Perf_Diffuse_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_Perf_Diffuse_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_Perf_Diffuse_PrePass.cs.spirv.h"
    #include "REBLUR_Perf_Diffuse_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Perf_Diffuse_HistoryFix.cs.spirv.h"
    #include "REBLUR_Perf_Diffuse_Blur.cs.spirv.h"
    #include "REBLUR_Perf_Diffuse_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Perf_Diffuse_PostBlur.cs.spirv.h"
    #include "REBLUR_Perf_Diffuse_PostBlur_NoTemporalStabilization.cs.spirv.h"
#endif

#include "Denoisers/Reblur_Diffuse.hpp"


// REBLUR_DIFFUSE_OCCLUSION
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_DiffuseOcclusion_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_DiffuseOcclusion_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_DiffuseOcclusion_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_DiffuseOcclusion_HistoryFix.cs.dxbc.h"
    #include "REBLUR_DiffuseOcclusion_Blur.cs.dxbc.h"
    #include "REBLUR_DiffuseOcclusion_PostBlur_NoTemporalStabilization.cs.dxbc.h"

    #include "REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseOcclusion_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseOcclusion_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseOcclusion_Blur.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseOcclusion_PostBlur_NoTemporalStabilization.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_DiffuseOcclusion_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_DiffuseOcclusion_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_DiffuseOcclusion_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_DiffuseOcclusion_HistoryFix.cs.dxil.h"
    #include "REBLUR_DiffuseOcclusion_Blur.cs.dxil.h"
    #include "REBLUR_DiffuseOcclusion_PostBlur_NoTemporalStabilization.cs.dxil.h"

    #include "REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseOcclusion_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseOcclusion_HistoryFix.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseOcclusion_Blur.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseOcclusion_PostBlur_NoTemporalStabilization.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_DiffuseOcclusion_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_DiffuseOcclusion_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_DiffuseOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_DiffuseOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_DiffuseOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_DiffuseOcclusion_PostBlur_NoTemporalStabilization.cs.spirv.h"

    #include "REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseOcclusion_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseOcclusion_PostBlur_NoTemporalStabilization.cs.spirv.h"
#endif

#include "Denoisers/Reblur_DiffuseOcclusion.hpp"


// REBLUR_DIFFUSE_SH
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_DiffuseSh_PrePass.cs.dxbc.h"
    #include "REBLUR_DiffuseSh_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_DiffuseSh_HistoryFix.cs.dxbc.h"
    #include "REBLUR_DiffuseSh_Blur.cs.dxbc.h"
    #include "REBLUR_DiffuseSh_PostBlur.cs.dxbc.h"
    #include "REBLUR_DiffuseSh_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_DiffuseSh_CopyStabilizedHistory.cs.dxbc.h"
    #include "REBLUR_DiffuseSh_TemporalStabilization.cs.dxbc.h"
    #include "REBLUR_DiffuseSh_SplitScreen.cs.dxbc.h"

    #include "REBLUR_Perf_DiffuseSh_PrePass.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSh_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSh_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSh_Blur.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSh_PostBlur.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSh_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSh_TemporalStabilization.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_DiffuseSh_PrePass.cs.dxil.h"
    #include "REBLUR_DiffuseSh_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_DiffuseSh_HistoryFix.cs.dxil.h"
    #include "REBLUR_DiffuseSh_Blur.cs.dxil.h"
    #include "REBLUR_DiffuseSh_PostBlur.cs.dxil.h"
    #include "REBLUR_DiffuseSh_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_DiffuseSh_CopyStabilizedHistory.cs.dxil.h"
    #include "REBLUR_DiffuseSh_TemporalStabilization.cs.dxil.h"
    #include "REBLUR_DiffuseSh_SplitScreen.cs.dxil.h"

    #include "REBLUR_Perf_DiffuseSh_PrePass.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSh_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSh_HistoryFix.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSh_Blur.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSh_PostBlur.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSh_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSh_TemporalStabilization.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_DiffuseSh_PrePass.cs.spirv.h"
    #include "REBLUR_DiffuseSh_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_DiffuseSh_HistoryFix.cs.spirv.h"
    #include "REBLUR_DiffuseSh_Blur.cs.spirv.h"
    #include "REBLUR_DiffuseSh_CopyStabilizedHistory.cs.spirv.h"
    #include "REBLUR_DiffuseSh_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_DiffuseSh_PostBlur.cs.spirv.h"
    #include "REBLUR_DiffuseSh_PostBlur_NoTemporalStabilization.cs.spirv.h"
    #include "REBLUR_DiffuseSh_SplitScreen.cs.spirv.h"

    #include "REBLUR_Perf_DiffuseSh_PrePass.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSh_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSh_HistoryFix.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSh_Blur.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSh_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSh_PostBlur.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSh_PostBlur_NoTemporalStabilization.cs.spirv.h"
#endif

#include "Denoisers/Reblur_DiffuseSh.hpp"


// REBLUR_SPECULAR
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_Specular_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_Specular_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_Specular_PrePass.cs.dxbc.h"
    #include "REBLUR_Specular_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Specular_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Specular_Blur.cs.dxbc.h"
    #include "REBLUR_Specular_PostBlur.cs.dxbc.h"
    #include "REBLUR_Specular_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Specular_CopyStabilizedHistory.cs.dxbc.h"
    #include "REBLUR_Specular_TemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Specular_SplitScreen.cs.dxbc.h"

    #include "REBLUR_Perf_Specular_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_Perf_Specular_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_Perf_Specular_PrePass.cs.dxbc.h"
    #include "REBLUR_Perf_Specular_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Perf_Specular_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Perf_Specular_Blur.cs.dxbc.h"
    #include "REBLUR_Perf_Specular_PostBlur.cs.dxbc.h"
    #include "REBLUR_Perf_Specular_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Perf_Specular_TemporalStabilization.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_Specular_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_Specular_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_Specular_PrePass.cs.dxil.h"
    #include "REBLUR_Specular_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Specular_HistoryFix.cs.dxil.h"
    #include "REBLUR_Specular_Blur.cs.dxil.h"
    #include "REBLUR_Specular_PostBlur.cs.dxil.h"
    #include "REBLUR_Specular_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_Specular_CopyStabilizedHistory.cs.dxil.h"
    #include "REBLUR_Specular_TemporalStabilization.cs.dxil.h"
    #include "REBLUR_Specular_SplitScreen.cs.dxil.h"

    #include "REBLUR_Perf_Specular_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_Perf_Specular_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_Perf_Specular_PrePass.cs.dxil.h"
    #include "REBLUR_Perf_Specular_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Perf_Specular_HistoryFix.cs.dxil.h"
    #include "REBLUR_Perf_Specular_Blur.cs.dxil.h"
    #include "REBLUR_Perf_Specular_PostBlur.cs.dxil.h"
    #include "REBLUR_Perf_Specular_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_Perf_Specular_TemporalStabilization.cs.dxil.h"

#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_Specular_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_Specular_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_Specular_PrePass.cs.spirv.h"
    #include "REBLUR_Specular_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Specular_HistoryFix.cs.spirv.h"
    #include "REBLUR_Specular_Blur.cs.spirv.h"
    #include "REBLUR_Specular_PostBlur.cs.spirv.h"
    #include "REBLUR_Specular_PostBlur_NoTemporalStabilization.cs.spirv.h"
    #include "REBLUR_Specular_CopyStabilizedHistory.cs.spirv.h"
    #include "REBLUR_Specular_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Specular_SplitScreen.cs.spirv.h"

    #include "REBLUR_Perf_Specular_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_Perf_Specular_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_Perf_Specular_PrePass.cs.spirv.h"
    #include "REBLUR_Perf_Specular_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Perf_Specular_HistoryFix.cs.spirv.h"
    #include "REBLUR_Perf_Specular_Blur.cs.spirv.h"
    #include "REBLUR_Perf_Specular_PostBlur.cs.spirv.h"
    #include "REBLUR_Perf_Specular_PostBlur_NoTemporalStabilization.cs.spirv.h"
    #include "REBLUR_Perf_Specular_TemporalStabilization.cs.spirv.h"
#endif

#include "Denoisers/Reblur_Specular.hpp"


// REBLUR_SPECULAR_OCCLUSION
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_SpecularOcclusion_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_SpecularOcclusion_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_SpecularOcclusion_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_SpecularOcclusion_HistoryFix.cs.dxbc.h"
    #include "REBLUR_SpecularOcclusion_Blur.cs.dxbc.h"
    #include "REBLUR_SpecularOcclusion_PostBlur_NoTemporalStabilization.cs.dxbc.h"

    #include "REBLUR_Perf_SpecularOcclusion_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularOcclusion_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularOcclusion_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularOcclusion_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularOcclusion_Blur.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularOcclusion_PostBlur_NoTemporalStabilization.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_SpecularOcclusion_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_SpecularOcclusion_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_SpecularOcclusion_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_SpecularOcclusion_HistoryFix.cs.dxil.h"
    #include "REBLUR_SpecularOcclusion_Blur.cs.dxil.h"
    #include "REBLUR_SpecularOcclusion_PostBlur_NoTemporalStabilization.cs.dxil.h"

    #include "REBLUR_Perf_SpecularOcclusion_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_Perf_SpecularOcclusion_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_Perf_SpecularOcclusion_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Perf_SpecularOcclusion_HistoryFix.cs.dxil.h"
    #include "REBLUR_Perf_SpecularOcclusion_Blur.cs.dxil.h"
    #include "REBLUR_Perf_SpecularOcclusion_PostBlur_NoTemporalStabilization.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_SpecularOcclusion_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_SpecularOcclusion_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_SpecularOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_SpecularOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_SpecularOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_SpecularOcclusion_PostBlur_NoTemporalStabilization.cs.spirv.h"

    #include "REBLUR_Perf_SpecularOcclusion_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_Perf_SpecularOcclusion_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_Perf_SpecularOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Perf_SpecularOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_Perf_SpecularOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_Perf_SpecularOcclusion_PostBlur_NoTemporalStabilization.cs.spirv.h"
#endif

#include "Denoisers/Reblur_SpecularOcclusion.hpp"


// REBLUR_SPECULAR_SH
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_SpecularSh_PrePass.cs.dxbc.h"
    #include "REBLUR_SpecularSh_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_SpecularSh_HistoryFix.cs.dxbc.h"
    #include "REBLUR_SpecularSh_Blur.cs.dxbc.h"
    #include "REBLUR_SpecularSh_PostBlur.cs.dxbc.h"
    #include "REBLUR_SpecularSh_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_SpecularSh_CopyStabilizedHistory.cs.dxbc.h"
    #include "REBLUR_SpecularSh_TemporalStabilization.cs.dxbc.h"
    #include "REBLUR_SpecularSh_SplitScreen.cs.dxbc.h"

    #include "REBLUR_Perf_SpecularSh_PrePass.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularSh_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularSh_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularSh_Blur.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularSh_PostBlur.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularSh_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Perf_SpecularSh_TemporalStabilization.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_SpecularSh_PrePass.cs.dxil.h"
    #include "REBLUR_SpecularSh_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_SpecularSh_HistoryFix.cs.dxil.h"
    #include "REBLUR_SpecularSh_Blur.cs.dxil.h"
    #include "REBLUR_SpecularSh_PostBlur.cs.dxil.h"
    #include "REBLUR_SpecularSh_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_SpecularSh_CopyStabilizedHistory.cs.dxil.h"
    #include "REBLUR_SpecularSh_TemporalStabilization.cs.dxil.h"
    #include "REBLUR_SpecularSh_SplitScreen.cs.dxil.h"

    #include "REBLUR_Perf_SpecularSh_PrePass.cs.dxil.h"
    #include "REBLUR_Perf_SpecularSh_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Perf_SpecularSh_HistoryFix.cs.dxil.h"
    #include "REBLUR_Perf_SpecularSh_Blur.cs.dxil.h"
    #include "REBLUR_Perf_SpecularSh_PostBlur.cs.dxil.h"
    #include "REBLUR_Perf_SpecularSh_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_Perf_SpecularSh_TemporalStabilization.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_SpecularSh_PrePass.cs.spirv.h"
    #include "REBLUR_SpecularSh_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_SpecularSh_HistoryFix.cs.spirv.h"
    #include "REBLUR_SpecularSh_Blur.cs.spirv.h"
    #include "REBLUR_SpecularSh_PostBlur.cs.spirv.h"
    #include "REBLUR_SpecularSh_PostBlur_NoTemporalStabilization.cs.spirv.h"
    #include "REBLUR_SpecularSh_CopyStabilizedHistory.cs.spirv.h"
    #include "REBLUR_SpecularSh_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_SpecularSh_SplitScreen.cs.spirv.h"

    #include "REBLUR_Perf_SpecularSh_PrePass.cs.spirv.h"
    #include "REBLUR_Perf_SpecularSh_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Perf_SpecularSh_HistoryFix.cs.spirv.h"
    #include "REBLUR_Perf_SpecularSh_Blur.cs.spirv.h"
    #include "REBLUR_Perf_SpecularSh_PostBlur.cs.spirv.h"
    #include "REBLUR_Perf_SpecularSh_PostBlur_NoTemporalStabilization.cs.spirv.h"
    #include "REBLUR_Perf_SpecularSh_TemporalStabilization.cs.spirv.h"
#endif

#include "Denoisers/Reblur_SpecularSh.hpp"


// REBLUR_DIFFUSE_SPECULAR
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_DiffuseSpecular_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecular_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecular_PrePass.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecular_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecular_HistoryFix.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecular_Blur.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecular_CopyStabilizedHistory.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecular_TemporalStabilization.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecular_PostBlur.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecular_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecular_SplitScreen.cs.dxbc.h"

    #include "REBLUR_Perf_DiffuseSpecular_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecular_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecular_PrePass.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecular_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecular_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecular_Blur.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecular_TemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecular_PostBlur.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecular_PostBlur_NoTemporalStabilization.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_DiffuseSpecular_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_DiffuseSpecular_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_DiffuseSpecular_PrePass.cs.dxil.h"
    #include "REBLUR_DiffuseSpecular_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_DiffuseSpecular_HistoryFix.cs.dxil.h"
    #include "REBLUR_DiffuseSpecular_Blur.cs.dxil.h"
    #include "REBLUR_DiffuseSpecular_CopyStabilizedHistory.cs.dxil.h"
    #include "REBLUR_DiffuseSpecular_TemporalStabilization.cs.dxil.h"
    #include "REBLUR_DiffuseSpecular_PostBlur.cs.dxil.h"
    #include "REBLUR_DiffuseSpecular_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_DiffuseSpecular_SplitScreen.cs.dxil.h"

    #include "REBLUR_Perf_DiffuseSpecular_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecular_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecular_PrePass.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecular_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecular_HistoryFix.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecular_Blur.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecular_TemporalStabilization.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecular_PostBlur.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecular_PostBlur_NoTemporalStabilization.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_DiffuseSpecular_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_PrePass.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_HistoryFix.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_Blur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_CopyStabilizedHistory.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_PostBlur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_PostBlur_NoTemporalStabilization.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_SplitScreen.cs.spirv.h"

    #include "REBLUR_Perf_DiffuseSpecular_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecular_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecular_PrePass.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecular_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecular_HistoryFix.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecular_Blur.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecular_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecular_PostBlur.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecular_PostBlur_NoTemporalStabilization.cs.spirv.h"
#endif

#include "Denoisers/Reblur_DiffuseSpecular.hpp"


// REBLUR_DIFFUSE_SPECULAR_OCCLUSION
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_DiffuseSpecularOcclusion_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularOcclusion_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularOcclusion_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularOcclusion_HistoryFix.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularOcclusion_Blur.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularOcclusion_PostBlur_NoTemporalStabilization.cs.dxbc.h"

    #include "REBLUR_Perf_DiffuseSpecularOcclusion_HitDistReconstruction.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_HitDistReconstruction_5x5.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_Blur.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_PostBlur_NoTemporalStabilization.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_DiffuseSpecularOcclusion_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularOcclusion_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularOcclusion_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularOcclusion_HistoryFix.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularOcclusion_Blur.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularOcclusion_PostBlur_NoTemporalStabilization.cs.dxil.h"

    #include "REBLUR_Perf_DiffuseSpecularOcclusion_HitDistReconstruction.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_HitDistReconstruction_5x5.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_HistoryFix.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_Blur.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_PostBlur_NoTemporalStabilization.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_DiffuseSpecularOcclusion_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularOcclusion_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularOcclusion_PostBlur_NoTemporalStabilization.cs.spirv.h"

    #include "REBLUR_Perf_DiffuseSpecularOcclusion_HitDistReconstruction.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_HitDistReconstruction_5x5.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularOcclusion_PostBlur_NoTemporalStabilization.cs.spirv.h"
#endif

#include "Denoisers/Reblur_DiffuseSpecularOcclusion.hpp"


// REBLUR_DIFFUSE_SPECULAR_SH
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_DiffuseSpecularSh_PrePass.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularSh_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularSh_HistoryFix.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularSh_Blur.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularSh_CopyStabilizedHistory.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularSh_TemporalStabilization.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularSh_PostBlur.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularSh_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_DiffuseSpecularSh_SplitScreen.cs.dxbc.h"

    #include "REBLUR_Perf_DiffuseSpecularSh_PrePass.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_Blur.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_TemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_PostBlur.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_PostBlur_NoTemporalStabilization.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_DiffuseSpecularSh_PrePass.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularSh_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularSh_HistoryFix.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularSh_Blur.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularSh_CopyStabilizedHistory.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularSh_TemporalStabilization.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularSh_PostBlur.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularSh_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_DiffuseSpecularSh_SplitScreen.cs.dxil.h"

    #include "REBLUR_Perf_DiffuseSpecularSh_PrePass.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_HistoryFix.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_Blur.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_TemporalStabilization.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_PostBlur.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_PostBlur_NoTemporalStabilization.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_DiffuseSpecularSh_PrePass.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularSh_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularSh_HistoryFix.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularSh_Blur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularSh_CopyStabilizedHistory.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularSh_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularSh_PostBlur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularSh_PostBlur_NoTemporalStabilization.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularSh_SplitScreen.cs.spirv.h"

    #include "REBLUR_Perf_DiffuseSpecularSh_PrePass.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_HistoryFix.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_Blur.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_PostBlur.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseSpecularSh_PostBlur_NoTemporalStabilization.cs.spirv.h"
#endif

#include "Denoisers/Reblur_DiffuseSpecularSh.hpp"


// REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REBLUR_DiffuseDirectionalOcclusion_PrePass.cs.dxbc.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_HistoryFix.cs.dxbc.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_Blur.cs.dxbc.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_PostBlur.cs.dxbc.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_TemporalStabilization.cs.dxbc.h"

    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_PrePass.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_TemporalAccumulation.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_HistoryFix.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_Blur.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_PostBlur.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_PostBlur_NoTemporalStabilization.cs.dxbc.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_TemporalStabilization.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REBLUR_DiffuseDirectionalOcclusion_PrePass.cs.dxil.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_HistoryFix.cs.dxil.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_Blur.cs.dxil.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_PostBlur.cs.dxil.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_TemporalStabilization.cs.dxil.h"

    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_PrePass.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_TemporalAccumulation.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_HistoryFix.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_Blur.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_PostBlur.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_PostBlur_NoTemporalStabilization.cs.dxil.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_TemporalStabilization.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REBLUR_DiffuseDirectionalOcclusion_PrePass.cs.spirv.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_PostBlur.cs.spirv.h"
    #include "REBLUR_DiffuseDirectionalOcclusion_PostBlur_NoTemporalStabilization.cs.spirv.h"

    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_PrePass.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_PostBlur.cs.spirv.h"
    #include "REBLUR_Perf_DiffuseDirectionalOcclusion_PostBlur_NoTemporalStabilization.cs.spirv.h"
#endif

#include "Denoisers/Reblur_DiffuseDirectionalOcclusion.hpp"
