/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "DenoiserImpl.h"

#include <array>

#ifndef BYTE
    #define BYTE unsigned char
#endif

#ifdef NRD_USE_PRECOMPILED_SHADERS
    // NRD
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "NRD_MipGeneration_Float2.cs.dxbc.h"
        #include "NRD_MipGeneration_Float2.cs.dxil.h"
        #include "NRD_MipGeneration_Float2_Float2.cs.dxbc.h"
        #include "NRD_MipGeneration_Float2_Float2.cs.dxil.h"
        #include "NRD_MipGeneration_Float4_Float.cs.dxbc.h"
        #include "NRD_MipGeneration_Float4_Float.cs.dxil.h"
        #include "NRD_MipGeneration_Float4_Float4_Float.cs.dxbc.h"
        #include "NRD_MipGeneration_Float4_Float4_Float.cs.dxil.h"
        #include "NRD_Clear_f.cs.dxbc.h"
        #include "NRD_Clear_f.cs.dxil.h"
        #include "NRD_Clear_ui.cs.dxbc.h"
        #include "NRD_Clear_ui.cs.dxil.h"
    #endif

    #include "NRD_MipGeneration_Float2.cs.spirv.h"
    #include "NRD_MipGeneration_Float2_Float2.cs.spirv.h"
    #include "NRD_MipGeneration_Float4_Float.cs.spirv.h"
    #include "NRD_MipGeneration_Float4_Float4_Float.cs.spirv.h"
    #include "NRD_Clear_f.cs.spirv.h"
    #include "NRD_Clear_ui.cs.spirv.h"
#endif

constexpr std::array<nrd::StaticSamplerDesc, (size_t)nrd::Sampler::MAX_NUM> g_StaticSamplers =
{{
    {nrd::Sampler::NEAREST_CLAMP, 0},
    {nrd::Sampler::NEAREST_MIRRORED_REPEAT, 1},
    {nrd::Sampler::LINEAR_CLAMP, 2},
    {nrd::Sampler::LINEAR_MIRRORED_REPEAT, 3},
}};

constexpr std::array<bool, (size_t)nrd::Format::MAX_NUM> g_IsIntegerFormat =
{
    false,        // R8_UNORM
    false,        // R8_SNORM
    true,         // R8_UINT
    false,        // R8_SINT
    false,        // RG8_UNORM
    false,        // RG8_SNORM
    true,         // RG8_UINT
    false,        // RG8_SINT
    false,        // RGBA8_UNORM
    false,        // RGBA8_SNORM
    true,         // RGBA8_UINT
    false,        // RGBA8_SINT
    false,        // RGBA8_SRGB
    false,        // R16_UNORM
    false,        // R16_SNORM
    true,         // R16_UINT
    false,        // R16_SINT
    false,        // R16_SFLOAT
    false,        // RG16_UNORM
    false,        // RG16_SNORM
    true,         // RG16_UINT
    false,        // RG16_SINT
    false,        // RG16_SFLOAT
    false,        // RGBA16_UNORM
    false,        // RGBA16_SNORM
    true,         // RGBA16_UINT
    false,        // RGBA16_SINT
    false,        // RGBA16_SFLOAT
    true,         // R32_UINT
    false,        // R32_SINT
    false,        // R32_SFLOAT
    true,         // RG32_UINT
    false,        // RG32_SINT
    false,        // RG32_SFLOAT
    true,         // RGB32_UINT
    false,        // RGB32_SINT
    false,        // RGB32_SFLOAT
    true,         // RGBA32_UINT
    false,        // RGBA32_SINT
    false,        // RGBA32_SFLOAT
    false,        // R10_G10_B10_A2_UNORM
    true,         // R10_G10_B10_A2_UINT
    false,        // R11_G11_B10_UFLOAT
    false,        // R9_G9_B9_E5_UFLOAT
};


nrd::Result nrd::DenoiserImpl::Create(const nrd::DenoiserCreationDesc& denoiserCreationDesc)
{
    const nrd::LibraryDesc& libraryDesc = nrd::GetLibraryDesc();

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
        {
            methodData.settings.diffuseReblur = ReblurDiffuseSettings();
            methodData.settingsSize = AddMethod_ReblurDiffuse(w, h);
        }
        else if (methodDesc.method == Method::REBLUR_DIFFUSE_OCCLUSION)
        {
            methodData.settings.diffuseReblur = ReblurDiffuseSettings();
            methodData.settingsSize = AddMethod_ReblurDiffuseOcclusion(w, h);
        }
        else if (methodDesc.method == Method::REBLUR_SPECULAR)
        {
            methodData.settings.specularReblur = ReblurSpecularSettings();
            methodData.settingsSize = AddMethod_ReblurSpecular(w, h);
        }
        else if (methodDesc.method == Method::REBLUR_SPECULAR_OCCLUSION)
        {
            methodData.settings.specularReblur = ReblurSpecularSettings();
            methodData.settingsSize = AddMethod_ReblurSpecularOcclusion(w, h);
        }
        else if (methodDesc.method == Method::REBLUR_DIFFUSE_SPECULAR)
        {
            methodData.settings.diffuseSpecularReblur = ReblurDiffuseSpecularSettings();
            methodData.settingsSize = AddMethod_ReblurDiffuseSpecular(w, h);
        }
        else if (methodDesc.method == Method::REBLUR_DIFFUSE_SPECULAR_OCCLUSION)
        {
            methodData.settings.diffuseSpecularReblur = ReblurDiffuseSpecularSettings();
            methodData.settingsSize = AddMethod_ReblurDiffuseSpecularOcclusion(w, h);
        }
        else if (methodDesc.method == Method::REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION)
        {
            methodData.settings.diffuseReblur = ReblurDiffuseSettings();
            methodData.settingsSize = AddMethod_ReblurDiffuseDirectionalOcclusion(w, h);
        }
        else if (methodDesc.method == Method::SIGMA_SHADOW)
        {
            methodData.settings.shadowSigma = SigmaShadowSettings();
            methodData.settingsSize = AddMethod_SigmaShadow(w, h);
        }
        else if (methodDesc.method == Method::SIGMA_SHADOW_TRANSLUCENCY)
        {
            methodData.settings.shadowSigma = SigmaShadowSettings();
            methodData.settingsSize = AddMethod_SigmaShadowTranslucency(w, h);
        }
        else if (methodDesc.method == Method::RELAX_DIFFUSE)
        {
            methodData.settings.diffuseRelax = RelaxDiffuseSettings();
            methodData.settingsSize = AddMethod_RelaxDiffuse(w, h);
        }
        else if (methodDesc.method == Method::RELAX_SPECULAR)
        {
            methodData.settings.specularRelax = RelaxSpecularSettings();
            methodData.settingsSize = AddMethod_RelaxSpecular(w, h);
        }
        else if (methodDesc.method == Method::RELAX_DIFFUSE_SPECULAR)
        {
            methodData.settings.diffuseSpecularRelax = RelaxDiffuseSpecularSettings();
            methodData.settingsSize = AddMethod_RelaxDiffuseSpecular(w, h);
        }
        else if (methodDesc.method == Method::REFERENCE)
        {
            methodData.settings.reference = ReferenceSettings();
            methodData.settingsSize = AddMethod_Reference(w, h);
        }
        else
            return Result::INVALID_ARGUMENT;

        methodData.pingPongNum = m_PingPongs.size() - methodData.pingPongOffset;

        m_MethodData.push_back(methodData);
    }

    // Clear
    m_ClearDispatchOffset = m_Dispatches.size();

    m_PermanentPoolOffset = 0;
    for (size_t textureIndex = 0; textureIndex < m_PermanentPool.size(); textureIndex++)
    {
        const TextureDesc& texture = m_PermanentPool[textureIndex];

        for (uint16_t mip = 0; mip < texture.mipNum; mip++)
        {
            _PushPass("Clear");
            {
                PushOutput((uint16_t)(textureIndex + PERMANENT_POOL_START), mip, 1);
                if (g_IsIntegerFormat[(size_t)texture.format])
                    AddDispatch(NRD_Clear_ui, 0, 16, 1);
                else
                    AddDispatch(NRD_Clear_f, 0, 16, 1);
            }
        }
    }

    m_TransientPoolOffset = 0;
    for (size_t textureIndex = 0; textureIndex < m_TransientPool.size(); textureIndex++)
    {
        const TextureDesc& texture = m_TransientPool[textureIndex];
        if (g_IsIntegerFormat[(size_t)texture.format])

        for (uint16_t mip = 0; mip < texture.mipNum; mip++)
        {
            _PushPass("Clear");
            {
                PushOutput((uint16_t)(textureIndex + TRANSIENT_POOL_START), mip, 1);
                if (g_IsIntegerFormat[(size_t)texture.format])
                    AddDispatch(NRD_Clear_ui, 0, 16, 1);
                else
                    AddDispatch(NRD_Clear_f, 0, 16, 1);
            }
        }
    }

    Optimize();
    PrepareDesc();

    return Result::SUCCESS;
}

nrd::Result nrd::DenoiserImpl::GetComputeDispatches(const nrd::CommonSettings& commonSettings, const nrd::DispatchDesc*& dispatchDescs, uint32_t& dispatchDescNum)
{
    const bool updatePingPong = commonSettings.frameIndex != m_CommonSettings.frameIndex;

    UpdateCommonSettings(commonSettings);

    m_ActiveDispatches.clear();

    // Clear
    if (m_CommonSettings.accumulationMode == AccumulationMode::CLEAR_AND_RESTART)
    {
        for (size_t i = m_ClearDispatchOffset; i < m_Dispatches.size(); i++)
        {
            const InternalDispatchDesc& internalDispatchDesc = m_Dispatches[i];
            const Resource& resource = *internalDispatchDesc.resources;

            size_t textureIndex = resource.indexInPool;
            TextureDesc& textureDesc = resource.type == ResourceType::PERMANENT_POOL ? m_PermanentPool[textureIndex] : m_TransientPool[textureIndex];
            uint32_t w = textureDesc.width >> resource.mipOffset;
            uint32_t h = textureDesc.height >> resource.mipOffset;

            DispatchDesc dispatchDesc = {};
            dispatchDesc.name = internalDispatchDesc.name;
            dispatchDesc.resources = internalDispatchDesc.resources;
            dispatchDesc.resourceNum = internalDispatchDesc.resourceNum;
            dispatchDesc.pipelineIndex = internalDispatchDesc.pipelineIndex;
            dispatchDesc.gridWidth = DivideUp(w, internalDispatchDesc.workgroupDimX);
            dispatchDesc.gridHeight = DivideUp(h, internalDispatchDesc.workgroupDimY);

            m_ActiveDispatches.push_back(dispatchDesc);
        }
    }

    for (const MethodData& methodData : m_MethodData)
    {
        if (updatePingPong)
            UpdatePingPong(methodData);

        if (methodData.desc.method == Method::REBLUR_DIFFUSE)
            UpdateMethod_ReblurDiffuse(methodData);
        else if (methodData.desc.method == Method::REBLUR_DIFFUSE_OCCLUSION)
            UpdateMethod_ReblurDiffuseOcclusion(methodData);
        else if (methodData.desc.method == Method::REBLUR_SPECULAR)
            UpdateMethod_ReblurSpecular(methodData);
        else if (methodData.desc.method == Method::REBLUR_SPECULAR_OCCLUSION)
            UpdateMethod_ReblurSpecularOcclusion(methodData);
        else if (methodData.desc.method == Method::REBLUR_DIFFUSE_SPECULAR)
            UpdateMethod_ReblurDiffuseSpecular(methodData);
        else if (methodData.desc.method == Method::REBLUR_DIFFUSE_SPECULAR_OCCLUSION)
            UpdateMethod_ReblurDiffuseSpecularOcclusion(methodData);
        else if (methodData.desc.method == Method::REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION)
            UpdateMethod_ReblurDiffuseDirectionalOcclusion(methodData);
        else if (methodData.desc.method == Method::SIGMA_SHADOW)
            UpdateMethod_SigmaShadow(methodData);
        else if (methodData.desc.method == Method::SIGMA_SHADOW_TRANSLUCENCY)
            UpdateMethod_SigmaShadowTranslucency(methodData);
        else if (methodData.desc.method == Method::RELAX_DIFFUSE)
            UpdateMethod_RelaxDiffuse(methodData);
        else if (methodData.desc.method == Method::RELAX_SPECULAR)
            UpdateMethod_RelaxSpecular(methodData);
        else if (methodData.desc.method == Method::RELAX_DIFFUSE_SPECULAR)
            UpdateMethod_RelaxDiffuseSpecular(methodData);
        else if (methodData.desc.method == Method::REFERENCE)
            UpdateMethod_Reference(methodData);
    }

    dispatchDescs = m_ActiveDispatches.data();
    dispatchDescNum = (uint32_t)m_ActiveDispatches.size();

    return Result::SUCCESS;
}

nrd::Result nrd::DenoiserImpl::SetMethodSettings(nrd::Method method, const void* methodSettings)
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

void nrd::DenoiserImpl::Optimize()
{
    /*
    TODO:
    - analyze dependencies and group dispatches without them
    - in case of bad Methods must verify the graph for unused passes, correctness... (at least in debug mode)
    - minimize transient pool size, maximize reuse
    */
}

void nrd::DenoiserImpl::PrepareDesc()
{
    m_Desc = {};

    m_Desc.pipelines = m_Pipelines.data();
    m_Desc.pipelineNum = (uint32_t)m_Pipelines.size();

    m_Desc.staticSamplers = g_StaticSamplers.data();
    m_Desc.staticSamplerNum = (uint32_t)g_StaticSamplers.size();

    m_Desc.permanentPool = m_PermanentPool.data();
    m_Desc.permanentPoolSize = (uint32_t)m_PermanentPool.size();

    m_Desc.transientPool = m_TransientPool.data();
    m_Desc.transientPoolSize = (uint32_t)m_TransientPool.size();

    m_Desc.constantBufferDesc.registerIndex = 0;

    for (InternalDispatchDesc& dispatchDesc : m_Dispatches)
    {
        size_t textureOffset = (size_t)dispatchDesc.resources;
        dispatchDesc.resources = &m_Resources[textureOffset];

        for (uint32_t i = 0; i < dispatchDesc.resourceNum; i++)
        {
            const Resource& resource = dispatchDesc.resources[i];
            if (resource.stateNeeded == DescriptorType::TEXTURE)
                m_Desc.descriptorSetDesc.textureMaxNum += dispatchDesc.maxRepeatNum;
            else if (resource.stateNeeded == DescriptorType::STORAGE_TEXTURE)
                m_Desc.descriptorSetDesc.storageTextureMaxNum += dispatchDesc.maxRepeatNum;
        }

        m_Desc.descriptorSetDesc.setMaxNum += dispatchDesc.maxRepeatNum;
        m_Desc.descriptorSetDesc.staticSamplerMaxNum += dispatchDesc.maxRepeatNum * m_Desc.staticSamplerNum;

        if (dispatchDesc.constantBufferDataSize != 0)
        {
            m_Desc.descriptorSetDesc.constantBufferMaxNum += dispatchDesc.maxRepeatNum;
            m_Desc.constantBufferDesc.maxDataSize = std::max(dispatchDesc.constantBufferDataSize, m_Desc.constantBufferDesc.maxDataSize);
        }
    }

    m_Desc.descriptorSetDesc.descriptorRangeMaxNumPerPipeline = 0;
    for (PipelineDesc& pipelineDesc : m_Pipelines)
    {
        size_t descriptorRangeffset = (size_t)pipelineDesc.descriptorRanges;
        pipelineDesc.descriptorRanges = &m_DescriptorRanges[descriptorRangeffset];

        m_Desc.descriptorSetDesc.descriptorRangeMaxNumPerPipeline = std::max(pipelineDesc.descriptorRangeNum, m_Desc.descriptorSetDesc.descriptorRangeMaxNumPerPipeline);
    }

    // Since now all std::vectors become "locked" (no reallocations)
}

void nrd::DenoiserImpl::AddComputeDispatchDesc(uint8_t workgroupDimX, uint8_t workgroupDimY, uint16_t downsampleFactor, uint32_t constantBufferDataSize, uint32_t maxRepeatNum, const char* shaderFileName, const nrd::ComputeShader& dxbc, const nrd::ComputeShader& dxil, const nrd::ComputeShader& spirv)
{
    // Pipeline
    size_t pipelineIndex = 0;
    for (; pipelineIndex < m_Pipelines.size(); pipelineIndex++)
    {
        const PipelineDesc& pipeline = m_Pipelines[pipelineIndex];

        if (!strcmp(pipeline.shaderFileName, shaderFileName))
            break;
    }

    if (pipelineIndex == m_Pipelines.size())
    {
        PipelineDesc pipelineDesc = {};
        pipelineDesc.shaderFileName = shaderFileName;
        pipelineDesc.shaderEntryPointName = NRD_CS_MAIN;
        pipelineDesc.computeShaderDXBC = dxbc;
        pipelineDesc.computeShaderDXIL = dxil;
        pipelineDesc.computeShaderSPIRV = spirv;
        pipelineDesc.descriptorRanges = (DescriptorRangeDesc*)m_DescriptorRanges.size();
        pipelineDesc.hasConstantData = constantBufferDataSize != 0;

        for (size_t r = 0; r < 2; r++)
        {
            DescriptorRangeDesc descriptorRange = {};
            descriptorRange.descriptorType = r == 0 ? DescriptorType::TEXTURE : DescriptorType::STORAGE_TEXTURE;

            for (size_t i = m_ResourceOffset; i < m_Resources.size(); i++ )
            {
                const Resource& resource = m_Resources[i];
                if (descriptorRange.descriptorType == resource.stateNeeded)
                    descriptorRange.descriptorNum++;
            }

            if (descriptorRange.descriptorNum != 0)
            {
                m_DescriptorRanges.push_back(descriptorRange);
                pipelineDesc.descriptorRangeNum++;
            }
        }

        m_Pipelines.push_back( pipelineDesc );
    }

    // Dispatch
    InternalDispatchDesc computeDispatchDesc = {};
    computeDispatchDesc.pipelineIndex = (uint16_t)pipelineIndex;
    computeDispatchDesc.maxRepeatNum = (uint16_t)maxRepeatNum;
    computeDispatchDesc.constantBufferDataSize = constantBufferDataSize;
    computeDispatchDesc.resourceNum = uint32_t(m_Resources.size() - m_ResourceOffset);
    computeDispatchDesc.resources = (Resource*)m_ResourceOffset;
    computeDispatchDesc.name = m_PassName;
    computeDispatchDesc.workgroupDimX = workgroupDimX;
    computeDispatchDesc.workgroupDimY = workgroupDimY;
    computeDispatchDesc.downsampleFactor = downsampleFactor;

    m_Dispatches.push_back(computeDispatchDesc);
}


void nrd::DenoiserImpl::PushTexture(nrd::DescriptorType descriptorType, uint16_t index, uint16_t mipOffset, uint16_t mipNum, uint16_t indexToSwapWith)
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

void nrd::DenoiserImpl::UpdatePingPong(const nrd::MethodData& methodData)
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

void nrd::DenoiserImpl::UpdateCommonSettings(const nrd::CommonSettings& commonSettings)
{
    // TODO: add to CommonSettings?
    m_JitterPrev = ml::float2(m_CommonSettings.cameraJitter[0], m_CommonSettings.cameraJitter[1]);
    m_ResolutionScalePrev = ml::float2(m_CommonSettings.resolutionScale[0], m_CommonSettings.resolutionScale[1]);

    memcpy(&m_CommonSettings, &commonSettings, sizeof(commonSettings));

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
        ml::float4(m_CommonSettings.worldToViewMatrix),
        ml::float4(m_CommonSettings.worldToViewMatrix + 4),
        ml::float4(m_CommonSettings.worldToViewMatrix + 8),
        ml::float4(m_CommonSettings.worldToViewMatrix + 12)
    );

    m_WorldToViewPrev = ml::float4x4
    (
        ml::float4(m_CommonSettings.worldToViewMatrixPrev),
        ml::float4(m_CommonSettings.worldToViewMatrixPrev + 4),
        ml::float4(m_CommonSettings.worldToViewMatrixPrev + 8),
        ml::float4(m_CommonSettings.worldToViewMatrixPrev + 12)
    );

    // There are many cases, where history buffers contain garbage - handle at least one of them internally
    if (m_IsFirstUse)
    {
        m_CommonSettings.accumulationMode = AccumulationMode::CLEAR_AND_RESTART;
        m_WorldToViewPrev = m_WorldToView;
        m_ViewToClipPrev = m_ViewToClip;
        m_IsFirstUse = false;
    }

    // Convert to LH
    uint32_t flags = 0;
    ml::DecomposeProjection(NDC_D3D, NDC_D3D, m_ViewToClip, &flags, nullptr, nullptr, m_Frustum.pv, nullptr, nullptr);

    if ( !(flags & ml::PROJ_LEFT_HANDED) )
    {
        m_ViewToClip.col2 = (-m_ViewToClip.GetCol2()).xmm;
        m_ViewToClipPrev.col2 = (-m_ViewToClipPrev.GetCol2()).xmm;

        m_WorldToView.Transpose();
        m_WorldToView.col2 = (-m_WorldToView.GetCol2()).xmm;
        m_WorldToView.Transpose();

        m_WorldToViewPrev.Transpose();
        m_WorldToViewPrev.col2 = (-m_WorldToViewPrev.GetCol2()).xmm;
        m_WorldToViewPrev.Transpose();
    }

    // Compute other matrices
    m_ViewToWorld = m_WorldToView;
    m_ViewToWorld.InvertOrtho();

    m_ViewToWorldPrev = m_WorldToViewPrev;
    m_ViewToWorldPrev.InvertOrtho();

    const ml::float3& cameraPosition = m_ViewToWorld.GetCol3().To3d();
    const ml::float3& cameraPositionPrev = m_ViewToWorldPrev.GetCol3().To3d();
    ml::float3 translationDelta = cameraPositionPrev - cameraPosition;

    // IMPORTANT: this part is mandatory needed to preserve precision by making matrices camera relative
    m_ViewToWorld.SetTranslation( ml::float3::Zero() );
    m_WorldToView = m_ViewToWorld;
    m_WorldToView.InvertOrtho();

    m_ViewToWorldPrev.SetTranslation( translationDelta );
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
    m_IsOrtho = (flags & ml::PROJ_ORTHO) ? -1.0f : 0.0f;

    ml::DecomposeProjection(NDC_D3D, NDC_D3D, m_ViewToClipPrev, &flags, nullptr, nullptr, m_FrustumPrev.pv, nullptr, nullptr);

    ml::float3 viewDirCurr = -ml::float3(m_ViewToWorld.GetCol2().xmm);
    ml::float3 viewDirPrev = -ml::float3(m_ViewToWorldPrev.GetCol2().xmm);
    float cosa = ml::Dot33(viewDirCurr, viewDirPrev);
    cosa = ml::Saturate(cosa / 0.9999999f);
    float angularDelta = ml::Acos(cosa) / ml::DegToRad(20.0f);

    m_CameraDelta.w = ml::Lerp(m_CameraDelta.w, angularDelta, 0.5f);
    m_CameraDelta = ml::float4(translationDelta.x, translationDelta.y, translationDelta.z, m_CameraDelta.w);
    m_ViewDirection = viewDirCurr;

    m_Timer.UpdateElapsedTimeSinceLastSave();
    m_Timer.SaveCurrentTime();

    m_TimeDelta = m_CommonSettings.timeDeltaBetweenFrames > 0.0f ? m_CommonSettings.timeDeltaBetweenFrames : m_Timer.GetSmoothedElapsedTime();
    m_FrameRateScale = ml::Max(33.333f / m_TimeDelta, 0.5f);

    float dx = ml::Abs(m_CommonSettings.cameraJitter[0] - m_JitterPrev.x);
    float dy = ml::Abs(m_CommonSettings.cameraJitter[1] - m_JitterPrev.y);
    m_JitterDelta = ml::Max(dx, dy);

    float FPS = m_FrameRateScale * 30.0f;
    float nonLinearAccumSpeed = FPS * 0.25f / (1.0f + FPS * 0.25f);
    m_CheckerboardResolveAccumSpeed = ml::Lerp(nonLinearAccumSpeed, 0.5f, m_JitterDelta);
}

// SHADERS =================================================================================

#ifdef NRD_USE_PRECOMPILED_SHADERS

    // REBLUR_DIFFUSE & REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REBLUR_Diffuse_PreBlur.cs.dxbc.h"
        #include "REBLUR_Diffuse_PreBlur.cs.dxil.h"
        #include "REBLUR_Diffuse_PreBlurAdvanced.cs.dxbc.h"
        #include "REBLUR_Diffuse_PreBlurAdvanced.cs.dxil.h"
        #include "REBLUR_Diffuse_TemporalAccumulation.cs.dxbc.h"
        #include "REBLUR_Diffuse_TemporalAccumulation.cs.dxil.h"
        #include "REBLUR_Diffuse_TemporalAccumulationWithConfidence.cs.dxbc.h"
        #include "REBLUR_Diffuse_TemporalAccumulationWithConfidence.cs.dxil.h"
        #include "REBLUR_Diffuse_AntiFirefly.cs.dxbc.h"
        #include "REBLUR_Diffuse_AntiFirefly.cs.dxil.h"
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

    #include "REBLUR_Diffuse_PreBlur.cs.spirv.h"
    #include "REBLUR_Diffuse_PreBlurAdvanced.cs.spirv.h"
    #include "REBLUR_Diffuse_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Diffuse_TemporalAccumulationWithConfidence.cs.spirv.h"
    #include "REBLUR_Diffuse_AntiFirefly.cs.spirv.h"
    #include "REBLUR_Diffuse_HistoryFix.cs.spirv.h"
    #include "REBLUR_Diffuse_Blur.cs.spirv.h"
    #include "REBLUR_Diffuse_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Diffuse_PostBlur.cs.spirv.h"
    #include "REBLUR_Diffuse_SplitScreen.cs.spirv.h"

    // REBLUR_DIFFUSE_OCCLUSION
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REBLUR_DiffuseOcclusion_TemporalAccumulation.cs.dxbc.h"
        #include "REBLUR_DiffuseOcclusion_TemporalAccumulation.cs.dxil.h"
        #include "REBLUR_DiffuseOcclusion_TemporalAccumulationWithConfidence.cs.dxbc.h"
        #include "REBLUR_DiffuseOcclusion_TemporalAccumulationWithConfidence.cs.dxil.h"
        #include "REBLUR_DiffuseOcclusion_HistoryFix.cs.dxbc.h"
        #include "REBLUR_DiffuseOcclusion_HistoryFix.cs.dxil.h"
        #include "REBLUR_DiffuseOcclusion_Blur.cs.dxbc.h"
        #include "REBLUR_DiffuseOcclusion_Blur.cs.dxil.h"
        #include "REBLUR_DiffuseOcclusion_PostBlur.cs.dxbc.h"
        #include "REBLUR_DiffuseOcclusion_PostBlur.cs.dxil.h"
        #include "REBLUR_DiffuseOcclusion_SplitScreen.cs.dxbc.h"
        #include "REBLUR_DiffuseOcclusion_SplitScreen.cs.dxil.h"
    #endif

    #include "REBLUR_DiffuseOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_DiffuseOcclusion_TemporalAccumulationWithConfidence.cs.spirv.h"
    #include "REBLUR_DiffuseOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_DiffuseOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_DiffuseOcclusion_PostBlur.cs.spirv.h"
    #include "REBLUR_DiffuseOcclusion_SplitScreen.cs.spirv.h"

    // REBLUR_SPECULAR
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REBLUR_Specular_PreBlur.cs.dxbc.h"
        #include "REBLUR_Specular_PreBlur.cs.dxil.h"
        #include "REBLUR_Specular_PreBlurAdvanced.cs.dxbc.h"
        #include "REBLUR_Specular_PreBlurAdvanced.cs.dxil.h"
        #include "REBLUR_Specular_TemporalAccumulation.cs.dxbc.h"
        #include "REBLUR_Specular_TemporalAccumulation.cs.dxil.h"
        #include "REBLUR_Specular_TemporalAccumulationWithConfidence.cs.dxbc.h"
        #include "REBLUR_Specular_TemporalAccumulationWithConfidence.cs.dxil.h"
        #include "REBLUR_Specular_AntiFirefly.cs.dxbc.h"
        #include "REBLUR_Specular_AntiFirefly.cs.dxil.h"
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
    #include "REBLUR_Specular_PreBlurAdvanced.cs.spirv.h"
    #include "REBLUR_Specular_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_Specular_TemporalAccumulationWithConfidence.cs.spirv.h"
    #include "REBLUR_Specular_AntiFirefly.cs.spirv.h"
    #include "REBLUR_Specular_HistoryFix.cs.spirv.h"
    #include "REBLUR_Specular_Blur.cs.spirv.h"
    #include "REBLUR_Specular_PostBlur.cs.spirv.h"
    #include "REBLUR_Specular_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_Specular_SplitScreen.cs.spirv.h"

    // REBLUR_SPECULAR_OCCLUSION
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REBLUR_SpecularOcclusion_TemporalAccumulation.cs.dxbc.h"
        #include "REBLUR_SpecularOcclusion_TemporalAccumulation.cs.dxil.h"
        #include "REBLUR_SpecularOcclusion_TemporalAccumulationWithConfidence.cs.dxbc.h"
        #include "REBLUR_SpecularOcclusion_TemporalAccumulationWithConfidence.cs.dxil.h"
        #include "REBLUR_SpecularOcclusion_HistoryFix.cs.dxbc.h"
        #include "REBLUR_SpecularOcclusion_HistoryFix.cs.dxil.h"
        #include "REBLUR_SpecularOcclusion_Blur.cs.dxbc.h"
        #include "REBLUR_SpecularOcclusion_Blur.cs.dxil.h"
        #include "REBLUR_SpecularOcclusion_PostBlur.cs.dxbc.h"
        #include "REBLUR_SpecularOcclusion_PostBlur.cs.dxil.h"
        #include "REBLUR_SpecularOcclusion_SplitScreen.cs.dxbc.h"
        #include "REBLUR_SpecularOcclusion_SplitScreen.cs.dxil.h"
    #endif

    #include "REBLUR_SpecularOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_SpecularOcclusion_TemporalAccumulationWithConfidence.cs.spirv.h"
    #include "REBLUR_SpecularOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_SpecularOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_SpecularOcclusion_PostBlur.cs.spirv.h"
    #include "REBLUR_SpecularOcclusion_SplitScreen.cs.spirv.h"

    // REBLUR_DIFFUSE_SPECULAR
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REBLUR_DiffuseSpecular_PreBlur.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_PreBlur.cs.dxil.h"
        #include "REBLUR_DiffuseSpecular_PreBlurAdvanced.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_PreBlurAdvanced.cs.dxil.h"
        #include "REBLUR_DiffuseSpecular_TemporalAccumulation.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_TemporalAccumulation.cs.dxil.h"
        #include "REBLUR_DiffuseSpecular_TemporalAccumulationWithConfidence.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_TemporalAccumulationWithConfidence.cs.dxil.h"
        #include "REBLUR_DiffuseSpecular_AntiFirefly.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecular_AntiFirefly.cs.dxil.h"
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
    #include "REBLUR_DiffuseSpecular_PreBlurAdvanced.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_TemporalAccumulationWithConfidence.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_AntiFirefly.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_HistoryFix.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_Blur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_TemporalStabilization.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_PostBlur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecular_SplitScreen.cs.spirv.h"

    // REBLUR_DIFFUSE_SPECULAR_OCCLUSION
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REBLUR_DiffuseSpecularOcclusion_TemporalAccumulation.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecularOcclusion_TemporalAccumulation.cs.dxil.h"
        #include "REBLUR_DiffuseSpecularOcclusion_TemporalAccumulationWithConfidence.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecularOcclusion_TemporalAccumulationWithConfidence.cs.dxil.h"
        #include "REBLUR_DiffuseSpecularOcclusion_HistoryFix.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecularOcclusion_HistoryFix.cs.dxil.h"
        #include "REBLUR_DiffuseSpecularOcclusion_Blur.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecularOcclusion_Blur.cs.dxil.h"
        #include "REBLUR_DiffuseSpecularOcclusion_PostBlur.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecularOcclusion_PostBlur.cs.dxil.h"
        #include "REBLUR_DiffuseSpecularOcclusion_SplitScreen.cs.dxbc.h"
        #include "REBLUR_DiffuseSpecularOcclusion_SplitScreen.cs.dxil.h"
    #endif

    #include "REBLUR_DiffuseSpecularOcclusion_TemporalAccumulation.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularOcclusion_TemporalAccumulationWithConfidence.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularOcclusion_HistoryFix.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularOcclusion_Blur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularOcclusion_PostBlur.cs.spirv.h"
    #include "REBLUR_DiffuseSpecularOcclusion_SplitScreen.cs.spirv.h"

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

    // RELAX_DIFFUSE
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "RELAX_Diffuse_Prepass.cs.dxbc.h"
        #include "RELAX_Diffuse_Prepass.cs.dxil.h"
        #include "RELAX_Diffuse_Reproject.cs.dxbc.h"
        #include "RELAX_Diffuse_Reproject.cs.dxil.h"
        #include "RELAX_Diffuse_DisocclusionFix.cs.dxbc.h"
        #include "RELAX_Diffuse_DisocclusionFix.cs.dxil.h"
        #include "RELAX_Diffuse_HistoryClamping.cs.dxbc.h"
        #include "RELAX_Diffuse_HistoryClamping.cs.dxil.h"
        #include "RELAX_Diffuse_Firefly.cs.dxbc.h"
        #include "RELAX_Diffuse_Firefly.cs.dxil.h"
        #include "RELAX_Diffuse_SpatialVarianceEstimation.cs.dxbc.h"
        #include "RELAX_Diffuse_SpatialVarianceEstimation.cs.dxil.h"
        #include "RELAX_Diffuse_ATrousShmem.cs.dxbc.h"
        #include "RELAX_Diffuse_ATrousShmem.cs.dxil.h"
        #include "RELAX_Diffuse_ATrousStandard.cs.dxbc.h"
        #include "RELAX_Diffuse_ATrousStandard.cs.dxil.h"
        #include "RELAX_Diffuse_SplitScreen.cs.dxbc.h"
        #include "RELAX_Diffuse_SplitScreen.cs.dxil.h"
    #endif

    #include "RELAX_Diffuse_Prepass.cs.spirv.h"
    #include "RELAX_Diffuse_Reproject.cs.spirv.h"
    #include "RELAX_Diffuse_DisocclusionFix.cs.spirv.h"
    #include "RELAX_Diffuse_HistoryClamping.cs.spirv.h"
    #include "RELAX_Diffuse_Firefly.cs.spirv.h"
    #include "RELAX_Diffuse_SpatialVarianceEstimation.cs.spirv.h"
    #include "RELAX_Diffuse_ATrousShmem.cs.spirv.h"
    #include "RELAX_Diffuse_ATrousStandard.cs.spirv.h"
    #include "RELAX_Diffuse_SplitScreen.cs.spirv.h"

    // RELAX_SPECULAR
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "RELAX_Specular_Prepass.cs.dxbc.h"
        #include "RELAX_Specular_Prepass.cs.dxil.h"
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

    #include "RELAX_Specular_Prepass.cs.spirv.h"
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
        #include "RELAX_DiffuseSpecular_Prepass.cs.dxbc.h"
        #include "RELAX_DiffuseSpecular_Prepass.cs.dxil.h"
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

    #include "RELAX_DiffuseSpecular_Prepass.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_Reproject.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_DisocclusionFix.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_HistoryClamping.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_Firefly.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_SpatialVarianceEstimation.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_ATrousShmem.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_ATrousStandard.cs.spirv.h"
    #include "RELAX_DiffuseSpecular_SplitScreen.cs.spirv.h"

    // REFERENCE
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REFERENCE_Accumulate.cs.dxbc.h"
        #include "REFERENCE_Accumulate.cs.dxil.h"
        #include "REFERENCE_SplitScreen.cs.dxbc.h"
        #include "REFERENCE_SplitScreen.cs.dxil.h"
    #endif

    #include "REFERENCE_Accumulate.cs.spirv.h"
    #include "REFERENCE_SplitScreen.cs.spirv.h"
#endif

// METHODS =================================================================================

#include "Methods/Reblur_Diffuse.hpp"
#include "Methods/Reblur_DiffuseOcclusion.hpp"
#include "Methods/Reblur_Specular.hpp"
#include "Methods/Reblur_SpecularOcclusion.hpp"
#include "Methods/Reblur_DiffuseSpecular.hpp"
#include "Methods/Reblur_DiffuseSpecularOcclusion.hpp"
#include "Methods/Reblur_DiffuseDirectionalOcclusion.hpp"
#include "Methods/Sigma_Shadow.hpp"
#include "Methods/Sigma_ShadowTranslucency.hpp"
#include "Methods/Relax_Diffuse.hpp"
#include "Methods/Relax_Specular.hpp"
#include "Methods/Relax_DiffuseSpecular.hpp"
#include "Methods/Reference.hpp"
