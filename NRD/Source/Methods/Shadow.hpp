/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_Shadow(uint32_t w, uint32_t h)
{
    DispatchDesc desc = {};

    enum class Transient
    {
        TEMP = TRANSIENT_POOL_START,
    };

    m_TransientPool.push_back( {Format::RGBA16_SFLOAT, w, h, 1} );

    PushPass("Shadow pre blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_SHADOW) );

        PushOutput( AsUint(Transient::TEMP) );

        desc.constantBufferDataSize = SumConstants(2, 1, 2, 7);

        AddDispatch(desc, NRD_Shadow_PreBlur, w, h);
    }

    PushPass("Shadow blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::TEMP) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW) );

        desc.constantBufferDataSize = SumConstants(2, 1, 2, 7);

        AddDispatch(desc, NRD_Shadow_Blur, w, h);
    }

    return sizeof(ShadowSettings);
}

void DenoiserImpl::UpdateMethod_Shadow(const MethodData& methodData)
{
    enum class Dispatch
    {
        PRE_BLUR,
        BLUR,
    };

    const ShadowSettings& settings = methodData.settings.shadow;

    float w = float(methodData.desc.fullResolutionWidth);
    float h = float(methodData.desc.fullResolutionHeight);
    float denoisingRadius = Tan( DegToRad( settings.lightSourceAngularDiameter ) );
    float unproject = 1.0f / ( 0.5f * w * m_Project );

    denoisingRadius *= 1.5f; // TODO: it's a temp solution, needs to be removed when TA is added!

    // Pre-blur
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PRE_BLUR));
    AddFloat4x4(data, m_WorldToView);
    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, denoisingRadius);
    AddFloat(data, m_CommonSettings.metersToUnitsMultiplier);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, unproject);
    AddUint(data, m_CommonSettings.frameIndex);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);

    // Blur
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddFloat4x4(data, m_WorldToView);
    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);
    AddFloat2(data, m_CommonSettings.xJitter / w, m_CommonSettings.yJitter / h);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, denoisingRadius);
    AddFloat(data, m_CommonSettings.metersToUnitsMultiplier);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, unproject);
    AddUint(data, m_CommonSettings.frameIndex);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);
}
