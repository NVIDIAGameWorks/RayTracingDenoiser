/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

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
        methodData.textureOffset = m_Resources.size();
        methodData.pingPongOffset = m_PingPongs.size();

        if (methodDesc.method == Method::REBLUR_DIFFUSE)
            methodData.settingsSize = AddMethod_ReblurDiffuse(w, h);
        else if (methodDesc.method == Method::REBLUR_SPECULAR)
            methodData.settingsSize = AddMethod_ReblurSpecular(w, h);
        else if (methodDesc.method == Method::REBLUR_DIFFUSE_SPECULAR)
            methodData.settingsSize = AddMethod_ReblurDiffuseSpecular(w, h);
        else if (methodDesc.method == Method::SIGMA_SHADOW)
            methodData.settingsSize = AddMethod_SigmaShadow(w, h);
        else if (methodDesc.method == Method::SIGMA_SHADOW_TRANSLUCENCY)
            methodData.settingsSize = AddMethod_SigmaShadowTranslucency(w, h);
        else if (methodDesc.method == Method::RELAX_SPECULAR)
            methodData.settingsSize = AddMethod_RelaxSpecular(w, h);
        else if (methodDesc.method == Method::RELAX_DIFFUSE_SPECULAR)
            methodData.settingsSize = AddMethod_RelaxDiffuseSpecular(w, h);
        else
            return Result::INVALID_ARGUMENT;

        methodData.dispatchNum = m_Dispatches.size() - methodData.dispatchOffset;
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

    m_ActiveDispatches.clear();

    for (const MethodData& methodData : m_MethodData)
    {
        if (updatePingPong)
            UpdatePingPong(methodData); // TODO: swap only if frameIndex has changed?

        if (methodData.desc.method == Method::REBLUR_DIFFUSE)
            UpdateMethod_ReblurDiffuse(methodData);
        else if (methodData.desc.method == Method::REBLUR_SPECULAR)
            UpdateMethod_ReblurSpecular(methodData);
        else if (methodData.desc.method == Method::REBLUR_DIFFUSE_SPECULAR)
            UpdateMethod_ReblurDiffuseSpecular(methodData);
        else if (methodData.desc.method == Method::SIGMA_SHADOW)
            UpdateMethod_SigmaShadow(methodData);
        else if (methodData.desc.method == Method::SIGMA_SHADOW_TRANSLUCENCY)
            UpdateMethod_SigmaShadowTranslucency(methodData);
        else if (methodData.desc.method == Method::RELAX_SPECULAR)
            UpdateMethod_RelaxSpecular(methodData);
        else if (methodData.desc.method == Method::RELAX_DIFFUSE_SPECULAR)
            UpdateMethod_RelaxDiffuseSpecular(methodData);
    }

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

    m_Desc.descriptorSetDesc.textureNum = 0;
    m_Desc.descriptorSetDesc.storageTextureNum = 0;
    m_Desc.constantBufferDesc.registerIndex = 0;
    m_Desc.constantBufferDesc.maxDataSize = 0;
    for (InternalDispatchDesc& dispatchDesc : m_Dispatches)
    {
        size_t textureOffset = (size_t)dispatchDesc.resources;
        dispatchDesc.resources = &m_Resources[textureOffset];

        for (uint32_t i = 0; i < dispatchDesc.resourceNum; i++)
        {
            const Resource& resource = dispatchDesc.resources[i];
            if (resource.stateNeeded == DescriptorType::TEXTURE)
                m_Desc.descriptorSetDesc.textureNum += dispatchDesc.maxRepeatNum;
            else if (resource.stateNeeded == DescriptorType::STORAGE_TEXTURE)
                m_Desc.descriptorSetDesc.storageTextureNum += dispatchDesc.maxRepeatNum;
        }

        m_Desc.descriptorSetDesc.setNum += dispatchDesc.maxRepeatNum;
        m_Desc.descriptorSetDesc.constantBufferNum += dispatchDesc.maxRepeatNum;

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

void DenoiserImpl::AddComputeDispatchDesc(uint16_t workgroupDim, uint16_t downsampleFactor, uint32_t constantBufferDataSize, uint32_t maxRepeatNum, const char* shaderFileName, const ComputeShader& dxbc, const ComputeShader& dxil, const ComputeShader& spirv)
{
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

    const size_t rangeOffset = m_DescriptorRanges.size();
    m_DescriptorRanges.push_back(descriptorRanges[0]);
    m_DescriptorRanges.push_back(descriptorRanges[1]);

    // Pipeline
    PipelineDesc pipelineDesc = {};
    pipelineDesc.descriptorRangeNum = GetCountOf(descriptorRanges);
    pipelineDesc.descriptorRanges = (DescriptorRangeDesc*)rangeOffset;
    pipelineDesc.shaderFileName = shaderFileName;
    pipelineDesc.shaderEntryPointName = NRD_CS_MAIN;
    pipelineDesc.computeShaderDXBC = dxbc;
    pipelineDesc.computeShaderDXIL = dxil;
    pipelineDesc.computeShaderSPIRV = spirv;

    const uint32_t pipelineIndex = (uint32_t)m_Pipelines.size();
    m_Pipelines.push_back( pipelineDesc );

    InternalDispatchDesc computeDispatchDesc = {};
    computeDispatchDesc.pipelineIndex = (uint16_t)pipelineIndex;
    computeDispatchDesc.maxRepeatNum = (uint16_t)maxRepeatNum;

    // Constants
    computeDispatchDesc.constantBufferDataSize = constantBufferDataSize;

    // Resources
    computeDispatchDesc.resourceNum = uint32_t(m_Resources.size() - m_ResourceOffset);
    computeDispatchDesc.resources = (Resource*)m_ResourceOffset;

    // Dispatch
    computeDispatchDesc.name = m_PassName;
    computeDispatchDesc.workgroupDim = workgroupDim;
    computeDispatchDesc.downsampleFactor = downsampleFactor;

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

void DenoiserImpl::UpdateCommonSettings(const CommonSettings& commonSettings)
{
    // TODO: add to CommonSettings?
    m_JitterPrev = ml::float2(m_CommonSettings.cameraJitter[0], m_CommonSettings.cameraJitter[1]);
    m_ResolutionScalePrev = m_CommonSettings.resolutionScale;

    memcpy(&m_CommonSettings, &commonSettings, sizeof(commonSettings));

    // There are many cases, where history buffers contain garbage - handle at least one of them internally
    if (m_IsFirstUse)
    {
        m_CommonSettings.frameIndex = 0;
        m_IsFirstUse = false;
    }

    // Rotators
    float whiteNoise = ml::Rand::uf1(&m_FastRandState) * ml::DegToRad(360.0f);
    float ca = ml::Cos( whiteNoise );
    float sa = ml::Sin( whiteNoise );
    m_Rotator[0] = ml::float4( ca, sa, -sa, ca );

    whiteNoise = ml::Rand::uf1(&m_FastRandState) * ml::DegToRad(360.0f);
    ca = ml::Cos( whiteNoise );
    sa = ml::Sin( whiteNoise );
    m_Rotator[1] = ml::float4( -sa, ca, -ca, -sa );
    m_Rotator[2] = ml::float4( ca, sa, -sa, ca );

    // Main matrices
    m_ViewToClip = ml::float4x4
    (
        ml::float4(m_CommonSettings.viewToClipMatrix),
        ml::float4(m_CommonSettings.viewToClipMatrix + 4),
        ml::float4(m_CommonSettings.viewToClipMatrix + 8),
        ml::float4(m_CommonSettings.viewToClipMatrix + 12)
    );

    m_ViewToClipPrev = ml::float4x4
    (
        ml::float4(m_CommonSettings.viewToClipMatrixPrev),
        ml::float4(m_CommonSettings.viewToClipMatrixPrev + 4),
        ml::float4(m_CommonSettings.viewToClipMatrixPrev + 8),
        ml::float4(m_CommonSettings.viewToClipMatrixPrev + 12)
    );

    m_WorldToView = ml::float4x4
    (
        ml::float4(m_CommonSettings.worldToViewRotationMatrix[0], m_CommonSettings.worldToViewRotationMatrix[1], m_CommonSettings.worldToViewRotationMatrix[2], 0.0f),
        ml::float4(m_CommonSettings.worldToViewRotationMatrix[4], m_CommonSettings.worldToViewRotationMatrix[5], m_CommonSettings.worldToViewRotationMatrix[6], 0.0f),
        ml::float4(m_CommonSettings.worldToViewRotationMatrix[8], m_CommonSettings.worldToViewRotationMatrix[9], m_CommonSettings.worldToViewRotationMatrix[10], 0.0f),
        ml::float4(0.0f, 0.0f, 0.0f, 1.0f)
    );

    m_WorldToViewPrev = ml::float4x4
    (
        ml::float4(m_CommonSettings.worldToViewRotationMatrixPrev[0], m_CommonSettings.worldToViewRotationMatrixPrev[1], m_CommonSettings.worldToViewRotationMatrixPrev[2], 0.0f),
        ml::float4(m_CommonSettings.worldToViewRotationMatrixPrev[4], m_CommonSettings.worldToViewRotationMatrixPrev[5], m_CommonSettings.worldToViewRotationMatrixPrev[6], 0.0f),
        ml::float4(m_CommonSettings.worldToViewRotationMatrixPrev[8], m_CommonSettings.worldToViewRotationMatrixPrev[9], m_CommonSettings.worldToViewRotationMatrixPrev[10], 0.0f),
        ml::float4(0.0f, 0.0f, 0.0f, 1.0f)
    );

    m_CameraDelta = ml::float3(m_CommonSettings.cameraMotion);

    // Convert projection matrix to LH
    uint32_t flags = 0;
    ml::DecomposeProjection(NDC_D3D, NDC_D3D, m_ViewToClip, &flags, nullptr, nullptr, m_Frustum.pv, nullptr, nullptr);

    bool isProjMatrixLH = (flags & ml::PROJ_LEFT_HANDED) != 0;
    if (!isProjMatrixLH)
    {
        m_ViewToClip.col2 = (-ml::float4(m_ViewToClip.col2)).xmm;
        m_ViewToClipPrev.col2 = (-ml::float4(m_ViewToClipPrev.col2)).xmm;
    }

    // Convert view matrices to LH (the code below assumes that projection matrix and view matrix Z-axises are same in any case, other axises will be flipped accordingly)
    bool isViewMatrixLH = m_WorldToView.IsLeftHanded();

    bool flip[3] = { false, false, false };
    if (!isViewMatrixLH)
        flip[ isProjMatrixLH ? 0 : 2 ] = true;
    else if (!isProjMatrixLH)
    {
        flip[0] = true;
        flip[1] = true;
    }

    m_WorldToView.Transpose();
    m_WorldToViewPrev.Transpose();

    ml::v4f* c = &m_WorldToView.col0;
    ml::v4f* cp = &m_WorldToViewPrev.col0;
    for (uint32_t i = 0; i < 3; i++ )
    {
        if (flip[i])
        {
            c[i] = (-ml::float4(c[i])).xmm;
            cp[i] = (-ml::float4(cp[i])).xmm;
        }
    }

    m_WorldToView.Transpose();
    m_WorldToViewPrev.Transpose();

    // Compute other matrices
    m_ViewToWorld = m_WorldToView;
    m_ViewToWorld.InvertOrtho();

    m_ViewToWorldPrev = m_WorldToViewPrev;
    m_ViewToWorldPrev.InvertOrtho();

    if( m_CommonSettings.frameIndex == 0 )
        m_CameraDeltaSmoothed = m_CameraDelta;
    else
    {
        float l1 = Length(m_CameraDeltaSmoothed);
        float l2 = Length(m_CameraDelta);

        float relativeDelta = ml::Abs(l1 - l2) / (ml::Min(l1, l2) + 1e-7f);
        float f = relativeDelta / (1.0f + relativeDelta);
        f = ml::Max(f, 0.1f);

        m_CameraDeltaSmoothed = ml::Lerp(m_CameraDeltaSmoothed, m_CameraDelta, ml::float3(f));
    }

    // IMPORTANT: this part is mandatory needed to preserve precision by making matrices camera relative
    m_ViewToWorld.SetTranslation( ml::float3::Zero() );
    m_WorldToView = m_ViewToWorld;
    m_WorldToView.InvertOrtho();

    m_ViewToWorldPrev.SetTranslation( m_CameraDelta );
    m_WorldToViewPrev = m_ViewToWorldPrev;
    m_WorldToViewPrev.InvertOrtho();

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
    float settings[ml::PROJ_NUM];
    ml::DecomposeProjection(NDC_D3D, NDC_D3D, m_ViewToClip, &flags, settings, nullptr, m_Frustum.pv, project, nullptr);
    m_ProjectY = project[1];
    m_IsOrtho = (flags & ml::PROJ_ORTHO) == 0 ? 0.0f : 1.0f;

    ml::DecomposeProjection(NDC_D3D, NDC_D3D, m_ViewToClipPrev, &flags, nullptr, nullptr, m_FrustumPrev.pv, nullptr, nullptr);

    float dx = ml::Abs(m_CommonSettings.cameraJitter[0] - m_JitterPrev.x);
    float dy = ml::Abs(m_CommonSettings.cameraJitter[1] - m_JitterPrev.y);
    m_JitterDelta = ml::Max(dx, dy);
    m_CheckerboardResolveAccumSpeed = ml::Lerp(0.95f, 0.5f, m_JitterDelta);

    m_Timer.UpdateElapsedTimeSinceLastSave();
    m_Timer.SaveCurrentTime();

    m_TimeDelta = m_CommonSettings.timeDeltaBetweenFrames > 0.0f ? m_CommonSettings.timeDeltaBetweenFrames : m_Timer.GetSmoothedElapsedTime();
    m_FrameRateScale = ml::Clamp(33.333f / m_TimeDelta, 2.0f / 16.0f, 4.0f);
}

// SHADERS =================================================================================

#ifndef BYTE
    #define BYTE unsigned char
#endif

#ifdef NRD_USE_PRECOMPILED_SHADERS
    // NRD
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "NRD_MipGeneration_Float4_Float.cs.dxbc.h"
        #include "NRD_MipGeneration_Float4_Float.cs.dxil.h"
        #include "NRD_MipGeneration_Float4_Float4_Float.cs.dxbc.h"
        #include "NRD_MipGeneration_Float4_Float4_Float.cs.dxil.h"
    #endif

    #include "NRD_MipGeneration_Float4_Float4_Float.cs.spirv.h"
    #include "NRD_MipGeneration_Float4_Float.cs.spirv.h"


    // REBLUR_DIFFUSE
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REBLUR_CopyViewZ.cs.dxbc.h"
        #include "REBLUR_CopyViewZ.cs.dxil.h"
        #include "REBLUR_Diffuse_PreBlur.cs.dxbc.h"
        #include "REBLUR_Diffuse_PreBlur.cs.dxil.h"
        #include "REBLUR_Diffuse_TemporalAccumulation.cs.dxbc.h"
        #include "REBLUR_Diffuse_TemporalAccumulation.cs.dxil.h"
        #include "REBLUR_Diffuse_HistoryFix.cs.dxbc.h"
        #include "REBLUR_Diffuse_HistoryFix.cs.dxil.h"
        #include "REBLUR_Diffuse_Blur.cs.dxbc.h"
        #include "REBLUR_Diffuse_Blur.cs.dxil.h"
        #include "REBLUR_Diffuse_PostBlur.cs.dxbc.h"
        #include "REBLUR_Diffuse_PostBlur.cs.dxil.h"
        #include "REBLUR_Diffuse_TemporalStabilization.cs.dxbc.h"
        #include "REBLUR_Diffuse_TemporalStabilization.cs.dxil.h"
        #include "REBLUR_Diffuse_SplitScreen.cs.dxbc.h"
        #include "REBLUR_Diffuse_SplitScreen.cs.dxil.h"
    #endif

    #include "REBLUR_CopyViewZ.cs.spirv.h"
    #include "REBLUR_Diffuse_PreBlur.cs.spirv.h"
    #include "REBLUR_Diffuse_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Diffuse_HistoryFix.cs.spirv.h"
    #include "REBLUR_Diffuse_Blur.cs.spirv.h"
    #include "REBLUR_Diffuse_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Diffuse_PostBlur.cs.spirv.h"
    #include "REBLUR_Diffuse_SplitScreen.cs.spirv.h"


    // REBLUR_SPECULAR
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REBLUR_Specular_PreBlur.cs.dxbc.h"
        #include "REBLUR_Specular_PreBlur.cs.dxil.h"
        #include "REBLUR_Specular_TemporalAccumulation.cs.dxbc.h"
        #include "REBLUR_Specular_TemporalAccumulation.cs.dxil.h"
        #include "REBLUR_Specular_HistoryFix.cs.dxbc.h"
        #include "REBLUR_Specular_HistoryFix.cs.dxil.h"
        #include "REBLUR_Specular_Blur.cs.dxbc.h"
        #include "REBLUR_Specular_Blur.cs.dxil.h"
        #include "REBLUR_Specular_PostBlur.cs.dxbc.h"
        #include "REBLUR_Specular_PostBlur.cs.dxil.h"
        #include "REBLUR_Specular_TemporalStabilization.cs.dxbc.h"
        #include "REBLUR_Specular_TemporalStabilization.cs.dxil.h"
        #include "REBLUR_Specular_SplitScreen.cs.dxbc.h"
        #include "REBLUR_Specular_SplitScreen.cs.dxil.h"
    #endif

    #include "REBLUR_Specular_PreBlur.cs.spirv.h"
    #include "REBLUR_Specular_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Specular_HistoryFix.cs.spirv.h"
    #include "REBLUR_Specular_Blur.cs.spirv.h"
    #include "REBLUR_Specular_PostBlur.cs.spirv.h"
    #include "REBLUR_Specular_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Specular_SplitScreen.cs.spirv.h"


    // REBLUR_DIFFUSE_SPECULAR
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REBLUR_DiffuseSpecular_PreBlur.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_PreBlur.cs.dxil.h"
        #include "REBLUR_DiffuseSpecular_TemporalAccumulation.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_TemporalAccumulation.cs.dxil.h"
        #include "REBLUR_DiffuseSpecular_HistoryFix.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_HistoryFix.cs.dxil.h"
        #include "REBLUR_DiffuseSpecular_Blur.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_Blur.cs.dxil.h"
        #include "REBLUR_DiffuseSpecular_TemporalStabilization.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_TemporalStabilization.cs.dxil.h"
        #include "REBLUR_DiffuseSpecular_PostBlur.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_PostBlur.cs.dxil.h"
        #include "REBLUR_DiffuseSpecular_SplitScreen.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_SplitScreen.cs.dxil.h"
    #endif

    #include "REBLUR_DiffuseSpecular_PreBlur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_HistoryFix.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_Blur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_PostBlur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_SplitScreen.cs.spirv.h"


    // SIGMA_SHADOW
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "SIGMA_Shadow_ClassifyTiles.cs.dxbc.h"
        #include "SIGMA_Shadow_ClassifyTiles.cs.dxil.h"
        #include "SIGMA_Shadow_SmoothTiles.cs.dxbc.h"
        #include "SIGMA_Shadow_SmoothTiles.cs.dxil.h"
        #include "SIGMA_Shadow_PreBlur.cs.dxbc.h"
        #include "SIGMA_Shadow_PreBlur.cs.dxil.h"
        #include "SIGMA_Shadow_Blur.cs.dxbc.h"
        #include "SIGMA_Shadow_Blur.cs.dxil.h"
        #include "SIGMA_Shadow_TemporalStabilization.cs.dxbc.h"
        #include "SIGMA_Shadow_TemporalStabilization.cs.dxil.h"
        #include "SIGMA_Shadow_SplitScreen.cs.dxbc.h"
        #include "SIGMA_Shadow_SplitScreen.cs.dxil.h"
    #endif

    #include "SIGMA_Shadow_ClassifyTiles.cs.spirv.h"
    #include "SIGMA_Shadow_SmoothTiles.cs.spirv.h"
    #include "SIGMA_Shadow_PreBlur.cs.spirv.h"
    #include "SIGMA_Shadow_Blur.cs.spirv.h"
    #include "SIGMA_Shadow_TemporalStabilization.cs.spirv.h"
    #include "SIGMA_Shadow_SplitScreen.cs.spirv.h"


    // SIGMA_SHADOW_TRANSLUCENCY
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "SIGMA_ShadowTranslucency_ClassifyTiles.cs.dxbc.h"
        #include "SIGMA_ShadowTranslucency_ClassifyTiles.cs.dxil.h"
        #include "SIGMA_ShadowTranslucency_PreBlur.cs.dxbc.h"
        #include "SIGMA_ShadowTranslucency_PreBlur.cs.dxil.h"
        #include "SIGMA_ShadowTranslucency_Blur.cs.dxbc.h"
        #include "SIGMA_ShadowTranslucency_Blur.cs.dxil.h"
        #include "SIGMA_ShadowTranslucency_TemporalStabilization.cs.dxbc.h"
        #include "SIGMA_ShadowTranslucency_TemporalStabilization.cs.dxil.h"
        #include "SIGMA_ShadowTranslucency_SplitScreen.cs.dxbc.h"
        #include "SIGMA_ShadowTranslucency_SplitScreen.cs.dxil.h"
    #endif

    #include "SIGMA_ShadowTranslucency_ClassifyTiles.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_PreBlur.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_Blur.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_TemporalStabilization.cs.spirv.h"
    #include "SIGMA_ShadowTranslucency_SplitScreen.cs.spirv.h"


    // RELAX_SPECULAR
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "RELAX_PackInputData.cs.dxbc.h"
        #include "RELAX_PackInputData.cs.dxil.h"
        #include "RELAX_Specular_Reproject.cs.dxbc.h"
        #include "RELAX_Specular_Reproject.cs.dxil.h"
        #include "RELAX_Specular_DisocclusionFix.cs.dxbc.h"
        #include "RELAX_Specular_DisocclusionFix.cs.dxil.h"
        #include "RELAX_Specular_HistoryClamping.cs.dxbc.h"
        #include "RELAX_Specular_HistoryClamping.cs.dxil.h"
        #include "RELAX_Specular_Firefly.cs.dxbc.h"
        #include "RELAX_Specular_Firefly.cs.dxil.h"
        #include "RELAX_Specular_SpatialVarianceEstimation.cs.dxbc.h"
        #include "RELAX_Specular_SpatialVarianceEstimation.cs.dxil.h"
        #include "RELAX_Specular_ATrousShmem.cs.dxbc.h"
        #include "RELAX_Specular_ATrousShmem.cs.dxil.h"
        #include "RELAX_Specular_ATrousStandard.cs.dxbc.h"
        #include "RELAX_Specular_ATrousStandard.cs.dxil.h"
        #include "RELAX_Specular_SplitScreen.cs.dxbc.h"
        #include "RELAX_Specular_SplitScreen.cs.dxil.h"
    #endif

    #include "RELAX_PackInputData.cs.spirv.h"
    #include "RELAX_Specular_Reproject.cs.spirv.h"
    #include "RELAX_Specular_DisocclusionFix.cs.spirv.h"
    #include "RELAX_Specular_HistoryClamping.cs.spirv.h"
    #include "RELAX_Specular_Firefly.cs.spirv.h"
    #include "RELAX_Specular_SpatialVarianceEstimation.cs.spirv.h"
    #include "RELAX_Specular_ATrousShmem.cs.spirv.h"
    #include "RELAX_Specular_ATrousStandard.cs.spirv.h"
    #include "RELAX_Specular_SplitScreen.cs.spirv.h"

    // RELAX_DIFFUSE_SPECULAR
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "RELAX_DiffuseSpecular_Reproject.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_Reproject.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_DisocclusionFix.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_DisocclusionFix.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_HistoryClamping.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_HistoryClamping.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_Firefly.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_Firefly.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_SpatialVarianceEstimation.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_SpatialVarianceEstimation.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_ATrousShmem.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_ATrousShmem.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_ATrousStandard.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_ATrousStandard.cs.dxil.h"
        #include "RELAX_DiffuseSpecular_SplitScreen.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_SplitScreen.cs.dxil.h"
    #endif

    #include "RELAX_DiffuseSpecular_Reproject.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_DisocclusionFix.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_HistoryClamping.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_Firefly.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_SpatialVarianceEstimation.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_ATrousShmem.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_ATrousStandard.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_SplitScreen.cs.spirv.h"
#endif

// METHODS =================================================================================

#include "Methods/Reblur_Diffuse.hpp"
#include "Methods/Reblur_Specular.hpp"
#include "Methods/Reblur_DiffuseSpecular.hpp"
#include "Methods/Sigma_Shadow.hpp"
#include "Methods/Sigma_ShadowTranslucency.hpp"
#include "Methods/Relax_Specular.hpp"
#include "Methods/Relax_DiffuseSpecular.hpp"
