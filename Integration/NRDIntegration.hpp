/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#if _WIN32
    #define NRD_ALLOCA _alloca
#else
    #define NRD_ALLOCA alloca
#endif

static const std::array<nri::Format, 44> g_NriFormat =
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

static inline nri::Format nrdGetNriFormat(nrd::Format format)
{
    return g_NriFormat[(uint32_t)format];
}

static inline uint64_t CreateDescriptorKey(bool isStorage, uint8_t poolIndex, uint16_t indexInPool, uint8_t mipOffset, uint8_t mipNum)
{
    uint64_t key = isStorage ? 1 : 0;
    key |= uint64_t(poolIndex) << 1ull;
    key |= uint64_t(indexInPool) << 9ull;
    key |= uint64_t(mipOffset) << 25ull;
    key |= uint64_t(mipNum) << 33ull;

    return key;
}

bool Nrd::Initialize(nri::Device& nriDevice, const nri::CoreInterface& nriCoreInterface, const nri::HelperInterface& nriHelperInterface, const nrd::DenoiserCreationDesc& denoiserCreationDesc)
{
    const nrd::LibraryDesc& libraryDesc = nrd::GetLibraryDesc();
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
    m_NRI = &nriCoreInterface;
    m_NRIHelper = &nriHelperInterface;

    CreatePipelines();
    CreateResources();

    return true;
}

void Nrd::CreatePipelines()
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

    nri::StaticSamplerDesc* staticSamplerDescs = (nri::StaticSamplerDesc*)NRD_ALLOCA( sizeof(nri::StaticSamplerDesc) * denoiserDesc.staticSamplerNum );
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

    nri::DescriptorRangeDesc* descriptorRanges = (nri::DescriptorRangeDesc*)NRD_ALLOCA( sizeof(nri::DescriptorRangeDesc) * denoiserDesc.descriptorSetDesc.maxDescriptorRangeNumPerPipeline );
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
        descriptorSetDesc.dynamicConstantBuffers = &dynamicConstantBufferDesc;
        descriptorSetDesc.dynamicConstantBufferNum = 1;

        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.descriptorSetNum = 1;
        pipelineLayoutDesc.descriptorSets = &descriptorSetDesc;
        pipelineLayoutDesc.ignoreGlobalSPIRVOffsets = true;
        pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::COMPUTE;

        nri::PipelineLayout* pipelineLayout = nullptr;
        NRD_ABORT_ON_FAILURE(m_NRI->CreatePipelineLayout(*m_Device, pipelineLayoutDesc, pipelineLayout));
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
        NRD_ABORT_ON_FAILURE(m_NRI->CreateComputePipeline(*m_Device, pipelineDesc, pipeline));
        m_Pipelines.push_back(pipeline);
    }

    m_IsShadersReloadRequested = true;
}

void Nrd::CreateResources()
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
        const nri::Format format = nrdGetNriFormat(nrdTextureDesc.format);

        nri::CTextureDesc textureDesc = nri::CTextureDesc::Texture2D(format, nrdTextureDesc.width, nrdTextureDesc.height, nrdTextureDesc.mipNum, 1, nri::TextureUsageBits::SHADER_RESOURCE | nri::TextureUsageBits::SHADER_RESOURCE_STORAGE);
        nri::Texture* texture = nullptr;
        NRD_ABORT_ON_FAILURE(m_NRI->CreateTexture(*m_Device, textureDesc, texture));

        NrdTexture nrdTexture = {};
        nrdTexture.texture = texture;
        nrdTexture.states = &m_ResourceState[resourceStateNum];
        nrdTexture.format = format;
        m_TexturePool[i] = nrdTexture;

        for (uint16_t mip = 0; mip < nrdTextureDesc.mipNum; mip++)
            nrdTexture.states[mip] = nri::TextureTransition(texture, nri::AccessBits::UNKNOWN, nri::TextureLayout::UNKNOWN, mip, 1);

        resourceStateNum += nrdTextureDesc.mipNum;
    }

    // Constant buffer
    const nri::DeviceDesc& deviceDesc = m_NRI->GetDeviceDesc(*m_Device);
    m_ConstantBufferViewSize = helper::GetAlignedSize(denoiserDesc.constantBufferDesc.maxDataSize, deviceDesc.constantBufferOffsetAlignment);
    m_ConstantBufferSize = uint64_t(m_ConstantBufferViewSize) * denoiserDesc.descriptorSetDesc.setNum * m_BufferedFrameMaxNum;

    nri::BufferDesc bufferDesc = {};
    bufferDesc.size = m_ConstantBufferSize;
    bufferDesc.usageMask = nri::BufferUsageBits::CONSTANT_BUFFER;

    NRD_ABORT_ON_FAILURE(m_NRI->CreateBuffer(*m_Device, bufferDesc, m_ConstantBuffer));

    AllocateAndBindMemory();

    nri::BufferViewDesc constantBufferViewDesc = {};
    constantBufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
    constantBufferViewDesc.buffer = m_ConstantBuffer;
    constantBufferViewDesc.size = m_ConstantBufferViewSize;

    NRD_ABORT_ON_FAILURE(m_NRI->CreateBufferView(constantBufferViewDesc, m_ConstantBufferView));

    // Descriptor pools
    nri::DescriptorPoolDesc descriptorPoolDesc = {};
    descriptorPoolDesc.descriptorSetMaxNum = denoiserDesc.descriptorSetDesc.setNum;
    descriptorPoolDesc.storageTextureMaxNum = denoiserDesc.descriptorSetDesc.storageTextureNum;
    descriptorPoolDesc.textureMaxNum = denoiserDesc.descriptorSetDesc.textureNum;
    descriptorPoolDesc.dynamicConstantBufferMaxNum = denoiserDesc.descriptorSetDesc.constantBufferNum;
    descriptorPoolDesc.staticSamplerMaxNum = denoiserDesc.descriptorSetDesc.constantBufferNum * denoiserDesc.staticSamplerNum; // 1 CB per pass

    for (nri::DescriptorPool*& descriptorPool : m_DescriptorPools)
        NRD_ABORT_ON_FAILURE(m_NRI->CreateDescriptorPool(*m_Device, descriptorPoolDesc, descriptorPool));
}

void Nrd::AllocateAndBindMemory()
{
    std::vector<nri::Texture*> textures(m_TexturePool.size(), nullptr);
    for (size_t i = 0; i < m_TexturePool.size(); i++)
        textures[i] = m_TexturePool[i].texture;

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = (uint32_t)textures.size();
    resourceGroupDesc.textures = textures.data();

    size_t baseAllocation = m_MemoryAllocations.size();
    const size_t allocationNum = m_NRIHelper->CalculateAllocationNumber(*m_Device, resourceGroupDesc);
    m_MemoryAllocations.resize(baseAllocation + allocationNum, nullptr);
    NRD_ABORT_ON_FAILURE(m_NRIHelper->AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));

    resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_ConstantBuffer;

    baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + 1, nullptr);
    NRD_ABORT_ON_FAILURE(m_NRIHelper->AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));
}

void Nrd::SetMethodSettings(nrd::Method method, const void* methodSettings)
{
    nrd::Result result = nrd::SetMethodSettings(*m_Denoiser, method, methodSettings);
    assert(result == nrd::Result::SUCCESS);
}

void Nrd::Denoise(uint32_t consecutiveFrameIndex, nri::CommandBuffer& commandBuffer, const nrd::CommonSettings& commonSettings, const NrdUserPool& userPool)
{
    #if( NRD_DEBUG_LOGGING == 1 )
        char s[128];
        sprintf_s(s, "Frame %u ==============================================================================\n", consecutiveFrameIndex);
        OutputDebugStringA(s);
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

        char annotationName[128];
        snprintf(annotationName, sizeof(annotationName), "%s", dispatchDesc.name);
        helper::Annotation annotation(*m_NRI, commandBuffer, annotationName);

        Dispatch(commandBuffer, *descriptorPool, dispatchDesc, userPool);
    }
}

void Nrd::Dispatch(nri::CommandBuffer& commandBuffer, nri::DescriptorPool& descriptorPool, const nrd::DispatchDesc& dispatchDesc, const NrdUserPool& userPool)
{
    const nrd::DenoiserDesc& denoiserDesc = nrd::GetDenoiserDesc(*m_Denoiser);
    const nrd::PipelineDesc& pipelineDesc = denoiserDesc.pipelines[dispatchDesc.pipelineIndex];

    uint32_t transitionNum = 0;
    for (uint32_t i = 0; i < dispatchDesc.resourceNum; i++)
        transitionNum += dispatchDesc.resources[i].mipNum;

    nri::Descriptor** descriptors = (nri::Descriptor**)NRD_ALLOCA( sizeof(nri::Descriptor*) * dispatchDesc.resourceNum );
    memset(descriptors, 0, sizeof(nri::Descriptor*) * dispatchDesc.resourceNum);

    nri::DescriptorRangeUpdateDesc* descriptorRangeUpdateDescs = (nri::DescriptorRangeUpdateDesc*)NRD_ALLOCA( sizeof(nri::DescriptorRangeUpdateDesc) * pipelineDesc.descriptorRangeNum );
    memset(descriptorRangeUpdateDescs, 0, sizeof(nri::DescriptorRangeUpdateDesc) * pipelineDesc.descriptorRangeNum);

    nri::TextureTransitionBarrierDesc* transitions = (nri::TextureTransitionBarrierDesc*)NRD_ALLOCA( sizeof(nri::TextureTransitionBarrierDesc) * transitionNum );
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

            NrdTexture* nrdTexture = nullptr;
            if (nrdResource.type == nrd::ResourceType::TRANSIENT_POOL)
                nrdTexture = &m_TexturePool[nrdResource.indexInPool + denoiserDesc.permanentPoolSize];
            else if (nrdResource.type == nrd::ResourceType::PERMANENT_POOL)
                nrdTexture = &m_TexturePool[nrdResource.indexInPool];
            else
                nrdTexture = (NrdTexture*)&userPool[(uint32_t)nrdResource.type];

            const nri::AccessBits nextAccess = nrdResource.stateNeeded == nrd::DescriptorType::TEXTURE ? nri::AccessBits::SHADER_RESOURCE : nri::AccessBits::SHADER_RESOURCE_STORAGE;
            const nri::TextureLayout nextLayout =  nrdResource.stateNeeded == nrd::DescriptorType::TEXTURE ? nri::TextureLayout::SHADER_RESOURCE : nri::TextureLayout::GENERAL;
            for (uint16_t mip = 0; mip < nrdResource.mipNum; mip++)
            {
                nri::TextureTransitionBarrierDesc* state = nrdTexture->states + nrdResource.mipOffset + mip;
                bool isStateChanged = nextAccess != state->nextAccess || nextLayout != state->nextLayout;
                bool isStorageBarrier = nextAccess == nri::AccessBits::SHADER_RESOURCE_STORAGE && state->nextAccess == nri::AccessBits::SHADER_RESOURCE_STORAGE;
                if (isStateChanged || isStorageBarrier)
                    transitions[transitionBarriers.textureNum++] = nri::TextureTransition(*state, nextAccess, nextLayout, nrdResource.mipOffset + mip, 1);
            }

            const bool isStorage = descriptorRangeDesc.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
            uint64_t key = CreateDescriptorKey(isStorage, (uint8_t)nrdResource.type, nrdResource.indexInPool, (uint8_t)nrdResource.mipOffset, (uint8_t)nrdResource.mipNum);
            const auto& entry = m_Descriptors.find(key);

            nri::Descriptor* descriptor = nullptr;
            if (entry == m_Descriptors.end())
            {
                nri::Texture2DViewDesc desc = {nrdTexture->texture, isStorage ? nri::Texture2DViewType::SHADER_RESOURCE_STORAGE_2D : nri::Texture2DViewType::SHADER_RESOURCE_2D, nrdTexture->format, nrdResource.mipOffset, nrdResource.mipNum};
                NRD_ABORT_ON_FAILURE(m_NRI->CreateTexture2DView(desc, descriptor));
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
    NRD_ABORT_ON_FAILURE(m_NRI->AllocateDescriptorSets(descriptorPool, *pipelineLayout, 0, &descriptorSet, 1, nri::WHOLE_DEVICE_GROUP, 0));

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
    #if( NRD_DEBUG_LOGGING == 1 )
        static const char* names[] =
        {
            "IN_MV ",
            "IN_NORMAL_ROUGHNESS ",
            "IN_VIEWZ ",
            "IN_DIFF_HIT ",
            "IN_SPEC_HIT ",
            "IN_SHADOWDATA ",
            "IN_SHADOW_TRANSLUCENCY ",

            "OUT_SHADOW_TRANSLUCENCY ",
            "OUT_DIFF_HIT ",
            "OUT_SPEC_HIT ",
        };

        static_assert( _countof(names) == (uint32_t)nrd::ResourceType::MAX_NUM - 2 );

        char s[128];
        char t[32];

        sprintf_s(s, "Pipeline #%u (%s)\n", dispatchDesc.pipelineIndex, dispatchDesc.name);
        OutputDebugStringA(s);

        strcpy_s(s, "\t");
        for( uint32_t i = 0; i < dispatchDesc.resourceNum; i++ )
        {
            const nrd::Resource& r = dispatchDesc.resources[i];

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
    #endif
}

void Nrd::Destroy()
{
    // Assuming that the device is in IDLE state
    m_NRI->DestroyDescriptor(*m_ConstantBufferView);
    m_ConstantBufferView = nullptr;

    m_NRI->DestroyBuffer(*m_ConstantBuffer);
    m_ConstantBuffer = nullptr;

    for (const auto& entry : m_Descriptors)
        m_NRI->DestroyDescriptor(*entry.second);
    m_Descriptors.clear();

    for (const NrdTexture& nrdTexture : m_TexturePool)
        m_NRI->DestroyTexture(*nrdTexture.texture);
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
