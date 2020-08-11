/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_Specular(uint32_t w, uint32_t h)
{
    DispatchDesc desc = {};

    enum class Permanent
    {
        VIEWZ_AND_ACCUM_SPEED_1 = PERMANENT_POOL_START,
        VIEWZ_AND_ACCUM_SPEED_2,
        HISTORY,
        STABILIZED_HISTORY_1,
        STABILIZED_HISTORY_2,
    };

    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RG32_UINT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    enum class Transient
    {
        TEMP = TRANSIENT_POOL_START,
        ACCUMULATED,
        INTERNAL_DATA,
        SCALED_VIEWZ,
    };

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 5} );
    m_TransientPool.push_back( {Format::RGBA8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R16_SFLOAT, w, h, 5} );

    PushPass("Specular pre-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_SPEC_HIT) );

        PushOutput( AsUint(Transient::TEMP) );

        desc.constantBufferDataSize = SumConstants(2, 3, 4, 8);

        AddDispatch(desc, NRD_Specular_PreBlur, w, h);
    }

    PushPass("Specular temporal accumulation");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MOTION_VECTOR) );
        PushInput( AsUint(Permanent::HISTORY) );
        PushInput( AsUint(Transient::TEMP) );
        PushInput( AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_2), 0, 1, AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_1) );

        PushOutput( AsUint(Transient::ACCUMULATED) );
        PushOutput( AsUint(Transient::INTERNAL_DATA) );
        PushOutput( AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_1), 0, 1, AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_2) );
        PushOutput( AsUint(Transient::SCALED_VIEWZ) );

        desc.constantBufferDataSize = SumConstants(3, 5, 4, 8);

        AddDispatch(desc, NRD_Specular_TemporalAccumulation, w, h);
    }

    PushPass("Specular mip generation");
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

        desc.constantBufferDataSize = SumConstants(0, 0, 1, 1);

        AddDispatchWithExplicitCTASize(desc, NRD_Specular_Mips, DivideUp(w, 2), DivideUp(h, 2), 16, 16);
    }

    PushPass("Specular history fix");
    {
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::ACCUMULATED), 1, 4 );
        PushInput( AsUint(Transient::SCALED_VIEWZ), 0, 5 );

        PushOutput( AsUint(Transient::ACCUMULATED) );

        desc.constantBufferDataSize = SumConstants(0, 0, 2, 2);

        AddDispatch(desc, NRD_Specular_HistoryFix, w, h);
    }

    PushPass("Specular blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Transient::ACCUMULATED) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );

        PushOutput( AsUint(Permanent::HISTORY) );

        desc.constantBufferDataSize = SumConstants(2, 3, 3, 7);

        AddDispatch(desc, NRD_Specular_Blur, w, h);
    }

    PushPass("Specular temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_MOTION_VECTOR) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Permanent::STABILIZED_HISTORY_2), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_1) );
        PushInput( AsUint(Permanent::HISTORY) );

        PushOutput( AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_1), 0, 1, AsUint(Permanent::VIEWZ_AND_ACCUM_SPEED_2) );
        PushOutput( AsUint(Permanent::STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_2) );

        desc.constantBufferDataSize = SumConstants(2, 2, 5, 6);

        AddDispatch(desc, NRD_Specular_TemporalStabilization, w, h);
    }

    PushPass("Specular post-blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::INTERNAL_DATA) );
        PushInput( AsUint(Permanent::STABILIZED_HISTORY_1), 0, 1, AsUint(Permanent::STABILIZED_HISTORY_2) );
        PushInput( AsUint(Transient::SCALED_VIEWZ) );

        PushOutput( AsUint(ResourceType::OUT_SPEC_HIT) );

        desc.constantBufferDataSize = SumConstants(2, 3, 3, 8);

        AddDispatch(desc, NRD_Specular_PostBlur, w, h);
    }

    return sizeof(SpecularSettings);
}

void DenoiserImpl::UpdateMethod_Specular(const MethodData& methodData)
{
    enum class Dispatch
    {
        PRE_BLUR,
        TEMPORAL_ACCUMULATION,
        MIP_GENERATION,
        HISTORY_FIX,
        BLUR,
        TEMPORAL_STABILIZATION,
        POST_BLUR,
    };

    const SpecularSettings& settings = methodData.settings.specular;

    float4 scalingParams = float4(settings.hitDistanceParameters.A, settings.hitDistanceParameters.B, settings.hitDistanceParameters.C, settings.hitDistanceParameters.D) * m_CommonSettings.metersToUnitsMultiplier;
    float4 trimmingParams_and_isOrtho = float4(settings.lobeTrimmingParameters.A, settings.lobeTrimmingParameters.B, settings.lobeTrimmingParameters.C, m_IsOrtho);
    float w = float(methodData.desc.fullResolutionWidth);
    float h = float(methodData.desc.fullResolutionHeight);
    float maxAccumulatedFrameNum = float( Min(settings.maxAccumulatedFrameNum, MAX_HISTORY_FRAME_NUM) );
    float denoisingRadius = settings.denoisingRadius;
    float unproject = 1.0f / ( 0.5f * w * m_Project );
    float disocclusionThreshold = settings.disocclusionThreshold;
    uint32_t remappedCheckerboard = ( uint32_t(settings.checkerboardMode) + 2 ) % 3;
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
    AddFloat4(data, scalingParams);
    AddFloat4(data, trimmingParams_and_isOrtho);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, m_BlueNoiseSinCos.x, m_BlueNoiseSinCos.y);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat2(data, w, h);
    AddFloat(data, m_CommonSettings.metersToUnitsMultiplier);
    AddFloat(data, denoisingRadius);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, unproject);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, remappedCheckerboard);
    AddUint(data, settings.anisotropicFiltering ? 1 : 0);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // Temporal accumulation
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_ACCUMULATION));
    AddFloat4x4(data, m_WorldToViewPrev);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, m_Frustum);
    AddFloat4(data, m_FrustumPrev);
    AddFloat4(data, float4( m_CameraDelta.x, m_CameraDelta.y, m_CameraDelta.z, m_CommonSettings.denoisingRange ) );
    AddFloat4(data, scalingParams);
    AddFloat4(data, trimmingParams_and_isOrtho);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat2(data, w, h);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, m_CommonSettings.xMotionVectorScale, m_CommonSettings.yzMotionVectorScale);
    AddFloat(data, m_IsOrthoPrev);
    AddFloat(data, disocclusionThreshold);
    AddFloat(data, float(maxAccumulatedFrameNum));
    AddFloat(data, m_CommonSettings.forceReferenceAccumulation ? 1.0f : 0.0f);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, remappedCheckerboard);
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
    AddFloat4(data, scalingParams);
    AddFloat4(data, trimmingParams_and_isOrtho);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, m_BlueNoiseSinCos.x, m_BlueNoiseSinCos.y);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, m_CommonSettings.metersToUnitsMultiplier);
    AddFloat(data, denoisingRadius);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, unproject);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, settings.anisotropicFiltering ? 1 : 0);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // Temporal stabilization
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat4(data, m_Frustum);
    AddFloat4(data, scalingParams);
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

    // Post-blur
    data = PushDispatch(methodData, AsUint(Dispatch::POST_BLUR));
    AddFloat4x4(data, m_WorldToView);
    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);
    AddFloat4(data, scalingParams);
    AddFloat4(data, trimmingParams_and_isOrtho);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, m_BlueNoiseSinCos.x, m_BlueNoiseSinCos.y);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, m_CommonSettings.metersToUnitsMultiplier);
    AddFloat(data, denoisingRadius);
    AddFloat(data, settings.minAdaptiveRadiusScale);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, unproject);
    AddUint(data, m_CommonSettings.frameIndex);
    AddUint(data, settings.anisotropicFiltering ? 1 : 0);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);
}
