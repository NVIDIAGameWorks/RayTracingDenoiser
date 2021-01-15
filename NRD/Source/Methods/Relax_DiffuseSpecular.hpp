/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_RelaxDiffuseSpecular(uint16_t w, uint16_t h)
{
    DispatchDesc desc = {};

    enum class Permanent
    {
        SPEC_DIFF_ILLUM_LOGLUV_CURR = PERMANENT_POOL_START,
        SPEC_DIFF_ILLUM_LOGLUV_PREV,
        SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_CURR,
        SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_PREV,
        SPEC_DIFF_MOMENTS_CURR,
        SPEC_DIFF_MOMENTS_PREV,
        SPEC_ILLUM_UNPACKED,
        DIFF_ILLUM_UNPACKED,
        REFLECTION_HIT_T_CURR,
        REFLECTION_HIT_T_PREV,
        HISTORY_LENGTH_CURR,
        HISTORY_LENGTH_PREV,
        NORMAL_ROUGHNESS_DEPTH_CURR,
        NORMAL_ROUGHNESS_DEPTH_PREV
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );

    enum class Transient
    {
        SPEC_ILLUM_VARIANCE_PING = TRANSIENT_POOL_START,
        SPEC_ILLUM_VARIANCE_PONG,
        DIFF_ILLUM_VARIANCE_PING,
        DIFF_ILLUM_VARIANCE_PONG,
        SPEC_REPROJECTION_CONFIDENCE
    };

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back({ Format::RGBA16_SFLOAT, w, h, 1 });
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back({ Format::RGBA16_SFLOAT, w, h, 1 });

    PushPass("RELAX::DiffuseSpecular - Pack input data");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        desc.constantBufferDataSize = SumConstants(1, 0, 0, 0, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_PackInputData, w, h, 16, 16);
    }

    PushPass("RELAX::DiffuseSpecular - Reproject");
    {
        PushInput( AsUint(ResourceType::IN_SPEC));
        PushInput( AsUint(ResourceType::IN_DIFF));
        PushInput( AsUint(ResourceType::IN_MV));
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_PREV) );
        PushInput( AsUint(Permanent::SPEC_ILLUM_UNPACKED) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_UNPACKED) );
        PushInput( AsUint(Permanent::SPEC_DIFF_MOMENTS_PREV) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR) );
        PushInput( AsUint(Permanent::REFLECTION_HIT_T_PREV), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_CURR) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_PREV), 0, 1, AsUint(Permanent::HISTORY_LENGTH_CURR) );

        PushOutput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_CURR) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_CURR) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_MOMENTS_CURR) );
        PushOutput( AsUint(Permanent::REFLECTION_HIT_T_CURR), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_PREV) );
        PushOutput( AsUint(Permanent::HISTORY_LENGTH_CURR), 0, 1, AsUint(Permanent::HISTORY_LENGTH_PREV) );
        PushOutput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE));

        desc.constantBufferDataSize = SumConstants(5, 1, 3, 10, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_Reproject, w, h, 16, 16);
    }

    PushPass("RELAX::DiffuseSpecular - Disocclusion fix");
    {
        PushInput(AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_CURR));
        PushInput(AsUint(Permanent::SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_CURR));
        PushInput(AsUint(Permanent::SPEC_DIFF_MOMENTS_CURR));
        PushInput( AsUint(Permanent::HISTORY_LENGTH_CURR), 0, 1, AsUint(Permanent::HISTORY_LENGTH_PREV) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput(AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_PREV));
        PushOutput(AsUint(Permanent::SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_PREV));
        PushOutput(AsUint(Permanent::SPEC_DIFF_MOMENTS_PREV));

        desc.constantBufferDataSize = SumConstants(2, 0, 2, 4, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_DisocclusionFix, w, h, 8, 8);
    }

    PushPass("RELAX::DiffuseSpecular - History clamping");
    {
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_RESPONSIVE_LOGLUV_PREV) );

        PushOutput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_CURR) );

        desc.constantBufferDataSize = SumConstants(0, 0, 1, 1, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_HistoryClamping, w, h, 16, 16);
    }

    PushPass("RELAX::DiffuseSpecular - Firefly");
    {
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_CURR) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_PREV) );
        PushOutput( AsUint(Permanent::SPEC_ILLUM_UNPACKED) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_UNPACKED) );

        desc.constantBufferDataSize = SumConstants(0, 0, 1, 1, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_Firefly, w, h, 16, 16);
    }

    PushPass("RELAX::DiffuseSpecular - Spatial variance estimation");
    {
        PushInput( AsUint(Permanent::SPEC_DIFF_ILLUM_LOGLUV_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_MOMENTS_PREV) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_CURR), 0, 1, AsUint(Permanent::HISTORY_LENGTH_PREV) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PING) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PING) );

        desc.constantBufferDataSize = SumConstants(0, 0, 1, 2, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_SpatialVarianceEstimation, w, h, 16, 16);
    }
    
    PushPass("RELAX::DiffuseSpecular - Atrous 1");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PING) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_CURR), 0, 1, AsUint(Permanent::HISTORY_LENGTH_PREV) );
        PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput(AsUint(Transient::SPEC_ILLUM_VARIANCE_PONG));
        PushOutput(AsUint(Transient::DIFF_ILLUM_VARIANCE_PONG));

        desc.constantBufferDataSize = SumConstants(2, 0, 2, 8, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_ATrousShmem, w, h, 16, 16);
    }

    PushPass("RELAX::DiffuseSpecular - Atrous 2");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PONG) );
        PushInput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PONG) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_CURR), 0, 1, AsUint(Permanent::HISTORY_LENGTH_PREV) );
        PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PING) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PING) );

        desc.constantBufferDataSize = SumConstants(2, 0, 2, 8, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_ATrousStandard, w, h, 8, 8);
    }

    PushPass("RELAX::DiffuseSpecular - Atrous 3");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PING) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_CURR), 0, 1, AsUint(Permanent::HISTORY_LENGTH_PREV) );
        PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PONG) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PONG) );

        desc.constantBufferDataSize = SumConstants(2, 0, 2, 8, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_ATrousStandard, w, h, 8, 8);
    }

    PushPass("RELAX::DiffuseSpecular - Atrous 4");
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PONG) );
        PushInput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PONG) );
        PushInput( AsUint(Permanent::HISTORY_LENGTH_CURR), 0, 1, AsUint(Permanent::HISTORY_LENGTH_PREV) );
        PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_VARIANCE_PING) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_VARIANCE_PING) );

        desc.constantBufferDataSize = SumConstants(2, 0, 2, 8, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_ATrousStandard, w, h, 8, 8);
    }

    PushPass("RELAX::DiffuseSpecular - Atrous 5");
    {
        PushInput(AsUint(Transient::SPEC_ILLUM_VARIANCE_PING));
        PushInput(AsUint(Transient::DIFF_ILLUM_VARIANCE_PING));
        PushInput( AsUint(Permanent::HISTORY_LENGTH_CURR), 0, 1, AsUint(Permanent::HISTORY_LENGTH_PREV) );
        PushInput(AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE));
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_CURR), 0, 1, AsUint(Permanent::NORMAL_ROUGHNESS_DEPTH_PREV) );

        PushOutput( AsUint(ResourceType::OUT_SPEC) );
        PushOutput( AsUint(ResourceType::OUT_DIFF) );

        desc.constantBufferDataSize = SumConstants(2, 0, 2, 8, false);

        AddDispatchWithExplicitCTASize(desc, RELAX_ATrousStandard, w, h, 8, 8);
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
        ATROUS_1,
        ATROUS_2,
        ATROUS_3,
        ATROUS_4,
        ATROUS_5,
    };

    const RelaxDiffuseSpecularSettings& settings = methodData.settings.relax;

    int w = methodData.desc.fullResolutionWidth;
    int h = methodData.desc.fullResolutionHeight;

    // PACK INPUT DATA
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PACK_INPUT_DATA));
    AddFloat4x4(data, m_ViewToClip);    
    ValidateConstants(data);

    // REPROJECT
    data = PushDispatch(methodData, AsUint(Dispatch::REPROJECT));

    AddFloat4x4(data, m_ClipToView);
    AddFloat4x4(data, m_ViewToClip);
    AddFloat4x4(data, m_ClipToWorld);
    AddFloat4x4(data, m_ClipToWorldPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4(data, float4(m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, m_JitterDelta));
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddUint2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.bicubicFilterForReprojectionEnabled ? 1.0f : 0.0f);
    AddFloat(data, settings.specularAlpha);
    AddFloat(data, settings.specularResponsiveAlpha);
    AddFloat(data, settings.specularVarianceBoost);
    AddFloat(data, settings.diffuseAlpha);
    AddFloat(data, settings.diffuseResponsiveAlpha);
    AddFloat(data, m_CommonSettings.worldSpaceMotion ? 1.0f : 0.0f);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, 1.0f / (0.5f * h * m_ProjectY));
    AddFloat(data, settings.needHistoryReset ? 1.0f : 0.0f);
    ValidateConstants(data);

    // DISOCCLUSION FIX
    data = PushDispatch(methodData, AsUint(Dispatch::DISOCCLUSION_FIX));
    AddFloat4x4(data, m_ClipToWorld);
    AddFloat4x4(data, m_ClipToView);
    AddUint2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.disocclusionFixEdgeStoppingZFraction);
    AddFloat(data, settings.disocclusionFixEdgeStoppingNormalPower);
    AddFloat(data, settings.disocclusionFixMaxRadius);
    AddUint(data, settings.disocclusionFixNumFramesToFix);
    ValidateConstants(data);

    // HISTORY CLAMPING
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_CLAMPING));
    AddUint2(data, w, h);
    AddFloat(data, settings.historyClampingColorBoxSigmaScale);
    ValidateConstants(data);
    
    // FIREFLY
    data = PushDispatch(methodData, AsUint(Dispatch::FIREFLY));
    AddUint2(data, w, h);
    AddUint(data, settings.fireflySuppressionEnabled ? 1 : 0);
    ValidateConstants(data);

    // SPATIAL VARIANCE ESTIMATION
    data = PushDispatch(methodData, AsUint(Dispatch::SPATIAL_VARIANCE_ESTIMATION));
    AddUint2(data, w, h);
    AddFloat(data, settings.phiNormal);
    AddUint(data, settings.spatialVarianceEstimationHistoryThreshold);    
    ValidateConstants(data);
    
    // ATROUS_1
    data = PushDispatch(methodData, AsUint(Dispatch::ATROUS_1));
    AddFloat4x4(data, m_ClipToWorld);
    AddFloat4x4(data, m_ViewToClip);
    AddUint2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.specularPhiLuminance);
    AddFloat(data, settings.diffusePhiLuminance);
    AddFloat(data, settings.phiDepth);
    AddFloat(data, settings.phiNormal);
    AddUint(data, 1);
    AddFloat(data, settings.roughnessEdgeStoppingRelaxation);
    AddFloat(data, settings.normalEdgeStoppingRelaxation);
    AddFloat(data, settings.luminanceEdgeStoppingRelaxation);
    ValidateConstants(data);

    // ATROUS_2
    data = PushDispatch(methodData, AsUint(Dispatch::ATROUS_2));
    AddFloat4x4(data, m_ClipToWorld);
    AddFloat4x4(data, m_ViewToClip);
    AddUint2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.specularPhiLuminance);
    AddFloat(data, settings.diffusePhiLuminance);
    AddFloat(data, settings.phiDepth);
    AddFloat(data, settings.phiNormal);
    AddUint(data, 2);
    AddFloat(data, settings.roughnessEdgeStoppingRelaxation);
    AddFloat(data, settings.normalEdgeStoppingRelaxation);
    AddFloat(data, settings.luminanceEdgeStoppingRelaxation);
    ValidateConstants(data);

    // ATROUS_3
    data = PushDispatch(methodData, AsUint(Dispatch::ATROUS_3));
    AddFloat4x4(data, m_ClipToWorld);
    AddFloat4x4(data, m_ViewToClip);
    AddUint2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.specularPhiLuminance);
    AddFloat(data, settings.diffusePhiLuminance);
    AddFloat(data, settings.phiDepth);
    AddFloat(data, settings.phiNormal);
    AddUint(data, 4);
    AddFloat(data, settings.roughnessEdgeStoppingRelaxation);
    AddFloat(data, settings.normalEdgeStoppingRelaxation);
    AddFloat(data, settings.luminanceEdgeStoppingRelaxation);
    ValidateConstants(data);

    // ATROUS_4
    data = PushDispatch(methodData, AsUint(Dispatch::ATROUS_4));
    AddFloat4x4(data, m_ClipToWorld);
    AddFloat4x4(data, m_ViewToClip);
    AddUint2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.specularPhiLuminance);
    AddFloat(data, settings.diffusePhiLuminance);
    AddFloat(data, settings.phiDepth);
    AddFloat(data, settings.phiNormal);
    AddUint(data, 8);
    AddFloat(data, settings.roughnessEdgeStoppingRelaxation);
    AddFloat(data, settings.normalEdgeStoppingRelaxation);
    AddFloat(data, settings.luminanceEdgeStoppingRelaxation);
    ValidateConstants(data);

    // ATROUS_5
    data = PushDispatch(methodData, AsUint(Dispatch::ATROUS_5));
    AddFloat4x4(data, m_ClipToWorld);
    AddFloat4x4(data, m_ViewToClip);
    AddUint2(data, w, h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, settings.specularPhiLuminance);
    AddFloat(data, settings.diffusePhiLuminance);
    AddFloat(data, settings.phiDepth);
    AddFloat(data, settings.phiNormal);
    AddUint(data, 16);
    AddFloat(data, settings.roughnessEdgeStoppingRelaxation);
    AddFloat(data, settings.normalEdgeStoppingRelaxation);
    AddFloat(data, settings.luminanceEdgeStoppingRelaxation);
    ValidateConstants(data);    
}
