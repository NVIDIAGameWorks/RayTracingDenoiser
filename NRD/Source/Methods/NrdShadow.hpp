/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t DenoiserImpl::AddMethod_NrdShadow(uint16_t w, uint16_t h)
{
    DispatchDesc desc = {};

    enum class Transient
    {
        HIT_VIEWZ = TRANSIENT_POOL_START,
        TEMP,
        SHADOW,
        HISTORY
    };

    m_TransientPool.push_back( {Format::RG16_SFLOAT, w, h, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );
    m_TransientPool.push_back( {Format::R8_UNORM, w, h, 1} );

    PushPass("Shadow - pre blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(ResourceType::IN_SHADOW) );
        PushInput( AsUint(ResourceType::OUT_SHADOW) );

        PushOutput( AsUint(Transient::HIT_VIEWZ) );
        PushOutput( AsUint(Transient::TEMP) );
        PushOutput( AsUint(Transient::HISTORY) );

        desc.constantBufferDataSize = SumConstants(1, 1, 0, 1);

        AddDispatch(desc, NRD_Shadow_PreBlur, w, h);
    }

    PushPass("Shadow - blur");
    {
        PushInput( AsUint(ResourceType::IN_NORMAL_ROUGHNESS) );
        PushInput( AsUint(Transient::HIT_VIEWZ) );
        PushInput( AsUint(Transient::TEMP) );

        PushOutput( AsUint(Transient::SHADOW) );

        desc.constantBufferDataSize = SumConstants(1, 1, 0, 1);

        AddDispatch(desc, NRD_Shadow_Blur, w, h);
    }

    PushPass("Shadow - temporal stabilization");
    {
        PushInput( AsUint(ResourceType::IN_VIEWZ) );
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(Transient::SHADOW) );
        PushInput( AsUint(Transient::HISTORY) );

        PushOutput( AsUint(ResourceType::OUT_SHADOW) );

        desc.constantBufferDataSize = SumConstants(2, 0, 1, 0);

        AddDispatch(desc, NRD_Shadow_TemporalStabilization, w, h);
    }

    return sizeof(NrdShadowSettings);
}

void DenoiserImpl::UpdateMethod_NrdShadow(const MethodData& methodData)
{
    enum class Dispatch
    {
        PRE_BLUR,
        BLUR,
        TEMPORAL_STABILIZATION,
    };

    const NrdShadowSettings& settings = methodData.settings.shadow;

    float blurRadius = Tan( DegToRad( settings.lightSourceAngularDiameter ) )  * settings.blurRadiusScale;

    // PRE_BLUR
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::PRE_BLUR));
    AddSharedConstants(methodData, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[0]);
    AddFloat(data, blurRadius);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(methodData, AsUint(Dispatch::BLUR));
    AddSharedConstants(methodData, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator[0]);
    AddFloat(data, blurRadius);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(methodData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddSharedConstants(methodData, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    ValidateConstants(data);
}
