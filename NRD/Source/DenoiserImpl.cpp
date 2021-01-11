/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "DenoiserImpl.h"
#include <array>

using namespace nrd;

static const std::array<StaticSamplerDesc, (size_t)Sampler::MAX_NUM> g_StaticSamplers =
{{
    {Sampler::NEAREST_CLAMP, 0},
    {Sampler::NEAREST_MIRRORED_REPEAT, 1},
    {Sampler::LINEAR_CLAMP, 2},
    {Sampler::LINEAR_MIRRORED_REPEAT, 3},
}};

Result DenoiserImpl::Create(const DenoiserCreationDesc& denoiserCreationDesc)
{
    const LibraryDesc& libraryDesc = GetLibraryDesc();

    m_EnableValidation = denoiserCreationDesc.enableValidation;

    for (uint32_t i = 0; i < denoiserCreationDesc.requestedMethodNum; i++)
    {
        const MethodDesc& methodDesc = denoiserCreationDesc.requestedMethods[i];

        const uint16_t w = methodDesc.fullResolutionWidth;
        const uint16_t h = methodDesc.fullResolutionHeight;

        uint32_t j = 0;
        for (; j < libraryDesc.supportedMethodNum; j++)
        {
            if (methodDesc.method == libraryDesc.supportedMethods[j])
                break;
        }
        if (j == libraryDesc.supportedMethodNum)
            return Result::INVALID_ARGUMENT;

        m_PermanentPoolOffset = (uint16_t)m_PermanentPool.size();
        m_TransientPoolOffset = (uint16_t)m_TransientPool.size();

        MethodData methodData = {};
        methodData.desc = methodDesc;
        methodData.dispatchOffset = m_Dispatches.size();
        methodData.constantOffset = m_Constants.size();
        methodData.textureOffset = m_Resources.size();
        methodData.pingPongOffset = m_PingPongs.size();

        if (methodDesc.method == Method::NRD_DIFFUSE)
            methodData.settingsSize = AddMethod_NrdDiffuse(w, h);
        else if (methodDesc.method == Method::NRD_SPECULAR)
            methodData.settingsSize = AddMethod_NrdSpecular(w, h);
        else if (methodDesc.method == Method::NRD_DIFFUSE_SPECULAR)
            methodData.settingsSize = AddMethod_NrdDiffuseSpecular(w, h);
        else if (methodDesc.method == Method::NRD_SHADOW)
            methodData.settingsSize = AddMethod_NrdShadow(w, h);
        else if (methodDesc.method == Method::NRD_TRANSLUCENT_SHADOW)
            methodData.settingsSize = AddMethod_NrdTranslucentShadow(w, h);
        else if (methodDesc.method == Method::RELAX)
            methodData.settingsSize = AddMethod_Relax(w, h);
        else if (methodDesc.method == Method::SVGF)
            methodData.settingsSize = AddMethod_Svgf(w, h);
        else
            return Result::INVALID_ARGUMENT;

        methodData.dispatchNum = m_Dispatches.size() - methodData.dispatchOffset;
        methodData.constantNum = m_Constants.size() - methodData.constantOffset;
        methodData.textureNum = m_Resources.size() - methodData.textureOffset;
        methodData.permanentPoolNum = m_PermanentPool.size() - m_PermanentPoolOffset;
        methodData.transientPoolNum = m_TransientPool.size() - m_TransientPoolOffset;
        methodData.pingPongNum = m_PingPongs.size() - methodData.pingPongOffset;

        m_MethodData.push_back(methodData);
    }

    Optimize();
    PrepareDesc();

    return Result::SUCCESS;
}

Result DenoiserImpl::GetComputeDispatches(const CommonSettings& commonSettings, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescNum)
{
    const bool updatePingPong = commonSettings.frameIndex != m_CommonSettings.frameIndex;

    UpdateCommonSettings(commonSettings);

    m_ActiveDispatchIndices.clear();
    m_ActiveDispatches.clear();

    for (const MethodData& methodData : m_MethodData)
    {
        if (updatePingPong)
            UpdatePingPong(methodData); // TODO: swap only if frameIndex has changed?

        if (methodData.desc.method == Method::NRD_DIFFUSE)
            UpdateMethod_NrdDiffuse(methodData);
        else if (methodData.desc.method == Method::NRD_SPECULAR)
            UpdateMethod_NrdSpecular(methodData);
        else if (methodData.desc.method == Method::NRD_DIFFUSE_SPECULAR)
            UpdateMethod_NrdDiffuseSpecular(methodData);
        else if (methodData.desc.method == Method::NRD_SHADOW)
            UpdateMethod_NrdShadow(methodData);
        else if (methodData.desc.method == Method::NRD_TRANSLUCENT_SHADOW)
            UpdateMethod_NrdTranslucentShadow(methodData);
        else if (methodData.desc.method == Method::RELAX)
            UpdateMethod_Relax(methodData);
        else if (methodData.desc.method == Method::SVGF)
            UpdateMethod_Svgf(methodData);
    }

    for (const auto& dispatchIndex : m_ActiveDispatchIndices)
        m_ActiveDispatches.push_back( m_Dispatches[dispatchIndex] );

    dispatchDescs = m_ActiveDispatches.data();
    dispatchDescNum = (uint32_t)m_ActiveDispatches.size();

    return Result::SUCCESS;
}

Result DenoiserImpl::SetMethodSettings(Method method, const void* methodSettings)
{
    for( MethodData& methodData : m_MethodData )
    {
        if (methodData.desc.method == method)
        {
            memcpy(&methodData.settings, methodSettings, methodData.settingsSize);

            return Result::SUCCESS;
        }
    }

    return Result::INVALID_ARGUMENT;
}

void DenoiserImpl::Optimize()
{
    /*
    TODO:
    - analyze dependencies and group dispatches without them
    - in case of bad Methods must verify the graph for unused passes, correctness... (at least in debug mode)
    - minimize transient pool size, maximize reuse
    */
}

void DenoiserImpl::PrepareDesc()
{
    m_Desc.pipelines = m_Pipelines.data();
    m_Desc.pipelineNum = (uint32_t)m_Pipelines.size();

    m_Desc.staticSamplers = g_StaticSamplers.data();
    m_Desc.staticSamplerNum = (uint32_t)g_StaticSamplers.size();

    m_Desc.permanentPool = m_PermanentPool.data();
    m_Desc.permanentPoolSize = (uint32_t)m_PermanentPool.size();

    m_Desc.transientPool = m_TransientPool.data();
    m_Desc.transientPoolSize = (uint32_t)m_TransientPool.size();

    m_Desc.descriptorSetDesc.setNum = (uint32_t)m_Dispatches.size();
    m_Desc.descriptorSetDesc.constantBufferNum = (uint32_t)m_Dispatches.size();
    for (const Resource& resource : m_Resources)
    {
        if (resource.stateNeeded == DescriptorType::TEXTURE)
            m_Desc.descriptorSetDesc.textureNum++;
        else if (resource.stateNeeded == DescriptorType::STORAGE_TEXTURE)
            m_Desc.descriptorSetDesc.storageTextureNum++;
    }

    m_Desc.constantBufferDesc.registerIndex = 0;
    m_Desc.constantBufferDesc.maxDataSize = 0;
    for (DispatchDesc& dispatchDesc : m_Dispatches)
    {
        size_t constantOffset = (size_t)dispatchDesc.constantBufferData;
        dispatchDesc.constantBufferData = (uint8_t*)&m_Constants[constantOffset];

        size_t textureOffset = (size_t)dispatchDesc.resources;
        dispatchDesc.resources = &m_Resources[textureOffset];

        m_Desc.constantBufferDesc.maxDataSize = std::max(dispatchDesc.constantBufferDataSize, m_Desc.constantBufferDesc.maxDataSize);
    }

    m_Desc.descriptorSetDesc.maxDescriptorRangeNumPerPipeline = 0;
    for (PipelineDesc& pipelineDesc : m_Pipelines)
    {
        size_t descriptorRangeffset = (size_t)pipelineDesc.descriptorRanges;
        pipelineDesc.descriptorRanges = &m_DescriptorRanges[descriptorRangeffset];

        m_Desc.descriptorSetDesc.maxDescriptorRangeNumPerPipeline = std::max(pipelineDesc.descriptorRangeNum, m_Desc.descriptorSetDesc.maxDescriptorRangeNumPerPipeline);
    }

    // Since now all std::vectors become "locked" (no reallocations)
}

void DenoiserImpl::AddComputeDispatchDesc(DispatchDesc& computeDispatchDesc, const char* entryPointName, const ComputeShader& dxbc, const ComputeShader& dxil, const ComputeShader& spirv, uint32_t width, uint32_t height, uint32_t ctaWidth, uint32_t ctaHeight)
{
    const size_t rangeOffset = m_DescriptorRanges.size();
    const uint32_t pipelineIndex = (uint32_t)m_Pipelines.size();

    // Resource binding
    DescriptorRangeDesc descriptorRanges[2] =
    {
        { DescriptorType::TEXTURE, 0, 0 },
        { DescriptorType::STORAGE_TEXTURE, 0, 0 },
    };

    for (size_t i = m_ResourceOffset; i < m_Resources.size(); i++ )
    {
        const Resource& resource = m_Resources[i];
        if (resource.stateNeeded == descriptorRanges[0].descriptorType)
            descriptorRanges[0].descriptorNum++;
        else if (resource.stateNeeded == descriptorRanges[1].descriptorType)
            descriptorRanges[1].descriptorNum++;
    }

    m_DescriptorRanges.push_back(descriptorRanges[0]);
    m_DescriptorRanges.push_back(descriptorRanges[1]);

    // Pipeline
    PipelineDesc pipelineDesc = {};
    pipelineDesc.descriptorRangeNum = GetCountOf(descriptorRanges);
    pipelineDesc.descriptorRanges = (DescriptorRangeDesc*)rangeOffset;
    pipelineDesc.shaderEntryPointName = entryPointName;
    pipelineDesc.computeShaderDXBC = dxbc;
    pipelineDesc.computeShaderDXIL = dxil;
    pipelineDesc.computeShaderSPIRV = spirv;

    m_Pipelines.push_back( pipelineDesc );
    computeDispatchDesc.pipelineIndex = pipelineIndex;

    // Constants
    size_t lastConstant = m_Constants.size();
    computeDispatchDesc.constantBufferData = (uint8_t*)lastConstant;

    Constant zeroConstant = {};
    m_Constants.resize(lastConstant + computeDispatchDesc.constantBufferDataSize / sizeof(uint32_t), zeroConstant);

    // Resources
    computeDispatchDesc.resourceNum = uint32_t(m_Resources.size() - m_ResourceOffset);
    computeDispatchDesc.resources = (Resource*)m_ResourceOffset;

    // Dispatch
    computeDispatchDesc.name = m_PassName;
    computeDispatchDesc.gridWidth = DivideUp(width, ctaWidth);
    computeDispatchDesc.gridHeight = DivideUp(height, ctaHeight);

    m_Dispatches.push_back(computeDispatchDesc);
}


void DenoiserImpl::PushTexture(DescriptorType descriptorType, uint16_t index, uint16_t mipOffset, uint16_t mipNum, uint16_t indexToSwapWith)
{
    ResourceType resourceType = (ResourceType)index;

    if (index >= TRANSIENT_POOL_START)
    {
        resourceType = ResourceType::TRANSIENT_POOL;
        index += m_TransientPoolOffset - TRANSIENT_POOL_START;

        if (indexToSwapWith != uint16_t(-1))
        {
            indexToSwapWith += m_TransientPoolOffset - TRANSIENT_POOL_START;
            m_PingPongs.push_back( {m_Resources.size(), indexToSwapWith} );
        }
    }
    else if (index >= PERMANENT_POOL_START)
    {
        resourceType = ResourceType::PERMANENT_POOL;
        index += m_PermanentPoolOffset - PERMANENT_POOL_START;

        if (indexToSwapWith != uint16_t(-1))
        {
            indexToSwapWith += m_PermanentPoolOffset - PERMANENT_POOL_START;
            m_PingPongs.push_back( {m_Resources.size(), indexToSwapWith} );
        }
    }
    else
       index = 0;

    m_Resources.push_back( {descriptorType, resourceType, index, mipOffset, mipNum} );
}

void DenoiserImpl::UpdatePingPong(const MethodData& methodData)
{
    for (uint32_t i = 0; i < methodData.pingPongNum; i++)
    {
        PingPong& pingPong = m_PingPongs[methodData.pingPongOffset + i];
        Resource& resource = m_Resources[pingPong.textureIndex];

        uint16_t t = pingPong.indexInPoolToSwapWith;
        pingPong.indexInPoolToSwapWith = resource.indexInPool;
        resource.indexInPool = t;
    }
}

void DenoiserImpl::AddNrdSharedConstants(const MethodData& methodData, float planeDistSensitivity, Constant*& data)
{
    float w = float(methodData.desc.fullResolutionWidth);
    float h = float(methodData.desc.fullResolutionHeight);
    float unproject = 1.0f / (0.5f * h * m_ProjectY);
    float infWithViewZSign = m_CommonSettings.denoisingRange * ( ( m_ProjectionFlags & PROJ_LEFT_HANDED ) ? 1.0f : -1.0f );
    float frameRateScale = Clamp( 16.7f / m_Timer.GetSmoothedElapsedTime(), 1.0f, 4.0f );
    
    uint32_t bools = 0;
    if (m_CommonSettings.worldSpaceMotion)
        bools |= 0x1;
    if (m_CommonSettings.forceReferenceAccumulation)
        bools |= 0x2;

    AddFloat4x4(data, m_ViewToClip);
    AddFloat4(data, m_Frustum);
    AddFloat2(data, 1.0f / w, 1.0f / h);
    AddFloat2(data, w, h);
    AddUint(data, bools);
    AddFloat(data, m_IsOrtho);
    AddFloat(data, unproject);
    AddFloat(data, m_CommonSettings.debug);
    AddFloat(data, infWithViewZSign);
    AddFloat(data, 1.0f / planeDistSensitivity);
    AddUint(data, m_CommonSettings.frameIndex);
    AddFloat(data, frameRateScale);
}

void DenoiserImpl::UpdateCommonSettings(const CommonSettings& commonSettings)
{
    m_JitterPrev.x = m_CommonSettings.cameraJitter[0]; // TODO: add to CommonSettings?
    m_JitterPrev.y = m_CommonSettings.cameraJitter[1];

    memcpy(&m_CommonSettings, &commonSettings, sizeof(commonSettings));

    float whiteNoise = Rand::uf1() * DegToRad(360.0f);
    float ca = Cos( whiteNoise );
    float sa = Sin( whiteNoise );

    m_Rotator[0] = float4( ca, sa, -sa, ca );
    m_Rotator[1] = float4( -sa, ca, -ca, -sa );
    m_Rotator[2] = float4( ca, sa, -sa, ca );

    m_ViewToClip = float4x4
    (
        float4(commonSettings.viewToClipMatrix),
        float4(commonSettings.viewToClipMatrix + 4),
        float4(commonSettings.viewToClipMatrix + 8),
        float4(commonSettings.viewToClipMatrix + 12)
    );

    m_ViewToClipPrev = float4x4
    (
        float4(commonSettings.viewToClipMatrixPrev),
        float4(commonSettings.viewToClipMatrixPrev + 4),
        float4(commonSettings.viewToClipMatrixPrev + 8),
        float4(commonSettings.viewToClipMatrixPrev + 12)
    );

    m_WorldToView = float4x4
    (
        float4(commonSettings.worldToViewMatrix),
        float4(commonSettings.worldToViewMatrix + 4),
        float4(commonSettings.worldToViewMatrix + 8),
        float4(commonSettings.worldToViewMatrix + 12)
    );

    m_WorldToViewPrev = float4x4
    (
        float4(commonSettings.worldToViewMatrixPrev),
        float4(commonSettings.worldToViewMatrixPrev + 4),
        float4(commonSettings.worldToViewMatrixPrev + 8),
        float4(commonSettings.worldToViewMatrixPrev + 12)
    );

    m_ViewToWorld = m_WorldToView;
    m_ViewToWorld.InvertOrtho();
    float3 cameraPosition = m_ViewToWorld.GetCol3().To3d();

    m_ViewToWorldPrev = m_WorldToViewPrev;
    m_ViewToWorldPrev.InvertOrtho();
    float3 cameraPositionPrev = m_ViewToWorldPrev.GetCol3().To3d();

    m_CameraDelta = cameraPositionPrev - cameraPosition;

    // IMPORTANT: this part is mandatory needed to preserve precision by making matrices camera relative
    m_ViewToWorld.SetTranslation( float3::Zero() );
    m_WorldToView = m_ViewToWorld;
    m_WorldToView.InvertOrtho();

    m_ViewToWorldPrev.SetTranslation( m_CameraDelta );
    m_WorldToViewPrev = m_ViewToWorldPrev;
    m_WorldToViewPrev.InvertOrtho();

    // Compute other matrices
    m_WorldToClip = m_ViewToClip * m_WorldToView;
    m_WorldToClipPrev = m_ViewToClipPrev * m_WorldToViewPrev;

    m_ClipToWorldPrev = m_WorldToClipPrev;
    m_ClipToWorldPrev.Invert();

    m_ClipToView = m_ViewToClip;
    m_ClipToView.Invert();

    m_ClipToViewPrev = m_ViewToClipPrev;
    m_ClipToViewPrev.Invert();

    m_ClipToWorld = m_WorldToClip;
    m_ClipToWorld.Invert();

    float project[3];
    float settings[PROJ_NUM];
    uint32_t flags = 0;
    DecomposeProjection(NDC_D3D, NDC_D3D, m_ViewToClip, &flags, settings, nullptr, m_Frustum.pv, project, nullptr);

    m_ProjectY = project[1];
    m_IsOrtho = ( flags & PROJ_ORTHO ) == 0 ? 0.0f : ( ( flags & PROJ_LEFT_HANDED ) ? 1.0f : -1.0f );
    m_ProjectionFlags = flags;

    DecomposeProjection(NDC_D3D, NDC_D3D, m_ViewToClipPrev, &flags, nullptr, nullptr, m_FrustumPrev.pv, nullptr, nullptr);
    m_IsOrthoPrev = ( flags & PROJ_ORTHO ) == 0 ? 0.0f : ( ( flags & PROJ_LEFT_HANDED ) ? 1.0f : -1.0f );

    float dx = Abs( m_CommonSettings.cameraJitter[0] - m_JitterPrev.x );
    float dy = Abs( m_CommonSettings.cameraJitter[1] - m_JitterPrev.y );
    m_JitterDelta = Max(dx, dy);
    m_CheckerboardResolveAccumSpeed = Lerp( 0.95f, 0.5f, m_JitterDelta );

    m_Timer.UpdateElapsedTimeSinceLastSave();
    m_Timer.SaveCurrentTime();
}

#ifndef BYTE
    #define BYTE unsigned char
#endif

// DXBC ====================================================================================

// SHARED
#include "..\..\_Build\Shaders\NRD_MipGeneration_Float4.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_MipGeneration_Float4_Float.cs.dxbc.h"

// NRD_DIFFUSE
#include "..\..\_Build\Shaders\NRD_Diffuse_PreBlur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalAccumulation.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_HistoryFix.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_Blur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalStabilization.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_PostBlur.cs.dxbc.h"

// NRD_SPECULAR
#include "..\..\_Build\Shaders\NRD_Specular_PreBlur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalAccumulation.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_HistoryFix.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_Blur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalStabilization.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_PostBlur.cs.dxbc.h"

// NRD_DIFFUSE_SPECULAR
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_PreBlur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_TemporalAccumulation.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_HistoryFix.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_Blur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_TemporalStabilization.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_PostBlur.cs.dxbc.h"

// NRD_SHADOW
#include "..\..\_Build\Shaders\NRD_Shadow_PreBlur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Shadow_Blur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Shadow_TemporalStabilization.cs.dxbc.h"

// NRD_TRANSLUCENT_SHADOW
#include "..\..\_Build\Shaders\NRD_TranslucentShadow_PreBlur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_TranslucentShadow_Blur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_TranslucentShadow_TemporalStabilization.cs.dxbc.h"

// RELAX
#include "..\..\_Build\Shaders\RELAX_PackInputData.cs.dxbc.h"
#include "..\..\_Build\Shaders\RELAX_Reproject.cs.dxbc.h"
#include "..\..\_Build\Shaders\RELAX_DisocclusionFix.cs.dxbc.h"
#include "..\..\_Build\Shaders\RELAX_HistoryClamping.cs.dxbc.h"
#include "..\..\_Build\Shaders\RELAX_Firefly.cs.dxbc.h"
#include "..\..\_Build\Shaders\RELAX_SpatialVarianceEstimation.cs.dxbc.h"
#include "..\..\_Build\Shaders\RELAX_ATrousShmem.cs.dxbc.h"
#include "..\..\_Build\Shaders\RELAX_ATrousStandard.cs.dxbc.h"

// SVGF
#include "..\..\_Build\Shaders\SVGF_Reproject.cs.dxbc.h"
#include "..\..\_Build\Shaders\SVGF_FilterMoments.cs.dxbc.h"
#include "..\..\_Build\Shaders\SVGF_Atrous.cs.dxbc.h"

// DXIL ====================================================================================

// SHARED
#include "..\..\_Build\Shaders\NRD_MipGeneration_Float4.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_MipGeneration_Float4_Float.cs.dxil.h"

// NRD_DIFFUSE
#include "..\..\_Build\Shaders\NRD_Diffuse_PreBlur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalAccumulation.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_HistoryFix.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_Blur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalStabilization.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_PostBlur.cs.dxil.h"

// NRD_SPECULAR
#include "..\..\_Build\Shaders\NRD_Specular_PreBlur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalAccumulation.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_HistoryFix.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_Blur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalStabilization.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_PostBlur.cs.dxil.h"

// NRD_DIFFUSE_SPECULAR
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_PreBlur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_TemporalAccumulation.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_HistoryFix.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_Blur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_TemporalStabilization.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_PostBlur.cs.dxil.h"

// NRD_SHADOW
#include "..\..\_Build\Shaders\NRD_Shadow_PreBlur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Shadow_Blur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Shadow_TemporalStabilization.cs.dxil.h"

// NRD_TRANSLUCENT_SHADOW
#include "..\..\_Build\Shaders\NRD_TranslucentShadow_PreBlur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_TranslucentShadow_Blur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_TranslucentShadow_TemporalStabilization.cs.dxil.h"

// RELAX
#include "..\..\_Build\Shaders\RELAX_PackInputData.cs.dxil.h"
#include "..\..\_Build\Shaders\RELAX_Reproject.cs.dxil.h"
#include "..\..\_Build\Shaders\RELAX_DisocclusionFix.cs.dxil.h"
#include "..\..\_Build\Shaders\RELAX_HistoryClamping.cs.dxil.h"
#include "..\..\_Build\Shaders\RELAX_Firefly.cs.dxil.h"
#include "..\..\_Build\Shaders\RELAX_SpatialVarianceEstimation.cs.dxil.h"
#include "..\..\_Build\Shaders\RELAX_ATrousShmem.cs.dxil.h"
#include "..\..\_Build\Shaders\RELAX_ATrousStandard.cs.dxil.h"

// SVGF
#include "..\..\_Build\Shaders\SVGF_Reproject.cs.dxil.h"
#include "..\..\_Build\Shaders\SVGF_FilterMoments.cs.dxil.h"
#include "..\..\_Build\Shaders\SVGF_Atrous.cs.dxil.h"

// SPIRV ===================================================================================

// SHARED
#include "..\..\_Build\Shaders\NRD_MipGeneration_Float4.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_MipGeneration_Float4_Float.cs.spirv.h"

// NRD_DIFFUSE
#include "..\..\_Build\Shaders\NRD_Diffuse_PreBlur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalAccumulation.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_HistoryFix.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_Blur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalStabilization.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_PostBlur.cs.spirv.h"

// NRD_SPECULAR
#include "..\..\_Build\Shaders\NRD_Specular_PreBlur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalAccumulation.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_HistoryFix.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_Blur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalStabilization.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_PostBlur.cs.spirv.h"

// NRD_DIFFUSE_SPECULAR
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_PreBlur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_TemporalAccumulation.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_HistoryFix.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_Blur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_TemporalStabilization.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_DiffuseSpecular_PostBlur.cs.spirv.h"

// NRD_SHADOW
#include "..\..\_Build\Shaders\NRD_Shadow_PreBlur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Shadow_Blur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Shadow_TemporalStabilization.cs.spirv.h"

// NRD_TRANSLUCENT_SHADOW
#include "..\..\_Build\Shaders\NRD_TranslucentShadow_PreBlur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_TranslucentShadow_Blur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_TranslucentShadow_TemporalStabilization.cs.spirv.h"

// RELAX
#include "..\..\_Build\Shaders\RELAX_PackInputData.cs.spirv.h"
#include "..\..\_Build\Shaders\RELAX_Reproject.cs.spirv.h"
#include "..\..\_Build\Shaders\RELAX_DisocclusionFix.cs.spirv.h"
#include "..\..\_Build\Shaders\RELAX_HistoryClamping.cs.spirv.h"
#include "..\..\_Build\Shaders\RELAX_Firefly.cs.spirv.h"
#include "..\..\_Build\Shaders\RELAX_SpatialVarianceEstimation.cs.spirv.h"
#include "..\..\_Build\Shaders\RELAX_ATrousShmem.cs.spirv.h"
#include "..\..\_Build\Shaders\RELAX_ATrousStandard.cs.spirv.h"

// SVGF
#include "..\..\_Build\Shaders\SVGF_Reproject.cs.spirv.h"
#include "..\..\_Build\Shaders\SVGF_FilterMoments.cs.spirv.h"
#include "..\..\_Build\Shaders\SVGF_Atrous.cs.spirv.h"

// METHODS =================================================================================

#include "Methods/NrdDiffuse.hpp"
#include "Methods/NrdSpecular.hpp"
#include "Methods/NrdDiffuseSpecular.hpp"
#include "Methods/NrdShadow.hpp"
#include "Methods/NrdTranslucentShadow.hpp"
#include "Methods/Relax.hpp"
#include "Methods/Svgf.hpp"

