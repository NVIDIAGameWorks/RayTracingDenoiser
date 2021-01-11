/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_NrdDiffuseSpecular(uint16_t w, uint16_t h)
{
    DispatchDesc desc = {};

    enum class Permanent
    {
        PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS = PERMANENT_POOL_START,
        DIFF_HISTORY,
        DIFF_STABILIZED_HISTORY_1,
        DIFF_STABILIZED_HISTORY_2,
        SPEC_HISTORY,
        SPEC_STABILIZED_HISTORY_1,
        SPEC_STABILIZED_HISTORY_2,
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    enum class Transient
    {
        INTERNAL_DATA = TRANSIENT_POOL_START,
        DIFF_ACCUMULATED,
        SPEC_ACCUMULATED,
        SCALED_VIEWZ,
    };

    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 5} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 5} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 5} );

    // Tricks to save memory
    #define DIFF_TEMP AsUint(Permanent::DIFF_STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::DIFF_STABILIZED_HISTORY_2)
    #define SPEC_TEMP AsUint(Permanent::SPEC_STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::SPEC_STABILIZED_HISTORY_2)

    PushPass("DiffuseSpecular - pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HIT) );
        PushInput( AsUint(ResourceType::IN_SPEC_HIT) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( DIFF_TEMP );
        PushOutput( SPEC_TEMP );

        desc.constantBufferDataSize = SumConstants(1, 4, 0, 3);

        AddDispatch(desc, NRD_DiffuseSpecular_PreBlur, w, h);
    }

    PushPass("DiffuseSpecular - temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::SPEC_HISTORY) );
        PushInput( DIFF_TEMP );
        PushInput( SPEC_TEMP );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        desc.constantBufferDataSize = SumConstants(4, 4, 1, 8);

        AddDispatch(desc, NRD_DiffuseSpecular_TemporalAccumulation, w, h);
    }

    PushPass("DiffuseSpecular - diff mip generation");
    {
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 1, 1 );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 2, 1 );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 3, 1 );
        PushOutput( AsUint(Transient::DIFF_ACCUMULATED), 4, 1 );

        desc.constantBufferDataSize = SumConstants(0, 0, 0, 0);

        AddDispatchWithExplicitCTASize(desc, NRD_MipGeneration_Float4, DivideUp(w, 2), DivideUp(h, 2), 16, 16);
    }

    PushPass("DiffuseSpecular - spec mip generation");
    {
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );

        PushOutput( AsUint(Transient::SPEC_ACCUMULATED), 1, 1 );
        PushOutput( AsUint(Transient::SCALED_VIEWZ), 1, 1 );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED), 2, 1 );
        PushOutput( AsUint(Transient::SCALED_VIEWZ), 2, 1 );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED), 3, 1 );
        PushOutput( AsUint(Transient::SCALED_VIEWZ), 3, 1 );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED), 4, 1 );
        PushOutput( AsUint(Transient::SCALED_VIEWZ), 4, 1 );

        desc.constantBufferDataSize = SumConstants(0, 0, 0, 0);

        AddDispatchWithExplicitCTASize(desc, NRD_MipGeneration_Float4_Float, DivideUp(w, 2), DivideUp(h, 2), 16, 16);
    }

    PushPass("DiffuseSpecular - history fix");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, 5 );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED), 1, 4 );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED), 1, 4 );

        PushOutput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushOutput( AsUint(Transient::SPEC_ACCUMULATED) );

        desc.constantBufferDataSize = SumConstants(0, 0, 1, 0);

        AddDispatch(desc, NRD_DiffuseSpecular_HistoryFix, w, h);
    }

    PushPass("DiffuseSpecular - blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        PushOutput( DIFF_TEMP );
        PushOutput( SPEC_TEMP );

        desc.constantBufferDataSize = SumConstants(1, 4, 0, 1);

        AddDispatch(desc, NRD_DiffuseSpecular_Blur, w, h);
    }

    PushPass("DiffuseSpecular - post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( DIFF_TEMP );
        PushInput( AsUint(Transient::DIFF_ACCUMULATED) );
        PushInput( SPEC_TEMP );
        PushInput( AsUint(Transient::SPEC_ACCUMULATED) );

        PushOutput( AsUint(Permanent::DIFF_HISTORY) );
        PushOutput( AsUint(Permanent::SPEC_HISTORY) );

        desc.constantBufferDataSize = SumConstants(1, 4, 0, 3);

        AddDispatch(desc, NRD_DiffuseSpecular_PostBlur, w, h);
    }

    PushPass("DiffuseSpecular - temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Permanent::DIFF_STABILIZED_HISTORY_2), 0, 1, AsUint(Permanent::DIFF_STABILIZED_HISTORY_1) );
        PushInput( AsUint(Permanent::DIFF_HISTORY) );
        PushInput( AsUint(Permanent::SPEC_STABILIZED_HISTORY_2), 0, 1, AsUint(Permanent::SPEC_STABILIZED_HISTORY_1) );
        PushInput( AsUint(Permanent::SPEC_HISTORY) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushOutput( AsUint(Permanent::DIFF_STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::DIFF_STABILIZED_HISTORY_2) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_HIT) );
        PushOutput( AsUint(Permanent::SPEC_STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::SPEC_STABILIZED_HISTORY_2) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_HIT) );

        desc.constantBufferDataSize = SumConstants(3, 2, 2, 2);

        AddDispatch(desc, NRD_DiffuseSpecular_TemporalStabilization, w, h);
    }

    #undef DIFF_TEMP
    #undef SPEC_TEMP

    return sizeof(NrdDiffuseSpecularSettings);
}

void DenoiserImpl::UpdateMethod_NrdDiffuseSpecular(const MethodData& methodData)
{
    enum class Dispatch
    {
        PRE_BLUR,
        TEMPORAL_ACCUMULATION,
        DIFF_MIP_GENERATION,
        SPEC_MIP_GENERATION,
        HISTORY_FIX,
        BLUR,
        POST_BLUR,
        TEMPORAL_STABILIZATION,
    };

    const NrdDiffuseSpecularSettings& settings = methodData.settings.diffuseSpecular;

    float disocclusionThreshold = settings.disocclusionThreshold;

    float diffMaxAccumulatedFrameNum = float( Min(settings.diffMaxAccumulatedFrameNum, NRD_MAX_HISTORY_FRAME_NUM) );
    float diffNoisinessBlurrinessBalance = settings.diffNoisinessBlurrinessBalance;
    float diffBlurRadius = settings.diffBlurRadius;
    uint32_t diffCheckerboard = ((uint32_t)settings.diffCheckerboardMode + 2) % 3;

    float specMaxAccumulatedFrameNum = float( Min(settings.specMaxAccumulatedFrameNum, NRD_MAX_HISTORY_FRAME_NUM) );
    float specNoisinessBlurrinessBalance = settings.diffNoisinessBlurrinessBalance;
    float specBlurRadius = settings.specBlurRadius;
    uint32_t specCheckerboard = ((uint32_t)settings.specCheckerboardMode + 2) % 3;

    if (m_CommonSettings.forceReferenceAccumulation)
    {
        diffMaxAccumulatedFrameNum = settings.diffMaxAccumulatedFrameNum == 0 ? 0.0f : NRD_MAX_HISTORY_FRAME_NUM;
        diffNoisinessBlurrinessBalance = 1.0f;
        diffBlurRadius = 0.0f;

        specMaxAccumulatedFrameNum = settings.specMaxAccumulatedFrameNum == 0 ? 0.0f : NRD_MAX_HISTORY_FRAME_NUM;
        specNoisinessBlurrinessBalance = 1.0f;
        specBlurRadius = 0.0f;

        disocclusionThreshold = 0.005f;
    }

    float4 diffHitDistParams = float4(&settings.diffHitDistanceParameters.A);
    float4 specHitDistParams = float4(&settings.specHitDistanceParameters.A);
    float4 specTrimmingParams_and_specBlurRadius = float4(settings.specLobeTrimmingParameters.A, settings.specLobeTrimmingParameters.B, settings.specLobeTrimmingParameters.C, specBlurRadius);
    float4 specTrimmingParams_and_checkerboardResolveAccumSpeed = float4(settings.specLobeTrimmingParameters.A, settings.specLobeTrimmingParameters.B, settings.specLobeTrimmingParameters.C, m_CheckerboardResolveAccumSpeed);

    // PRE_BLUR
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PRE_BLUR));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[0]);
    AddFloat4(data, diffHitDistParams);
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, specTrimmingParams_and_specBlurRadius);
    AddUint(data, specCheckerboard);
    AddFloat(data, diffBlurRadius);
    AddUint(data, diffCheckerboard);
    ValidateConstants(data);

    // TEMPORAL_ACCUMULATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_ACCUMULATION));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, float4( m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, m_IsOrthoPrev ) );
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, specTrimmingParams_and_checkerboardResolveAccumSpeed);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, disocclusionThreshold);
    AddFloat(data, m_JitterDelta );
    AddFloat(data, diffMaxAccumulatedFrameNum);
    AddFloat(data, diffNoisinessBlurrinessBalance);
    AddUint(data, diffCheckerboard);
    AddFloat(data, specMaxAccumulatedFrameNum);
    AddFloat(data, specNoisinessBlurrinessBalance);
    AddUint(data, specCheckerboard);
    ValidateConstants(data);

    // DIFF_MIP_GENERATION
    data = PushDispatch(methodData, AsUint(Dispatch::DIFF_MIP_GENERATION));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    ValidateConstants(data);

    // SPEC_MIP_GENERATION
    data = PushDispatch(methodData, AsUint(Dispatch::SPEC_MIP_GENERATION));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    ValidateConstants(data);

    // HISTORY_FIX
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_FIX));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddUint2(data, methodData.desc.fullResolutionWidth, methodData.desc.fullResolutionHeight);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[1]);
    AddFloat4(data, diffHitDistParams);
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, specTrimmingParams_and_specBlurRadius);
    AddFloat(data, diffBlurRadius);
    ValidateConstants(data);

    // POST_BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::POST_BLUR));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[2]);
    AddFloat4(data, diffHitDistParams);
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, specTrimmingParams_and_specBlurRadius);
    AddFloat(data, settings.specMaxAdaptiveRadiusScale);
    AddFloat(data, diffBlurRadius);
    AddFloat(data, settings.diffMaxAdaptiveRadiusScale);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, float4( m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, settings.antilagSettings.enable ? 1.0f : 0.0f ) );
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat2(data, settings.antilagSettings.intensityThresholdMin, settings.antilagSettings.intensityThresholdMax);
    AddFloat(data, diffNoisinessBlurrinessBalance);
    AddFloat(data, specNoisinessBlurrinessBalance);
    ValidateConstants(data);
}
