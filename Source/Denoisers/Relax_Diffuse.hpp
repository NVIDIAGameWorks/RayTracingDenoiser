/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::InstanceImpl::Add_RelaxDiffuse(DenoiserData& denoiserData)
{
    #define DENOISER_NAME RELAX_Diffuse

    denoiserData.settings.diffuseRelax = RelaxDiffuseSettings();
    denoiserData.settingsSize = sizeof(denoiserData.settings.diffuseRelax);

    uint16_t w = denoiserData.desc.renderWidth;
    uint16_t h = denoiserData.desc.renderHeight;
    uint16_t tilesW = DivideUp(w, 16);
    uint16_t tilesH = DivideUp(h, 16);

    enum class Permanent
    {
        DIFF_ILLUM_PREV = PERMANENT_POOL_START,
        DIFF_ILLUM_RESPONSIVE_PREV,
        HISTORY_LENGTH_PREV,
        NORMAL_ROUGHNESS_PREV,
        MATERIAL_ID_PREV,
        VIEWZ_PREV
    };

    AddTextureToPermanentPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::R8_UNORM, w, h, 1} );
    AddTextureToPermanentPool( {Format::RGBA8_UNORM, w, h, 1} );
    AddTextureToPermanentPool( {Format::R8_UNORM, w, h, 1} );
    AddTextureToPermanentPool( {Format::R32_SFLOAT, w, h, 1} );
    AddTextureToPermanentPool( {Format::R32_SFLOAT, w, h, 1} );

    enum class Transient
    {
        DIFF_ILLUM_PING = TRANSIENT_POOL_START,
        DIFF_ILLUM_PONG,
        TILES,
        HISTORY_LENGTH
    };

    AddTextureToTransientPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::RGBA16_SFLOAT, w, h, 1} );
    AddTextureToTransientPool( {Format::R8_UNORM, tilesW, tilesH, 1} );
    AddTextureToTransientPool( {Format::R8_UNORM, w, h, 1} );

    RELAX_SET_SHARED_CONSTANTS;

    const uint32_t halfMaxPassNum = (RELAX_MAX_ATROUS_PASS_NUM - 2 + 1) / 2;

    PushPass("Classify tiles");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushOutput( AsUint(Transient::TILES) );

        AddDispatch(RELAX_ClassifyTiles, SumConstants(0, 0, 0, 1, false), NumThreads(16, 16), 1);
    }

    PushPass("Hit distance reconstruction"); // 3x3
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

        AddDispatch( RELAX_Diffuse_HitDistReconstruction, SumConstants(0, 0, 0, 0), NumThreads(8, 8), 1 );
    }

    PushPass("Hit distance reconstruction"); // 5x5
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

        AddDispatch( RELAX_Diffuse_HitDistReconstruction_5x5, SumConstants(0, 0, 0, 0), NumThreads(8, 8), 1 );
    }

    PushPass("Pre-pass"); // After hit distance reconstruction
    {
        // Does preblur (if enabled), checkerboard reconstruction (if enabled) and generates FP16 ViewZ texture
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        AddDispatch( RELAX_Diffuse_PrePass, SumConstants(0, 1, 0, 5), NumThreads(16, 16), 1 );
    }

    PushPass("Pre-pass"); // Without hit distance reconstruction
    {
        // Does preblur (if enabled), checkerboard reconstruction (if enabled) and generates FP16 ViewZ texture
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

        AddDispatch( RELAX_Diffuse_PrePass, SumConstants(0, 1, 0, 5), NumThreads(16, 16), 1 );
    }

    for (int i = 0; i < 4; i++)
    {
        // The following passes are defined here:
        // TEMPORAL_ACCUMULATION,
        // TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS,
        // TEMPORAL_ACCUMULATION_WITH_THRESHOLD_MIX,
        // TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS_WITH_THRESHOLD_MIX

        PushPass("Temporal accumulation");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
            PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
            PushInput( AsUint(Permanent::VIEWZ_PREV) );
            PushInput( AsUint(Permanent::HISTORY_LENGTH_PREV) );
            PushInput( AsUint(Permanent::MATERIAL_ID_PREV) );
            // Confidence inputs:
            if (i == 0)
            {
                PushInput( AsUint(ResourceType::IN_VIEWZ) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(Permanent::HISTORY_LENGTH_PREV) ); // Bogus input that will not be fetched anyway
            }
            if (i == 1)
            {
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
                PushInput( AsUint(Permanent::HISTORY_LENGTH_PREV) ); // Bogus input that will not be fetched anyway
            }
            if (i == 2)
            {
                PushInput( AsUint(ResourceType::IN_VIEWZ) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) );
            }
            if (i == 3)
            {
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
                PushInput( AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) );
            }

            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushOutput( AsUint(Transient::HISTORY_LENGTH) );

            AddDispatch( RELAX_Diffuse_TemporalAccumulation, SumConstants(0, 0, 0, 8), NumThreads(8, 8), 1 );
        }
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Transient::HISTORY_LENGTH) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );

        AddDispatch( RELAX_Diffuse_HistoryFix, SumConstants(0, 0, 0, 4), NumThreads(8, 8), 1 );
    }

    PushPass("History clamping"); // with firefly after it
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
        PushInput( AsUint(Transient::HISTORY_LENGTH) );

        PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::HISTORY_LENGTH_PREV) );

        AddDispatch( RELAX_Diffuse_HistoryClamping, SumConstants(0, 0, 0, 3), NumThreads(8, 8), 1 );
    }

    PushPass("History clamping"); // without firefly after it
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
        PushInput( AsUint(Transient::HISTORY_LENGTH) );

        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::HISTORY_LENGTH_PREV) );

        AddDispatch( RELAX_Diffuse_HistoryClamping, SumConstants(0, 0, 0, 3), NumThreads(8, 8), 1 );
    }

    PushPass("Anti-firefly");
    {
        PushInput( AsUint(Transient::TILES) );
        PushInput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );

        AddDispatch( RELAX_Diffuse_AntiFirefly, SumConstants(0, 0, 0, 0), NumThreads(16, 16), 1 );
    }

    for (int i = 0; i < 2; i++)
    {
        bool withConfidenceInputs = (i == 1);

        // A-trous (first)
        PushPass("A-trous (SMEM)");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
            PushInput( AsUint(Transient::HISTORY_LENGTH) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );

            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushOutput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
            PushOutput( AsUint(Permanent::MATERIAL_ID_PREV) );
            PushOutput( AsUint(Permanent::VIEWZ_PREV) );

            AddDispatch( RELAX_Diffuse_AtrousSmem, SumConstants(0, 0, 1, 10), NumThreads(8, 8), 1 );
        }

        // A-trous (odd)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushInput( AsUint(Transient::HISTORY_LENGTH) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );

            PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );

            AddDispatchRepeated( RELAX_Diffuse_Atrous, SumConstants(0, 0, 0, 9), NumThreads(16, 16), 1, halfMaxPassNum );
        }

        // A-trous (even)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushInput( AsUint(Transient::HISTORY_LENGTH) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );

            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

            AddDispatchRepeated( RELAX_Diffuse_Atrous, SumConstants(0, 0, 0, 9), NumThreads(16, 16), 1, halfMaxPassNum );
        }

        // A-trous (odd, last)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushInput( AsUint(Transient::HISTORY_LENGTH) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );

            PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

            AddDispatch( RELAX_Diffuse_Atrous, SumConstants(0, 0, 0, 9), NumThreads(16, 16), 1 );
        }

        // A-trous (even, last)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::TILES) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushInput( AsUint(Transient::HISTORY_LENGTH) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(ResourceType::IN_VIEWZ) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(ResourceType::IN_VIEWZ) );

            PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

            AddDispatch( RELAX_Diffuse_Atrous, SumConstants(0, 0, 0, 9), NumThreads(16, 16), 1 );
        }
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

        PushOutput( AsUint( ResourceType::OUT_DIFF_RADIANCE_HITDIST ) );

        AddDispatch( RELAX_Diffuse_SplitScreen, SumConstants(0, 0, 0, 2), NumThreads(16, 16), 1 );
    }

    RELAX_ADD_VALIDATION_DISPATCH;

    #undef DENOISER_NAME
}

void nrd::InstanceImpl::Update_RelaxDiffuse(const DenoiserData& denoiserData)
{
    enum class Dispatch
    {
        CLASSIFY_TILES,
        HITDIST_RECONSTRUCTION_3x3,
        HITDIST_RECONSTRUCTION_5x5,
        PREPASS_AFTER_HITDIST_RECONSTRUCTION,
        PREPASS,
        TEMPORAL_ACCUMULATION,
        TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS,
        TEMPORAL_ACCUMULATION_WITH_THRESHOLD_MIX,
        TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS_WITH_THRESHOLD_MIX,
        HISTORY_FIX,
        HISTORY_CLAMPING,
        HISTORY_CLAMPING_NO_FIREFLY,
        FIREFLY,
        ATROUS_SMEM,
        ATROUS_ODD,
        ATROUS_EVEN,
        ATROUS_ODD_LAST,
        ATROUS_EVEN_LAST,
        ATROUS_SMEM_WITH_CONFIDENCE_INPUTS,
        ATROUS_ODD_WITH_CONFIDENCE_INPUTS,
        ATROUS_EVEN_WITH_CONFIDENCE_INPUTS,
        ATROUS_ODD_LAST_WITH_CONFIDENCE_INPUTS,
        ATROUS_EVEN_LAST_WITH_CONFIDENCE_INPUTS,
        SPLIT_SCREEN,
        VALIDATION,
    };

    const RelaxDiffuseSettings& settings = denoiserData.settings.diffuseRelax;

    NRD_DECLARE_DIMS;

    float maxLuminanceRelativeDifference = -ml::Log(ml::Saturate(settings.minLuminanceWeight));

    float disocclusionThreshold = m_CommonSettings.disocclusionThreshold + (1.0f + m_JitterDelta) / float(rectH);
    float disocclusionThresholdOrtho = disocclusionThreshold;
    float disocclusionThresholdAlternate = m_CommonSettings.disocclusionThresholdAlternate + (1.0f + m_JitterDelta) / float(rectH);
    float disocclusionThresholdAlternateOrtho = disocclusionThresholdAlternate;
    float depthThresholdOrtho = settings.depthThreshold;

    float tanHalfFov = 1.0f / m_ViewToClip.a00;
    float aspect = m_ViewToClip.a00 / m_ViewToClip.a11;
    ml::float3 frustumRight = m_WorldToView.GetRow0().To3d() * tanHalfFov;
    ml::float3 frustumUp = m_WorldToView.GetRow1().To3d() * tanHalfFov * aspect;
    ml::float3 frustumForward = RELAX_GetFrustumForward(m_ViewToWorld, m_Frustum);

    float prevTanHalfFov = 1.0f / m_ViewToClipPrev.a00;
    float prevAspect = m_ViewToClipPrev.a00 / m_ViewToClipPrev.a11;
    ml::float3 prevFrustumRight = m_WorldToViewPrev.GetRow0().To3d() * prevTanHalfFov;
    ml::float3 prevFrustumUp = m_WorldToViewPrev.GetRow1().To3d() * prevTanHalfFov * prevAspect;
    ml::float3 prevFrustumForward = RELAX_GetFrustumForward(m_ViewToWorldPrev, m_FrustumPrev);
    bool isCameraStatic = RELAX_IsCameraStatic(ml::float3(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z), frustumRight, frustumUp, frustumForward, prevFrustumRight, prevFrustumUp, prevFrustumForward);

    bool enableHitDistanceReconstruction = settings.hitDistanceReconstructionMode != HitDistanceReconstructionMode::OFF && settings.checkerboardMode == CheckerboardMode::OFF;

    // Checkerboard logic
    uint32_t diffuseCheckerboard = 2;

    switch (settings.checkerboardMode)
    {
    case CheckerboardMode::BLACK:
        diffuseCheckerboard = 0;
        break;
    case CheckerboardMode::WHITE:
        diffuseCheckerboard = 1;
        break;
    default:
        break;
    }

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffuseCheckerboard);
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
        bool is3x3 = settings.hitDistanceReconstructionMode == HitDistanceReconstructionMode::AREA_3X3;
        data = PushDispatch(denoiserData, is3x3 ? AsUint(Dispatch::HITDIST_RECONSTRUCTION_3x3) : AsUint(Dispatch::HITDIST_RECONSTRUCTION_5x5));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE);
        ValidateConstants(data);
    }

    // PREPASS
    data = PushDispatch(denoiserData, AsUint(enableHitDistanceReconstruction ? Dispatch::PREPASS_AFTER_HITDIST_RECONSTRUCTION : Dispatch::PREPASS));
    AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE);
    AddFloat4(data, m_Rotator_PrePass);
    AddUint(data, diffuseCheckerboard);
    AddFloat(data, settings.prepassBlurRadius);
    AddFloat(data, 1.0f);
    AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
    AddFloat(data, settings.diffuseLobeAngleFraction);
    
    ValidateConstants(data);

    // TEMPORAL_ACCUMULATION
    if (!m_CommonSettings.isDisocclusionThresholdMixAvailable)
    {
        data = PushDispatch(
            denoiserData,
            AsUint(m_CommonSettings.isHistoryConfidenceAvailable ?
                Dispatch::TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS :
                Dispatch::TEMPORAL_ACCUMULATION));
    }
    else
    {
        data = PushDispatch(
            denoiserData,
            AsUint(m_CommonSettings.isHistoryConfidenceAvailable ?
                Dispatch::TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS_WITH_THRESHOLD_MIX :
                Dispatch::TEMPORAL_ACCUMULATION_WITH_THRESHOLD_MIX));
    }
    AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE);
    AddFloat(data, (float)settings.diffuseMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.diffuseMaxFastAccumulatedFrameNum);
    AddUint(data, diffuseCheckerboard);
    AddFloat(data, m_IsOrtho == 0 ? disocclusionThreshold : disocclusionThresholdOrtho);
    AddFloat(data, m_IsOrtho == 0 ? disocclusionThresholdAlternate : disocclusionThresholdAlternateOrtho);
    AddUint(data, settings.enableReprojectionTestSkippingWithoutMotion && isCameraStatic);
    AddUint(data, m_CommonSettings.isHistoryConfidenceAvailable ? 1 : 0);
    AddUint(data, m_CommonSettings.isDisocclusionThresholdMixAvailable ? 1 : 0);
    ValidateConstants(data);

    // HISTORY_FIX
    data = PushDispatch(denoiserData, AsUint(Dispatch::HISTORY_FIX));
    AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE);
    AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
    AddFloat(data, settings.historyFixEdgeStoppingNormalPower);
    AddFloat(data, settings.historyFixStrideBetweenSamples);
    AddFloat(data, float(settings.historyFixFrameNum));
    ValidateConstants(data);

    if (settings.enableAntiFirefly)
    {
        // HISTORY_CLAMPING
        data = PushDispatch(denoiserData, AsUint(Dispatch::HISTORY_CLAMPING));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE);
        AddFloat(data, settings.historyClampingColorBoxSigmaScale);
        AddFloat(data, float(settings.historyFixFrameNum));
        AddUint(data, settings.diffuseMaxFastAccumulatedFrameNum < settings.diffuseMaxAccumulatedFrameNum ? 1 : 0);
        ValidateConstants(data);

        // FIREFLY
        data = PushDispatch(denoiserData, AsUint(Dispatch::FIREFLY));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE);
        ValidateConstants(data);
    }
    else
    {
        // HISTORY_CLAMPING (without firefly)
        data = PushDispatch(denoiserData, AsUint(Dispatch::HISTORY_CLAMPING_NO_FIREFLY));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE);
        AddFloat(data, settings.historyClampingColorBoxSigmaScale);
        AddFloat(data, float(settings.historyFixFrameNum));
        AddUint(data, settings.diffuseMaxFastAccumulatedFrameNum < settings.diffuseMaxAccumulatedFrameNum ? 1 : 0);
        ValidateConstants(data);
    }

    // A-TROUS
    uint32_t iterationNum = ml::Clamp(settings.atrousIterationNum, 2u, RELAX_MAX_ATROUS_PASS_NUM);
    for (uint32_t i = 0; i < iterationNum; i++)
    {
        Dispatch dispatch;
        if (!m_CommonSettings.isHistoryConfidenceAvailable)
        {
            if (i == 0)
                dispatch = Dispatch::ATROUS_SMEM;
            else if (i == iterationNum - 1)
                dispatch = (i % 2 == 0) ? Dispatch::ATROUS_EVEN_LAST : Dispatch::ATROUS_ODD_LAST;
            else
                dispatch = (i % 2 == 0) ? Dispatch::ATROUS_EVEN : Dispatch::ATROUS_ODD;
        }
        else
        {
            if (i == 0)
                dispatch = Dispatch::ATROUS_SMEM_WITH_CONFIDENCE_INPUTS;
            else if (i == iterationNum - 1)
                dispatch = (i % 2 == 0) ? Dispatch::ATROUS_EVEN_LAST_WITH_CONFIDENCE_INPUTS : Dispatch::ATROUS_ODD_LAST_WITH_CONFIDENCE_INPUTS;
            else
                dispatch = (i % 2 == 0) ? Dispatch::ATROUS_EVEN_WITH_CONFIDENCE_INPUTS : Dispatch::ATROUS_ODD_WITH_CONFIDENCE_INPUTS;
        }

        data = PushDispatch(denoiserData, AsUint(dispatch));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE);

        if (i == 0)
        {
            AddUint2(data, screenW, screenH); // For Atrous_shmem
            AddUint(data, settings.spatialVarianceEstimationHistoryThreshold);
        }

        AddFloat(data, settings.diffusePhiLuminance);
        AddFloat(data, maxLuminanceRelativeDifference);
        AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
        AddFloat(data, settings.diffuseLobeAngleFraction);
        AddUint(data, 1 << i);
        AddUint(data, m_CommonSettings.isHistoryConfidenceAvailable ? 1 : 0);
        AddFloat(data, settings.confidenceDrivenRelaxationMultiplier);
        AddFloat(data, settings.confidenceDrivenLuminanceEdgeStoppingRelaxation);
        AddFloat(data, settings.confidenceDrivenNormalEdgeStoppingRelaxation);
        ValidateConstants(data);
    }

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffuseCheckerboard);
        ValidateConstants(data);
    }

    // VALIDATION
    if (m_CommonSettings.enableValidation)
    {
        data = PushDispatch(denoiserData, AsUint(Dispatch::VALIDATION));
        AddSharedConstants_Relax(denoiserData, data, Denoiser::RELAX_DIFFUSE_SPECULAR);
        AddFloat4x4(data, m_WorldToClipPrev);
        AddFloat2(data, m_CommonSettings.cameraJitter[0], m_CommonSettings.cameraJitter[1]);
        AddFloat(data, (float)settings.diffuseMaxAccumulatedFrameNum);
        ValidateConstants(data);
    }
}
