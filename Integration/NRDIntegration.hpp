/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRDIntegration.h"

static_assert(NRD_VERSION_MAJOR >= 2 && NRD_VERSION_MINOR >= 6, "Unsupported NRD version!");

#if _WIN32
    #define NRD_INTEGRATION_ALLOCA _alloca
#else
    #define NRD_INTEGRATION_ALLOCA alloca
#endif

constexpr std::array<nri::Format, (size_t)nrd::Format::MAX_NUM> g_NriFormat =
{
    nri::Format::R8_UNORM,
    nri::Format::R8_SNORM,
    nri::Format::R8_UINT,
    nri::Format::R8_SINT,
    nri::Format::RG8_UNORM,
    nri::Format::RG8_SNORM,
    nri::Format::RG8_UINT,
    nri::Format::RG8_SINT,
    nri::Format::RGBA8_UNORM,
    nri::Format::RGBA8_SNORM,
    nri::Format::RGBA8_UINT,
    nri::Format::RGBA8_SINT,
    nri::Format::RGBA8_SRGB,
    nri::Format::R16_UNORM,
    nri::Format::R16_SNORM,
    nri::Format::R16_UINT,
    nri::Format::R16_SINT,
    nri::Format::R16_SFLOAT,
    nri::Format::RG16_UNORM,
    nri::Format::RG16_SNORM,
    nri::Format::RG16_UINT,
    nri::Format::RG16_SINT,
    nri::Format::RG16_SFLOAT,
    nri::Format::RGBA16_UNORM,
    nri::Format::RGBA16_SNORM,
    nri::Format::RGBA16_UINT,
    nri::Format::RGBA16_SINT,
    nri::Format::RGBA16_SFLOAT,
    nri::Format::R32_UINT,
    nri::Format::R32_SINT,
    nri::Format::R32_SFLOAT,
    nri::Format::RG32_UINT,
    nri::Format::RG32_SINT,
    nri::Format::RG32_SFLOAT,
    nri::Format::RGB32_UINT,
    nri::Format::RGB32_SINT,
    nri::Format::RGB32_SFLOAT,
    nri::Format::RGBA32_UINT,
    nri::Format::RGBA32_SINT,
    nri::Format::RGBA32_SFLOAT,
    nri::Format::R10_G10_B10_A2_UNORM,
    nri::Format::R10_G10_B10_A2_UINT,
    nri::Format::R11_G11_B10_UFLOAT,
    nri::Format::R9_G9_B9_E5_UFLOAT,
};

static inline nri::Format NRD_GetNriFormat(nrd::Format format)
{
    return g_NriFormat[(uint32_t)format];
}

static inline uint64_t NRD_CreateDescriptorKey(bool isStorage, uint8_t poolIndex, uint16_t indexInPool, uint8_t mipOffset, uint8_t mipNum, uint32_t bufferedFrameIndex)
{
    uint64_t key = isStorage ? 1 : 0;
    key |= uint64_t(poolIndex) << 1ull;
    key |= uint64_t(indexInPool) << 9ull;
    key |= uint64_t(mipOffset) << 25ull;
    key |= uint64_t(mipNum) << 33ull;
    key |= uint64_t(bufferedFrameIndex) << 41ull;
    return key;
}

template<typename T, typename A> constexpr T NRD_GetAlignedSize(const T& size, A alignment)
{
    return T(((size + alignment - 1) / alignment) * alignment);
}

bool NrdIntegration::Initialize(nri::Device& nriDevice, const nri::CoreInterface& nriCore, const nri::HelperInterface& nriHelper, const nrd::DenoiserCreationDesc& denoiserCreationDesc)
{
    const nri::DeviceDesc& deviceDesc = nriCore.GetDeviceDesc(nriDevice);
    if (deviceDesc.nriVersionMajor != NRI_VERSION_MAJOR || deviceDesc.nriVersionMinor != NRI_VERSION_MINOR)
    {
        NRD_INTEGRATION_ASSERT(false, "NRI version mismatch detected!");
        return false;
    }

    const nrd::LibraryDesc& libraryDesc = nrd::GetLibraryDesc();
    if (libraryDesc.versionMajor != NRD_VERSION_MAJOR || libraryDesc.versionMinor != NRD_VERSION_MINOR)
    {
        NRD_INTEGRATION_ASSERT(false, "NRD version mismatch detected!");
        return false;
    }

    for (uint32_t i = 0; i < denoiserCreationDesc.requestedMethodNum; i++)
    {
        uint32_t j = 0;
        for (; j < libraryDesc.supportedMethodNum; j++)
        {
            if (libraryDesc.supportedMethods[j] == denoiserCreationDesc.requestedMethods[i].method)
                break;
        }
        if (j == libraryDesc.supportedMethodNum)
            return false;
    }

    if (nrd::CreateDenoiser(denoiserCreationDesc, m_Denoiser) != nrd::Result::SUCCESS)
        return false;

    m_Device = &nriDevice;
    m_NRI = &nriCore;
    m_NRIHelper = &nriHelper;

    CreatePipelines();
    CreateResources();

    return true;
}

void NrdIntegration::CreatePipelines()
{
    // Assuming that the device is in IDLE state
    for (nri::Pipeline* pipeline : m_Pipelines)
        m_NRI->DestroyPipeline(*pipeline);
    m_Pipelines.clear();

#ifdef PROJECT_NAME
     utils::ShaderCodeStorage shaderCodeStorage;
#endif

    const nrd::DenoiserDesc& denoiserDesc = nrd::GetDenoiserDesc(*m_Denoiser);
    const nri::DeviceDesc& deviceDesc = m_NRI->GetDeviceDesc(*m_Device);

    uint32_t constantBufferOffset = 0;
    uint32_t textureOffset = 0;
    uint32_t storageTextureAndBufferOffset = 0;
    uint32_t samplerOffset = 0;
    if (m_NRI->GetDeviceDesc(*m_Device).graphicsAPI == nri::GraphicsAPI::VULKAN)
    {
        const nrd::LibraryDesc& nrdLibraryDesc = nrd::GetLibraryDesc();
        constantBufferOffset = nrdLibraryDesc.spirvBindingOffsets.constantBufferOffset;
        textureOffset = nrdLibraryDesc.spirvBindingOffsets.textureOffset;
        storageTextureAndBufferOffset = nrdLibraryDesc.spirvBindingOffsets.storageTextureAndBufferOffset;
        samplerOffset = nrdLibraryDesc.spirvBindingOffsets.samplerOffset;
    }

    const nri::DynamicConstantBufferDesc dynamicConstantBufferDesc = { constantBufferOffset + denoiserDesc.constantBufferDesc.registerIndex, nri::ShaderStage::ALL };

    nri::StaticSamplerDesc* staticSamplerDescs = (nri::StaticSamplerDesc*)NRD_INTEGRATION_ALLOCA( sizeof(nri::StaticSamplerDesc) * denoiserDesc.staticSamplerNum );
    memset(staticSamplerDescs, 0, sizeof(nri::StaticSamplerDesc) * denoiserDesc.staticSamplerNum);
    for (uint32_t i = 0; i < denoiserDesc.staticSamplerNum; i++)
    {
        const nrd::StaticSamplerDesc& nrdStaticsampler = denoiserDesc.staticSamplers[i];

        staticSamplerDescs[i].visibility = nri::ShaderStage::ALL;
        staticSamplerDescs[i].registerIndex = samplerOffset + nrdStaticsampler.registerIndex;
        staticSamplerDescs[i].samplerDesc.mipMax = 16.0f;

        if (nrdStaticsampler.sampler == nrd::Sampler::NEAREST_CLAMP || nrdStaticsampler.sampler == nrd::Sampler::LINEAR_CLAMP)
            staticSamplerDescs[i].samplerDesc.addressModes = {nri::AddressMode::CLAMP_TO_EDGE, nri::AddressMode::CLAMP_TO_EDGE};
        else
            staticSamplerDescs[i].samplerDesc.addressModes = {nri::AddressMode::MIRRORED_REPEAT, nri::AddressMode::MIRRORED_REPEAT};

        if (nrdStaticsampler.sampler == nrd::Sampler::NEAREST_CLAMP || nrdStaticsampler.sampler == nrd::Sampler::NEAREST_MIRRORED_REPEAT)
        {
            staticSamplerDescs[i].samplerDesc.minification = nri::Filter::NEAREST;
            staticSamplerDescs[i].samplerDesc.magnification = nri::Filter::NEAREST;
        }
        else
        {
            staticSamplerDescs[i].samplerDesc.minification = nri::Filter::LINEAR;
            staticSamplerDescs[i].samplerDesc.magnification = nri::Filter::LINEAR;
        }
    }

    nri::DescriptorRangeDesc* descriptorRanges = (nri::DescriptorRangeDesc*)NRD_INTEGRATION_ALLOCA( sizeof(nri::DescriptorRangeDesc) * denoiserDesc.descriptorSetDesc.descriptorRangeMaxNumPerPipeline );
    for (uint32_t i = 0; i < denoiserDesc.pipelineNum; i++)
    {
        const nrd::PipelineDesc& nrdPipelineDesc = denoiserDesc.pipelines[i];
        const nrd::ComputeShader& nrdComputeShader = deviceDesc.graphicsAPI == nri::GraphicsAPI::VULKAN ? nrdPipelineDesc.computeShaderSPIRV : ( deviceDesc.graphicsAPI == nri::GraphicsAPI::D3D11 ? nrdPipelineDesc.computeShaderDXBC : nrdPipelineDesc.computeShaderDXIL );

        memset(descriptorRanges, 0, sizeof(nri::DescriptorRangeDesc) * nrdPipelineDesc.descriptorRangeNum);
        for (uint32_t j = 0; j < nrdPipelineDesc.descriptorRangeNum; j++)
        {
            const nrd::DescriptorRangeDesc& nrdDescriptorRange = nrdPipelineDesc.descriptorRanges[j];

            if (nrdDescriptorRange.descriptorType == nrd::DescriptorType::TEXTURE)
            {
                descriptorRanges[j].baseRegisterIndex = textureOffset + nrdDescriptorRange.baseRegisterIndex;
                descriptorRanges[j].descriptorType = nri::DescriptorType::TEXTURE;
            }
            else
            {
                descriptorRanges[j].baseRegisterIndex = storageTextureAndBufferOffset + nrdDescriptorRange.baseRegisterIndex;
                descriptorRanges[j].descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
            }
            descriptorRanges[j].descriptorNum = nrdDescriptorRange.descriptorNum;
            descriptorRanges[j].visibility = nri::ShaderStage::ALL;
        }

        nri::DescriptorSetDesc descriptorSetDesc = {};
        descriptorSetDesc.ranges = descriptorRanges;
        descriptorSetDesc.rangeNum = nrdPipelineDesc.descriptorRangeNum;
        descriptorSetDesc.staticSamplers = staticSamplerDescs;
        descriptorSetDesc.staticSamplerNum = denoiserDesc.staticSamplerNum;

        if (nrdPipelineDesc.hasConstantData)
        {
            descriptorSetDesc.dynamicConstantBuffers = &dynamicConstantBufferDesc;
            descriptorSetDesc.dynamicConstantBufferNum = 1;
        }

        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.descriptorSetNum = 1;
        pipelineLayoutDesc.descriptorSets = &descriptorSetDesc;
        pipelineLayoutDesc.ignoreGlobalSPIRVOffsets = true;
        pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::COMPUTE;

        nri::PipelineLayout* pipelineLayout = nullptr;
        NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->CreatePipelineLayout(*m_Device, pipelineLayoutDesc, pipelineLayout));
        m_PipelineLayouts.push_back(pipelineLayout);

        nri::ShaderDesc computeShader = {};
    #ifdef PROJECT_NAME
        if (nrdComputeShader.bytecode && !m_IsShadersReloadRequested)
        {
    #endif
            computeShader.bytecode = nrdComputeShader.bytecode;
            computeShader.size = nrdComputeShader.size;
            computeShader.entryPointName = nrdPipelineDesc.shaderEntryPointName;
            computeShader.stage = nri::ShaderStage::COMPUTE;
    #ifdef PROJECT_NAME
        }
        else
            computeShader = utils::LoadShader(deviceDesc.graphicsAPI, nrdPipelineDesc.shaderFileName, shaderCodeStorage, nrdPipelineDesc.shaderEntryPointName);
    #endif

        nri::ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.pipelineLayout = pipelineLayout;
        pipelineDesc.computeShader = computeShader;

        nri::Pipeline* pipeline = nullptr;
        NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->CreateComputePipeline(*m_Device, pipelineDesc, pipeline));
        m_Pipelines.push_back(pipeline);
    }

    m_IsShadersReloadRequested = true;
}

void NrdIntegration::CreateResources()
{
    const nrd::DenoiserDesc& denoiserDesc = nrd::GetDenoiserDesc(*m_Denoiser);
    const uint32_t poolSize = denoiserDesc.permanentPoolSize + denoiserDesc.transientPoolSize;

    uint32_t resourceStateNum = 0;
    for (uint32_t i = 0; i < poolSize; i++)
    {
        const nrd::TextureDesc& nrdTextureDesc = (i < denoiserDesc.permanentPoolSize) ? denoiserDesc.permanentPool[i] : denoiserDesc.transientPool[i - denoiserDesc.permanentPoolSize];
        resourceStateNum += nrdTextureDesc.mipNum;
    }
    m_ResourceState.resize(resourceStateNum); // No reallocation!

    m_TexturePool.resize(poolSize);

    // Texture pool
    resourceStateNum = 0;
    for (uint32_t i = 0; i < poolSize; i++)
    {
        const nrd::TextureDesc& nrdTextureDesc = (i < denoiserDesc.permanentPoolSize) ? denoiserDesc.permanentPool[i] : denoiserDesc.transientPool[i - denoiserDesc.permanentPoolSize];
        const nri::Format format = NRD_GetNriFormat(nrdTextureDesc.format);

        nri::CTextureDesc textureDesc = nri::CTextureDesc::Texture2D(format, nrdTextureDesc.width, nrdTextureDesc.height, nrdTextureDesc.mipNum, 1, nri::TextureUsageBits::SHADER_RESOURCE | nri::TextureUsageBits::SHADER_RESOURCE_STORAGE);
        nri::Texture* texture = nullptr;
        NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->CreateTexture(*m_Device, textureDesc, texture));

        NrdIntegrationTexture nrdTexture = {};
        nrdTexture.subresourceStates = &m_ResourceState[resourceStateNum];
        nrdTexture.format = format;
        m_TexturePool[i] = nrdTexture;

        for (uint16_t mip = 0; mip < nrdTextureDesc.mipNum; mip++)
            nrdTexture.subresourceStates[mip] = nri::TextureTransition(texture, nri::AccessBits::UNKNOWN, nri::TextureLayout::UNKNOWN, mip, 1);

        resourceStateNum += nrdTextureDesc.mipNum;
    }

    // Constant buffer
    const nri::DeviceDesc& deviceDesc = m_NRI->GetDeviceDesc(*m_Device);
    m_ConstantBufferViewSize = NRD_GetAlignedSize(denoiserDesc.constantBufferDesc.maxDataSize, deviceDesc.constantBufferOffsetAlignment);
    m_ConstantBufferSize = uint64_t(m_ConstantBufferViewSize) * denoiserDesc.descriptorSetDesc.setMaxNum * m_BufferedFrameMaxNum;

    nri::BufferDesc bufferDesc = {};
    bufferDesc.size = m_ConstantBufferSize;
    bufferDesc.usageMask = nri::BufferUsageBits::CONSTANT_BUFFER;
    NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->CreateBuffer(*m_Device, bufferDesc, m_ConstantBuffer));

    AllocateAndBindMemory();

    nri::BufferViewDesc constantBufferViewDesc = {};
    constantBufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
    constantBufferViewDesc.buffer = m_ConstantBuffer;
    constantBufferViewDesc.size = m_ConstantBufferViewSize;
    NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->CreateBufferView(constantBufferViewDesc, m_ConstantBufferView));

    // Descriptor pools
    nri::DescriptorPoolDesc descriptorPoolDesc = {};
    descriptorPoolDesc.descriptorSetMaxNum = denoiserDesc.descriptorSetDesc.setMaxNum;
    descriptorPoolDesc.storageTextureMaxNum = denoiserDesc.descriptorSetDesc.storageTextureMaxNum;
    descriptorPoolDesc.textureMaxNum = denoiserDesc.descriptorSetDesc.textureMaxNum;
    descriptorPoolDesc.dynamicConstantBufferMaxNum = denoiserDesc.descriptorSetDesc.constantBufferMaxNum;
    descriptorPoolDesc.staticSamplerMaxNum = denoiserDesc.descriptorSetDesc.staticSamplerMaxNum;

    for (nri::DescriptorPool*& descriptorPool : m_DescriptorPools)
        NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->CreateDescriptorPool(*m_Device, descriptorPoolDesc, descriptorPool));
}

void NrdIntegration::AllocateAndBindMemory()
{
    std::vector<nri::Texture*> textures(m_TexturePool.size(), nullptr);
    for (size_t i = 0; i < m_TexturePool.size(); i++)
        textures[i] = (nri::Texture*)m_TexturePool[i].subresourceStates->texture;

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = (uint32_t)textures.size();
    resourceGroupDesc.textures = textures.data();

    size_t baseAllocation = m_MemoryAllocations.size();
    const size_t allocationNum = m_NRIHelper->CalculateAllocationNumber(*m_Device, resourceGroupDesc);
    m_MemoryAllocations.resize(baseAllocation + allocationNum, nullptr);
    NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRIHelper->AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));

    resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_ConstantBuffer;

    baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + 1, nullptr);
    NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRIHelper->AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));
}

bool NrdIntegration::SetMethodSettings(nrd::Method method, const void* methodSettings)
{
    nrd::Result result = nrd::SetMethodSettings(*m_Denoiser, method, methodSettings);
    assert(result == nrd::Result::SUCCESS);

    return result == nrd::Result::SUCCESS;
}

void NrdIntegration::Denoise(uint32_t consecutiveFrameIndex, nri::CommandBuffer& commandBuffer, const nrd::CommonSettings& commonSettings, const NrdUserPool& userPool)
{
    #if( NRD_INTEGRATION_DEBUG_LOGGING == 1 )
        printf("Frame %u ==============================================================================\n", consecutiveFrameIndex);
    #endif

    const nrd::DispatchDesc* dispatchDescs = nullptr;
    uint32_t dispatchDescNum = 0;
    nrd::Result result = nrd::GetComputeDispatches(*m_Denoiser, commonSettings, dispatchDescs, dispatchDescNum);
    assert(result == nrd::Result::SUCCESS);

    const uint32_t bufferedFrameIndex = consecutiveFrameIndex % m_BufferedFrameMaxNum;
    nri::DescriptorPool* descriptorPool = m_DescriptorPools[bufferedFrameIndex];
    m_NRI->ResetDescriptorPool(*descriptorPool);
    m_NRI->CmdSetDescriptorPool(commandBuffer, *descriptorPool);

    for (uint32_t i = 0; i < dispatchDescNum; i++)
    {
        const nrd::DispatchDesc& dispatchDesc = dispatchDescs[i];
        m_NRI->CmdBeginAnnotation(commandBuffer, dispatchDesc.name);

        Dispatch(bufferedFrameIndex, commandBuffer, *descriptorPool, dispatchDesc, userPool);

        m_NRI->CmdEndAnnotation(commandBuffer);
    }
}

void NrdIntegration::Dispatch(uint32_t bufferedFrameIndex, nri::CommandBuffer& commandBuffer, nri::DescriptorPool& descriptorPool, const nrd::DispatchDesc& dispatchDesc, const NrdUserPool& userPool)
{
    const nrd::DenoiserDesc& denoiserDesc = nrd::GetDenoiserDesc(*m_Denoiser);
    const nrd::PipelineDesc& pipelineDesc = denoiserDesc.pipelines[dispatchDesc.pipelineIndex];

    uint32_t transitionNum = 0;
    for (uint32_t i = 0; i < dispatchDesc.resourceNum; i++)
        transitionNum += dispatchDesc.resources[i].mipNum;

    nri::Descriptor** descriptors = (nri::Descriptor**)NRD_INTEGRATION_ALLOCA( sizeof(nri::Descriptor*) * dispatchDesc.resourceNum );
    memset(descriptors, 0, sizeof(nri::Descriptor*) * dispatchDesc.resourceNum);

    nri::DescriptorRangeUpdateDesc* descriptorRangeUpdateDescs = (nri::DescriptorRangeUpdateDesc*)NRD_INTEGRATION_ALLOCA( sizeof(nri::DescriptorRangeUpdateDesc) * pipelineDesc.descriptorRangeNum );
    memset(descriptorRangeUpdateDescs, 0, sizeof(nri::DescriptorRangeUpdateDesc) * pipelineDesc.descriptorRangeNum);

    nri::TextureTransitionBarrierDesc* transitions = (nri::TextureTransitionBarrierDesc*)NRD_INTEGRATION_ALLOCA( sizeof(nri::TextureTransitionBarrierDesc) * transitionNum );
    memset(transitions, 0, sizeof(nri::TextureTransitionBarrierDesc) * transitionNum);

    nri::TransitionBarrierDesc transitionBarriers = {};
    transitionBarriers.textures = transitions;

    uint32_t n = 0;
    for (uint32_t i = 0; i < pipelineDesc.descriptorRangeNum; i++)
    {
        const nrd::DescriptorRangeDesc& descriptorRangeDesc = pipelineDesc.descriptorRanges[i];

        descriptorRangeUpdateDescs[i].descriptors = descriptors + n;
        descriptorRangeUpdateDescs[i].descriptorNum = descriptorRangeDesc.descriptorNum;

        for (uint32_t j = 0; j < descriptorRangeDesc.descriptorNum; j++)
        {
            const nrd::Resource& nrdResource = dispatchDesc.resources[n];

            NrdIntegrationTexture* nrdTexture = nullptr;
            if (nrdResource.type == nrd::ResourceType::TRANSIENT_POOL)
                nrdTexture = &m_TexturePool[nrdResource.indexInPool + denoiserDesc.permanentPoolSize];
            else if (nrdResource.type == nrd::ResourceType::PERMANENT_POOL)
                nrdTexture = &m_TexturePool[nrdResource.indexInPool];
            else
            {
                nrdTexture = (NrdIntegrationTexture*)&userPool[(uint32_t)nrdResource.type];

                NRD_INTEGRATION_ASSERT( nrdTexture && nrdTexture->subresourceStates && nrdTexture->subresourceStates->texture, "IN_XXX can't be NULL if it's in use!");
                NRD_INTEGRATION_ASSERT( nrdTexture->format != nri::Format::UNKNOWN, "Format must be a valid format!");
            }

            const nri::AccessBits nextAccess = nrdResource.stateNeeded == nrd::DescriptorType::TEXTURE ? nri::AccessBits::SHADER_RESOURCE : nri::AccessBits::SHADER_RESOURCE_STORAGE;
            const nri::TextureLayout nextLayout =  nrdResource.stateNeeded == nrd::DescriptorType::TEXTURE ? nri::TextureLayout::SHADER_RESOURCE : nri::TextureLayout::GENERAL;
            for (uint16_t mip = 0; mip < nrdResource.mipNum; mip++)
            {
                nri::TextureTransitionBarrierDesc* state = nrdTexture->subresourceStates + nrdResource.mipOffset + mip;
                bool isStateChanged = nextAccess != state->nextAccess || nextLayout != state->nextLayout;
                bool isStorageBarrier = nextAccess == nri::AccessBits::SHADER_RESOURCE_STORAGE && state->nextAccess == nri::AccessBits::SHADER_RESOURCE_STORAGE;
                if (isStateChanged || isStorageBarrier)
                    transitions[transitionBarriers.textureNum++] = nri::TextureTransition(*state, nextAccess, nextLayout, nrdResource.mipOffset + mip, 1);
            }

            const bool isStorage = descriptorRangeDesc.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
            uint64_t key = NRD_CreateDescriptorKey(isStorage, (uint8_t)nrdResource.type, nrdResource.indexInPool, (uint8_t)nrdResource.mipOffset, (uint8_t)nrdResource.mipNum, bufferedFrameIndex);
            const auto& entry = m_Descriptors.find(key);

            nri::Descriptor* descriptor = nullptr;
            if (entry == m_Descriptors.end())
            {
                nri::Texture2DViewDesc desc = {nrdTexture->subresourceStates->texture, isStorage ? nri::Texture2DViewType::SHADER_RESOURCE_STORAGE_2D : nri::Texture2DViewType::SHADER_RESOURCE_2D, nrdTexture->format, nrdResource.mipOffset, nrdResource.mipNum};
                NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->CreateTexture2DView(desc, descriptor));
                m_Descriptors.insert( std::make_pair(key, descriptor) );
            }
            else
                descriptor = entry->second;

            descriptors[n++] = descriptor;
        }
    }

    // Descriptor set
    nri::DescriptorSet* descriptorSet;
    nri::PipelineLayout* pipelineLayout = m_PipelineLayouts[dispatchDesc.pipelineIndex];
    NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->AllocateDescriptorSets(descriptorPool, *pipelineLayout, 0, &descriptorSet, 1, nri::WHOLE_DEVICE_GROUP, 0));

    m_NRI->UpdateDescriptorRanges(*descriptorSet, nri::WHOLE_DEVICE_GROUP, 0, pipelineDesc.descriptorRangeNum, descriptorRangeUpdateDescs);

    // Uploading constants
    uint32_t dynamicConstantBufferOffset = 0;
    if (dispatchDesc.constantBufferDataSize)
    {
        if (m_ConstantBufferOffset + m_ConstantBufferViewSize > m_ConstantBufferSize)
            m_ConstantBufferOffset = 0;

        // TODO: persistent mapping? But no D3D11 support...
        void* data = m_NRI->MapBuffer(*m_ConstantBuffer, m_ConstantBufferOffset, dispatchDesc.constantBufferDataSize);
        memcpy(data, dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);
        m_NRI->UnmapBuffer(*m_ConstantBuffer);
        m_NRI->UpdateDynamicConstantBuffers(*descriptorSet, nri::WHOLE_DEVICE_GROUP, 0, 1, &m_ConstantBufferView);

        dynamicConstantBufferOffset = m_ConstantBufferOffset;
        m_ConstantBufferOffset += m_ConstantBufferViewSize;
    }

    // Rendering
    nri::Pipeline* pipeline = m_Pipelines[dispatchDesc.pipelineIndex];
    m_NRI->CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);
    m_NRI->CmdSetPipelineLayout(commandBuffer, *pipelineLayout);
    m_NRI->CmdSetPipeline(commandBuffer, *pipeline);
    m_NRI->CmdSetDescriptorSets(commandBuffer, 0, 1, &descriptorSet, &dynamicConstantBufferOffset);
    m_NRI->CmdDispatch(commandBuffer, dispatchDesc.gridWidth, dispatchDesc.gridHeight, 1);

    // Debug logging
    #if( NRD_INTEGRATION_DEBUG_LOGGING == 1 )
        static constexpr std::array<const char*, (size_t)nrd::ResourceType::MAX_NUM - 2> names =
        {
            "IN_MV ",
            "IN_NORMAL_ROUGHNESS ",
            "IN_VIEWZ ",
            "IN_DIFF_RADIANCE_HITDIST ",
            "IN_SPEC_RADIANCE_HITDIST ",
            "IN_DIFF_HITDIST ",
            "IN_SPEC_HITDIST ",
            "IN_DIFF_DIRECTION_PDF ",
            "IN_SPEC_DIRECTION_PDF ",
            "IN_DIFF_CONFIDENCE ",
            "IN_SPEC_CONFIDENCE ",
            "IN_SHADOWDATA ",
            "IN_SHADOW_TRANSLUCENCY ",

            "OUT_SHADOW_TRANSLUCENCY ",
            "OUT_DIFF_RADIANCE_HITDIST ",
            "OUT_SPEC_RADIANCE_HITDIST ",
            "OUT_DIFF_HITDIST ",
            "OUT_SPEC_HITDIST ",
        };

        printf("Pipeline #%u : %s\n\t", dispatchDesc.pipelineIndex, dispatchDesc.name);
        for( uint32_t i = 0; i < dispatchDesc.resourceNum; i++ )
        {
            const nrd::Resource& r = dispatchDesc.resources[i];

            if( r.type == nrd::ResourceType::PERMANENT_POOL )
                printf("P(%u) ", r.indexInPool);
            else if( r.type == nrd::ResourceType::TRANSIENT_POOL )
            {
                if (r.mipNum || r.mipOffset)
                    printf("T(%u)[%u:%u] ", r.indexInPool, r.mipOffset, r.mipNum);
                else
                    printf("T(%u) ", r.indexInPool);
            }
            else
                printf(names[(uint32_t)r.type]);
        }
        printf("\n\n");
    #endif
}

void NrdIntegration::Destroy()
{
    m_NRI->DestroyDescriptor(*m_ConstantBufferView);
    m_ConstantBufferView = nullptr;

    m_NRI->DestroyBuffer(*m_ConstantBuffer);
    m_ConstantBuffer = nullptr;

    for (const auto& entry : m_Descriptors)
        m_NRI->DestroyDescriptor(*entry.second);
    m_Descriptors.clear();

    for (const NrdIntegrationTexture& nrdTexture : m_TexturePool)
        m_NRI->DestroyTexture(*(nri::Texture*)nrdTexture.subresourceStates->texture);
    m_TexturePool.clear();

    for (nri::Pipeline* pipeline : m_Pipelines)
        m_NRI->DestroyPipeline(*pipeline);
    m_Pipelines.clear();

    for (nri::PipelineLayout* pipelineLayout : m_PipelineLayouts)
        m_NRI->DestroyPipelineLayout(*pipelineLayout);
    m_PipelineLayouts.clear();

    for (nri::Memory* memory : m_MemoryAllocations)
        m_NRI->FreeMemory(*memory);
    m_MemoryAllocations.clear();

    for (nri::DescriptorPool* descriptorPool : m_DescriptorPools)
        m_NRI->DestroyDescriptorPool(*descriptorPool);

    nrd::DestroyDenoiser(*m_Denoiser);
    m_Denoiser = nullptr;

    m_NRI = nullptr;
    m_Device = nullptr;
    m_ConstantBufferSize = 0;
    m_ConstantBufferViewSize = 0;
    m_ConstantBufferOffset = 0;
    m_IsShadersReloadRequested = false;
}
