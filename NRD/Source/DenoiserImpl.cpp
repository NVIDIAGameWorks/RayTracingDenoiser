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

/*
TODO:
- UpdatePingPong - swap only if frameIndex has changed!
- add Settings { ShaderSettings, MethodSettings }
*/

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

        const uint32_t w = methodDesc.fullResolutionWidth;
        const uint32_t h = methodDesc.fullResolutionHeight;

        uint32_t j = 0;
        for (; j < libraryDesc.supportedMethodNum; j++)
        {
            if (methodDesc.method == libraryDesc.supportedMethods[j])
                break;
        }
        if (j == libraryDesc.supportedMethodNum)
            return Result::INVALID_ARGUMENT;

        m_PermanentPoolOffset = (uint32_t)m_PermanentPool.size();
        m_TransientPoolOffset = (uint32_t)m_TransientPool.size();

        MethodData methodData = {};
        methodData.desc = methodDesc;
        methodData.dispatchOffset = m_Dispatches.size();
        methodData.constantOffset = m_Constants.size();
        methodData.textureOffset = m_Resources.size();
        methodData.pingPongOffset = m_PingPongs.size();

        if (methodDesc.method == Method::DIFFUSE)
            methodData.settingsSize = AddMethod_Diffuse(w, h);
        else if (methodDesc.method == Method::SPECULAR)
            methodData.settingsSize = AddMethod_Specular(w, h);
        else if (methodDesc.method == Method::SHADOW)
            methodData.settingsSize = AddMethod_Shadow(w, h);

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
            UpdatePingPong(methodData);

        if (methodData.desc.method == Method::DIFFUSE)
            UpdateMethod_Diffuse(methodData);
        else if (methodData.desc.method == Method::SPECULAR)
            UpdateMethod_Specular(methodData);
        else if (methodData.desc.method == Method::SHADOW)
            UpdateMethod_Shadow(methodData);
    }

    for (const auto& dispatchIndex : m_ActiveDispatchIndices)
        m_ActiveDispatches.push_back( m_Dispatches[dispatchIndex] );

    #if 0
        static const char* names[] =
        {
            "IN_MOTION_VECTOR ",
            "IN_NORMAL_ROUGHNESS ",
            "IN_VIEWZ ",
            "IN_SHADOW ",
            "IN_DIFF_A ",
            "IN_DIFF_B ",
            "IN_SPEC_HIT ",

            "OUT_SHADOW ",
            "OUT_DIFF_A ",
            "OUT_DIFF_B ",
            "OUT_SPEC_HIT ",
        };

        char s[128];
        char t[32];

        for (const auto& dispatchIndex : m_ActiveDispatchIndices)
        {
            const DispatchDesc& d = m_Dispatches[dispatchIndex];

            sprintf_s(s, "Pipeline #%u (%s)\n", d.pipelineIndex, d.name);
            OutputDebugStringA(s);

            strcpy_s(s, "\t");
            for( uint32_t i = 0; i < d.resourceNum; i++ )
            {
                const Resource& r = d.resources[i];

                if( r.type == nrd::ResourceType::PERMANENT_POOL )
                    sprintf_s(t, "P(%u) ", r.indexInPool);
                else if( r.type == nrd::ResourceType::TRANSIENT_POOL )
                    sprintf_s(t, "T(%u) ", r.indexInPool);
                else
                    sprintf_s(t, names[(uint32_t)r.type]);

                strcat_s(s, t);
            }
            strcat_s(s, "\n");
            OutputDebugStringA(s);
        }

        __debugbreak();
    #endif

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


void DenoiserImpl::PushTexture(DescriptorType descriptorType, uint32_t index, uint16_t mipOffset, uint16_t mipNum, uint32_t indexToSwapWith)
{
    ResourceType resourceType = (ResourceType)index;

    if (index >= TRANSIENT_POOL_START)
    {
        resourceType = ResourceType::TRANSIENT_POOL;
        index += m_TransientPoolOffset - TRANSIENT_POOL_START;

        if (indexToSwapWith != uint32_t(-1))
        {
            indexToSwapWith += m_TransientPoolOffset - TRANSIENT_POOL_START;
            m_PingPongs.push_back( {m_Resources.size(), indexToSwapWith} );
        }
    }
    else if (index >= PERMANENT_POOL_START)
    {
        resourceType = ResourceType::PERMANENT_POOL;
        index += m_PermanentPoolOffset - PERMANENT_POOL_START;

        if (indexToSwapWith != uint32_t(-1))
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

        uint32_t t = pingPong.indexInPoolToSwapWith;
        pingPong.indexInPoolToSwapWith = resource.indexInPool;
        resource.indexInPool = t;
    }
}

void DenoiserImpl::UpdateCommonSettings(const CommonSettings& commonSettings)
{
    memcpy(&m_CommonSettings, &commonSettings, sizeof(commonSettings));

    float whiteNoise = Rand::uf1();

    float blueNoise = (float)ReverseBits4(m_CommonSettings.frameIndex);
    blueNoise += whiteNoise;
    blueNoise *= 1.0f / 16.0f;

    m_WhiteNoiseSinCos.x = Sin(whiteNoise * DegToRad(360.0f));
    m_WhiteNoiseSinCos.y = Cos(whiteNoise * DegToRad(360.0f));

    m_BlueNoiseSinCos.x = Sin(blueNoise * DegToRad(360.0f));
    m_BlueNoiseSinCos.y = Cos(blueNoise * DegToRad(360.0f));

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

    // TODO: this part is mandatory needed to preserve precision by making matrices camera relative
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

    m_Project = project[0];
    m_CommonSettings.denoisingRange = Min(m_CommonSettings.denoisingRange, Abs(settings[PROJ_ZFAR]) * 0.99f);
    m_IsOrtho = ( flags & PROJ_ORTHO ) == 0 ? 0.0f : ( ( flags & PROJ_LEFT_HANDED ) ? 1.0f : -1.0f );

    DecomposeProjection(NDC_D3D, NDC_D3D, m_ViewToClipPrev, &flags, nullptr, nullptr, m_FrustumPrev.pv, nullptr, nullptr);
    m_IsOrthoPrev = ( flags & PROJ_ORTHO ) == 0 ? 0.0f : ( ( flags & PROJ_LEFT_HANDED ) ? 1.0f : -1.0f );

    m_Timer.UpdateElapsedTimeSinceLastSave();
    m_Timer.SaveCurrentTime();
}

// DXBC
#ifndef BYTE
    #define BYTE unsigned char
#endif

#include "..\..\_Build\Shaders\NRD_Diffuse_PreBlur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalAccumulation.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_Mips.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_HistoryFix.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_Blur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalStabilization.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_PostBlur.cs.dxbc.h"

#include "..\..\_Build\Shaders\NRD_Specular_PreBlur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalAccumulation.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_Mips.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_HistoryFix.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_Blur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalStabilization.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Specular_PostBlur.cs.dxbc.h"

#include "..\..\_Build\Shaders\NRD_Shadow_PreBlur.cs.dxbc.h"
#include "..\..\_Build\Shaders\NRD_Shadow_Blur.cs.dxbc.h"

// DXIL
#include "..\..\_Build\Shaders\NRD_Diffuse_PreBlur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalAccumulation.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_Mips.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_HistoryFix.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_Blur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalStabilization.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_PostBlur.cs.dxil.h"

#include "..\..\_Build\Shaders\NRD_Specular_PreBlur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalAccumulation.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_Mips.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_HistoryFix.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_Blur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalStabilization.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Specular_PostBlur.cs.dxil.h"

#include "..\..\_Build\Shaders\NRD_Shadow_PreBlur.cs.dxil.h"
#include "..\..\_Build\Shaders\NRD_Shadow_Blur.cs.dxil.h"

// SPIRV
#include "..\..\_Build\Shaders\NRD_Diffuse_PreBlur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalAccumulation.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_Mips.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_HistoryFix.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_Blur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_TemporalStabilization.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Diffuse_PostBlur.cs.spirv.h"

#include "..\..\_Build\Shaders\NRD_Specular_PreBlur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalAccumulation.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_Mips.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_HistoryFix.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_Blur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_TemporalStabilization.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Specular_PostBlur.cs.spirv.h"

#include "..\..\_Build\Shaders\NRD_Shadow_PreBlur.cs.spirv.h"
#include "..\..\_Build\Shaders\NRD_Shadow_Blur.cs.spirv.h"

// METHODS
#include "Methods/Diffuse.hpp"
#include "Methods/Specular.hpp"
#include "Methods/Shadow.hpp"

