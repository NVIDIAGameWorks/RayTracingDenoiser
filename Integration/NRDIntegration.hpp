/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRDIntegration.h"

static_assert(NRD_VERSION_MAJOR >= 4 && NRD_VERSION_MINOR >= 2, "Unsupported NRD version!");
static_assert(NRI_VERSION_MAJOR >= 1 && NRI_VERSION_MINOR >= 93, "Unsupported NRI version!");

#if _WIN32
    #define NRD_INTEGRATION_ALLOCA _alloca
#else
    #define NRD_INTEGRATION_ALLOCA alloca
#endif

constexpr std::array<nri::Format, (size_t)nrd::Format::MAX_NUM> g_NRD_NrdToNriFormat =
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
    return g_NRD_NrdToNriFormat[(uint32_t)format];
}

static inline uint64_t NRD_CreateDescriptorKey(uint64_t texture, bool isStorage, uint8_t mipOffset, uint8_t mipNum)
{
    uint64_t key = uint64_t(isStorage ? 1 : 0) << 49ull;
    key |= uint64_t(mipOffset & 127) << 50ull;
    key |= uint64_t(mipNum & 127) << 57ull;
    key |= texture & ((1ull << 49ull) - 1);

    return key;
}

template<typename T, typename A> constexpr T NRD_GetAlignedSize(const T& size, A alignment)
{
    return T(((size + alignment - 1) / alignment) * alignment);
}

bool NrdIntegration::Initialize(const nrd::InstanceCreationDesc& instanceCreationDesc, nri::Device& nriDevice,
    const nri::CoreInterface& nriCore, const nri::HelperInterface& nriHelper)
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

    if (nrd::CreateInstance(instanceCreationDesc, m_Instance) != nrd::Result::SUCCESS)
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

    const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_Instance);
    const nri::DeviceDesc& deviceDesc = m_NRI->GetDeviceDesc(*m_Device);

    uint32_t constantBufferOffset = 0;
    uint32_t samplerOffset = 0;
    uint32_t textureOffset = 0;
    uint32_t storageTextureAndBufferOffset = 0;
    if (m_NRI->GetDeviceDesc(*m_Device).graphicsAPI == nri::GraphicsAPI::VULKAN)
    {
        const nrd::LibraryDesc& nrdLibraryDesc = nrd::GetLibraryDesc();
        constantBufferOffset = nrdLibraryDesc.spirvBindingOffsets.constantBufferOffset;
        samplerOffset = nrdLibraryDesc.spirvBindingOffsets.samplerOffset;
        textureOffset = nrdLibraryDesc.spirvBindingOffsets.textureOffset;
        storageTextureAndBufferOffset = nrdLibraryDesc.spirvBindingOffsets.storageTextureAndBufferOffset;
    }

    // Allocate memory for descriptor sets
    uint32_t descriptorSetSamplersIndex = instanceDesc.constantBufferSpaceIndex == instanceDesc.samplersSpaceIndex ? 0 : 1;
    uint32_t descriptorSetResourcesIndex = instanceDesc.resourcesSpaceIndex == instanceDesc.constantBufferSpaceIndex ? 0 : (instanceDesc.resourcesSpaceIndex == instanceDesc.samplersSpaceIndex ? descriptorSetSamplersIndex : descriptorSetSamplersIndex + 1);
    uint32_t descriptorSetNum = std::max(descriptorSetSamplersIndex, descriptorSetResourcesIndex) + 1;

    nri::DescriptorSetDesc* descriptorSetDescs = (nri::DescriptorSetDesc*)NRD_INTEGRATION_ALLOCA(sizeof(nri::DescriptorSetDesc) * descriptorSetNum);
    memset(descriptorSetDescs, 0, sizeof(nri::DescriptorSetDesc) * descriptorSetNum);

    nri::DescriptorSetDesc& descriptorSetConstantBuffer = descriptorSetDescs[0];
    descriptorSetConstantBuffer.registerSpace = instanceDesc.constantBufferSpaceIndex;

    nri::DescriptorSetDesc& descriptorSetSamplers = descriptorSetDescs[descriptorSetSamplersIndex];
    descriptorSetSamplers.registerSpace = instanceDesc.samplersSpaceIndex;

    nri::DescriptorSetDesc& descriptorSetResources = descriptorSetDescs[descriptorSetResourcesIndex];
    descriptorSetResources.registerSpace = instanceDesc.resourcesSpaceIndex;

    // Allocate memory for descriptor ranges
    uint32_t resourceRangesNum = 0;
    for (uint32_t i = 0; i < instanceDesc.pipelinesNum; i++)
    {
        const nrd::PipelineDesc& nrdPipelineDesc = instanceDesc.pipelines[i];
        resourceRangesNum = std::max(resourceRangesNum, nrdPipelineDesc.resourceRangesNum);
    }
    resourceRangesNum += 1; // samplers

    nri::DescriptorRangeDesc* descriptorRanges = (nri::DescriptorRangeDesc*)NRD_INTEGRATION_ALLOCA(sizeof(nri::DescriptorRangeDesc) * resourceRangesNum);
    memset(descriptorRanges, 0, sizeof(nri::DescriptorRangeDesc) * resourceRangesNum);

    nri::DescriptorRangeDesc* samplersRange = descriptorRanges;
    nri::DescriptorRangeDesc* resourcesRanges = descriptorRanges + 1;

    // Constant buffer
    const nri::DynamicConstantBufferDesc dynamicConstantBufferDesc = {constantBufferOffset + instanceDesc.constantBufferRegisterIndex, nri::ShaderStage::COMPUTE};
    descriptorSetConstantBuffer.dynamicConstantBuffers = &dynamicConstantBufferDesc;

    // Samplers
    samplersRange->descriptorType = nri::DescriptorType::SAMPLER;
    samplersRange->baseRegisterIndex = samplerOffset + instanceDesc.samplersBaseRegisterIndex;
    samplersRange->descriptorNum = instanceDesc.samplersNum;
    samplersRange->visibility =  nri::ShaderStage::COMPUTE;

    // Pipelines
    for (uint32_t i = 0; i < instanceDesc.pipelinesNum; i++)
    {
        const nrd::PipelineDesc& nrdPipelineDesc = instanceDesc.pipelines[i];
        const nrd::ComputeShaderDesc& nrdComputeShader = (&nrdPipelineDesc.computeShaderDXBC)[(uint32_t)deviceDesc.graphicsAPI];

        // Resources
        for (uint32_t j = 0; j < nrdPipelineDesc.resourceRangesNum; j++)
        {
            const nrd::ResourceRangeDesc& nrdResourceRange = nrdPipelineDesc.resourceRanges[j];

            if (nrdResourceRange.descriptorType == nrd::DescriptorType::TEXTURE)
            {
                resourcesRanges[j].baseRegisterIndex = textureOffset + nrdResourceRange.baseRegisterIndex;
                resourcesRanges[j].descriptorType = nri::DescriptorType::TEXTURE;
            }
            else
            {
                resourcesRanges[j].baseRegisterIndex = storageTextureAndBufferOffset + nrdResourceRange.baseRegisterIndex;
                resourcesRanges[j].descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
            }

            resourcesRanges[j].descriptorNum = nrdResourceRange.descriptorsNum;
            resourcesRanges[j].visibility = nri::ShaderStage::COMPUTE;
        }

        // Descriptor sets
        if (instanceDesc.resourcesSpaceIndex != instanceDesc.samplersSpaceIndex)
        {
            descriptorSetSamplers.rangeNum = 1;
            descriptorSetSamplers.ranges = samplersRange;

            descriptorSetResources.ranges = resourcesRanges;
            descriptorSetResources.rangeNum = nrdPipelineDesc.resourceRangesNum;
        }
        else
        {
            descriptorSetResources.ranges = descriptorRanges;
            descriptorSetResources.rangeNum = nrdPipelineDesc.resourceRangesNum + 1;
        }

        descriptorSetConstantBuffer.dynamicConstantBufferNum = nrdPipelineDesc.hasConstantData ? 1 : 0;

        // Pipeline layout
        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.descriptorSetNum = descriptorSetNum;
        pipelineLayoutDesc.descriptorSets = descriptorSetDescs;
        pipelineLayoutDesc.ignoreGlobalSPIRVOffsets = true;
        pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::COMPUTE;

        nri::PipelineLayout* pipelineLayout = nullptr;
        NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->CreatePipelineLayout(*m_Device, pipelineLayoutDesc, pipelineLayout));
        m_PipelineLayouts.push_back(pipelineLayout);

        // Pipeline
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
    const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_Instance);
    const uint32_t poolSize = instanceDesc.permanentPoolSize + instanceDesc.transientPoolSize;

    uint32_t resourceStateNum = 0;
    for (uint32_t i = 0; i < poolSize; i++)
    {
        const nrd::TextureDesc& nrdTextureDesc = (i < instanceDesc.permanentPoolSize) ? instanceDesc.permanentPool[i] : instanceDesc.transientPool[i - instanceDesc.permanentPoolSize];
        resourceStateNum += nrdTextureDesc.mipNum;
    }
    m_ResourceState.resize(resourceStateNum); // No reallocation!

    m_TexturePool.resize(poolSize);

    // Texture pool
    resourceStateNum = 0;
    for (uint32_t i = 0; i < poolSize; i++)
    {
        // Create NRI texture
        const nrd::TextureDesc& nrdTextureDesc = (i < instanceDesc.permanentPoolSize) ? instanceDesc.permanentPool[i] : instanceDesc.transientPool[i - instanceDesc.permanentPoolSize];
        const nri::Format format = NRD_GetNriFormat(nrdTextureDesc.format);

        nri::TextureDesc textureDesc = nri::Texture2D(format, nrdTextureDesc.width, nrdTextureDesc.height, nrdTextureDesc.mipNum, 1, nri::TextureUsageBits::SHADER_RESOURCE | nri::TextureUsageBits::SHADER_RESOURCE_STORAGE);
        nri::Texture* texture = nullptr;
        NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->CreateTexture(*m_Device, textureDesc, texture));

        char name[128];
        if (i < instanceDesc.permanentPoolSize)
            snprintf(name, sizeof(name), "%s::PermamentPool%u", m_Name, i);
        else
            snprintf(name, sizeof(name), "%s::TransientPool%u", m_Name, i - instanceDesc.permanentPoolSize);
        m_NRI->SetTextureDebugName(*texture, name);

        // Construct NRD texture
        NrdIntegrationTexture nrdTexture = {};
        nrdTexture.subresourceStates = &m_ResourceState[resourceStateNum];
        nrdTexture.format = format;
        m_TexturePool[i] = nrdTexture;

        for (uint16_t mip = 0; mip < nrdTextureDesc.mipNum; mip++)
            nrdTexture.subresourceStates[mip] = nri::TextureTransitionFromUnknown(texture, nri::AccessBits::UNKNOWN, nri::TextureLayout::UNKNOWN, mip, 1);

        resourceStateNum += nrdTextureDesc.mipNum;

        // Adjust memory usage
        nri::MemoryDesc memoryDesc = {};
        m_NRI->GetTextureMemoryInfo(*texture, nri::MemoryLocation::DEVICE, memoryDesc);

        if (i < instanceDesc.permanentPoolSize)
            m_PermanentPoolSize += memoryDesc.size;
        else
            m_TransientPoolSize += memoryDesc.size;

    #if( NRD_INTEGRATION_DEBUG_LOGGING == 1 )
        printf("%s %ux%u format=%u mips=%u\n", name, nrdTextureDesc.width, nrdTextureDesc.height, nrdTextureDesc.format, nrdTextureDesc.mipNum);
    #endif
    }

    #if( NRD_INTEGRATION_DEBUG_LOGGING == 1 )
        printf("%s: %.1f Mb (permanent), %.1f Mb (transient)\n\n", m_Name, double(m_PermanentPoolSize) / (1024.0f * 1024.0f), double(m_TransientPoolSize) / (1024.0f * 1024.0f));
    #endif

    // Samplers
    for (uint32_t i = 0; i < instanceDesc.samplersNum; i++)
    {
        nrd::Sampler nrdSampler = instanceDesc.samplers[i];

        nri::SamplerDesc samplerDesc = {};
        samplerDesc.mipMax = 16.0f;

        if (nrdSampler == nrd::Sampler::NEAREST_CLAMP || nrdSampler == nrd::Sampler::LINEAR_CLAMP)
            samplerDesc.addressModes = {nri::AddressMode::CLAMP_TO_EDGE, nri::AddressMode::CLAMP_TO_EDGE};
        else
            samplerDesc.addressModes = {nri::AddressMode::MIRRORED_REPEAT, nri::AddressMode::MIRRORED_REPEAT};

        if (nrdSampler == nrd::Sampler::NEAREST_CLAMP || nrdSampler == nrd::Sampler::NEAREST_MIRRORED_REPEAT)
        {
            samplerDesc.minification = nri::Filter::NEAREST;
            samplerDesc.magnification = nri::Filter::NEAREST;
        }
        else
        {
            samplerDesc.minification = nri::Filter::LINEAR;
            samplerDesc.magnification = nri::Filter::LINEAR;
        }

        nri::Descriptor* descriptor = nullptr;
        NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->CreateSampler(*m_Device, samplerDesc, descriptor));
        m_Samplers.push_back(descriptor);
    }

    // Constant buffer
    const nri::DeviceDesc& deviceDesc = m_NRI->GetDeviceDesc(*m_Device);
    m_ConstantBufferViewSize = NRD_GetAlignedSize(instanceDesc.constantBufferMaxDataSize, deviceDesc.constantBufferOffsetAlignment);
    m_ConstantBufferSize = uint64_t(m_ConstantBufferViewSize) * instanceDesc.descriptorPoolDesc.setsMaxNum * m_BufferedFramesNum;

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
    descriptorPoolDesc.descriptorSetMaxNum = instanceDesc.descriptorPoolDesc.setsMaxNum;
    descriptorPoolDesc.storageTextureMaxNum = instanceDesc.descriptorPoolDesc.storageTexturesMaxNum;
    descriptorPoolDesc.textureMaxNum = instanceDesc.descriptorPoolDesc.texturesMaxNum;
    descriptorPoolDesc.dynamicConstantBufferMaxNum = instanceDesc.descriptorPoolDesc.constantBuffersMaxNum;
    descriptorPoolDesc.samplerMaxNum = instanceDesc.descriptorPoolDesc.samplersMaxNum;

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

void NrdIntegration::NewFrame(uint32_t frameIndex)
{
    #if( NRD_INTEGRATION_DEBUG_LOGGING == 1 )
        printf("%s (frame %u) ==============================================================================\n\n", m_Name, frameIndex);
    #endif

    m_DescriptorPoolIndex = frameIndex % m_BufferedFramesNum;
    nri::DescriptorPool* descriptorPool = m_DescriptorPools[m_DescriptorPoolIndex];
    m_NRI->ResetDescriptorPool(*descriptorPool);
}

bool NrdIntegration::SetCommonSettings(const nrd::CommonSettings& commonSettings)
{
    nrd::Result result = nrd::SetCommonSettings(*m_Instance, commonSettings);
    assert(result == nrd::Result::SUCCESS);

    return result == nrd::Result::SUCCESS;
}

bool NrdIntegration::SetDenoiserSettings(nrd::Identifier denoiser, const void* denoiserSettings)
{
    nrd::Result result = nrd::SetDenoiserSettings(*m_Instance, denoiser, denoiserSettings);
    assert(result == nrd::Result::SUCCESS);

    return result == nrd::Result::SUCCESS;
}

void NrdIntegration::Denoise(const nrd::Identifier* denoisers, uint32_t denoisersNum, nri::CommandBuffer& commandBuffer, const NrdUserPool& userPool, bool enableDescriptorCaching)
{
    const nrd::DispatchDesc* dispatchDescs = nullptr;
    uint32_t dispatchDescsNum = 0;
    nrd::GetComputeDispatches(*m_Instance, denoisers, denoisersNum, dispatchDescs, dispatchDescsNum);

    nri::DescriptorPool* descriptorPool = m_DescriptorPools[m_DescriptorPoolIndex];
    m_NRI->CmdSetDescriptorPool(commandBuffer, *descriptorPool);

    for (uint32_t i = 0; i < dispatchDescsNum; i++)
    {
        const nrd::DispatchDesc& dispatchDesc = dispatchDescs[i];
        m_NRI->CmdBeginAnnotation(commandBuffer, dispatchDesc.name);

        Dispatch(commandBuffer, *descriptorPool, dispatchDesc, userPool, enableDescriptorCaching);

        m_NRI->CmdEndAnnotation(commandBuffer);
    }
}

void NrdIntegration::Dispatch(nri::CommandBuffer& commandBuffer, nri::DescriptorPool& descriptorPool, const nrd::DispatchDesc& dispatchDesc, const NrdUserPool& userPool, bool enableDescriptorCaching)
{
    const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_Instance);
    const nrd::PipelineDesc& pipelineDesc = instanceDesc.pipelines[dispatchDesc.pipelineIndex];

    uint32_t transitionNum = 0;
    for (uint32_t i = 0; i < dispatchDesc.resourcesNum; i++)
        transitionNum += dispatchDesc.resources[i].mipNum;

    nri::Descriptor** descriptors = (nri::Descriptor**)NRD_INTEGRATION_ALLOCA(sizeof(nri::Descriptor*) * dispatchDesc.resourcesNum);
    memset(descriptors, 0, sizeof(nri::Descriptor*) * dispatchDesc.resourcesNum);

    nri::DescriptorRangeUpdateDesc* resourceRanges = (nri::DescriptorRangeUpdateDesc*)NRD_INTEGRATION_ALLOCA(sizeof(nri::DescriptorRangeUpdateDesc) * pipelineDesc.resourceRangesNum);
    memset(resourceRanges, 0, sizeof(nri::DescriptorRangeUpdateDesc) * pipelineDesc.resourceRangesNum);

    nri::TextureTransitionBarrierDesc* transitions = (nri::TextureTransitionBarrierDesc*)NRD_INTEGRATION_ALLOCA(sizeof(nri::TextureTransitionBarrierDesc) * transitionNum);
    memset(transitions, 0, sizeof(nri::TextureTransitionBarrierDesc) * transitionNum);

    nri::TransitionBarrierDesc transitionBarriers = {};
    transitionBarriers.textures = transitions;

    uint32_t n = 0;
    for (uint32_t i = 0; i < pipelineDesc.resourceRangesNum; i++)
    {
        const nrd::ResourceRangeDesc& resourceRange = pipelineDesc.resourceRanges[i];
        const bool isStorage = resourceRange.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;

        resourceRanges[i].descriptors = descriptors + n;
        resourceRanges[i].descriptorNum = resourceRange.descriptorsNum;

        for (uint32_t j = 0; j < resourceRange.descriptorsNum; j++)
        {
            const nrd::ResourceDesc& nrdResource = dispatchDesc.resources[n];

            NrdIntegrationTexture* nrdTexture = nullptr;
            if (nrdResource.type == nrd::ResourceType::TRANSIENT_POOL)
                nrdTexture = &m_TexturePool[nrdResource.indexInPool + instanceDesc.permanentPoolSize];
            else if (nrdResource.type == nrd::ResourceType::PERMANENT_POOL)
                nrdTexture = &m_TexturePool[nrdResource.indexInPool];
            else
            {
                nrdTexture = (NrdIntegrationTexture*)&userPool[(uint32_t)nrdResource.type];

                NRD_INTEGRATION_ASSERT(nrdTexture && nrdTexture->subresourceStates && nrdTexture->subresourceStates->texture, "'userPool' entry can't be NULL if it's in use!");
                NRD_INTEGRATION_ASSERT(nrdTexture->format != nri::Format::UNKNOWN, "Format must be a valid format!");
            }

            const nri::AccessBits nextAccess = nrdResource.stateNeeded == nrd::DescriptorType::TEXTURE ? nri::AccessBits::SHADER_RESOURCE : nri::AccessBits::SHADER_RESOURCE_STORAGE;
            const nri::TextureLayout nextLayout =  nrdResource.stateNeeded == nrd::DescriptorType::TEXTURE ? nri::TextureLayout::SHADER_RESOURCE : nri::TextureLayout::GENERAL;
            for (uint16_t mip = 0; mip < nrdResource.mipNum; mip++)
            {
                nri::TextureTransitionBarrierDesc* state = nrdTexture->subresourceStates + nrdResource.mipOffset + mip;
                bool isStateChanged = nextAccess != state->nextAccess || nextLayout != state->nextLayout;
                bool isStorageBarrier = nextAccess == nri::AccessBits::SHADER_RESOURCE_STORAGE && state->nextAccess == nri::AccessBits::SHADER_RESOURCE_STORAGE;
                if (isStateChanged || isStorageBarrier)
                    transitions[transitionBarriers.textureNum++] = nri::TextureTransitionFromState(*state, nextAccess, nextLayout, nrdResource.mipOffset + mip, 1);
            }

            uint64_t resource = m_NRI->GetTextureNativeObject(*nrdTexture->subresourceStates->texture, 0);
            uint64_t key = NRD_CreateDescriptorKey(resource, isStorage, (uint8_t)nrdResource.mipOffset, (uint8_t)nrdResource.mipNum);
            const auto& entry = enableDescriptorCaching ? m_Descriptors.find(key) : m_Descriptors.end();

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

    // Allocating descriptor sets
    uint32_t descriptorSetSamplersIndex = instanceDesc.constantBufferSpaceIndex == instanceDesc.samplersSpaceIndex ? 0 : 1;
    uint32_t descriptorSetResourcesIndex = instanceDesc.resourcesSpaceIndex == instanceDesc.constantBufferSpaceIndex ? 0 : (instanceDesc.resourcesSpaceIndex == instanceDesc.samplersSpaceIndex ? descriptorSetSamplersIndex : descriptorSetSamplersIndex + 1);
    uint32_t descriptorSetNum = std::max(descriptorSetSamplersIndex, descriptorSetResourcesIndex) + 1;

    nri::DescriptorSet** descriptorSets = (nri::DescriptorSet**)NRD_INTEGRATION_ALLOCA(sizeof(nri::DescriptorSet*) * descriptorSetNum);
    nri::PipelineLayout* pipelineLayout = m_PipelineLayouts[dispatchDesc.pipelineIndex];

    for (uint32_t i = 0; i < descriptorSetNum; i++)
        NRD_INTEGRATION_ABORT_ON_FAILURE(m_NRI->AllocateDescriptorSets(descriptorPool, *pipelineLayout, i, &descriptorSets[i], 1, nri::WHOLE_DEVICE_GROUP, 0));

    // Updating constants
    uint32_t dynamicConstantBufferOffset = 0;
    if (dispatchDesc.constantBufferDataSize)
    {
        if (m_ConstantBufferOffset + m_ConstantBufferViewSize > m_ConstantBufferSize)
            m_ConstantBufferOffset = 0;

        // TODO: persistent mapping? But no D3D11 support...
        void* data = m_NRI->MapBuffer(*m_ConstantBuffer, m_ConstantBufferOffset, dispatchDesc.constantBufferDataSize);
        memcpy(data, dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);
        m_NRI->UnmapBuffer(*m_ConstantBuffer);

        m_NRI->UpdateDynamicConstantBuffers(*descriptorSets[0], nri::WHOLE_DEVICE_GROUP, 0, 1, &m_ConstantBufferView);

        dynamicConstantBufferOffset = m_ConstantBufferOffset;
        m_ConstantBufferOffset += m_ConstantBufferViewSize;
    }

    // Updating samplers
    nri::DescriptorRangeUpdateDesc samplersDescriptorRange = {m_Samplers.data(), instanceDesc.samplersNum, 0};
    m_NRI->UpdateDescriptorRanges(*descriptorSets[descriptorSetSamplersIndex], nri::WHOLE_DEVICE_GROUP, 0, 1, &samplersDescriptorRange);

    // Updating resources
    m_NRI->UpdateDescriptorRanges(*descriptorSets[descriptorSetResourcesIndex], nri::WHOLE_DEVICE_GROUP, instanceDesc.samplersSpaceIndex == instanceDesc.resourcesSpaceIndex ? 1 : 0, pipelineDesc.resourceRangesNum, resourceRanges);

    // Rendering
    m_NRI->CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);
    m_NRI->CmdSetPipelineLayout(commandBuffer, *pipelineLayout);

    nri::Pipeline* pipeline = m_Pipelines[dispatchDesc.pipelineIndex];
    m_NRI->CmdSetPipeline(commandBuffer, *pipeline);

    for (uint32_t i = 0; i < descriptorSetNum; i++)
        m_NRI->CmdSetDescriptorSet(commandBuffer, i, *descriptorSets[i], i == 0 ? &dynamicConstantBufferOffset : nullptr);

    m_NRI->CmdDispatch(commandBuffer, dispatchDesc.gridWidth, dispatchDesc.gridHeight, 1);

    // Cleanup
    if (!enableDescriptorCaching)
    {
        for (const auto& entry : m_Descriptors)
            m_NRI->DestroyDescriptor(*entry.second);
        m_Descriptors.clear();
    }

    // Debug logging
    #if( NRD_INTEGRATION_DEBUG_LOGGING == 1 )
        printf("Pipeline #%u : %s\n\t", dispatchDesc.pipelineIndex, dispatchDesc.name);
        for( uint32_t i = 0; i < dispatchDesc.resourcesNum; i++ )
        {
            const nrd::ResourceDesc& r = dispatchDesc.resources[i];

            if( r.type == nrd::ResourceType::PERMANENT_POOL )
                printf("P(%u) ", r.indexInPool);
            else if( r.type == nrd::ResourceType::TRANSIENT_POOL )
            {
                if (r.mipNum != 1 || r.mipOffset != 0)
                    printf("T(%u)[%u:%u] ", r.indexInPool, r.mipOffset, r.mipNum);
                else
                    printf("T(%u) ", r.indexInPool);
            }
            else
            {
                const char* s = nrd::GetResourceTypeString(r.type);
                printf("%s ", s);
            }
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

    for (nri::Descriptor* descriptor : m_Samplers)
        m_NRI->DestroyDescriptor(*descriptor);
    m_Samplers.clear();

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

    nrd::DestroyInstance(*m_Instance);
    m_Instance = nullptr;

    m_NRI = nullptr;
    m_Device = nullptr;
    m_PermanentPoolSize = 0;
    m_TransientPoolSize = 0;
    m_ConstantBufferSize = 0;
    m_ConstantBufferViewSize = 0;
    m_ConstantBufferOffset = 0;
    m_IsShadersReloadRequested = false;
}
