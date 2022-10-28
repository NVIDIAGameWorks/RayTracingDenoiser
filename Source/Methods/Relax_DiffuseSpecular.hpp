/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

void nrd::DenoiserImpl::AddMethod_RelaxDiffuseSpecular(nrd::MethodData& methodData)
{
    #define METHOD_NAME RELAX_DiffuseSpecular

    methodData.settings.diffuseSpecularRelax = RelaxDiffuseSpecularSettings();
    methodData.settingsSize = sizeof(methodData.settings.diffuseSpecularRelax);

    uint16_t w = methodData.desc.fullResolutionWidth;
    uint16_t h = methodData.desc.fullResolutionHeight;

    enum class Permanent
    {
        SPEC_ILLUM_PREV = PERMANENT_POOL_START,
        DIFF_ILLUM_PREV,
        SPEC_ILLUM_RESPONSIVE_PREV,
        DIFF_ILLUM_RESPONSIVE_PREV,
        REFLECTION_HIT_T_CURR,
        REFLECTION_HIT_T_PREV,
        SPEC_DIFF_HISTORY_LENGTH_CURR,
        SPEC_DIFF_HISTORY_LENGTH_PREV,
        NORMAL_ROUGHNESS_PREV,
        MATERIAL_ID_PREV,
        VIEWZ_CURR,
        VIEWZ_PREV,
    };

    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R32_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R32_SFLOAT, w, h, 1} );

    enum class Transient
    {
        SPEC_ILLUM_PING = TRANSIENT_POOL_START,
        SPEC_ILLUM_PONG,
        DIFF_ILLUM_PING,
        DIFF_ILLUM_PONG,
        SPEC_ILLUM_TMP,
        DIFF_ILLUM_TMP,
        SPEC_REPROJECTION_CONFIDENCE,
        VIEWZ_R16F
    };

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 1} );

    RELAX_SET_SHARED_CONSTANTS;

    const uint32_t halfMaxPassNum = (RELAX_MAX_ATROUS_PASS_NUM - 2 + 1) / 2;

    PushPass("Hit distance reconstruction"); // 3x3
    {
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

        AddDispatch( RELAX_DiffuseSpecular_HitDistReconstruction, SumConstants(0, 0, 0, 0), NumThreads(8, 8), 1 );
    }

    PushPass("Hit distance reconstruction"); // 5x5
    {
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput(AsUint(Transient::SPEC_ILLUM_PING));
        PushOutput(AsUint(Transient::DIFF_ILLUM_PING));

        AddDispatch( RELAX_DiffuseSpecular_HitDistReconstruction_5x5, SumConstants(0, 0, 0, 0), NumThreads(8, 8), 1 );
    }

    PushPass("Pre-pass"); // After hit distance reconstruction
    {
        // Does preblur (if enabled), checkerboard reconstruction (if enabled) and generates FP16 ViewZ texture
        PushInput( AsUint(Transient::SPEC_ILLUM_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_TMP) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_TMP) );
        PushOutput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV) );
        PushOutput( AsUint(Transient::VIEWZ_R16F) );

        AddDispatch( RELAX_DiffuseSpecular_PrePass, SumConstants(0, 1, 0, 7), NumThreads(16, 16), 1 );
    }

    PushPass("Pre-pass"); // Without hit distance reconstruction
    {
        // Does preblur (if enabled), checkerboard reconstruction (if enabled) and generates FP16 ViewZ texture
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_TMP) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_TMP) );
        PushOutput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV) );
        PushOutput( AsUint(Transient::VIEWZ_R16F) );

        AddDispatch( RELAX_DiffuseSpecular_PrePass, SumConstants(0, 1, 0, 7), NumThreads(16, 16), 1 );
    }

    for (int i = 0; i < 4; i++)
    {
        // The following passes are defined here:
        // REPROJECT,
        // REPROJECT_WITH_CONFIDENCE_INPUTS,
        // REPROJECT_WITH_THRESHOLD_MIX,
        // REPROJECT_WITH_CONFIDENCE_INPUTS_WITH_THRESHOLD_MIX

        PushPass("Temporal accumulation");
        {
            PushInput( AsUint(Transient::SPEC_ILLUM_TMP) );
            PushInput( AsUint(Transient::DIFF_ILLUM_TMP) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV) );
            PushInput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
            PushInput( AsUint(Permanent::SPEC_ILLUM_PREV) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
            PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
            PushInput( AsUint(Permanent::VIEWZ_PREV), 0, 1, AsUint(Permanent::VIEWZ_CURR) );
            PushInput( AsUint(Permanent::REFLECTION_HIT_T_PREV), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_CURR) );
            PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV) );
            PushInput( AsUint(Permanent::MATERIAL_ID_PREV) );

            // Optional inputs:
            if (i == 0)
            {
                PushInput( AsUint(Transient::VIEWZ_R16F) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(Transient::VIEWZ_R16F) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV) ); // Bogus input that will not be fetched anyway
            }
            if (i == 1)
            {
                PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE) );
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
                PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV) ); // Bogus input that will not be fetched anyway
            }
            if (i == 2)
            {
                PushInput( AsUint(Transient::VIEWZ_R16F) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(Transient::VIEWZ_R16F) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) );
            }
            if (i == 3)
            {
                PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE) );
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
                PushInput( AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) );
            }

            PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushOutput( AsUint(Transient::SPEC_ILLUM_PONG) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushOutput( AsUint(Permanent::REFLECTION_HIT_T_CURR), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_PREV) );
            PushOutput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
            PushOutput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );

            AddDispatch(RELAX_DiffuseSpecular_TemporalAccumulation, SumConstants(0, 0, 0, 15), NumThreads(8, 8), 1);
        }
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_PING) ); // Normal history
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_PONG) ); // Responsive history
        PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );

        AddDispatch( RELAX_DiffuseSpecular_HistoryFix, SumConstants(0, 0, 0, 8), NumThreads(8, 8), 1 );
    }

    PushPass("History clamping"); // with firefly after it
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_PING) ); // Normal history
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Transient::SPEC_ILLUM_PONG) ); // Responsive history
        PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_TMP) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_TMP) );
        PushOutput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV) );

        AddDispatch( RELAX_DiffuseSpecular_HistoryClamping, SumConstants(0, 0, 0, 4), NumThreads(8, 8), 1 );
    }

    PushPass("History clamping"); // without firefly after it
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_PING) ); // Normal history
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Transient::SPEC_ILLUM_PONG) ); // Responsive history
        PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );

        PushOutput( AsUint(Permanent::SPEC_ILLUM_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushOutput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV) );

        AddDispatch( RELAX_DiffuseSpecular_HistoryClamping, SumConstants(0, 0, 0, 4), NumThreads(8, 8), 1 );
    }

    PushPass("Anti-firefly");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_TMP) );
        PushInput( AsUint(Transient::DIFF_ILLUM_TMP) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Permanent::SPEC_ILLUM_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );

        AddDispatch( RELAX_DiffuseSpecular_AntiFirefly, SumConstants(0, 0, 0, 0), NumThreads(16, 16), 1 );
    }

    for (int i = 0; i < 2; i++)
    {
        bool withConfidenceInputs = (i == 1);

        // A-trous (first)
        PushPass("A-trous (SMEM)");
        {
            PushInput( AsUint(Permanent::SPEC_ILLUM_PREV) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
            PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
            PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );

            PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushOutput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
            PushOutput( AsUint(Permanent::MATERIAL_ID_PREV) );

            AddDispatch( RELAX_DiffuseSpecular_AtrousSmem, SumConstants(0, 0, 1, 19), NumThreads(8, 8), 1 );
        }

        // A-trous (odd)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::SPEC_ILLUM_PING) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
            PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );

            PushOutput( AsUint(Transient::SPEC_ILLUM_PONG) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );

            AddDispatchRepeated( RELAX_DiffuseSpecular_Atrous, SumConstants(0, 0, 0, 18), NumThreads(16, 16), 1, halfMaxPassNum );
        }

        // A-trous (even)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::SPEC_ILLUM_PONG) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
            PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );

            PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

            AddDispatchRepeated( RELAX_DiffuseSpecular_Atrous, SumConstants(0, 0, 0, 18), NumThreads(16, 16), 1, halfMaxPassNum );
        }

        // A-trous (odd, last)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::SPEC_ILLUM_PING) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
            PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );

            PushOutput( AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

            AddDispatch( RELAX_DiffuseSpecular_Atrous, SumConstants(0, 0, 0, 18), NumThreads(16, 16), 1 );
        }

        // A-trous (even, last)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::SPEC_ILLUM_PONG) );
            PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
            PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_SPEC_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );

            PushOutput( AsUint(ResourceType::OUT_SPEC_RADIANCE_HITDIST) );
            PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

            AddDispatch( RELAX_DiffuseSpecular_Atrous, SumConstants(0, 0, 0, 18), NumThreads(16, 16), 1 );
        }
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );

        PushOutput( AsUint( ResourceType::OUT_SPEC_RADIANCE_HITDIST ) );
        PushOutput( AsUint( ResourceType::OUT_DIFF_RADIANCE_HITDIST ) );

        AddDispatch( RELAX_DiffuseSpecular_SplitScreen, SumConstants(0, 0, 0, 3), NumThreads(16, 16), 1 );
    }

    #undef METHOD_NAME
}

void nrd::DenoiserImpl::UpdateMethod_RelaxDiffuseSpecular(const MethodData& methodData)
{
    enum class Dispatch
    {
        HITDIST_RECONSTRUCTION_3x3,
        HITDIST_RECONSTRUCTION_5x5,
        PREPASS_AFTER_HITDIST_RECONSTRUCTION,
        PREPASS,
        REPROJECT,
        REPROJECT_WITH_CONFIDENCE_INPUTS,
        REPROJECT_WITH_THRESHOLD_MIX,
        REPROJECT_WITH_CONFIDENCE_INPUTS_WITH_THRESHOLD_MIX,
        DISOCCLUSION_FIX,
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
    };

    const RelaxDiffuseSpecularSettings& settings = methodData.settings.diffuseSpecularRelax;

    NRD_DECLARE_DIMS;

    float maxDiffuseLuminanceRelativeDifference = -ml::Log( ml::Saturate(settings.diffuseMinLuminanceWeight) );
    float maxSpecularLuminanceRelativeDifference = -ml::Log( ml::Saturate(settings.specularMinLuminanceWeight) );

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
    uint32_t specularCheckerboard = 2;
    uint32_t diffuseCheckerboard = 2;

    switch (settings.checkerboardMode)
    {
    case nrd::CheckerboardMode::BLACK:
        diffuseCheckerboard = 0;
        specularCheckerboard = 1;
        break;
    case nrd::CheckerboardMode::WHITE:
        diffuseCheckerboard = 1;
        specularCheckerboard = 0;
        break;
    default:
        break;
    }

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffuseCheckerboard);
        AddUint(data, specularCheckerboard);
        ValidateConstants(data);

        return;
    }

    // HIT DISTANCE RECONSTRUCTION
    if (enableHitDistanceReconstruction)
    {
        bool is3x3 = settings.hitDistanceReconstructionMode == HitDistanceReconstructionMode::AREA_3X3;
        Constant* data = PushDispatch(methodData, is3x3 ? AsUint(Dispatch::HITDIST_RECONSTRUCTION_3x3) : AsUint(Dispatch::HITDIST_RECONSTRUCTION_5x5));
        AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
        ValidateConstants(data);
    }

    // PREPASS
    Constant* data = PushDispatch(methodData, AsUint(enableHitDistanceReconstruction ? Dispatch::PREPASS_AFTER_HITDIST_RECONSTRUCTION : Dispatch::PREPASS));
    AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
    AddFloat4(data, m_Rotator_PrePass);
    AddUint(data, diffuseCheckerboard);
    AddUint(data, specularCheckerboard);
    AddFloat(data, settings.diffusePrepassBlurRadius);
    AddFloat(data, settings.specularPrepassBlurRadius);
    AddFloat(data, 1.0f);
    AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
    AddFloat(data, settings.roughnessFraction);
    ValidateConstants(data);

    // REPROJECT
    if (!m_CommonSettings.isDisocclusionThresholdMixAvailable)
    {
        data = PushDispatch(
            methodData,
            AsUint(m_CommonSettings.isHistoryConfidenceInputsAvailable ?
                Dispatch::REPROJECT_WITH_CONFIDENCE_INPUTS :
                Dispatch::REPROJECT));
    }
    else
    {
        data = PushDispatch(
            methodData,
            AsUint(m_CommonSettings.isHistoryConfidenceInputsAvailable ?
                Dispatch::REPROJECT_WITH_CONFIDENCE_INPUTS_WITH_THRESHOLD_MIX :
                Dispatch::REPROJECT_WITH_THRESHOLD_MIX));
    }
    AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
    AddFloat(data, (float)settings.specularMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.specularMaxFastAccumulatedFrameNum);
    AddFloat(data, (float)settings.diffuseMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.diffuseMaxFastAccumulatedFrameNum);
    AddUint(data, diffuseCheckerboard);
    AddUint(data, specularCheckerboard);
    AddFloat(data, m_IsOrtho == 0 ? disocclusionThreshold : disocclusionThresholdOrtho);
    AddFloat(data, m_IsOrtho == 0 ? disocclusionThresholdAlternate : disocclusionThresholdAlternateOrtho);
    AddFloat(data, settings.roughnessFraction);
    AddFloat(data, settings.specularVarianceBoost);
    AddUint(data, settings.enableSpecularVirtualHistoryClamping ? 1 : 0);
    AddUint(data, settings.enableReprojectionTestSkippingWithoutMotion && isCameraStatic);
    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);
    AddUint(data, m_CommonSettings.isHistoryConfidenceInputsAvailable ? 1 : 0);
    AddUint(data, m_CommonSettings.isDisocclusionThresholdMixAvailable ? 1 : 0);
    ValidateConstants(data);

    // DISOCCLUSION FIX
    data = PushDispatch(methodData, AsUint(Dispatch::DISOCCLUSION_FIX));
    AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
    AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
    AddFloat(data, settings.historyFixEdgeStoppingNormalPower);
    AddFloat(data, settings.historyFixStrideBetweenSamples);
    AddFloat(data, float(settings.historyFixFrameNum));
    AddFloat(data, settings.specularLobeAngleFraction);
    AddFloat(data, ml::DegToRad(settings.specularLobeAngleSlack));
    AddFloat(data, settings.roughnessEdgeStoppingRelaxation);
    AddFloat(data, settings.normalEdgeStoppingRelaxation);
    ValidateConstants(data);

    if (settings.enableAntiFirefly)
    {
        // HISTORY CLAMPING
        data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_CLAMPING));
        AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
        AddFloat(data, settings.historyClampingColorBoxSigmaScale);
        AddFloat(data, float(settings.historyFixFrameNum));
        AddUint(data, settings.specularMaxFastAccumulatedFrameNum < settings.specularMaxAccumulatedFrameNum ? 1 : 0);
        AddUint(data, settings.diffuseMaxFastAccumulatedFrameNum < settings.diffuseMaxAccumulatedFrameNum ? 1 : 0);
        ValidateConstants(data);

        // FIREFLY
        data = PushDispatch(methodData, AsUint(Dispatch::FIREFLY));
        AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
        ValidateConstants(data);
    }
    else
    {
        // HISTORY CLAMPING WITHOUT FIREFLY
        data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_CLAMPING_NO_FIREFLY));
        AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
        AddFloat(data, settings.historyClampingColorBoxSigmaScale);
        AddFloat(data, float(settings.historyFixFrameNum));
        AddUint(data, settings.specularMaxFastAccumulatedFrameNum < settings.specularMaxAccumulatedFrameNum ? 1 : 0);
        AddUint(data, settings.diffuseMaxFastAccumulatedFrameNum < settings.diffuseMaxAccumulatedFrameNum ? 1 : 0);
        ValidateConstants(data);
    }

    // A-TROUS
    uint32_t iterationNum = ml::Clamp(settings.atrousIterationNum, 2u, RELAX_MAX_ATROUS_PASS_NUM);
    for (uint32_t i = 0; i < iterationNum; i++)
    {
        Dispatch dispatch;
        if (!m_CommonSettings.isHistoryConfidenceInputsAvailable)
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

        data = PushDispatch(methodData, AsUint(dispatch));
        AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);

        if (i == 0)
        {
            AddUint2(data, screenW, screenH); // For Atrous_shmem
            AddUint(data, settings.spatialVarianceEstimationHistoryThreshold);
        }

        AddFloat(data, settings.specularPhiLuminance);
        AddFloat(data, settings.diffusePhiLuminance);
        AddFloat(data, maxDiffuseLuminanceRelativeDifference);
        AddFloat(data, maxSpecularLuminanceRelativeDifference);
        AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
        AddFloat(data, settings.diffuseLobeAngleFraction);
        AddFloat(data, settings.roughnessFraction);
        AddFloat(data, settings.specularLobeAngleFraction);
        AddFloat(data, ml::DegToRad(settings.specularLobeAngleSlack));
        AddUint(data, 1 << i);
        AddUint(data, settings.enableRoughnessEdgeStopping);
        AddFloat(data, settings.roughnessEdgeStoppingRelaxation);
        AddFloat(data, settings.normalEdgeStoppingRelaxation);
        AddFloat(data, settings.luminanceEdgeStoppingRelaxation);
        AddUint(data, m_CommonSettings.isHistoryConfidenceInputsAvailable ? 1 : 0);
        AddFloat(data, settings.confidenceDrivenRelaxationMultiplier);
        AddFloat(data, settings.confidenceDrivenLuminanceEdgeStoppingRelaxation);
        AddFloat(data, settings.confidenceDrivenNormalEdgeStoppingRelaxation);
        ValidateConstants(data);
    }

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffuseCheckerboard);
        AddUint(data, specularCheckerboard);
        ValidateConstants(data);
    }
}
