/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_ReblurDiffuse(uint16_t w, uint16_t h)
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

    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 5} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 5} );

    // Tricks to save memory
    #define TEMP AsUint(Permanent::STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_2)

    PushPass("REBLUR::Diffuse - pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFF_HIT) );

        PushOutput( AsUint(Transient::SCALED_VIEWZ) );
        PushOutput( TEMP );

        desc.constantBufferDataSize = SumConstants(1, 2, 0, 2);

        AddDispatch(desc, REBLUR_Diffuse_PreBlur, w, h);
    }

    PushPass("REBLUR::Diffuse - temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushInput( AsUint(Permanent::HISTORY) ); // TODO: STABILIZED_HISTORY can be used here - it looks better, but adds lag because TS uses wide variance clamping
        PushInput( TEMP );

        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Transient::ACCUMULATED) );

        desc.constantBufferDataSize = SumConstants(4, 1, 1, 5);

        AddDispatch(desc, REBLUR_Diffuse_TemporalAccumulation, w, h);
    }

    PushPass("REBLUR::Diffuse - mip generation");
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

    PushPass("REBLUR::Diffuse - history fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, 5 );
        PushInput( AsUint(Transient::ACCUMULATED), 1, 4 );

        PushOutput( AsUint(Transient::ACCUMULATED) );

        desc.constantBufferDataSize = SumConstants(0, 0, 1, 0);

        AddDispatch(desc, REBLUR_Diffuse_HistoryFix, w, h);
    }

    PushPass("REBLUR::Diffuse - blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( AsUint(Transient::ACCUMULATED) );

        PushOutput( TEMP );

        desc.constantBufferDataSize = SumConstants(1, 2, 0, 1);

        AddDispatch(desc, REBLUR_Diffuse_Blur, w, h);
    }

    PushPass("REBLUR::Diffuse - post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );
        PushInput( TEMP );
        PushInput( AsUint(Transient::ACCUMULATED) );

        PushOutput( AsUint(Permanent::HISTORY) );

        desc.constantBufferDataSize = SumConstants(1, 2, 0, 2);

        AddDispatch(desc, REBLUR_Diffuse_PostBlur, w, h);
    }

    PushPass("REBLUR::Diffuse - temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Permanent::STABILIZED_HISTORY_2), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_1) );
        PushInput( AsUint(Permanent::HISTORY) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_NORMAL_ROUGHNESS_ACCUMSPEEDS) );
        PushOutput( AsUint(Permanent::STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_2) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_HIT) );

        desc.constantBufferDataSize = SumConstants(2, 2, 1, 0);

        AddDispatch(desc, REBLUR_Diffuse_TemporalStabilization, w, h);
    }

    #undef TEMP

    return sizeof(ReblurDiffuseSettings);
}

void DenoiserImpl::UpdateMethod_ReblurDiffuse(const MethodData& methodData)
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

    const ReblurDiffuseSettings& settings = methodData.settings.diffuse;

    float4 diffHitDistParams = float4(&settings.hitDistanceParameters.A);
    float maxAccumulatedFrameNum = float( Min(settings.maxAccumulatedFrameNum, REBLUR_MAX_HISTORY_FRAME_NUM) );
    float noisinessBlurrinessBalance = settings.noisinessBlurrinessBalance;
    float blurRadius = settings.blurRadius;
    float disocclusionThreshold = settings.disocclusionThreshold;
    uint32_t checkerboard = ((uint32_t)settings.checkerboardMode + 2) % 3;

    if (m_CommonSettings.forceReferenceAccumulation)
    {
        maxAccumulatedFrameNum = settings.maxAccumulatedFrameNum == 0 ? 0.0f : REBLUR_MAX_HISTORY_FRAME_NUM;
        noisinessBlurrinessBalance = 1.0f;
        blurRadius = 0.0f;
        disocclusionThreshold = 0.005f;
    }

    // PRE_BLUR
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PRE_BLUR));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[0]);
    AddFloat4(data, diffHitDistParams);
    AddFloat(data, blurRadius);
    AddUint(data, checkerboard);
    ValidateConstants(data);

    // TEMPORAL_ACCUMULATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_ACCUMULATION));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4x4(data, m_WorldToClip);
    AddFloat4(data, float4( m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, m_JitterDelta ) );
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, disocclusionThreshold );
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
    AddFloat4(data, diffHitDistParams);
    AddFloat(data, blurRadius);
    ValidateConstants(data);

    // POST_BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::POST_BLUR));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[2]);
    AddFloat4(data, diffHitDistParams);
    AddFloat(data, blurRadius);
    AddFloat(data, settings.maxAdaptiveRadiusScale);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddNrdSharedConstants(methodData, settings.planeDistanceSensitivity, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, float4( settings.intensityAntilagSettings.sigmaScale, settings.intensityAntilagSettings.enable ? settings.intensityAntilagSettings.thresholdMin : 99998.0f, settings.intensityAntilagSettings.enable ? settings.intensityAntilagSettings.thresholdMax : 99999.0f, 0.0f ) );  // TODO: .w - unused
    AddFloat4(data, float4( settings.hitDistanceAntilagSettings.sigmaScale, settings.hitDistanceAntilagSettings.enable ? settings.hitDistanceAntilagSettings.thresholdMin : 99998.0f, settings.hitDistanceAntilagSettings.enable ? settings.hitDistanceAntilagSettings.thresholdMax : 99999.0f, 0.0f ) );  // TODO: .w - unused
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    ValidateConstants(data);
}
