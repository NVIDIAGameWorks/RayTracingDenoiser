/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "InstanceImpl.h"

#define SIGMA_SET_SHARED_CONSTANTS                      SetSharedConstants(1, 2, 8, 8)

#define SIGMA_CLASSIFY_TILES_SET_CONSTANTS              SumConstants(0, 0, 0, 0)
#define SIGMA_CLASSIFY_TILES_NUM_THREADS                NumThreads(1, 1)

#define SIGMA_SMOOTH_TILES_SET_CONSTANTS                SumConstants(0, 0, 1, 0)
#define SIGMA_SMOOTH_TILES_NUM_THREADS                  NumThreads(16, 16)

#define SIGMA_BLUR_SET_CONSTANTS                        SumConstants(1, 1, 0, 0)
#define SIGMA_BLUR_NUM_THREADS                          NumThreads(16, 16)

#define SIGMA_TEMPORAL_STABILIZATION_SET_CONSTANTS      SumConstants(2, 0, 0, 0)
#define SIGMA_TEMPORAL_STABILIZATION_NUM_THREADS        NumThreads(16, 16)

#define SIGMA_SPLIT_SCREEN_SET_CONSTANTS                SumConstants(0, 0, 0, 1)
#define SIGMA_SPLIT_SCREEN_NUM_THREADS                  NumThreads(16, 16)

void nrd::InstanceImpl::Update_SigmaShadow(const DenoiserData& denoiserData)
{
    enum class Dispatch
    {
        CLASSIFY_TILES,
        SMOOTH_TILES,
        BLUR,
        POST_BLUR,
        TEMPORAL_STABILIZATION,
        SPLIT_SCREEN,
    };

    const SigmaSettings& settings = denoiserData.settings.sigma;

    NRD_DECLARE_DIMS;

    uint16_t tilesW = DivideUp(rectW, 16);
    uint16_t tilesH = DivideUp(rectH, 16);

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        Constant* data = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Sigma(denoiserData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);

        return;
    }

    // CLASSIFY_TILES
    Constant* data = PushDispatch(denoiserData, AsUint(Dispatch::CLASSIFY_TILES));
    AddSharedConstants_Sigma(denoiserData, settings, data);
    ValidateConstants(data);

    // SMOOTH_TILES
    data = PushDispatch(denoiserData, AsUint(Dispatch::SMOOTH_TILES));
    AddSharedConstants_Sigma(denoiserData, settings, data);
    AddUint2(data, tilesW, tilesH);
    ValidateConstants(data);

    // BLUR
    data = PushDispatch(denoiserData, AsUint(Dispatch::BLUR));
    AddSharedConstants_Sigma(denoiserData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator_Blur);
    ValidateConstants(data);

    // POST_BLUR
    data = PushDispatch(denoiserData, AsUint(Dispatch::POST_BLUR));
    AddSharedConstants_Sigma(denoiserData, settings, data);
    AddFloat4x4(data, m_WorldToView);
    AddFloat4(data, m_Rotator_PostBlur);
    ValidateConstants(data);

    // TEMPORAL_STABILIZATION
    data = PushDispatch(denoiserData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
    AddSharedConstants_Sigma(denoiserData, settings, data);
    AddFloat4x4(data, m_WorldToClipPrev);
    AddFloat4x4(data, m_ViewToWorld);
    ValidateConstants(data);

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        data = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Sigma(denoiserData, settings, data);
        AddFloat(data, m_CommonSettings.splitScreen);
        ValidateConstants(data);
    }
}

void nrd::InstanceImpl::AddSharedConstants_Sigma(const DenoiserData& denoiserData, const SigmaSettings& settings, Constant*& data)
{
    NRD_DECLARE_DIMS;

    // Even with DRS keep radius, it works well for shadows
    float unproject = 1.0f / (0.5f * screenH * m_ProjectY);

    AddFloat4x4(data, m_ViewToClip);

    AddFloat4(data, m_Frustum);
    AddFloat4(data, ml::float4(m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1], m_CommonSettings.motionVectorScale[2], m_CommonSettings.debug));

    AddFloat2(data, 1.0f / float(screenW), 1.0f / float(screenH));
    AddFloat2(data, float(screenW), float(screenH));

    AddFloat2(data, 1.0f / float(rectW), 1.0f / float(rectH));
    AddFloat2(data, float(rectW), float(rectH));

    AddFloat2(data, float(rectWprev), float(rectHprev));
    AddFloat2(data, float(rectW) / float(screenW), float(rectH) / float(screenH));

    AddFloat2(data, float(m_CommonSettings.inputSubrectOrigin[0]) / float(screenW), float(m_CommonSettings.inputSubrectOrigin[1]) / float(screenH));
    AddUint2(data, m_CommonSettings.inputSubrectOrigin[0], m_CommonSettings.inputSubrectOrigin[1]);

    AddFloat(data, m_IsOrtho);
    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.denoisingRange);
    AddFloat(data, settings.planeDistanceSensitivity);

    AddFloat(data, settings.blurRadiusScale);
    AddFloat(data, m_CommonSettings.accumulationMode != AccumulationMode::CONTINUE ? 0.0f : 1.0f);
    AddUint(data, m_CommonSettings.isMotionVectorInWorldSpace ? 1 : 0);
    AddUint(data, m_CommonSettings.frameIndex);
}

#ifdef NRD_USE_PRECOMPILED_SHADERS

    // SIGMA_SHADOW
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "SIGMA_Shadow_ClassifyTiles.cs.dxbc.h"
        #include "SIGMA_Shadow_ClassifyTiles.cs.dxil.h"
        #include "SIGMA_Shadow_SmoothTiles.cs.dxbc.h"
        #include "SIGMA_Shadow_SmoothTiles.cs.dxil.h"
        #include "SIGMA_Shadow_Blur.cs.dxbc.h"
        #include "SIGMA_Shadow_Blur.cs.dxil.h"
        #include "SIGMA_Shadow_PostBlur.cs.dxbc.h"
        #include "SIGMA_Shadow_PostBlur.cs.dxil.h"
        #include "SIGMA_Shadow_TemporalStabilization.cs.dxbc.h"
        #include "SIGMA_Shadow_TemporalStabilization.cs.dxil.h"
        #include "SIGMA_Shadow_SplitScreen.cs.dxbc.h"
        #include "SIGMA_Shadow_SplitScreen.cs.dxil.h"
    #endif

    #include "SIGMA_Shadow_ClassifyTiles.cs.spirv.h"
    #include "SIGMA_Shadow_SmoothTiles.cs.spirv.h"
    #include "SIGMA_Shadow_Blur.cs.spirv.h"
    #include "SIGMA_Shadow_PostBlur.cs.spirv.h"
    #include "SIGMA_Shadow_TemporalStabilization.cs.spirv.h"
    #include "SIGMA_Shadow_SplitScreen.cs.spirv.h"

    // SIGMA_SHADOW_TRANSLUCENCY
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "SIGMA_ShadowTranslucency_ClassifyTiles.cs.dxbc.h"
        #include "SIGMA_ShadowTranslucency_ClassifyTiles.cs.dxil.h"
        #include "SIGMA_ShadowTranslucency_Blur.cs.dxbc.h"
        #include "SIGMA_ShadowTranslucency_Blur.cs.dxil.h"
        #include "SIGMA_ShadowTranslucency_PostBlur.cs.dxbc.h"
        #include "SIGMA_ShadowTranslucency_PostBlur.cs.dxil.h"
        #include "SIGMA_ShadowTranslucency_TemporalStabilization.cs.dxbc.h"
        #include "SIGMA_ShadowTranslucency_TemporalStabilization.cs.dxil.h"
        #include "SIGMA_ShadowTranslucency_SplitScreen.cs.dxbc.h"
        #include "SIGMA_ShadowTranslucency_SplitScreen.cs.dxil.h"
    #endif

    #include "SIGMA_ShadowTranslucency_ClassifyTiles.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_Blur.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_PostBlur.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_TemporalStabilization.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_SplitScreen.cs.spirv.h"

#endif

#include "Denoisers/Sigma_Shadow.hpp"
#include "Denoisers/Sigma_ShadowTranslucency.hpp"
