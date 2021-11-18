/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/
constexpr uint32_t RELAX_MAX_ATROUS_PASS_NUM = 8;

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

size_t nrd::DenoiserImpl::AddMethod_RelaxDiffuse(uint16_t w, uint16_t h)
{
    #define DENOISER_NAME "RELAX::Diffuse"

    enum class Permanent
    {
        DIFF_ILLUM_CURR = PERMANENT_POOL_START,
        DIFF_ILLUM_PREV,
        DIFF_ILLUM_RESPONSIVE_CURR,
        DIFF_ILLUM_RESPONSIVE_PREV,
        DIFF_HISTORY_LENGTH_CURR,
        DIFF_HISTORY_LENGTH_PREV,
        NORMAL_ROUGHNESS_PREV,
        VIEWZ_CURR,
        VIEWZ_PREV
    };

    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back({ Format::RGBA16_SFLOAT, w, h, 1 });
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( { Format::RGBA8_UNORM, w, h, 1 } );
    m_PermanentPool.push_back( { Format::R32_SFLOAT, w, h, 1 } );
    m_PermanentPool.push_back( { Format::R32_SFLOAT, w, h, 1 } );


    enum class Transient
    {
        DIFF_ILLUM_PING = TRANSIENT_POOL_START,
        DIFF_ILLUM_PONG,
        VIEWZ_R16F
    };

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( { Format::R16_SFLOAT, w, h, 1 } );

    SetSharedConstants(0, 0, 0, 0);

    const uint32_t halfMaxPassNum = (RELAX_MAX_ATROUS_PASS_NUM - 2 + 1) / 2;

    PushPass("Prepass"); // Does preblur (if enabled), checkerboard reconstruction (if enabled) and generates FP16 ViewZ texture
    {
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ));

        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushOutput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV) );
        PushOutput( AsUint(Transient::VIEWZ_R16F) );

        AddDispatch(RELAX_Diffuse_Prepass, SumConstants(3, 4, 6, 7), 8, 1);
    }

    PushPass("Reproject");
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV));
        PushInput( AsUint(Permanent::VIEWZ_PREV), 0, 1, AsUint(Permanent::VIEWZ_CURR) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_PREV) );
        PushInput( AsUint(Transient::VIEWZ_R16F) ); // Bogus input that will not be fetched anyway

        PushOutput( AsUint(Permanent::DIFF_ILLUM_CURR) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_CURR) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );

        AddDispatch( RELAX_Diffuse_Reproject, SumConstants(2, 7, 6, 15), 8, 1 );
    }

    PushPass("Reproject"); // With confidence input
    {
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST));
        PushInput( AsUint(ResourceType::IN_MV));
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
        PushInput( AsUint(Permanent::VIEWZ_PREV), 0, 1, AsUint(Permanent::VIEWZ_CURR) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_PREV) );
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE) );

        PushOutput( AsUint(Permanent::DIFF_ILLUM_CURR) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_CURR) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );

        AddDispatch( RELAX_Diffuse_Reproject, SumConstants(2, 7, 6, 15), 8, 1 );
    }

    PushPass("Disocclusion fix");
    {
        PushInput( AsUint(Permanent::DIFF_ILLUM_CURR) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_CURR) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );

        AddDispatch( RELAX_Diffuse_DisocclusionFix, SumConstants(0, 3, 2, 5), 8, 1 );
    }

    PushPass("History clamping"); // with firefly after it
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );

        PushOutput( AsUint(Permanent::DIFF_ILLUM_CURR) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_LENGTH_PREV) );

        AddDispatch( RELAX_Diffuse_HistoryClamping, SumConstants(0, 0, 1, 1), 16, 1 );
    }

    PushPass("History clamping"); // without firefly after it
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );

        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushOutput( AsUint(Permanent::DIFF_HISTORY_LENGTH_PREV) );

        AddDispatch(RELAX_Diffuse_HistoryClamping, SumConstants(0, 0, 1, 1), 16, 1);
    }

    PushPass("Anti-firefly");
    {
        PushInput( AsUint(Permanent::DIFF_ILLUM_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );

        AddDispatch( RELAX_Diffuse_Firefly, SumConstants(0, 0, 1, 1), 16, 1 );
    }

    PushPass("Spatial variance estimation");
    {
        PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushOutput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );

        AddDispatch( RELAX_Diffuse_SpatialVarianceEstimation, SumConstants(0, 0, 1, 3), 16, 1 );
    }

    // A-trous (first)
    PushPass("A-trous (SMEM)");
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );

        AddDispatch( RELAX_Diffuse_ATrousShmem, SumConstants(0, 3, 2, 7), 8, 1 );
    }

    // A-trous (odd)
    PushPass("A-trous");
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

        AddDispatchRepeated( RELAX_Diffuse_ATrousStandard, SumConstants(0, 3, 2, 7), 8, 1, halfMaxPassNum );
    }

    // A-trous (even)
    PushPass("A-trous");
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );

        AddDispatchRepeated( RELAX_Diffuse_ATrousStandard, SumConstants(0, 3, 2, 7), 8, 1, halfMaxPassNum );
    }

    // A-trous (odd, last)
    PushPass("A-trous");
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PONG) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint( ResourceType::OUT_DIFF_RADIANCE_HITDIST ) );

        AddDispatch( RELAX_Diffuse_ATrousStandard, SumConstants(0, 3, 2, 7), 8, 1 );
    }

    // A-trous (even, last)
    PushPass("A-trous");
    {
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Permanent::DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint( ResourceType::OUT_DIFF_RADIANCE_HITDIST ) );

        AddDispatch( RELAX_Diffuse_ATrousStandard, SumConstants(0, 3, 2, 7), 8, 1 );
    }
    
    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST));

        PushOutput( AsUint( ResourceType::OUT_DIFF_RADIANCE_HITDIST ) );

        AddDispatch( RELAX_Diffuse_SplitScreen, SumConstants(0, 0, 2, 3), 16, 1 );
    }

    #undef DENOISER_NAME

    return sizeof(RelaxDiffuseSpecularSettings);
}

void nrd::DenoiserImpl::UpdateMethod_RelaxDiffuse(const MethodData& methodData)
{
    enum class Dispatch
    {
        PREPASS,
        REPROJECT,
        REPROJECT_WITH_CONFIDENCE_INPUTS,
        DISOCCLUSION_FIX,
        HISTORY_CLAMPING,
        HISTORY_CLAMPING_NO_FIREFLY,
        FIREFLY,
        SPATIAL_VARIANCE_ESTIMATION,
        ATROUS_SMEM,
        ATROUS_ODD,
        ATROUS_EVEN,
        ATROUS_ODD_LAST,
        ATROUS_EVEN_LAST,
        SPLIT_SCREEN,
    };

    const RelaxDiffuseSettings& settings = methodData.settings.diffuseRelax;

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

    float maxLuminanceRelativeDifference = -ml::Log( ml::Saturate(settings.minLuminanceWeight) );

    uint32_t diffCheckerboard = 2;

    switch (settings.checkerboardMode)
    {
    case nrd::CheckerboardMode::BLACK:
        diffCheckerboard = 0;
        break;
    case nrd::CheckerboardMode::WHITE:
        diffCheckerboard = 1;
        break;
    default:
        break;
    }

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
        AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
        AddUint(data, diffCheckerboard);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddFloat(data, m_CommonSettings.denoisingRange);
        ValidateConstants(data);

        return;
    }

    // PREPASS
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PREPASS));
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Rotator[0]);
    AddFloat4(data, ml::float4(frustumRight.x, frustumRight.y, frustumRight.z, 0));
    AddFloat4(data, ml::float4(frustumUp.x, frustumUp.y, frustumUp.z, 0));
    AddFloat4(data, ml::float4(frustumForward.x, frustumForward.y, frustumForward.z, 0));
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));
    AddUint2(data, rectW, rectH);
    AddFloat2(data, 1.0f / screenW, 1.0f / screenH);
    AddFloat2(data, 1.0f / rectW, 1.0f / rectH);
    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));
    AddFloat(data, m_IsOrtho);
    AddFloat(data, 1.0f / (0.5f * rectH * m_ProjectY));
    AddUint(data, m_CommonSettings.frameIndex);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddUint(data, diffCheckerboard);
    AddFloat(data, settings.prepassBlurRadius);
    AddFloat(data, m_CommonSettings.meterToUnitsMultiplier);
    ValidateConstants(data);

    // REPROJECT
    data = PushDispatch(methodData, m_CommonSettings.isHistoryConfidenceInputsAvailable ? AsUint(Dispatch::REPROJECT_WITH_CONFIDENCE_INPUTS) : AsUint(Dispatch::REPROJECT));
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4(data, ml::float4(frustumRight.x, frustumRight.y, frustumRight.z, 0));
    AddFloat4(data, ml::float4(frustumUp.x, frustumUp.y, frustumUp.z, 0));
    AddFloat4(data, ml::float4(frustumForward.x, frustumForward.y, frustumForward.z ,0));
    AddFloat4(data, ml::float4(prevFrustumRight.x, prevFrustumRight.y, prevFrustumRight.z, 0));
    AddFloat4(data, ml::float4(prevFrustumUp.x, prevFrustumUp.y, prevFrustumUp.z, 0));
    AddFloat4(data, ml::float4(prevFrustumForward.x, prevFrustumForward.y, prevFrustumForward.z, 0));
    AddFloat4(data, ml::float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, m_JitterDelta));
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddUint2(data, rectW, rectH);
    AddFloat2(data, 1.0f / screenW, 1.0f / screenH);
    AddFloat2(data, 1.0f / rectW, 1.0f / rectH);
    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddFloat(data, (float)settings.diffuseMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.diffuseMaxFastAccumulatedFrameNum);
    AddFloat(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1.0f : 0.0f);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, 1.0f / (0.5f * rectH * m_ProjectY));
    AddUint(data, m_CommonSettings.frameIndex);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, m_CommonSettings.disocclusionThreshold);
    AddUint(data, (uint32_t)RELAX_IsCameraStatic(ml::float3(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z), frustumRight, frustumUp, frustumForward, prevFrustumRight, prevFrustumUp, prevFrustumForward));
    AddUint(data, settings.enableSkipReprojectionTestWithoutMotion);
    AddUint(data, diffCheckerboard);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);
    AddUint(data, m_CommonSettings.isHistoryConfidenceInputsAvailable ? 1 : 0);
    AddFloat(data, settings.rejectDiffuseHistoryNormalThreshold);
    ValidateConstants(data);

    // DISOCCLUSION FIX
    data = PushDispatch(methodData, AsUint(Dispatch::DISOCCLUSION_FIX));
    AddFloat4(data, ml::float4(frustumRight.x, frustumRight.y, frustumRight.z, 0));
    AddFloat4(data, ml::float4(frustumUp.x, frustumUp.y, frustumUp.z, 0));
    AddFloat4(data, ml::float4(frustumForward.x, frustumForward.y, frustumForward.z, 0));
    AddUint2(data, rectW, rectH);
    AddFloat2(data, 1.0f / rectW, 1.0f / rectH);
    AddFloat(data, m_CommonSettings.disocclusionThreshold);
    AddFloat(data, settings.disocclusionFixEdgeStoppingNormalPower);
    AddFloat(data, settings.disocclusionFixMaxRadius);
    AddUint(data, settings.disocclusionFixNumFramesToFix);
    AddFloat(data, m_CommonSettings.denoisingRange);
    ValidateConstants(data);

    if (settings.enableAntiFirefly)
    {
        // HISTORY CLAMPING
        data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_CLAMPING));
        AddUint2(data, rectW, rectH);
        AddFloat(data, settings.historyClampingColorBoxSigmaScale);
        // Antilag parameters removed
        ValidateConstants(data);

        // FIREFLY
        data = PushDispatch(methodData, AsUint(Dispatch::FIREFLY));
        AddUint2(data, rectW, rectH);
        AddFloat(data, m_CommonSettings.denoisingRange);
        ValidateConstants(data);
    }
    else
    {
        // HISTORY CLAMPING WITHOUT FIREFLY
        data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_CLAMPING_NO_FIREFLY));
        AddUint2(data, rectW, rectH);
        AddFloat(data, settings.historyClampingColorBoxSigmaScale);
        // Antilag parameters removed
        ValidateConstants(data);
    }

    // SPATIAL VARIANCE ESTIMATION
    data = PushDispatch(methodData, AsUint(Dispatch::SPATIAL_VARIANCE_ESTIMATION));
    AddUint2(data, rectW, rectH);
    AddFloat(data, settings.phiNormal);
    AddUint(data, settings.spatialVarianceEstimationHistoryThreshold);    
    AddFloat(data, m_CommonSettings.denoisingRange);
    ValidateConstants(data);
    
    // A-TROUS
    uint32_t iterationNum = ml::Clamp(settings.atrousIterationNum, 2u, RELAX_MAX_ATROUS_PASS_NUM);
    for (uint32_t i = 0; i < iterationNum; i++)
    {
        Dispatch dispatch;
        if( i == 0 )
            dispatch = Dispatch::ATROUS_SMEM;
        else if( i == iterationNum - 1 )
            dispatch = (i % 2 == 0) ? Dispatch::ATROUS_EVEN_LAST : Dispatch::ATROUS_ODD_LAST;
        else
            dispatch = (i % 2 == 0) ? Dispatch::ATROUS_EVEN : Dispatch::ATROUS_ODD;

        data = PushDispatch(methodData, AsUint(dispatch));
        AddFloat4(data, ml::float4(frustumRight.x, frustumRight.y, frustumRight.z, 0));
        AddFloat4(data, ml::float4(frustumUp.x, frustumUp.y, frustumUp.z, 0));
        AddFloat4(data, ml::float4(frustumForward.x, frustumForward.y, frustumForward.z, 0));

        if( i == 0 )
            AddUint2(data, screenW, screenH); // TODO: rectW & rectH must be used, but... it breaks the intere pass for no reason
        else
            AddUint2(data, rectW, rectH);

        AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
        AddFloat(data, settings.diffusePhiLuminance);
        AddFloat(data, maxLuminanceRelativeDifference);
        AddFloat(data, settings.phiDepth);
        AddFloat(data, settings.phiNormal);
        AddUint(data, 1 << i);
        AddFloat(data, m_CommonSettings.denoisingRange);
        AddUint(data, m_CommonSettings.frameIndex);
        ValidateConstants(data);
    }

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
        AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
        AddUint(data, diffCheckerboard);
        AddFloat(data, m_CommonSettings.splitScreen);
        AddFloat(data, m_CommonSettings.denoisingRange);
        ValidateConstants(data);
    }
}
