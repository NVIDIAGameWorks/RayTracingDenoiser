/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_Diffuse(uint32_t w, uint32_t h)
{
    DispatchDesc desc = {};

    enum class Permanent
    {
        VIEWZ_AND_ACCUM_SPEED_1 = PERMANENT_POOL_START,
        VIEWZ_AND_ACCUM_SPEED_2,
        A_HISTORY,
        B_HISTORY,
        RESOLVED_HISTORY_1,
        RESOLVED_HISTORY_2,
    };

    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::R32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    enum class Transient
    {
        A_TEMP = TRANSIENT_POOL_START,
        A_ACCUMULATED,
        B_TEMP,
        B_ACCUMULATED,
        INTERNAL_DATA,

        RESOLVED = A_ACCUMULATED,
    };

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 5} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 5} );
    m_TransientPool.push_back( {Format::R32_UINT, w, h, 1} );

    PushPass("Diffuse pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_DIFF_A) );
        PushInput( AsUint(ResourceType::IN_DIFF_B) );

        PushOutput( AsUint(Transient::A_TEMP) );
        PushOutput( AsUint(Transient::B_TEMP) );

        desc.constantBufferDataSize = SumConstants(2, 2, 4, 8);

        AddDispatch(desc, NRD_Diffuse_PreBlur, w, h);
    }

    PushPass("Diffuse temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MOTION_VECTOR) );
        PushInput( AsUint(Permanent::A_HISTORY) );
        PushInput( AsUint(Permanent::B_HISTORY) );
        PushInput( AsUint(Transient::A_TEMP) );
        PushInput( AsUint(Transient::B_TEMP) );
        PushInput( AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_2), 0, 1, AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_1) );

        PushOutput( AsUint(Transient::A_ACCUMULATED) );
        PushOutput( AsUint(Transient::B_ACCUMULATED) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_1), 0, 1, AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_2) );

        desc.constantBufferDataSize = SumConstants(3, 1, 4, 8);

        AddDispatch(desc, NRD_Diffuse_TemporalAccumulation, w, h);
    }

    PushPass("Diffuse mip generation");
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

        desc.constantBufferDataSize = SumConstants(0, 0, 1, 1);

        AddDispatchWithExplicitCTASize(desc, NRD_Diffuse_Mips, DivideUp(w, 2), DivideUp(h, 2), 16, 16);
    }

    PushPass("Diffuse history fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::A_ACCUMULATED), 1, 4 );
        PushInput( AsUint(Transient::B_ACCUMULATED), 1, 4 );

        PushOutput( AsUint(Transient::A_ACCUMULATED) );
        PushOutput( AsUint(Transient::B_ACCUMULATED) );

        desc.constantBufferDataSize = SumConstants(0, 0, 2, 2);

        AddDispatch(desc, NRD_Diffuse_HistoryFix, w, h);
    }

    PushPass("Diffuse blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::A_ACCUMULATED) );
        PushInput( AsUint(Transient::B_ACCUMULATED) );

        PushOutput( AsUint(Transient::A_TEMP) );
        PushOutput( AsUint(Transient::B_TEMP) );

        desc.constantBufferDataSize = SumConstants(2, 2, 3, 7);

        AddDispatch(desc, NRD_Diffuse_Blur, w, h);
    }

    PushPass("Diffuse post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::A_TEMP) );
        PushInput( AsUint(Transient::B_TEMP) );

        PushOutput( AsUint(Permanent::A_HISTORY) );
        PushOutput( AsUint(Permanent::B_HISTORY) );
        PushOutput( AsUint(Transient::RESOLVED) );

        desc.constantBufferDataSize = SumConstants(2, 2, 3, 8);

        AddDispatch(desc, NRD_Diffuse_PostBlur, w, h);
    }

    PushPass("Diffuse temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_MOTION_VECTOR) );
        PushInput( AsUint(Permanent::RESOLVED_HISTORY_2), 0, 1, AsUint(Permanent::RESOLVED_HISTORY_1) );
        PushInput( AsUint(Transient::RESOLVED) );

        PushOutput( AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_1), 0, 1, AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_2) );
        PushOutput( AsUint(Permanent::RESOLVED_HISTORY_1), 0, 1, AsUint(Permanent::RESOLVED_HISTORY_2) );
        PushOutput( AsUint(ResourceType::OUT_DIFF_HIT) );

        desc.constantBufferDataSize = SumConstants(2, 1, 5, 6);

        AddDispatch(desc, NRD_Diffuse_TemporalStabilization, w, h);
    }

    return sizeof(DiffuseSettings);
}

void DenoiserImpl::UpdateMethod_Diffuse(const MethodData& methodData)
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

    const DiffuseSettings& settings = methodData.settings.diffuse;

    float4 distanceScale = float4(settings.hitDistanceParameters.A, settings.hitDistanceParameters.B, settings.hitDistanceParameters.C, settings.hitDistanceParameters.D) * m_CommonSettings.metersToUnitsMultiplier;
    float w = float(methodData.desc.fullResolutionWidth);
    float h = float(methodData.desc.fullResolutionHeight);
    float maxAccumulatedFrameNum = float( Min(settings.maxAccumulatedFrameNum, MAX_HISTORY_FRAME_NUM) );
    float denoisingRadius = settings.denoisingRadius;
    float unproject = 1.0f / ( 0.5f * w * m_Project );
    float frameTime = Max(m_Timer.GetSmoothedElapsedTime(), 1.0f) * 0.001f;
    float invFrameTime = 1.0f / frameTime;
    float disocclusionThreshold = settings.disocclusionThreshold;
    bool useAntilag = !m_CommonSettings.forceReferenceAccumulation && settings.antilagSettings.enable;

    if (m_CommonSettings.forceReferenceAccumulation)
    {
        maxAccumulatedFrameNum = settings.maxAccumulatedFrameNum == 0 ? 0.0f : MAX_HISTORY_FRAME_NUM;
        denoisingRadius = 0.0f;
        disocclusionThreshold = 0.005f;
    }

    // Pre-blur
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PRE_BLUR));
    AddFloat4x4(data, m_WorldToView);
    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);
    AddFloat4(data, distanceScale);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, m_BlueNoiseSinCos.x, m_BlueNoiseSinCos.y);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat2(data, w, h);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.metersToUnitsMultiplier);
    AddFloat(data, denoisingRadius);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, settings.checkerboard ? 1 : 0);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // Temporal accumulation
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_ACCUMULATION));
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, m_Frustum);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat2(data, w, h);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, m_CommonSettings.xMotionVectorScale, m_CommonSettings.yzMotionVectorScale);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, m_CommonSettings.denoisingRange );
    AddFloat(data, disocclusionThreshold );
    AddFloat(data, maxAccumulatedFrameNum);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, settings.checkerboard ? 1 : 0);
    AddUint(data, m_CommonSettings.worldSpaceMotion ? 1 : 0);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // Mip generation
    data = PushDispatch(methodData, AsUint(Dispatch::MIP_GENERATION));
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // History fix
    data = PushDispatch(methodData, AsUint(Dispatch::HISTORY_FIX));
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddUint2(data, methodData.desc.fullResolutionWidth, methodData.desc.fullResolutionHeight);
    AddUint(data, m_CommonSettings.frameIndex);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // Blur
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddFloat4x4(data, m_WorldToView);
    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);
    AddFloat4(data, distanceScale);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat2(data, m_BlueNoiseSinCos.x, m_BlueNoiseSinCos.y);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, m_CommonSettings.metersToUnitsMultiplier);
    AddFloat(data, denoisingRadius);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, unproject);
    AddUint(data, m_CommonSettings.frameIndex);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // Post-blur
    data = PushDispatch(methodData, AsUint(Dispatch::POST_BLUR));
    AddFloat4x4(data, m_WorldToView);
    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);
    AddFloat4(data, distanceScale);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat2(data, m_BlueNoiseSinCos.x, m_BlueNoiseSinCos.y);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, m_CommonSettings.metersToUnitsMultiplier);
    AddFloat(data, denoisingRadius);
    AddFloat(data, settings.maxAdaptiveRadiusScale);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, unproject);
    AddUint(data, m_CommonSettings.frameIndex);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // Temporal stabilization
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, m_Frustum);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat2(data, w, h);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, m_CommonSettings.xMotionVectorScale, m_CommonSettings.yzMotionVectorScale);
    AddFloat2(data, settings.antilagSettings.intensityThresholdMin, settings.antilagSettings.intensityThresholdMax);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, m_CommonSettings.worldSpaceMotion ? 1 : 0);
    AddUint(data, useAntilag ? 1 : 0);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);
}
