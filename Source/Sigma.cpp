/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "InstanceImpl.h"

#include "../Shaders/Include/SIGMA_Config.hlsli"
#include "../Shaders/Resources/SIGMA_ClassifyTiles.resources.hlsli"
#include "../Shaders/Resources/SIGMA_SmoothTiles.resources.hlsli"
#include "../Shaders/Resources/SIGMA_Blur.resources.hlsli"
#include "../Shaders/Resources/SIGMA_TemporalStabilization.resources.hlsli"
#include "../Shaders/Resources/SIGMA_SplitScreen.resources.hlsli"

// Permutations
#define SIGMA_POST_BLUR_PERMUTATION_NUM     2
#define SIGMA_NO_PERMUTATIONS               1

void nrd::InstanceImpl::Update_SigmaShadow(const DenoiserData& denoiserData)
{
    enum class Dispatch
    {
        CLASSIFY_TILES,
        SMOOTH_TILES            = CLASSIFY_TILES + SIGMA_NO_PERMUTATIONS,
        BLUR                    = SMOOTH_TILES + SIGMA_NO_PERMUTATIONS,
        POST_BLUR               = BLUR + SIGMA_NO_PERMUTATIONS,
        TEMPORAL_STABILIZATION  = POST_BLUR + SIGMA_POST_BLUR_PERMUTATION_NUM,
        SPLIT_SCREEN            = TEMPORAL_STABILIZATION + SIGMA_NO_PERMUTATIONS,
    };

    const SigmaSettings& settings = denoiserData.settings.sigma;

    // SPLIT_SCREEN (passthrough)
    if (m_CommonSettings.splitScreen >= 1.0f)
    {
        void* consts = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Sigma(settings, consts);

        return;
    }

    { // CLASSIFY_TILES
        void* consts = PushDispatch(denoiserData, AsUint(Dispatch::CLASSIFY_TILES));
        AddSharedConstants_Sigma(settings, consts);
    }

    { // SMOOTH_TILES
        void* consts = PushDispatch(denoiserData, AsUint(Dispatch::SMOOTH_TILES));
        AddSharedConstants_Sigma(settings, consts);
    }

    { // BLUR
        SIGMA_BlurConstants* consts = (SIGMA_BlurConstants*)PushDispatch(denoiserData, AsUint(Dispatch::BLUR));
        AddSharedConstants_Sigma(settings, consts);
        consts->gRotator = m_Rotator_Blur; // TODO: push constant
    }

    { // POST_BLUR
        uint32_t passIndex = AsUint(Dispatch::POST_BLUR) + (settings.stabilizationStrength != 0.0f ? 1 : 0);
        SIGMA_BlurConstants* consts = (SIGMA_BlurConstants*)PushDispatch(denoiserData, passIndex);
        AddSharedConstants_Sigma(settings, consts);
        consts->gRotator = m_Rotator_PostBlur; // TODO: push constant
    }

    // TEMPORAL_STABILIZATION
    if (settings.stabilizationStrength != 0.0f)
    {
        void* consts = PushDispatch(denoiserData, AsUint(Dispatch::TEMPORAL_STABILIZATION));
        AddSharedConstants_Sigma(settings, consts);
    }

    // SPLIT_SCREEN
    if (m_CommonSettings.splitScreen > 0.0f)
    {
        void* consts = PushDispatch(denoiserData, AsUint(Dispatch::SPLIT_SCREEN));
        AddSharedConstants_Sigma(settings, consts);
    }
}

void nrd::InstanceImpl::AddSharedConstants_Sigma(const SigmaSettings& settings, void* data)
{
    struct SharedConstants
    {
        SIGMA_SHARED_CONSTANTS
    };

    NRD_DECLARE_DIMS;

    float unproject = 1.0f / (0.5f * rectH * m_ProjectY);
    uint16_t tilesW = DivideUp(rectW, 16);
    uint16_t tilesH = DivideUp(rectH, 16);

    float3 lightDirectionView = Rotate(m_WorldToView, float3(settings.lightDirection[0], settings.lightDirection[1], settings.lightDirection[2]));

    SharedConstants* consts         = (SharedConstants*)data;
    consts->gWorldToView            = m_WorldToView;
    consts->gViewToClip             = m_ViewToClip;
    consts->gWorldToClipPrev        = m_WorldToClipPrev;
    consts->gLightDirectionView     = float4(lightDirectionView.x, lightDirectionView.y, lightDirectionView.z, 0.0f);
    consts->gFrustum                = m_Frustum;
    consts->gMvScale                = float4(m_CommonSettings.motionVectorScale[0], m_CommonSettings.motionVectorScale[1], m_CommonSettings.motionVectorScale[2], m_CommonSettings.isMotionVectorInWorldSpace ? 1.0f : 0.0f);
    consts->gResourceSizeInv        = float2(1.0f / float(resourceW), 1.0f / float(resourceH));
    consts->gResourceSizeInvPrev    = float2(1.0f / float(resourceWprev), 1.0f / float(resourceHprev));
    consts->gRectSize               = float2(float(rectW), float(rectH));
    consts->gRectSizeInv            = float2(1.0f / float(rectW), 1.0f / float(rectH));
    consts->gRectSizePrev           = float2(float(rectWprev), float(rectHprev));
    consts->gResolutionScale        = float2(float(rectW) / float(resourceW), float(rectH) / float(resourceH));
    consts->gRectOffset             = float2(float(m_CommonSettings.rectOrigin[0]) / float(resourceW), float(m_CommonSettings.rectOrigin[1]) / float(resourceH));
    consts->gPrintfAt               = uint2(m_CommonSettings.printfAt[0], m_CommonSettings.printfAt[1]);
    consts->gRectOrigin             = uint2(m_CommonSettings.rectOrigin[0], m_CommonSettings.rectOrigin[1]);
    consts->gRectSizeMinusOne       = int2(rectW - 1, rectH - 1);
    consts->gTilesSizeMinusOne      = int2(tilesW - 1, tilesH - 1);
    consts->gOrthoMode              = m_OrthoMode;
    consts->gUnproject              = unproject;
    consts->gDenoisingRange         = m_CommonSettings.denoisingRange;
    consts->gPlaneDistSensitivity   = settings.planeDistanceSensitivity;
    consts->gStabilizationStrength  = m_CommonSettings.accumulationMode == AccumulationMode::CONTINUE ? settings.stabilizationStrength : 0.0f;
    consts->gDebug                  = m_CommonSettings.debug;
    consts->gSplitScreen            = m_CommonSettings.splitScreen;
    consts->gViewZScale             = m_CommonSettings.viewZScale;
    consts->gFrameIndex             = m_CommonSettings.frameIndex;
}

// SIGMA_SHADOW
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "SIGMA_Shadow_ClassifyTiles.cs.dxbc.h"
    #include "SIGMA_Shadow_SmoothTiles.cs.dxbc.h"
    #include "SIGMA_Shadow_Blur.cs.dxbc.h"
    #include "SIGMA_Shadow_PostBlur.cs.dxbc.h"
    #include "SIGMA_Shadow_TemporalStabilization.cs.dxbc.h"
    #include "SIGMA_Shadow_SplitScreen.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "SIGMA_Shadow_ClassifyTiles.cs.dxil.h"
    #include "SIGMA_Shadow_SmoothTiles.cs.dxil.h"
    #include "SIGMA_Shadow_Blur.cs.dxil.h"
    #include "SIGMA_Shadow_PostBlur.cs.dxil.h"
    #include "SIGMA_Shadow_TemporalStabilization.cs.dxil.h"
    #include "SIGMA_Shadow_SplitScreen.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "SIGMA_Shadow_ClassifyTiles.cs.spirv.h"
    #include "SIGMA_Shadow_SmoothTiles.cs.spirv.h"
    #include "SIGMA_Shadow_Blur.cs.spirv.h"
    #include "SIGMA_Shadow_PostBlur.cs.spirv.h"
    #include "SIGMA_Shadow_TemporalStabilization.cs.spirv.h"
    #include "SIGMA_Shadow_SplitScreen.cs.spirv.h"
#endif

#include "Denoisers/Sigma_Shadow.hpp"


// SIGMA_SHADOW_TRANSLUCENCY
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "SIGMA_ShadowTranslucency_ClassifyTiles.cs.dxbc.h"
    #include "SIGMA_ShadowTranslucency_Blur.cs.dxbc.h"
    #include "SIGMA_ShadowTranslucency_PostBlur.cs.dxbc.h"
    #include "SIGMA_ShadowTranslucency_TemporalStabilization.cs.dxbc.h"
    #include "SIGMA_ShadowTranslucency_SplitScreen.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "SIGMA_ShadowTranslucency_ClassifyTiles.cs.dxil.h"
    #include "SIGMA_ShadowTranslucency_Blur.cs.dxil.h"
    #include "SIGMA_ShadowTranslucency_PostBlur.cs.dxil.h"
    #include "SIGMA_ShadowTranslucency_TemporalStabilization.cs.dxil.h"
    #include "SIGMA_ShadowTranslucency_SplitScreen.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "SIGMA_ShadowTranslucency_ClassifyTiles.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_Blur.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_PostBlur.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_TemporalStabilization.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_SplitScreen.cs.spirv.h"
#endif

#include "Denoisers/Sigma_ShadowTranslucency.hpp"
