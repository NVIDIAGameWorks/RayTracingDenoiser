/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_NrdDiffuse(uint16_t w, uint16_t h)
{
    DispatchDesc desc = {};

    enum class Permanent
    {
        PREV_VIEWZ_AND_ACCUM_SPEED = PERMANENT_POOL_START,
        A_HISTORY,
        B_HISTORY,
        RESOLVED_HISTORY_1,
        RESOLVED_HISTORY_2,
    };

    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    enum class Transient
    {
        A_TEMP = TRANSIENT_POOL_START,
        A_ACCUMULATED,
        B_ACCUMULATED,
        INTERNAL_DATA,
    };

    // It's a nice trick to save memory. While one of RESOLVED_HISTORY is the history this frame, the opposite is free to be reused
    #define B_TEMP AsUint(Permanent::RESOLVED_HISTORY_1), 0, 1, AsUint(Permanent::RESOLVED_HISTORY_2)
    #define RESOLVED A_ACCUMULATED

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 5} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 5} );
    m_TransientPool.push_back( {Format::R32_UINT, w, h, 1} );

    PushPass("Diffuse - pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_DIFFA) );
        PushInput( AsUint(ResourceType::IN_DIFFB) );

        PushOutput( AsUint(Transient::A_TEMP) );
        PushOutput( B_TEMP );

        desc.constantBufferDataSize = SumConstants(1, 2, 1, 1);

        AddDispatch(desc, NRD_Diffuse_PreBlur, w, h);
    }

    PushPass("Diffuse - temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Permanent::A_HISTORY) );
        PushInput( AsUint(Permanent::B_HISTORY) );
        PushInput( AsUint(Transient::A_TEMP) );
        PushInput( B_TEMP );
        PushInput( AsUint(Permanent::PREV_VIEWZ_AND_ACCUM_SPEED) );

        PushOutput( AsUint(Transient::A_ACCUMULATED) );
        PushOutput( AsUint(Transient::B_ACCUMULATED) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );

        desc.constantBufferDataSize = SumConstants(3, 0, 2, 4);

        AddDispatch(desc, NRD_Diffuse_TemporalAccumulation, w, h);
    }

    PushPass("Diffuse - mip generation");
    {
        PushInput( AsUint(Transient::A_ACCUMULATED) );
        PushInput( AsUint(Transient::B_ACCUMULATED) );

        PushOutput( AsUint(Transient::A_ACCUMULATED), 1, 1 );
        PushOutput( AsUint(Transient::B_ACCUMULATED), 1, 1 );
        PushOutput( AsUint(Transient::A_ACCUMULATED), 2, 1 );
        PushOutput( AsUint(Transient::B_ACCUMULATED), 2, 1 );
        PushOutput( AsUint(Transient::A_ACCUMULATED), 3, 1 );
        PushOutput( AsUint(Transient::B_ACCUMULATED), 3, 1 );
        PushOutput( AsUint(Transient::A_ACCUMULATED), 4, 1 );
        PushOutput( AsUint(Transient::B_ACCUMULATED), 4, 1 );

        desc.constantBufferDataSize = SumConstants(0, 0, 0, 0);

        AddDispatchWithExplicitCTASize(desc, NRD_Diffuse_Mips, DivideUp(w, 2), DivideUp(h, 2), 16, 16);
    }

    PushPass("Diffuse - history fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::A_ACCUMULATED), 1, 4 );
        PushInput( AsUint(Transient::B_ACCUMULATED), 1, 4 );

        PushOutput( AsUint(Transient::A_ACCUMULATED) );
        PushOutput( AsUint(Transient::B_ACCUMULATED) );

        desc.constantBufferDataSize = SumConstants(0, 0, 1, 0);

        AddDispatch(desc, NRD_Diffuse_HistoryFix, w, h);
    }

    PushPass("Diffuse - blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::A_ACCUMULATED) );
        PushInput( AsUint(Transient::B_ACCUMULATED) );

        PushOutput( AsUint(Transient::A_TEMP) );
        PushOutput( B_TEMP );

        desc.constantBufferDataSize = SumConstants(1, 2, 0, 1);

        AddDispatch(desc, NRD_Diffuse_Blur, w, h);
    }

    PushPass("Diffuse - post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::A_TEMP) );
        PushInput( B_TEMP );

        PushOutput( AsUint(Permanent::A_HISTORY) );
        PushOutput( AsUint(Permanent::B_HISTORY) );
        PushOutput( AsUint(Transient::RESOLVED) );

        desc.constantBufferDataSize = SumConstants(1, 2, 0, 2);

        AddDispatch(desc, NRD_Diffuse_PostBlur, w, h);
    }

    PushPass("Diffuse - temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Permanent::RESOLVED_HISTORY_2), 0, 1, AsUint(Permanent::RESOLVED_HISTORY_1) );
        PushInput( AsUint(Transient::RESOLVED) );

        PushOutput( AsUint(Permanent::PREV_VIEWZ_AND_ACCUM_SPEED) );
        PushOutput( AsUint(Permanent::RESOLVED_HISTORY_1), 0, 1, AsUint(Permanent::RESOLVED_HISTORY_2) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_HIT) );

        desc.constantBufferDataSize = SumConstants(2, 0, 3, 1);

        AddDispatch(desc, NRD_Diffuse_TemporalStabilization, w, h);
    }

    #undef B_TEMP
    #undef RESOLVED

    return sizeof(NrdDiffuseSettings);
}

void DenoiserImpl::UpdateMethod_NrdDiffuse(const MethodData& methodData)
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

    const NrdDiffuseSettings& settings = methodData.settings.diffuse;

    float4 distanceScale = float4(settings.hitDistanceParameters.A, settings.hitDistanceParameters.B, settings.hitDistanceParameters.C, settings.hitDistanceParameters.D) * m_CommonSettings.metersToUnitsMultiplier;
    float w = float(methodData.desc.fullResolutionWidth);
    float h = float(methodData.desc.fullResolutionHeight);
    float maxAccumulatedFrameNum = float( Min(settings.maxAccumulatedFrameNum, NRD_DIFFUSE_MAX_HISTORY_FRAME_NUM) );
    float blurRadius = settings.blurRadius;
    float disocclusionThreshold = settings.disocclusionThreshold;
    bool useAntilag = !m_CommonSettings.forceReferenceAccumulation && settings.antilagSettings.enable;

    if (m_CommonSettings.forceReferenceAccumulation)
    {
        maxAccumulatedFrameNum = settings.maxAccumulatedFrameNum == 0 ? 0.0f : NRD_DIFFUSE_MAX_HISTORY_FRAME_NUM;
        blurRadius = 0.0f;
        disocclusionThreshold = 0.005f;
    }

    // PRE_BLUR
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PRE_BLUR));
    AddSharedConstants(methodData, settings.checkerboardMode, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, distanceScale);
    AddFloat4(data, m_Rotator[0]);
    AddFloat2(data, w, h);
    AddFloat(data, blurRadius);
    ValidateConstants(data);

    // TEMPORAL_ACCUMULATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_ACCUMULATION));
    AddSharedConstants(methodData, settings.checkerboardMode, data);
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat2(data, w, h);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat(data, m_CheckerboardResolveAccumSpeed);
    AddFloat(data, disocclusionThreshold );
    AddFloat(data, m_JitterDelta );
    AddFloat(data, maxAccumulatedFrameNum);
    ValidateConstants(data);

    // MIP_GENERATION
    data = PushDispatch(methodData, AsUint(Dispatch::MIP_GENERATION));
    AddSharedConstants(methodData, settings.checkerboardMode, data);
    ValidateConstants(data);

    // HISTORY_FIX
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_FIX));
    AddSharedConstants(methodData, settings.checkerboardMode, data);
    AddUint2(data, methodData.desc.fullResolutionWidth, methodData.desc.fullResolutionHeight);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddSharedConstants(methodData, settings.checkerboardMode, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, distanceScale);
    AddFloat4(data, m_Rotator[1]);
    AddFloat(data, blurRadius);
    ValidateConstants(data);

    // POST_BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::POST_BLUR));
    AddSharedConstants(methodData, settings.checkerboardMode, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, distanceScale);
    AddFloat4(data, m_Rotator[2]);
    AddFloat(data, blurRadius);
    AddFloat(data, settings.postBlurMaxAdaptiveRadiusScale);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddSharedConstants(methodData, settings.checkerboardMode, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat2(data, w, h);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddFloat2(data, settings.antilagSettings.intensityThresholdMin, settings.antilagSettings.intensityThresholdMax);
    AddUint(data, useAntilag ? 1 : 0);
    ValidateConstants(data);
}
