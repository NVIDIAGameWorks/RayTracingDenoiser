/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_ReblurSpecular(uint16_t w, uint16_t h)
{
    DispatchDesc desc = {};

    enum class Permanent
    {
        PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS = PERMANENT_POOL_START,
        HISTORY,
        STABILIZED_HISTORY_1,
        STABILIZED_HISTORY_2,
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    enum class Transient
    {
        INTERNAL_DATA = TRANSIENT_POOL_START,
        ACCUMULATED,
        SCALED_VIEWZ,
    };

    m_TransientPool.push_back( {Format::RG8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 5} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 5} );

    // Tricks to save memory
    #define TEMP AsUint(Permanent::STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_2)

    PushPass("REBLUR::Specular - pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_HIT) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( TEMP );

        desc.constantBufferDataSize = SumConstants(1, 3, 0, 1);

        AddDispatch(desc, REBLUR_Specular_PreBlur, w, h);
    }

    PushPass("REBLUR::Specular - temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(Permanent::HISTORY) );
        PushInput( TEMP );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ACCUMULATED) );

        desc.constantBufferDataSize = SumConstants(4, 3, 1, 7);

        AddDispatchWithExplicitCTASize(desc, REBLUR_Specular_TemporalAccumulation, w, h, 8, 8);
    }

    PushPass("REBLUR::Specular - mip generation");
    {
        PushInput( AsUint(Transient::ACCUMULATED) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );

        PushOutput( AsUint(Transient::ACCUMULATED), 1, 1 );
        PushOutput( AsUint(Transient::SCALED_VIEWZ), 1, 1 );
        PushOutput( AsUint(Transient::ACCUMULATED), 2, 1 );
        PushOutput( AsUint(Transient::SCALED_VIEWZ), 2, 1 );
        PushOutput( AsUint(Transient::ACCUMULATED), 3, 1 );
        PushOutput( AsUint(Transient::SCALED_VIEWZ), 3, 1 );
        PushOutput( AsUint(Transient::ACCUMULATED), 4, 1 );
        PushOutput( AsUint(Transient::SCALED_VIEWZ), 4, 1 );

        desc.constantBufferDataSize = SumConstants(0, 0, 0, 0);

        AddDispatchWithExplicitCTASize(desc, NRD_MipGeneration_Float4_Float, DivideUp(w, 2), DivideUp(h, 2), 16, 16);
    }

    PushPass("REBLUR::Specular - history fix");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, 5 );
        PushInput( AsUint(Transient::ACCUMULATED), 1, 4 );

        PushOutput( AsUint(Transient::ACCUMULATED) );

        desc.constantBufferDataSize = SumConstants(0, 0, 1, 0);

        AddDispatch(desc, REBLUR_Specular_HistoryFix, w, h);
    }

    PushPass("REBLUR::Specular - blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::ACCUMULATED) );

        PushOutput( TEMP );

        desc.constantBufferDataSize = SumConstants(1, 3, 0, 0);

        AddDispatch(desc, REBLUR_Specular_Blur, w, h);
    }

    PushPass("REBLUR::Specular - post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( TEMP );
        PushInput( AsUint(Transient::ACCUMULATED) );

        PushOutput( AsUint(Permanent::HISTORY) );

        desc.constantBufferDataSize = SumConstants(1, 3, 0, 1);

        AddDispatch(desc, REBLUR_Specular_PostBlur, w, h);
    }

    PushPass("REBLUR::Specular - temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Permanent::STABILIZED_HISTORY_2), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_1) );
        PushInput( AsUint(Permanent::HISTORY) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushOutput( AsUint(Permanent::STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_2) );
        PushOutput( AsUint(ResourceType::OUT_SPEC_HIT) );

        desc.constantBufferDataSize = SumConstants(3, 4, 1, 1);

        AddDispatch(desc, REBLUR_Specular_TemporalStabilization, w, h);
    }

    #undef TEMP

    return sizeof(ReblurSpecularSettings);
}

void DenoiserImpl::UpdateMethod_ReblurSpecular(const MethodData& methodData)
{
    enum class Dispatch
    {
        PRE_BLUR,
        TEMPORAL_ACCUMULATION,
        MIP_GENERATION,
        HISTORY_FIX,
        BLUR,
        POST_BLUR,
        TEMPORAL_STABILIZATION,
   };

    const ReblurSpecularSettings& settings = methodData.settings.specular;

    float maxAccumulatedFrameNum = float( Min(settings.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM) );
    float noisinessBlurrinessBalance = settings.noisinessBlurrinessBalance;
    float blurRadius = settings.blurRadius;
    float disocclusionThreshold = settings.disocclusionThreshold;

    if (m_CommonSettings.forceReferenceAccumulation)
    {
        maxAccumulatedFrameNum = settings.maxAccumulatedFrameNum == 0 ? 0.0f : REBLUR_MAX_HISTORY_FRAME_NUM;
        noisinessBlurrinessBalance = 1.0f;
        blurRadius = 0.0f;
        disocclusionThreshold = 0.005f;
    }

    float4 specHitDistParams = float4(&settings.hitDistanceParameters.A);
    float4 trimmingParams_and_blurRadius = float4(settings.lobeTrimmingParameters.A, settings.lobeTrimmingParameters.B, settings.lobeTrimmingParameters.C, blurRadius);
    uint32_t checkerboard = ((uint32_t)settings.checkerboardMode + 2) % 3;
    float4 antilag1 = float4(settings.antilagIntensitySettings.sigmaScale, settings.antilagHitDistanceSettings.sigmaScale, settings.antilagIntensitySettings.sensitivityToDarkness, settings.antilagHitDistanceSettings.sensitivityToDarkness);
    float4 antilag2 = float4(settings.antilagIntensitySettings.thresholdMin, settings.antilagHitDistanceSettings.thresholdMin, settings.antilagIntensitySettings.thresholdMax, settings.antilagHitDistanceSettings.thresholdMax);

    if (!settings.antilagIntensitySettings.enable)
    {
        antilag2.x = 99998.0f;
        antilag2.z = 99999.0f;
    }

    if (!settings.antilagHitDistanceSettings.enable)
    {
        antilag2.y = 99998.0f;
        antilag2.w = 99999.0f;
    }

    // PRE_BLUR
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PRE_BLUR));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[0]);
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, trimmingParams_and_blurRadius);
    AddUint(data, checkerboard);
    ValidateConstants(data);

    // TEMPORAL_ACCUMULATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_ACCUMULATION));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4(data, float4(m_CameraDeltaSmoothed));
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, specHitDistParams);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, m_IsOrthoPrev );
    AddFloat(data, disocclusionThreshold);
    AddFloat(data, m_JitterDelta );
    AddFloat(data, maxAccumulatedFrameNum);
    AddFloat(data, noisinessBlurrinessBalance);
    AddUint(data, checkerboard);
    ValidateConstants(data);

    // MIP_GENERATION
    data = PushDispatch(methodData, AsUint(Dispatch::MIP_GENERATION));
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
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, trimmingParams_and_blurRadius);
    ValidateConstants(data);

    // POST_BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::POST_BLUR));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[2]);
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, trimmingParams_and_blurRadius);
    AddFloat(data, settings.maxAdaptiveRadiusScale);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4(data, float4(m_CameraDeltaSmoothed));
    AddFloat4(data, specHitDistParams);
    AddFloat4(data, antilag1 );
    AddFloat4(data, antilag2 );
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, maxAccumulatedFrameNum );
    ValidateConstants(data);
}
