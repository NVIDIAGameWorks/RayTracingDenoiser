/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/
constexpr uint32_t RELAX_MAX_ATROUS_PASS_NUM = 8;

#define RELAX_SET_SHARED_CONSTANTS SetSharedConstants(5, 8, 7, 14)

#define RELAX_ADD_VALIDATION_DISPATCH \
    PushPass("Validation"); \
    { \
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) ); \
        PushInput( AsUint(ResourceType::IN_VIEWZ) ); \
        PushInput( AsUint(ResourceType::IN_MV) ); \
        PushOutput( AsUint(ResourceType::OUT_VALIDATION) ); \
        AddDispatch( RELAX_Validation, SumConstants(1, 0, 1, 0), NumThreads(16, 16), IGNORE_RS ); \
    }

inline ml::float3 RELAX_GetFrustumForward(const ml::float4x4& viewToWorld, const ml::float4& frustum)
{
    // Note: this vector is not normalized for non-symmetric projections but that's correct.
    // It has to have .z coordinate equal to 1.0 to correctly reconstruct world position in shaders.
    ml::float4 frustumForwardView = ml::float4(0.5f, 0.5f, 1.0f, 0.0f) * ml::float4(frustum.z, frustum.w, 1.0f, 0.0f) + ml::float4(frustum.x, frustum.y, 0.0f, 0.0f);
    ml::float3 frustumForwardWorld = (viewToWorld * frustumForwardView).To3d();
    return frustumForwardWorld;
}

inline bool RELAX_IsCameraStatic
(
    const ml::float3& cameraDelta,
    const ml::float3& frustumRight, const ml::float3& frustumUp, const ml::float3& frustumForward,
    const ml::float3& prevFrustumRight, const ml::float3& prevFrustumUp, const ml::float3& prevFrustumForward, float eps = ml::c_fEps
)
{
    return ml::Length(cameraDelta) < eps && ml::Length(frustumRight - prevFrustumRight) < eps && ml::Length(frustumUp - prevFrustumUp) < eps && ml::Length(frustumForward - prevFrustumForward) < eps;
}

void nrd::DenoiserImpl::AddSharedConstants_Relax(const MethodData& methodData, Constant*& data, Method method)
{
    NRD_DECLARE_DIMS;

    // Calculate camera right and up vectors in worldspace scaled according to frustum extents,
    // and unit forward vector, for fast worldspace position reconstruction in shaders
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

    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldPrevToWorld);
    AddFloat4x4(data, m_ViewToWorld);

    AddFloat4(data, ml::float4(frustumRight.x, frustumRight.y, frustumRight.z, 0));
    AddFloat4(data, ml::float4(frustumUp.x, frustumUp.y, frustumUp.z, 0));
    AddFloat4(data, ml::float4(frustumForward.x, frustumForward.y, frustumForward.z, 0));
    AddFloat4(data, ml::float4(prevFrustumRight.x, prevFrustumRight.y, prevFrustumRight.z, 0));
    AddFloat4(data, ml::float4(prevFrustumUp.x, prevFrustumUp.y, prevFrustumUp.z, 0));
    AddFloat4(data, ml::float4(prevFrustumForward.x, prevFrustumForward.y, prevFrustumForward.z, 0));
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, 0.0f));
    AddFloat4(data, ml::float4(m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1], m_CommonSettings.motionVectorScale[2], m_CommonSettings.debug));

    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);

    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));
    AddUint2(data, rectW, rectH);

    AddFloat2(data, 1.0f / screenW, 1.0f / screenH);
    AddFloat2(data, 1.0f / rectW, 1.0f / rectH);

    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    AddFloat(data, m_IsOrtho);

    AddFloat(data, 1.0f / (0.5f * rectH * m_ProjectY));
    AddUint(data, m_CommonSettings.frameIndex);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, ml::Clamp(16.66f / m_TimeDelta, 0.25f, 4.0f)); // Normalizing to 60 FPS

    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, m_JitterDelta);
    switch (method)
    {
    case Method::RELAX_DIFFUSE:
        AddUint(data, methodData.settings.diffuseRelax.enableMaterialTest ? 1 : 0);
        AddUint(data, 0);
        break;
    case Method::RELAX_SPECULAR:
        AddUint(data, 0);
        AddUint(data, methodData.settings.specularRelax.enableMaterialTest ? 1 : 0);
        break;
    case Method::RELAX_DIFFUSE_SPECULAR:
        AddUint(data, methodData.settings.diffuseSpecularRelax.enableMaterialTestForDiffuse ? 1 : 0);
        AddUint(data, methodData.settings.diffuseSpecularRelax.enableMaterialTestForSpecular ? 1 : 0);
        break;
    default:
        // Should never get here
        AddUint(data, 0);
        AddUint(data, 0);
        break;
    }

    // 1 if m_WorldPrevToWorld should be used in shader, otherwise we can skip multiplication
    AddUint(data, (m_WorldPrevToWorld != ml::float4x4::Identity()) ? 1 : 0);
    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);
    AddUint(data, 0);
    AddUint(data, 0);
}

void nrd::DenoiserImpl::AddMethod_RelaxDiffuse(MethodData& methodData)
{
    #define METHOD_NAME RELAX_Diffuse

    methodData.settings.diffuseRelax = RelaxDiffuseSettings();
    methodData.settingsSize = sizeof(methodData.settings.diffuseRelax);

    uint16_t w = methodData.desc.fullResolutionWidth;
    uint16_t h = methodData.desc.fullResolutionHeight;

    enum class Permanent
    {
        DIFF_ILLUM_PREV = PERMANENT_POOL_START,
        DIFF_ILLUM_RESPONSIVE_PREV,
        DIFF_HISTORY_LENGTH_CURR,
        DIFF_HISTORY_LENGTH_PREV,
        NORMAL_ROUGHNESS_PREV,
        MATERIAL_ID_PREV,
        VIEWZ_CURR,
        VIEWZ_PREV
    };

    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R32_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R32_SFLOAT, w, h, 1} );

    enum class Transient
    {
        DIFF_ILLUM_PING = TRANSIENT_POOL_START,
        DIFF_ILLUM_PONG,
        DIFF_ILLUM_TMP,
        VIEWZ_R16F
    };

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 1} );

    RELAX_SET_SHARED_CONSTANTS;

    const uint32_t halfMaxPassNum = (RELAX_MAX_ATROUS_PASS_NUM - 2 + 1) / 2;

    PushPass("Hit distance reconstruction"); // 3x3
    {
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

        AddDispatch( RELAX_Diffuse_HitDistReconstruction, SumConstants(0, 0, 0, 0), NumThreads(8, 8), 1 );
    }

    PushPass("Hit distance reconstruction"); // 5x5
    {
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

        AddDispatch( RELAX_Diffuse_HitDistReconstruction_5x5, SumConstants(0, 0, 0, 0), NumThreads(8, 8), 1 );
    }

    PushPass("Pre-pass"); // After hit distance reconstruction
    {
        // Does preblur (if enabled), checkerboard reconstruction (if enabled) and generates FP16 ViewZ texture
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_TMP) );
        PushOutput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV) );
        PushOutput( AsUint(Transient::VIEWZ_R16F) );

        AddDispatch( RELAX_Diffuse_PrePass, SumConstants(0, 1, 0, 4), NumThreads(16, 16), 1 );
    }

    PushPass("Pre-pass"); // Without hit distance reconstruction
    {
        // Does preblur (if enabled), checkerboard reconstruction (if enabled) and generates FP16 ViewZ texture
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_TMP) );
        PushOutput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV) );
        PushOutput( AsUint(Transient::VIEWZ_R16F) );

        AddDispatch( RELAX_Diffuse_PrePass, SumConstants(0, 1, 0, 4), NumThreads(16, 16), 1 );
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
            PushInput( AsUint(Transient::DIFF_ILLUM_TMP) );
            PushInput( AsUint(ResourceType::IN_MV) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
            PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
            PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
            PushInput( AsUint(Permanent::VIEWZ_PREV), 0, 1, AsUint(Permanent::VIEWZ_CURR) );
            PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_PREV) );
            PushInput( AsUint(Permanent::MATERIAL_ID_PREV) );
            // Optional inputs:
            if (i == 0)
            {
                PushInput( AsUint(Transient::VIEWZ_R16F) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_PREV) ); // Bogus input that will not be fetched anyway
            }
            if (i == 1)
            {
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
                PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_PREV) ); // Bogus input that will not be fetched anyway
            }
            if (i == 2)
            {
                PushInput( AsUint(Transient::VIEWZ_R16F) ); // Bogus input that will not be fetched anyway
                PushInput( AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) );
            }
            if (i == 3)
            {
                PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );
                PushInput( AsUint(ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX) );
            }

            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushOutput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );

            AddDispatch( RELAX_Diffuse_TemporalAccumulation, SumConstants(0, 0, 0, 8), NumThreads(8, 8), 1 );
        }
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );

        AddDispatch( RELAX_Diffuse_HistoryFix, SumConstants(0, 0, 0, 4), NumThreads(8, 8), 1 );
    }

    PushPass("History clamping"); // with firefly after it
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_TMP) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_LENGTH_PREV) );

        AddDispatch( RELAX_Diffuse_HistoryClamping, SumConstants(0, 0, 0, 3), NumThreads(8, 8), 1 );
    }

    PushPass("History clamping"); // without firefly after it
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );

        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_LENGTH_PREV) );

        AddDispatch( RELAX_Diffuse_HistoryClamping, SumConstants(0, 0, 0, 3), NumThreads(8, 8), 1 );
    }

    PushPass("Anti-firefly");
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_TMP) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );

        AddDispatch( RELAX_Diffuse_AntiFirefly, SumConstants(0, 0, 0, 0), NumThreads(16, 16), 1 );
    }

    for (int i = 0; i < 2; i++)
    {
        bool withConfidenceInputs = (i == 1);

        // A-trous (first)
        PushPass("A-trous (SMEM)");
        {
            PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
            PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );

            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushOutput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
            PushOutput( AsUint(Permanent::MATERIAL_ID_PREV) );

            AddDispatch( RELAX_Diffuse_AtrousSmem, SumConstants(0, 0, 1, 10), NumThreads(8, 8), 1 );
        }

        // A-trous (odd)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );

            PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );

            AddDispatchRepeated( RELAX_Diffuse_Atrous, SumConstants(0, 0, 0, 9), NumThreads(16, 16), 1, halfMaxPassNum );
        }

        // A-trous (even)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );

            PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

            AddDispatchRepeated( RELAX_Diffuse_Atrous, SumConstants(0, 0, 0, 9), NumThreads(16, 16), 1, halfMaxPassNum );
        }

        // A-trous (odd, last)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
            PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );

            PushOutput( AsUint(ResourceType::OUT_DIFF_RADIANCE_HITDIST) );

            AddDispatch( RELAX_Diffuse_Atrous, SumConstants(0, 0, 0, 9), NumThreads(16, 16), 1 );
        }

        // A-trous (even, last)
        PushPass("A-trous");
        {
            PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
            PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
            PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
            PushInput( AsUint(Transient::VIEWZ_R16F) );
            PushInput( withConfidenceInputs ? AsUint(ResourceType::IN_DIFF_CONFIDENCE) : AsUint(Transient::VIEWZ_R16F) );

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

    #undef METHOD_NAME
}

void nrd::DenoiserImpl::UpdateMethod_RelaxDiffuse(const MethodData& methodData)
{
    enum class Dispatch
    {
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

    const RelaxDiffuseSettings& settings = methodData.settings.diffuseRelax;

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
        Constant* data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffuseCheckerboard);
        ValidateConstants(data);

        return;
    }

    // HITDIST_RECONSTRUCTION
    if (enableHitDistanceReconstruction)
    {
        bool is3x3 = settings.hitDistanceReconstructionMode == HitDistanceReconstructionMode::AREA_3X3;
        Constant* data = PushDispatch(methodData, is3x3 ? AsUint(Dispatch::HITDIST_RECONSTRUCTION_3x3) : AsUint(Dispatch::HITDIST_RECONSTRUCTION_5x5));
        AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE);
        ValidateConstants(data);
    }

    // PREPASS
    Constant* data = PushDispatch(methodData, AsUint(enableHitDistanceReconstruction ? Dispatch::PREPASS_AFTER_HITDIST_RECONSTRUCTION : Dispatch::PREPASS));
    AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE);
    AddFloat4(data, m_Rotator_PrePass);
    AddUint(data, diffuseCheckerboard);
    AddFloat(data, settings.prepassBlurRadius);
    AddFloat(data, 1.0f);
    AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
    ValidateConstants(data);

    // TEMPORAL_ACCUMULATION
    if (!m_CommonSettings.isDisocclusionThresholdMixAvailable)
    {
        data = PushDispatch(
            methodData,
            AsUint(m_CommonSettings.isHistoryConfidenceInputsAvailable ?
                Dispatch::TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS :
                Dispatch::TEMPORAL_ACCUMULATION));
    }
    else
    {
        data = PushDispatch(
            methodData,
            AsUint(m_CommonSettings.isHistoryConfidenceInputsAvailable ?
                Dispatch::TEMPORAL_ACCUMULATION_WITH_CONFIDENCE_INPUTS_WITH_THRESHOLD_MIX :
                Dispatch::TEMPORAL_ACCUMULATION_WITH_THRESHOLD_MIX));
    }
    AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE);
    AddFloat(data, (float)settings.diffuseMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.diffuseMaxFastAccumulatedFrameNum);
    AddUint(data, diffuseCheckerboard);
    AddFloat(data, m_IsOrtho == 0 ? disocclusionThreshold : disocclusionThresholdOrtho);
    AddFloat(data, m_IsOrtho == 0 ? disocclusionThresholdAlternate : disocclusionThresholdAlternateOrtho);
    AddUint(data, settings.enableReprojectionTestSkippingWithoutMotion && isCameraStatic);
    AddUint(data, m_CommonSettings.isHistoryConfidenceInputsAvailable ? 1 : 0);
    AddUint(data, m_CommonSettings.isDisocclusionThresholdMixAvailable ? 1 : 0);
    ValidateConstants(data);

    // HISTORY_FIX
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_FIX));
    AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE);
    AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
    AddFloat(data, settings.historyFixEdgeStoppingNormalPower);
    AddFloat(data, settings.historyFixStrideBetweenSamples);
    AddFloat(data, float(settings.historyFixFrameNum));
    ValidateConstants(data);

    if (settings.enableAntiFirefly)
    {
        // HISTORY_CLAMPING
        data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_CLAMPING));
        AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE);
        AddFloat(data, settings.historyClampingColorBoxSigmaScale);
        AddFloat(data, float(settings.historyFixFrameNum));
        AddUint(data, settings.diffuseMaxFastAccumulatedFrameNum < settings.diffuseMaxAccumulatedFrameNum ? 1 : 0);
        ValidateConstants(data);

        // FIREFLY
        data = PushDispatch(methodData, AsUint(Dispatch::FIREFLY));
        AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE);
        ValidateConstants(data);
    }
    else
    {
        // HISTORY_CLAMPING (without firefly)
        data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_CLAMPING_NO_FIREFLY));
        AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE);
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
        AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE);

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
        AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddUint(data, diffuseCheckerboard);
        ValidateConstants(data);
    }

    // VALIDATION
    if (m_CommonSettings.enableValidation)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::VALIDATION));
        AddSharedConstants_Relax(methodData, data, Method::RELAX_DIFFUSE_SPECULAR);
        AddFloat4x4(data, m_WorldToClipPrev);
        AddFloat2(data, m_CommonSettings.cameraJitter[0], m_CommonSettings.cameraJitter[1]);
        ValidateConstants(data);
    }
}
