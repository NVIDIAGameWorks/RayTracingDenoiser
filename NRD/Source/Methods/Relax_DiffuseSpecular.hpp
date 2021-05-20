/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

constexpr uint32_t RELAX_MAX_ATROUS_PASS_NUM = 8;

inline ml::float3 GetFrustumForward(const ml::float4x4& viewToWorld, const ml::float4& frustum)
{
    // Note: this vector is not normalized for non-symmetric projections but that's correct.
    // It has to have .z coordinate equal to 1.0 to correctly reconstruct world position in shaders.
    ml::float4 frustumForwardView = ml::float4(0.5f, 0.5f, 1.0f, 0.0f) * ml::float4(frustum.z, frustum.w, 1.0f, 0.0f) + ml::float4(frustum.x, frustum.y, 0.0f, 0.0f);
    ml::float3 frustumForwardWorld = (viewToWorld * frustumForwardView).To3d();
    return frustumForwardWorld;
}

size_t DenoiserImpl::AddMethod_RelaxDiffuseSpecular(uint16_t w, uint16_t h)
{
    enum class Permanent
    {
        SPEC_DIFF_ILLUM_LOGLUV_CURR = PERMANENT_POOL_START,
        SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_CURR,
        SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_PREV,
        SPEC_DIFF_MOMENTS_CURR,
        SPEC_DIFF_MOMENTS_PREV,
        SPEC_ILLUM_UNPACKED,
        DIFF_ILLUM_UNPACKED,
        REFLECTION_HIT_T_CURR,
        REFLECTION_HIT_T_PREV,
        SPEC_DIFF_HISTORY_LENGTH_CURR,
        SPEC_DIFF_HISTORY_LENGTH_PREV,
        NORMAL_ROUGHNESS_DEPTH_CURR,
        NORMAL_ROUGHNESS_DEPTH_PREV
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );

    enum class Transient
    {
        SPEC_ILLUM_VARIANCE_PING = TRANSIENT_POOL_START,
        SPEC_ILLUM_VARIANCE_PONG,
        DIFF_ILLUM_VARIANCE_PING,
        DIFF_ILLUM_VARIANCE_PONG,
        SPEC_REPROJECTION_CONFIDENCE,
        SPEC_DIFF_ILLUM_LOGLUV_PREV
    };

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back({ Format::RGBA16_SFLOAT, w, h, 1 });
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RG32_UINT, w, h, 1} );

    SetSharedConstants(0, 0, 0, 0);

    const uint32_t halfMaxPassNum = (RELAX_MAX_ATROUS_PASS_NUM - 2 + 1) / 2;

    PushPass("RELAX::DiffuseSpecular - Pack input data");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        AddDispatch( RELAX_PackInputData, SumConstants(0, 0, 1, 0), 16, 1 );
    }

    PushPass("RELAX::DiffuseSpecular - Reproject");
    {
        PushInput( AsUint(ResourceType::IN_SPEC_HIT));
        PushInput( AsUint(ResourceType::IN_DIFF_HIT));
        PushInput( AsUint(ResourceType::IN_MV));
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_PREV) );
        PushInput( AsUint(Permanent::SPEC_ILLUM_UNPACKED) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_UNPACKED) );
        PushInput( AsUint(Permanent::SPEC_DIFF_MOMENTS_PREV) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR) );
        PushInput( AsUint(Permanent::REFLECTION_HIT_T_PREV), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_CURR) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV) );

        PushOutput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_CURR) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_CURR) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_MOMENTS_CURR) );
        PushOutput( AsUint(Permanent::REFLECTION_HIT_T_CURR), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_PREV) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushOutput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE));

        AddDispatch( RELAX_Reproject, SumConstants(2, 7, 6, 14), 8, 1 );
    }

    PushPass("RELAX::DiffuseSpecular - Disocclusion fix");
    {
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_CURR) );
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_CURR) );
        PushInput( AsUint(Permanent::SPEC_DIFF_MOMENTS_CURR) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Transient::SPEC_DIFF_ILLUM_LOGLUV_PREV) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_PREV) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_MOMENTS_PREV) );

        AddDispatch( RELAX_DisocclusionFix, SumConstants(0, 3, 2, 5), 8, 1 );
    }

    PushPass("RELAX::DiffuseSpecular - History clamping");
    {
        PushInput( AsUint(Transient::SPEC_DIFF_ILLUM_LOGLUV_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );

        PushOutput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_CURR) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV) );

        AddDispatch( RELAX_HistoryClamping, SumConstants(0, 0, 1, 5), 16, 1 );
    }

    PushPass("RELAX::DiffuseSpecular - Firefly suppression");
    {
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_CURR) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Transient::SPEC_DIFF_ILLUM_LOGLUV_PREV) );
        PushOutput( AsUint(Permanent::SPEC_ILLUM_UNPACKED) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_UNPACKED) );

        AddDispatch( RELAX_Firefly, SumConstants(0, 0, 1, 2), 16, 1 );
    }

    PushPass("RELAX::DiffuseSpecular - Spatial variance estimation");
    {
        PushInput( AsUint(Transient::SPEC_DIFF_ILLUM_LOGLUV_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_MOMENTS_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PING) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PING) );

        AddDispatch( RELAX_SpatialVarianceEstimation, SumConstants(0, 0, 1, 3), 16, 1 );
    }

    // A-trous (first)
    PushPass("RELAX::DiffuseSpecular - A-trous (SMEM)");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PING) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PONG) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PONG) );

        AddDispatch( RELAX_ATrousShmem, SumConstants(0, 3, 2, 13), 8, 1 );
    }

    // A-trous (odd)
    PushPass("RELAX::DiffuseSpecular - A-trous");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PONG) );
        PushInput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PONG) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PING) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PING) );

        AddDispatchRepeated( RELAX_ATrousStandard, SumConstants(0, 3, 2, 13), 8, 1, halfMaxPassNum );
    }

    // A-trous (even)
    PushPass("RELAX::DiffuseSpecular - A-trous");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PING) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PONG) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PONG) );

        AddDispatchRepeated( RELAX_ATrousStandard, SumConstants(0, 3, 2, 13), 8, 1, halfMaxPassNum );
    }

    // A-trous (odd, last)
    PushPass("RELAX::DiffuseSpecular - A-trous");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PONG) );
        PushInput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PONG) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint( ResourceType::OUT_SPEC_HIT ) );
        PushOutput( AsUint( ResourceType::OUT_DIFF_HIT ) );

        AddDispatch( RELAX_ATrousStandard, SumConstants(0, 3, 2, 13), 8, 1 );
    }

    // A-trous (even, last)
    PushPass("RELAX::DiffuseSpecular - A-trous");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PING) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint( ResourceType::OUT_SPEC_HIT ) );
        PushOutput( AsUint( ResourceType::OUT_DIFF_HIT ) );

        AddDispatch( RELAX_ATrousStandard, SumConstants(0, 3, 2, 13), 8, 1 );
    }
    
    PushPass("RELAX::DiffuseSpecular - split screen");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_HIT));
        PushInput( AsUint(ResourceType::IN_DIFF_HIT));

        PushOutput( AsUint( ResourceType::OUT_SPEC_HIT ) );
        PushOutput( AsUint( ResourceType::OUT_DIFF_HIT ) );

        AddDispatch( RELAX_SplitScreen, SumConstants(0, 0, 2, 2), 16, 1 );
    }

    return sizeof(RelaxDiffuseSpecularSettings);
}

void DenoiserImpl::UpdateMethod_RelaxDiffuseSpecular(const MethodData& methodData)
{
    enum class Dispatch
    {
        PACK_INPUT_DATA,
        REPROJECT,
        DISOCCLUSION_FIX,
        HISTORY_CLAMPING,
        FIREFLY,
        SPATIAL_VARIANCE_ESTIMATION,
        ATROUS_SMEM,
        ATROUS_ODD,
        ATROUS_EVEN,
        ATROUS_ODD_LAST,
        ATROUS_EVEN_LAST,
        SPLIT_SCREEN,
    };

    const RelaxDiffuseSpecularSettings& settings = methodData.settings.relax;

    uint32_t screenW = methodData.desc.fullResolutionWidth;
    uint32_t screenH = methodData.desc.fullResolutionHeight;
    uint32_t rectW = uint32_t(screenW * m_CommonSettings.resolutionScale + 0.5f);
    uint32_t rectH = uint32_t(screenH * m_CommonSettings.resolutionScale + 0.5f);
    uint32_t rectWprev = uint32_t(screenW * m_ResolutionScalePrev + 0.5f);
    uint32_t rectHprev = uint32_t(screenH * m_ResolutionScalePrev + 0.5f);

    // Calculate camera right and up vectors in worldspace scaled according to frustum extents,
    // and unit forward vector, for fast worldspace position reconstruction in shaders
    float tanHalfFov = 1.0f / m_ViewToClip.a00;
    float aspect = m_ViewToClip.a00 / m_ViewToClip.a11;
    ml::float3 frustumRight = m_WorldToView.GetRow0().To3d() * tanHalfFov;
    ml::float3 frustumUp = m_WorldToView.GetRow1().To3d() * tanHalfFov * aspect;
    ml::float3 frustumForward = GetFrustumForward(m_ViewToWorld, m_Frustum);

    float prevTanHalfFov = 1.0f / m_ViewToClipPrev.a00;
    float prevAspect = m_ViewToClipPrev.a00 / m_ViewToClipPrev.a11;
    ml::float3 prevFrustumRight = m_WorldToViewPrev.GetRow0().To3d() * prevTanHalfFov;
    ml::float3 prevFrustumUp = m_WorldToViewPrev.GetRow1().To3d() * prevTanHalfFov * prevAspect;
    ml::float3 prevFrustumForward = GetFrustumForward(m_ViewToWorldPrev, m_FrustumPrev);

    float maxLuminanceRelativeDifference = -ml::Log( ml::Saturate(settings.minLuminanceWeight) );

    // PACK INPUT DATA
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PACK_INPUT_DATA));
    AddUint2(data, m_CommonSettings.inputDataOrigin[0], m_CommonSettings.inputDataOrigin[1]);
    ValidateConstants(data);

    // REPROJECT
    data = PushDispatch(methodData, AsUint(Dispatch::REPROJECT));
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
    AddUint2(data, m_CommonSettings.inputDataOrigin[0], m_CommonSettings.inputDataOrigin[1]);
    AddUint2(data, rectW, rectH);
    AddFloat2(data, 1.0f / screenW, 1.0f / screenH);
    AddFloat2(data, 1.0f / rectW, 1.0f / rectH);
    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddFloat(data, settings.bicubicFilterForReprojectionEnabled ? 1.0f : 0.0f);
    AddFloat(data, (float)settings.specularMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.specularMaxFastAccumulatedFrameNum);
    AddFloat(data, settings.specularVarianceBoost);
    AddFloat(data, (float)settings.diffuseMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.diffuseMaxFastAccumulatedFrameNum);
    AddFloat(data, m_CommonSettings.worldSpaceMotion ? 1.0f : 0.0f);
    AddFloat(data, m_IsOrtho);
    AddUint(data, settings.specularVirtualHistoryClamping ? 1 : 0);
    AddUint(data, settings.roughnessBasedSpecularAccumulation ? 1 : 0);
    AddFloat(data, 1.0f / (0.5f * rectH * m_ProjectY));
    AddFloat(data, m_CommonSettings.frameIndex == 0 ? 1.0f : 0.0f);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, m_CommonSettings.disocclusionThreshold);
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

    // HISTORY CLAMPING
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_CLAMPING));
    AddUint2(data, rectW, rectH);
    AddFloat(data, settings.historyClampingColorBoxSigmaScale);
    AddFloat(data, settings.specularAntiLagColorBoxSigmaScale);
    AddFloat(data, settings.specularAntiLagPower);
    AddFloat(data, settings.diffuseAntiLagColorBoxSigmaScale);
    AddFloat(data, settings.diffuseAntiLagPower);
    ValidateConstants(data);
    
    // FIREFLY
    data = PushDispatch(methodData, AsUint(Dispatch::FIREFLY));
    AddUint2(data, rectW, rectH);
    AddUint(data, settings.antifirefly ? 1 : 0);
    AddFloat(data, m_CommonSettings.denoisingRange);
    ValidateConstants(data);

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
        AddFloat(data, settings.specularPhiLuminance);
        AddFloat(data, settings.diffusePhiLuminance);
        AddFloat(data, maxLuminanceRelativeDifference);
        AddFloat(data, settings.phiDepth);
        AddFloat(data, settings.phiNormal);
        AddFloat(data, settings.specularLobeAngleFraction);
        AddFloat(data, ml::DegToRad(settings.specularLobeAngleSlack));
        AddUint(data, 1 << i);
        AddUint(data, settings.roughnessEdgeStoppingEnabled);
        AddFloat(data, settings.roughnessEdgeStoppingRelaxation);
        AddFloat(data, settings.normalEdgeStoppingRelaxation);
        AddFloat(data, settings.luminanceEdgeStoppingRelaxation);
        AddFloat(data, m_CommonSettings.denoisingRange);
        ValidateConstants(data);
    }

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(methodData, AsUint(Dispatch::SPLIT_SCREEN));
        AddUint2(data, m_CommonSettings.inputDataOrigin[0], m_CommonSettings.inputDataOrigin[1]);
        AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
        AddFloat(data, m_CommonSettings.splitScreen);
        AddFloat(data, m_CommonSettings.denoisingRange);
        ValidateConstants(data);
    }
}
