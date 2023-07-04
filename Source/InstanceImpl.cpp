/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "../Shaders/Include/NRD.hlsli"
#include "InstanceImpl.h"

#include <array>

constexpr std::array<nrd::Sampler, (size_t)nrd::Sampler::MAX_NUM> g_Samplers =
{
    nrd::Sampler::NEAREST_CLAMP,
    nrd::Sampler::NEAREST_MIRRORED_REPEAT,
    nrd::Sampler::LINEAR_CLAMP,
    nrd::Sampler::LINEAR_MIRRORED_REPEAT,
};

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

#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "Clear_f.cs.dxbc.h"
    #include "Clear_ui.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "Clear_f.cs.dxil.h"
    #include "Clear_ui.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "Clear_f.cs.spirv.h"
    #include "Clear_ui.cs.spirv.h"
#endif

inline bool IsInList(nrd::Identifier identifier, const nrd::Identifier* identifiers, uint32_t identifiersNum)
{
    for (uint32_t i = 0; i < identifiersNum; i++)
    {
        if (identifiers[i] == identifier)
            return true;
    }

    return false;
}

nrd::Result nrd::InstanceImpl::Create(const InstanceCreationDesc& instanceCreationDesc)
{
    const LibraryDesc& libraryDesc = GetLibraryDesc();

    // Collect dispatches from all denoisers
    for (uint32_t i = 0; i < instanceCreationDesc.denoisersNum; i++)
    {
        const DenoiserDesc& denoiserDesc = instanceCreationDesc.denoisers[i];

        // Check that denoiser is supported
        uint32_t j = 0;
        for (; j < libraryDesc.supportedDenoisersNum; j++)
        {
            if (denoiserDesc.denoiser == libraryDesc.supportedDenoisers[j])
                break;
        }
        if (j == libraryDesc.supportedDenoisersNum)
            return Result::UNSUPPORTED;

        // Check that identifier is unique
        for (j = 0; j < instanceCreationDesc.denoisersNum; j++)
        {
            if (i != j && instanceCreationDesc.denoisers[j].identifier == denoiserDesc.identifier)
                return Result::NON_UNIQUE_IDENTIFIER;
        }

        // Append dispatches for the current denoiser
        m_PermanentPoolOffset = (uint16_t)m_PermanentPool.size();
        m_TransientPoolOffset = (uint16_t)m_TransientPool.size();

        m_IndexRemap.clear();

        DenoiserData denoiserData = {};
        denoiserData.desc = denoiserDesc;
        denoiserData.dispatchOffset = m_Dispatches.size();
        denoiserData.pingPongOffset = m_PingPongs.size();

        size_t resourceOffset = m_Resources.size();

        if (denoiserDesc.denoiser == Denoiser::REBLUR_DIFFUSE)
            Add_ReblurDiffuse(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::REBLUR_DIFFUSE_OCCLUSION)
            Add_ReblurDiffuseOcclusion(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::REBLUR_DIFFUSE_SH)
            Add_ReblurDiffuseSh(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::REBLUR_SPECULAR)
            Add_ReblurSpecular(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::REBLUR_SPECULAR_OCCLUSION)
            Add_ReblurSpecularOcclusion(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::REBLUR_SPECULAR_SH)
            Add_ReblurSpecularSh(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::REBLUR_DIFFUSE_SPECULAR)
            Add_ReblurDiffuseSpecular(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::REBLUR_DIFFUSE_SPECULAR_OCCLUSION)
            Add_ReblurDiffuseSpecularOcclusion(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::REBLUR_DIFFUSE_SPECULAR_SH)
            Add_ReblurDiffuseSpecularSh(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION)
            Add_ReblurDiffuseDirectionalOcclusion(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::SIGMA_SHADOW)
            Add_SigmaShadow(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::SIGMA_SHADOW_TRANSLUCENCY)
            Add_SigmaShadowTranslucency(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::RELAX_DIFFUSE)
            Add_RelaxDiffuse(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::RELAX_DIFFUSE_SH)
            Add_RelaxDiffuseSh(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::RELAX_SPECULAR)
            Add_RelaxSpecular(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::RELAX_SPECULAR_SH)
            Add_RelaxSpecularSh(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::RELAX_DIFFUSE_SPECULAR)
            Add_RelaxDiffuseSpecular(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::RELAX_DIFFUSE_SPECULAR_SH)
            Add_RelaxDiffuseSpecularSh(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::REFERENCE)
            Add_Reference(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::SPECULAR_REFLECTION_MV)
            Add_SpecularReflectionMv(denoiserData);
        else if (denoiserDesc.denoiser == Denoiser::SPECULAR_DELTA_MV)
            Add_SpecularDeltaMv(denoiserData);
        else // Should not be here
            return Result::INVALID_ARGUMENT;

        denoiserData.pingPongNum = m_PingPongs.size() - denoiserData.pingPongOffset;

        // Gather resources, which need to be cleared
        for (size_t resourceIndex = resourceOffset; resourceIndex < m_Resources.size(); resourceIndex++)
        {
            const ResourceDesc& resource = m_Resources[resourceIndex];

            // Loop through all resources and find all used as STORAGE (i.e. ignore read-only user provided inputs)
            if (resource.stateNeeded != DescriptorType::STORAGE_TEXTURE)
                continue;

            // Keep only unique instances
            bool isFound = false;
            for (const ClearResource& temp : m_ClearResources)
            {
                if (temp.resource.stateNeeded == resource.stateNeeded &&
                    temp.resource.type == resource.type &&
                    temp.resource.indexInPool == resource.indexInPool &&
                    temp.resource.mipOffset == resource.mipOffset &&
                    temp.resource.mipNum == resource.mipNum)
                {
                    isFound = true;
                    break;
                }
            }

            // Skip "OUT_VALIDATION" resource because it can be not provided
            if (resource.type == ResourceType::OUT_VALIDATION)
                isFound = true;

            if (!isFound)
            {
                // Texture props
                uint32_t w = denoiserDesc.renderWidth;
                uint32_t h = denoiserDesc.renderHeight;
                bool isInteger = false;

                if (resource.type == ResourceType::PERMANENT_POOL || resource.type == ResourceType::TRANSIENT_POOL)
                {
                    TextureDesc& textureDesc = resource.type == ResourceType::PERMANENT_POOL ? m_PermanentPool[resource.indexInPool] : m_TransientPool[resource.indexInPool];

                    w = textureDesc.width >> resource.mipOffset;
                    h = textureDesc.height >> resource.mipOffset;
                    isInteger = g_IsIntegerFormat[(size_t)textureDesc.format];
                }

                // Add PING resource
                m_ClearResources.push_back( {denoiserDesc.identifier, resource, w, h, isInteger} );

                // Add PONG resource
                for (uint32_t p = 0; p < denoiserData.pingPongNum; p++)
                {
                    const PingPong& pingPong = m_PingPongs[denoiserData.pingPongOffset + p];
                    if (pingPong.resourceIndex == (uint32_t)resourceIndex)
                    {
                        ResourceDesc resourcePong = {resource.stateNeeded, resource.type, pingPong.indexInPoolToSwapWith, resource.mipOffset, resource.mipNum};
                        m_ClearResources.push_back( {denoiserDesc.identifier, resourcePong, w, h, isInteger} );
                        break;
                    }
                }
            }
        }

        m_DenoiserData.push_back(denoiserData);
    }

    // Add "clear" dispatches
    m_DispatchClearIndex[0] = m_Dispatches.size();
    _PushPass("Clear (f)");
    {
        PushOutput(0, 0, 1);
        AddDispatch( Clear_f, 0, NumThreads(16, 16), 1 );
    }

    m_DispatchClearIndex[1] = m_Dispatches.size();
    _PushPass("Clear (ui)");
    {
        PushOutput(0, 0, 1);
        AddDispatch( Clear_ui, 0, NumThreads(16, 16), 1 );
    }

    PrepareDesc();

    // IMPORTANT: since now all std::vectors become "locked" (no reallocations)

    return Result::SUCCESS;
}

nrd::Result nrd::InstanceImpl::SetCommonSettings(const CommonSettings& commonSettings)
{
    // TODO: add a lot of verifications of fields in CommonSettings
    memcpy(&m_CommonSettings, &commonSettings, sizeof(commonSettings));

    // Rotators
    ml::float4 rndScale = ml::float4(1.0f) + ml::Rand::sf4(&m_FastRandState) * 0.25f;
    ml::float4 rndAngle = ml::Rand::uf4(&m_FastRandState) * ml::DegToRad(360.0f);
    rndAngle.w = ml::DegToRad( 120.0f * float(m_CommonSettings.frameIndex % 3) );

    float ca = ml::Cos( rndAngle.x );
    float sa = ml::Sin( rndAngle.x );
    m_Rotator_PrePass = ml::float4( ca, sa, -sa, ca ) * rndScale.x;

    ca = ml::Cos( rndAngle.y );
    sa = ml::Sin( rndAngle.y );
    m_Rotator_Blur = ml::float4( ca, sa, -sa, ca ) * rndScale.y;

    ca = ml::Cos( rndAngle.z );
    sa = ml::Sin( rndAngle.z );
    m_Rotator_PostBlur = ml::float4( ca, sa, -sa, ca ) * rndScale.z;

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

    m_WorldPrevToWorld = ml::float4x4
    (
        ml::float4(m_CommonSettings.worldPrevToWorldMatrix),
        ml::float4(m_CommonSettings.worldPrevToWorldMatrix + 4),
        ml::float4(m_CommonSettings.worldPrevToWorldMatrix + 8),
        ml::float4(m_CommonSettings.worldPrevToWorldMatrix + 12)
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

    m_ViewDirection = -ml::float3(m_ViewToWorld.GetCol2().xmm);
    m_ViewDirectionPrev = -ml::float3(m_ViewToWorldPrev.GetCol2().xmm);

    m_CameraDelta = ml::float3(translationDelta.x, translationDelta.y, translationDelta.z);

    m_Timer.UpdateElapsedTimeSinceLastSave();
    m_Timer.SaveCurrentTime();

    m_TimeDelta = m_CommonSettings.timeDeltaBetweenFrames > 0.0f ? m_CommonSettings.timeDeltaBetweenFrames : m_Timer.GetSmoothedElapsedTime();
    m_FrameRateScale = ml::Max(33.333f / m_TimeDelta, 0.5f);

    float dx = ml::Abs(m_CommonSettings.cameraJitter[0] - m_CommonSettings.cameraJitterPrev[0]);
    float dy = ml::Abs(m_CommonSettings.cameraJitter[1] - m_CommonSettings.cameraJitterPrev[1]);
    m_JitterDelta = ml::Max(dx, dy);

    float FPS = m_FrameRateScale * 30.0f;
    float nonLinearAccumSpeed = FPS * 0.25f / (1.0f + FPS * 0.25f);
    m_CheckerboardResolveAccumSpeed = ml::Lerp(nonLinearAccumSpeed, 0.5f, m_JitterDelta);

    return Result::SUCCESS;
}

nrd::Result nrd::InstanceImpl::SetDenoiserSettings(Identifier identifier, const void* denoiserSettings)
{
    for( DenoiserData& denoiserData : m_DenoiserData )
    {
        if (denoiserData.desc.identifier == identifier)
        {
            memcpy(&denoiserData.settings, denoiserSettings, denoiserData.settingsSize);

            return Result::SUCCESS;
        }
    }

    return Result::INVALID_ARGUMENT;
}

nrd::Result nrd::InstanceImpl::GetComputeDispatches(const Identifier* identifiers, uint32_t identifiersNum, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescsNum)
{
    m_ActiveDispatches.clear();

    // Trivial checks
    if (!identifiers || !identifiersNum)
    {
        dispatchDescs = nullptr;
        dispatchDescsNum = 0;

        return !identifiersNum ? Result::SUCCESS : Result::INVALID_ARGUMENT;
    }

    // Inject "clear" calls if needed
    if (m_CommonSettings.accumulationMode == AccumulationMode::CLEAR_AND_RESTART)
    {
        for (const ClearResource& clearResource : m_ClearResources)
        {
            // If current denoiser is in list
            if (!IsInList(clearResource.identifier, identifiers, identifiersNum))
                continue;

            // Add a clear dispatch
            const InternalDispatchDesc& internalDispatchDesc = m_Dispatches[ m_DispatchClearIndex[clearResource.isInteger ? 1 : 0] ];

            DispatchDesc dispatchDesc = {};
            dispatchDesc.resourcesNum = 1;
            dispatchDesc.name = internalDispatchDesc.name;
            dispatchDesc.resources = &clearResource.resource;
            dispatchDesc.pipelineIndex = internalDispatchDesc.pipelineIndex;
            dispatchDesc.gridWidth = DivideUp(clearResource.w, internalDispatchDesc.numThreads.width);
            dispatchDesc.gridHeight = DivideUp(clearResource.h, internalDispatchDesc.numThreads.height);

            m_ActiveDispatches.push_back(dispatchDesc);
        }
    }

    // Collect dispatches for requested denoisers
    for (const DenoiserData& denoiserData : m_DenoiserData)
    {
        // If current denoiser is in list
        if (!IsInList(denoiserData.desc.identifier, identifiers, identifiersNum))
            continue;

        // Update denoiser and gather dispatches
        UpdatePingPong(denoiserData);

        if( denoiserData.desc.denoiser == Denoiser::REBLUR_DIFFUSE || denoiserData.desc.denoiser == Denoiser::REBLUR_DIFFUSE_SH ||
            denoiserData.desc.denoiser == Denoiser::REBLUR_SPECULAR || denoiserData.desc.denoiser == Denoiser::REBLUR_SPECULAR_SH ||
            denoiserData.desc.denoiser == Denoiser::REBLUR_DIFFUSE_SPECULAR || denoiserData.desc.denoiser == Denoiser::REBLUR_DIFFUSE_SPECULAR_SH ||
            denoiserData.desc.denoiser == Denoiser::REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION )
            Update_Reblur(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::REBLUR_DIFFUSE_OCCLUSION ||
            denoiserData.desc.denoiser == Denoiser::REBLUR_SPECULAR_OCCLUSION ||
            denoiserData.desc.denoiser == Denoiser::REBLUR_DIFFUSE_SPECULAR_OCCLUSION )
            Update_ReblurOcclusion(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::SIGMA_SHADOW || denoiserData.desc.denoiser == Denoiser::SIGMA_SHADOW_TRANSLUCENCY)
            Update_SigmaShadow(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::RELAX_DIFFUSE)
            Update_RelaxDiffuse(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::RELAX_DIFFUSE_SH)
            Update_RelaxDiffuseSh(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::RELAX_SPECULAR)
            Update_RelaxSpecular(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::RELAX_SPECULAR_SH)
            Update_RelaxSpecularSh(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::RELAX_DIFFUSE_SPECULAR)
            Update_RelaxDiffuseSpecular(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::RELAX_DIFFUSE_SPECULAR_SH)
            Update_RelaxDiffuseSpecularSh(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::REFERENCE)
            Update_Reference(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::SPECULAR_REFLECTION_MV)
            Update_SpecularReflectionMv(denoiserData);
        else if (denoiserData.desc.denoiser == Denoiser::SPECULAR_DELTA_MV)
            Update_SpecularDeltaMv(denoiserData);
    }

    dispatchDescs = m_ActiveDispatches.data();
    dispatchDescsNum = (uint32_t)m_ActiveDispatches.size();

    return dispatchDescsNum ? Result::SUCCESS : Result::INVALID_ARGUMENT;
}

void nrd::InstanceImpl::AddComputeDispatchDesc
(
    NumThreads numThreads,
    uint16_t downsampleFactor,
    uint32_t constantBufferDataSize,
    uint32_t maxRepeatNum,
    const char* shaderFileName,
    const ComputeShaderDesc& dxbc,
    const ComputeShaderDesc& dxil,
    const ComputeShaderDesc& spirv
)
{
    // Pipeline (unique only)
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
        pipelineDesc.shaderEntryPointName = NRD_STRINGIFY(NRD_CS_MAIN);
        pipelineDesc.computeShaderDXBC = dxbc;
        pipelineDesc.computeShaderDXIL = dxil;
        pipelineDesc.computeShaderSPIRV = spirv;
        pipelineDesc.resourceRanges = (ResourceRangeDesc*)m_ResourceRanges.size();
        pipelineDesc.hasConstantData = constantBufferDataSize != 0;

        for (size_t r = 0; r < 2; r++)
        {
            ResourceRangeDesc descriptorRange = {};
            descriptorRange.descriptorType = r == 0 ? DescriptorType::TEXTURE : DescriptorType::STORAGE_TEXTURE;

            for (size_t i = m_ResourceOffset; i < m_Resources.size(); i++ )
            {
                const ResourceDesc& resource = m_Resources[i];
                if (descriptorRange.descriptorType == resource.stateNeeded)
                    descriptorRange.descriptorsNum++;
            }

            if (descriptorRange.descriptorsNum != 0)
            {
                m_ResourceRanges.push_back(descriptorRange);
                pipelineDesc.resourceRangesNum++;
            }
        }

        m_Pipelines.push_back( pipelineDesc );
    }

    // Dispatch
    InternalDispatchDesc computeDispatchDesc = {};
    computeDispatchDesc.name = m_PassName;
    computeDispatchDesc.pipelineIndex = (uint16_t)pipelineIndex;
    computeDispatchDesc.downsampleFactor = downsampleFactor;
    computeDispatchDesc.maxRepeatsNum = (uint16_t)maxRepeatNum;
    computeDispatchDesc.constantBufferDataSize = constantBufferDataSize;
    computeDispatchDesc.resourcesNum = uint32_t(m_Resources.size() - m_ResourceOffset);
    computeDispatchDesc.resources = (ResourceDesc*)m_ResourceOffset;
    computeDispatchDesc.numThreads = numThreads;

    m_Dispatches.push_back(computeDispatchDesc);
}

void nrd::InstanceImpl::PrepareDesc()
{
    m_Desc = {};

    m_Desc.constantBufferRegisterIndex = 0;
    m_Desc.constantBufferSpaceIndex = NRD_CONSTANT_BUFFER_SPACE_INDEX;

    m_Desc.samplers = g_Samplers.data();
    m_Desc.samplersNum = (uint32_t)g_Samplers.size();
    m_Desc.samplersSpaceIndex = NRD_SAMPLERS_SPACE_INDEX;
    m_Desc.samplersBaseRegisterIndex = 0;

    m_Desc.pipelines = m_Pipelines.data();
    m_Desc.pipelinesNum = (uint32_t)m_Pipelines.size();
    m_Desc.resourcesSpaceIndex = NRD_RESOURCES_SPACE_INDEX;

    m_Desc.permanentPool = m_PermanentPool.data();
    m_Desc.permanentPoolSize = (uint32_t)m_PermanentPool.size();

    m_Desc.transientPool = m_TransientPool.data();
    m_Desc.transientPoolSize = (uint32_t)m_TransientPool.size();

    // Calculate descriptor heap (sets) requirements
    for (InternalDispatchDesc& dispatchDesc : m_Dispatches)
    {
        size_t textureOffset = (size_t)dispatchDesc.resources;
        dispatchDesc.resources = &m_Resources[textureOffset];

        for (uint32_t i = 0; i < dispatchDesc.resourcesNum; i++)
        {
            const ResourceDesc& resource = dispatchDesc.resources[i];
            if (resource.stateNeeded == DescriptorType::TEXTURE)
                m_Desc.descriptorPoolDesc.texturesMaxNum += dispatchDesc.maxRepeatsNum;
            else if (resource.stateNeeded == DescriptorType::STORAGE_TEXTURE)
                m_Desc.descriptorPoolDesc.storageTexturesMaxNum += dispatchDesc.maxRepeatsNum;
        }

        m_Desc.descriptorPoolDesc.setsMaxNum += dispatchDesc.maxRepeatsNum;
        m_Desc.descriptorPoolDesc.samplersMaxNum += dispatchDesc.maxRepeatsNum * m_Desc.samplersNum;

        if (dispatchDesc.constantBufferDataSize != 0)
        {
            m_Desc.descriptorPoolDesc.constantBuffersMaxNum += dispatchDesc.maxRepeatsNum;
            m_Desc.constantBufferMaxDataSize = std::max(dispatchDesc.constantBufferDataSize, m_Desc.constantBufferMaxDataSize);
        }
    }

    // For potential clears
    uint32_t clearNum = (uint32_t)m_ClearResources.size();
    m_Desc.descriptorPoolDesc.storageTexturesMaxNum += clearNum;
    m_Desc.descriptorPoolDesc.setsMaxNum += clearNum;
    m_Desc.descriptorPoolDesc.samplersMaxNum += clearNum * m_Desc.samplersNum;

    // Assign resources
    for (PipelineDesc& pipelineDesc : m_Pipelines)
    {
        size_t descriptorRangeffset = (size_t)pipelineDesc.resourceRanges;
        pipelineDesc.resourceRanges = &m_ResourceRanges[descriptorRangeffset];
    }

    // *= number of "spaces"
    uint32_t descriptorSetNum = 1;
    if (m_Desc.constantBufferSpaceIndex != m_Desc.samplersSpaceIndex)
        descriptorSetNum++;
    if (m_Desc.samplersSpaceIndex != m_Desc.resourcesSpaceIndex)
        descriptorSetNum++;

    m_Desc.descriptorPoolDesc.setsMaxNum *= descriptorSetNum;
}

void nrd::InstanceImpl::UpdatePingPong(const DenoiserData& denoiserData)
{
    for (uint32_t i = 0; i < denoiserData.pingPongNum; i++)
    {
        PingPong& pingPong = m_PingPongs[denoiserData.pingPongOffset + i];
        ResourceDesc& resource = m_Resources[pingPong.resourceIndex];

        ml::Swap(resource.indexInPool, pingPong.indexInPoolToSwapWith);
    }
}

void nrd::InstanceImpl::PushTexture(DescriptorType descriptorType, uint16_t localIndex, uint16_t mipOffset, uint16_t mipNum, uint16_t indexToSwapWith)
{
    ResourceType resourceType = (ResourceType)localIndex;
    uint16_t globalIndex = 0;

    if (localIndex >= TRANSIENT_POOL_START)
    {
        resourceType = ResourceType::TRANSIENT_POOL;
        globalIndex = m_IndexRemap[localIndex - TRANSIENT_POOL_START];

        if (indexToSwapWith != uint16_t(-1))
        {
            assert(indexToSwapWith >= TRANSIENT_POOL_START && indexToSwapWith < PERMANENT_POOL_START);

            indexToSwapWith = m_IndexRemap[indexToSwapWith - TRANSIENT_POOL_START];
            m_PingPongs.push_back( {m_Resources.size(), indexToSwapWith} );
        }
    }
    else if (localIndex >= PERMANENT_POOL_START)
    {
        resourceType = ResourceType::PERMANENT_POOL;
        globalIndex = m_PermanentPoolOffset + localIndex - PERMANENT_POOL_START;

        if (indexToSwapWith != uint16_t(-1))
        {
            assert(indexToSwapWith >= PERMANENT_POOL_START);

            indexToSwapWith = m_PermanentPoolOffset + indexToSwapWith - PERMANENT_POOL_START;
            m_PingPongs.push_back( {m_Resources.size(), indexToSwapWith} );
        }
    }

    m_Resources.push_back( {descriptorType, resourceType, globalIndex, mipOffset, mipNum} );
}

void nrd::InstanceImpl::AddTextureToTransientPool(const TextureDesc& textureDesc)
{
    // Try to find a replacement from previous denoisers
    for (uint16_t i = 0; i < m_TransientPoolOffset; i++)
    {
        // Format and dimensions must match
        const TextureDesc& t = m_TransientPool[i];
        if (t.format == textureDesc.format && t.width == textureDesc.width && t.height == textureDesc.height && t.mipNum == textureDesc.mipNum)
        {
            // The candidate must not be already in use in the current denoiser
            size_t j = 0;
            for (; j < m_IndexRemap.size(); j++)
            {
                if (m_IndexRemap[j] == i)
                    break;
            }

            // A replacement is found - reuse memory
            if (j == m_IndexRemap.size())
            {
                m_IndexRemap.push_back(i);

                return;
            }
        }
    }

    // A replacement is not found - add memory
    m_IndexRemap.push_back( (uint16_t)m_TransientPool.size() );
    m_TransientPool.push_back(textureDesc);
}

nrd::Constant* nrd::InstanceImpl::PushDispatch(const DenoiserData& denoiserData, uint32_t localIndex)
{
    size_t dispatchIndex = denoiserData.dispatchOffset + localIndex;
    const InternalDispatchDesc& internalDispatchDesc = m_Dispatches[dispatchIndex];

    // Copy data
    DispatchDesc dispatchDesc = {};
    dispatchDesc.name = internalDispatchDesc.name;
    dispatchDesc.resources = internalDispatchDesc.resources;
    dispatchDesc.resourcesNum = internalDispatchDesc.resourcesNum;
    dispatchDesc.pipelineIndex = internalDispatchDesc.pipelineIndex;

    // Update constant data
    if (m_ConstantDataOffset + internalDispatchDesc.constantBufferDataSize > CONSTANT_DATA_SIZE)
        m_ConstantDataOffset = 0;
    dispatchDesc.constantBufferData = m_ConstantData + m_ConstantDataOffset;
    dispatchDesc.constantBufferDataSize = internalDispatchDesc.constantBufferDataSize;
    m_ConstantDataOffset += internalDispatchDesc.constantBufferDataSize;

    // Update grid size
    float sx = ml::Max(internalDispatchDesc.downsampleFactor == USE_MAX_DIMS ? m_CommonSettings.resolutionScalePrev[0] : 0.0f, m_CommonSettings.resolutionScale[0]);
    float sy = ml::Max(internalDispatchDesc.downsampleFactor == USE_MAX_DIMS ? m_CommonSettings.resolutionScalePrev[1] : 0.0f, m_CommonSettings.resolutionScale[1]);
    uint16_t d = internalDispatchDesc.downsampleFactor == USE_MAX_DIMS ? 1 : internalDispatchDesc.downsampleFactor;

    if (internalDispatchDesc.downsampleFactor == IGNORE_RS)
    {
        sx = 1.0f;
        sy = 1.0f;
        d = 1;
    }

    uint16_t w = uint16_t( float(DivideUp(denoiserData.desc.renderWidth, d)) * sx + 0.5f );
    uint16_t h = uint16_t( float(DivideUp(denoiserData.desc.renderHeight, d)) * sy + 0.5f );

    dispatchDesc.gridWidth = DivideUp(w, internalDispatchDesc.numThreads.width);
    dispatchDesc.gridHeight = DivideUp(h, internalDispatchDesc.numThreads.height);

    // Store
    m_ActiveDispatches.push_back(dispatchDesc);

    return (Constant*)dispatchDesc.constantBufferData;
}
