/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

size_t nrd::DenoiserImpl::AddMethod_SpecularDeltaMv(uint16_t w, uint16_t h)
{
    #define METHOD_NAME SpecularDeltaMv

    enum class Permanent
    {
        DELTA_SECONDARY_POS_CURR = PERMANENT_POOL_START,
        DELTA_SECONDARY_POS_PREV
    };

    m_PermanentPool.push_back( {Format::RGBA32_SFLOAT, w, h, 1} );
    m_PermanentPool.push_back( {Format::RGBA32_SFLOAT, w, h, 1} );

    SetSharedConstants(0, 0, 0, 0);

    PushPass("Compute");
    {
        PushInput( AsUint(ResourceType::IN_MV) );
        PushInput( AsUint(ResourceType::IN_DELTA_PRIMARY_POS) );
        PushInput( AsUint(ResourceType::IN_DELTA_SECONDARY_POS) );
        PushInput( AsUint(Permanent::DELTA_SECONDARY_POS_PREV), 0, 1, AsUint(Permanent::DELTA_SECONDARY_POS_CURR) );

        PushOutput( AsUint(ResourceType::OUT_DELTA_MV) );
        PushOutput( AsUint(Permanent::DELTA_SECONDARY_POS_CURR), 0, 1, AsUint(Permanent::DELTA_SECONDARY_POS_PREV) );

        AddDispatch( SpecularDeltaMv_Compute, SumConstants(1, 0, 4, 2), 16, 1 );
    }

    #undef METHOD_NAME

    return sizeof(SpecularDeltaMvSettings);
}

void nrd::DenoiserImpl::UpdateMethod_SpecularDeltaMv(const MethodData& methodData)
{
    enum class Dispatch
    {
        COMPUTE,
    };

    NRD_DECLARE_DIMS;

    // COMPUTE
    Constant* data = PushDispatch(methodData, AsUint(Dispatch::COMPUTE));
    AddFloat4x4(data, m_WorldToClipPrev);
    AddUint2(data, rectW, rectH);
    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat2(data, m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1]);
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    AddFloat(data, m_CommonSettings.debug);
    ValidateConstants(data);
}
