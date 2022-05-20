/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_RelaxDiffuseSpecular(uint16_t w, uint16_t h)
{
    #define METHOD_NAME RELAX_DiffuseSpecular

    enum class Permanent
    {
        SPEC_ILLUM_CURR = PERMANENT_POOL_START,
        SPEC_ILLUM_PREV,
        DIFF_ILLUM_CURR,
        DIFF_ILLUM_PREV,
        SPEC_ILLUM_RESPONSIVE_CURR,
        SPEC_ILLUM_RESPONSIVE_PREV,
        DIFF_ILLUM_RESPONSIVE_CURR,
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
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG8_UNORM, w, h, 1} );
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
        SPEC_REPROJECTION_CONFIDENCE,
        VIEWZ_R16F
    };

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 1} );

    RELAX_DECLARE_SHARED_CONSTANT_NUM;

    const uint32_t halfMaxPassNum = (RELAX_MAX_ATROUS_PASS_NUM - 2 + 1) / 2;

    PushPass("Pre-pass"); // Does preblur (if enabled), checkerboard reconstruction (if enabled) and generates FP16 ViewZ texture
    {
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushOutput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV));
        PushOutput( AsUint(Transient::VIEWZ_R16F));

        AddDispatch( RELAX_DiffuseSpecular_PrePass, SumConstants(0, 1, 0, 7), 16, 1 );
    }

    PushPass("Temporal accumulation");
    {

        PushInput( AsUint(Transient::SPEC_ILLUM_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(ResourceType::IN_MV));
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV));
        PushInput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushInput( AsUint(Permanent::SPEC_ILLUM_PREV) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
        PushInput( AsUint(Permanent::VIEWZ_PREV), 0, 1, AsUint(Permanent::VIEWZ_CURR));
        PushInput( AsUint(Permanent::REFLECTION_HIT_T_PREV), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_CURR) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV) );
        PushInput( AsUint(Permanent::MATERIAL_ID_PREV) );
        PushInput( AsUint(Transient::VIEWZ_R16F) ); // Bogus inputs that will not be fetched anyway
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Permanent::SPEC_ILLUM_CURR) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_CURR) );
        PushOutput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_CURR) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_CURR) );
        PushOutput( AsUint(Permanent::REFLECTION_HIT_T_CURR), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_PREV) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushOutput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE));

        AddDispatch( RELAX_DiffuseSpecular_TemporalAccumulation, SumConstants(0, 0, 0, 14), 8, 1 );
    }

    PushPass("Temporal accumulation"); // With confidence inputs
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_PING) );
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(ResourceType::IN_MV));
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Permanent::VIEWZ_CURR), 0, 1, AsUint(Permanent::VIEWZ_PREV));
        PushInput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushInput( AsUint(Permanent::SPEC_ILLUM_PREV) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushInput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
        PushInput( AsUint(Permanent::VIEWZ_PREV), 0, 1, AsUint(Permanent::VIEWZ_CURR));
        PushInput( AsUint(Permanent::REFLECTION_HIT_T_PREV), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_CURR) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV) );
        PushInput( AsUint(Permanent::MATERIAL_ID_PREV) );
        PushInput( AsUint(ResourceType::IN_SPEC_CONFIDENCE));
        PushInput( AsUint(ResourceType::IN_DIFF_CONFIDENCE));

        PushOutput( AsUint(Permanent::SPEC_ILLUM_CURR) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_CURR) );
        PushOutput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_CURR) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_CURR) );
        PushOutput( AsUint(Permanent::REFLECTION_HIT_T_CURR), 0, 1, AsUint(Permanent::REFLECTION_HIT_T_PREV) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushOutput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE));

        AddDispatch( RELAX_DiffuseSpecular_TemporalAccumulation, SumConstants(0, 0, 0, 14), 8, 1 );
    }

    PushPass("History fix");
    {
        PushInput( AsUint(Permanent::SPEC_ILLUM_CURR) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_CURR) );
        PushInput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_CURR) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_CURR));
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_PING) ); // Normal history
        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushOutput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );

        AddDispatch( RELAX_DiffuseSpecular_HistoryFix, SumConstants(0, 0, 0, 4), 8, 1 );
    }

    PushPass("History clamping"); // with firefly after it
    {
        PushInput( AsUint(Transient::SPEC_ILLUM_PING) ); // Normal history
        PushInput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushInput( AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV) ); // Responsive history
        PushInput( AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );

        PushOutput( AsUint(Permanent::SPEC_ILLUM_CURR) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_CURR) );
        PushOutput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV) );

        AddDispatch( RELAX_DiffuseSpecular_HistoryClamping, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    PushPass("History clamping"); // without firefly after it
    {
        PushInput(AsUint(Transient::SPEC_ILLUM_PING)); // Normal history
        PushInput(AsUint(Transient::DIFF_ILLUM_PING));
        PushInput(AsUint(Permanent::SPEC_ILLUM_RESPONSIVE_PREV)); // Responsive history
        PushInput(AsUint(Permanent::DIFF_ILLUM_RESPONSIVE_PREV));
        PushInput(AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR));

        PushOutput(AsUint(Permanent::SPEC_ILLUM_PREV));
        PushOutput(AsUint(Permanent::DIFF_ILLUM_PREV));
        PushOutput(AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_PREV));

        AddDispatch( RELAX_DiffuseSpecular_HistoryClamping, SumConstants(0, 0, 0, 1), 16, 1 );
    }

    PushPass("Anti-firefly");
    {
        PushInput( AsUint(Permanent::SPEC_ILLUM_CURR) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_CURR) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Permanent::SPEC_ILLUM_PREV) );
        PushOutput( AsUint(Permanent::DIFF_ILLUM_PREV) );

        AddDispatch( RELAX_DiffuseSpecular_AntiFirefly, SumConstants(0, 0, 0, 0), 16, 1 );
    }

    // A-trous (first)
    PushPass("A-trous (SMEM)");
    {
        PushInput( AsUint(Permanent::SPEC_ILLUM_PREV) );
        PushInput( AsUint(Permanent::DIFF_ILLUM_PREV) );
        PushInput( AsUint(Permanent::SPEC_DIFF_HISTORY_LENGTH_CURR) );
        PushInput( AsUint(Transient::SPEC_REPROJECTION_CONFIDENCE) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::VIEWZ_R16F) );

        PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );
        PushOutput( AsUint(Permanent::NORMAL_ROUGHNESS_PREV) );
        PushOutput( AsUint(Permanent::MATERIAL_ID_PREV) );

        AddDispatch( RELAX_DiffuseSpecular_AtrousSmem, SumConstants(0, 0, 1, 14), 8, 1 );
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

        PushOutput( AsUint(Transient::SPEC_ILLUM_PONG) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_PONG) );

        AddDispatchRepeated( RELAX_DiffuseSpecular_Atrous, SumConstants(0, 0, 0, 13), 16, 1, halfMaxPassNum );
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

        PushOutput( AsUint(Transient::SPEC_ILLUM_PING) );
        PushOutput( AsUint(Transient::DIFF_ILLUM_PING) );

        AddDispatchRepeated( RELAX_DiffuseSpecular_Atrous, SumConstants(0, 0, 0, 13), 16, 1, halfMaxPassNum );
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

        PushOutput( AsUint( ResourceType::OUT_SPEC_RADIANCE_HITDIST ) );
        PushOutput( AsUint( ResourceType::OUT_DIFF_RADIANCE_HITDIST ) );

        AddDispatch( RELAX_DiffuseSpecular_Atrous, SumConstants(0, 0, 0, 13), 16, 1 );
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

        PushOutput( AsUint( ResourceType::OUT_SPEC_RADIANCE_HITDIST ) );
        PushOutput( AsUint( ResourceType::OUT_DIFF_RADIANCE_HITDIST ) );

        AddDispatch( RELAX_DiffuseSpecular_Atrous, SumConstants(0, 0, 0, 13), 16, 1 );
    }

    PushPass("Split screen");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_RADIANCE_HITDIST));
        PushInput( AsUint(ResourceType::IN_DIFF_RADIANCE_HITDIST));

        PushOutput( AsUint( ResourceType::OUT_SPEC_RADIANCE_HITDIST ) );
        PushOutput( AsUint( ResourceType::OUT_DIFF_RADIANCE_HITDIST ) );

        AddDispatch( RELAX_DiffuseSpecular_SplitScreen, SumConstants(0, 0, 0, 3), 16, 1 );
    }

    #undef METHOD_NAME

    return sizeof(RelaxDiffuseSpecularSettings);
}

void nrd::DenoiserImpl::UpdateMethod_RelaxDiffuseSpecular(const MethodData& methodData)
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
        ATROUS_SMEM,
        ATROUS_ODD,
        ATROUS_EVEN,
        ATROUS_ODD_LAST,
        ATROUS_EVEN_LAST,
        SPLIT_SCREEN,
    };

    const RelaxDiffuseSpecularSettings& settings = methodData.settings.diffuseSpecularRelax;

    NRD_DECLARE_DIMS;

    float maxLuminanceRelativeDifference = -ml::Log( ml::Saturate(settings.minLuminanceWeight) );

    // Finding near and far Z and calculating Z thresholds in case of ortho projection
    /* commented out until we decide how the depth thresholds should be defined in ortho case
    ml::float4 frustumPlanes[ml::ePlaneType::PLANES_NUM];
    ml::MvpToPlanes(NDC_D3D, m_WorldToClip, frustumPlanes);
    float zNear = -frustumPlanes[ml::ePlaneType::PLANE_NEAR].w;
    float zFar = frustumPlanes[ml::ePlaneType::PLANE_FAR].w;
    */
    float disocclusionThresholdOrtho = m_CommonSettings.disocclusionThreshold;// * fabs(zFar - zNear);
    float depthThresholdOrtho = settings.depthThreshold; // * fabs(zFar - zNear);

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

    // PREPASS
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PREPASS));
    AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
    AddFloat4(data, m_Rotator[0]);
    AddUint(data, diffuseCheckerboard);
    AddUint(data, specularCheckerboard);
    AddFloat(data, settings.diffusePrepassBlurRadius);
    AddFloat(data, settings.specularPrepassBlurRadius);
    AddFloat(data, 1.0f);
    AddFloat(data, m_IsOrtho == 0 ? settings.depthThreshold : depthThresholdOrtho);
    AddFloat(data, settings.roughnessFraction);
    ValidateConstants(data);

    // REPROJECT
    data = PushDispatch(methodData, m_CommonSettings.isHistoryConfidenceInputsAvailable ? AsUint(Dispatch::REPROJECT_WITH_CONFIDENCE_INPUTS) : AsUint(Dispatch::REPROJECT));
    AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
    AddFloat(data, (float)settings.specularMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.specularMaxFastAccumulatedFrameNum);
    AddFloat(data, (float)settings.diffuseMaxAccumulatedFrameNum);
    AddFloat(data, (float)settings.diffuseMaxFastAccumulatedFrameNum);
    AddUint(data, diffuseCheckerboard);
    AddUint(data, specularCheckerboard);
    AddFloat(data, m_IsOrtho == 0 ? m_CommonSettings.disocclusionThreshold : disocclusionThresholdOrtho);
    AddFloat(data, settings.roughnessFraction);
    AddFloat(data, settings.specularVarianceBoost);
    AddUint(data, settings.enableSpecularVirtualHistoryClamping ? 1 : 0);
    AddUint(data, settings.enableReprojectionTestSkippingWithoutMotion && isCameraStatic);
    AddUint(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 1 : 0);
    AddFloat(data, settings.diffuseHistoryRejectionNormalThreshold);
    AddUint(data, m_CommonSettings.isHistoryConfidenceInputsAvailable ? 1 : 0);
    ValidateConstants(data);

    // DISOCCLUSION FIX
    data = PushDispatch(methodData, AsUint(Dispatch::DISOCCLUSION_FIX));
    AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
    AddFloat(data, m_IsOrtho == 0 ? m_CommonSettings.disocclusionThreshold : disocclusionThresholdOrtho);
    AddFloat(data, settings.disocclusionFixEdgeStoppingNormalPower);
    AddFloat(data, settings.disocclusionFixMaxRadius);
    AddUint(data, settings.disocclusionFixNumFramesToFix);
    ValidateConstants(data);

    if (settings.enableAntiFirefly)
    {
        // HISTORY CLAMPING
        data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_CLAMPING));
        AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);
        AddFloat(data, settings.historyClampingColorBoxSigmaScale);
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
        ValidateConstants(data);
    }

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
        AddSharedConstants_Relax(methodData, data, nrd::Method::RELAX_DIFFUSE_SPECULAR);

        if (i == 0)
        {
            AddUint2(data, screenW, screenH); // For Atrous_shmem
            AddUint(data, settings.spatialVarianceEstimationHistoryThreshold);
        }

        AddFloat(data, settings.specularPhiLuminance);
        AddFloat(data, settings.diffusePhiLuminance);
        AddFloat(data, maxLuminanceRelativeDifference);
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