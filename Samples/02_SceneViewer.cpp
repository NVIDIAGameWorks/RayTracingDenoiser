/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SampleBase.h"

#include <array>

constexpr uint32_t GLOBAL_DESCRIPTOR_SET = 0;
constexpr uint32_t MATERIAL_DESCRIPTOR_SET = 1;
constexpr float CLEAR_DEPTH = 0.0f;
constexpr uint32_t TEXTURES_PER_MATERIAL = 4;
constexpr uint32_t CONSTANT_BUFFER = 0;
constexpr uint32_t INDEX_BUFFER = 1;
constexpr uint32_t VERTEX_BUFFER = 2;

struct GlobalConstantBufferLayout
{
    float4x4 gWorldToClip;
    float3 gCameraPos;
};

struct NRIInterface
    : public nri::CoreInterface
    , public nri::SwapChainInterface
    , public nri::HelperInterface
{};

class Sample : public SampleBase
{
public:

    Sample()
    {}

    ~Sample();

    bool Initialize(nri::GraphicsAPI graphicsAPI);
    void PrepareFrame(uint32_t frameIndex);
    void RenderFrame(uint32_t frameIndex);
    void UpdateConstantBuffer(uint32_t frameIndex);

private:

    NRIInterface NRI = {};
    nri::Device* m_Device = nullptr;
    nri::SwapChain* m_SwapChain = nullptr;
    nri::CommandQueue* m_CommandQueue = nullptr;
    nri::QueueSemaphore* m_AcquireSemaphore = nullptr;
    nri::QueueSemaphore* m_ReleaseSemaphore = nullptr;
    nri::DescriptorPool* m_DescriptorPool = nullptr;

    std::array<nri::DeviceSemaphore*, BUFFERED_FRAME_MAX_NUM> m_DeviceSemaphore = {};
    std::array<nri::CommandAllocator*, BUFFERED_FRAME_MAX_NUM> m_CommandAllocator = {};
    std::array<nri::CommandBuffer*, BUFFERED_FRAME_MAX_NUM> m_CommandBuffer = {};
    std::array<uint32_t, BUFFERED_FRAME_MAX_NUM>  m_GlobalConstantBufferViewOffsets = {};
    nri::PipelineLayout* m_PipelineLayout = nullptr;
    std::vector<nri::Pipeline*> m_Pipelines;
    std::vector<BackBuffer> m_SwapChainBuffers;
    std::vector<nri::DescriptorSet*> m_DescriptorSets;
    std::vector<nri::Texture*> m_Textures;
    std::vector<nri::Buffer*> m_Buffers;
    std::vector<nri::Memory*> m_MemoryAllocations;
    std::vector<nri::Descriptor*> m_Descriptors;

    nri::Format m_DepthFormat = nri::Format::UNKNOWN;

    utils::Scene m_Scene;
};

Sample::~Sample()
{
    NRI.WaitForIdle(*m_CommandQueue);

    for (uint32_t i = 0; i < BUFFERED_FRAME_MAX_NUM; i++)
    {
        NRI.DestroyCommandBuffer(*m_CommandBuffer[i]);
        NRI.DestroyCommandAllocator(*m_CommandAllocator[i]);
        NRI.DestroyDeviceSemaphore(*m_DeviceSemaphore[i]);
    }

    for (uint32_t i = 0; i < m_SwapChainBuffers.size(); i++)
    {
        NRI.DestroyFrameBuffer(*m_SwapChainBuffers[i].frameBuffer);
        NRI.DestroyDescriptor(*m_SwapChainBuffers[i].colorAttachment);
    }

    for (size_t i = 0; i < m_Descriptors.size(); i++)
        NRI.DestroyDescriptor(*m_Descriptors[i]);

    for (size_t i = 0; i < m_Textures.size(); i++)
        NRI.DestroyTexture(*m_Textures[i]);

    for (size_t i = 0; i < m_Buffers.size(); i++)
        NRI.DestroyBuffer(*m_Buffers[i]);

    for (size_t i = 0; i < m_MemoryAllocations.size(); i++)
        NRI.FreeMemory(*m_MemoryAllocations[i]);

    for (size_t i = 0; i < m_Pipelines.size(); i++)
        NRI.DestroyPipeline(*m_Pipelines[i]);
    NRI.DestroyPipelineLayout(*m_PipelineLayout);

    NRI.DestroyDescriptorPool(*m_DescriptorPool);
    NRI.DestroyQueueSemaphore(*m_AcquireSemaphore);
    NRI.DestroyQueueSemaphore(*m_ReleaseSemaphore);
    NRI.DestroySwapChain(*m_SwapChain);

    nri::DestroyDevice(*m_Device);
}

bool Sample::Initialize(nri::GraphicsAPI graphicsAPI)
{
    // Device
    nri::DeviceCreationDesc deviceCreationDesc = {};
    deviceCreationDesc.graphicsAPI = graphicsAPI;
    deviceCreationDesc.enableAPIValidation = m_DebugAPI;
    deviceCreationDesc.enableNRIValidation = m_DebugNRI;
    deviceCreationDesc.D3D11CommandBufferEmulation = D3D11_COMMANDBUFFER_EMULATION;
    deviceCreationDesc.spirvBindingOffsets = SPIRV_BINDING_OFFSETS;
    NRI_ABORT_ON_FAILURE( nri::CreateDevice(deviceCreationDesc, m_Device) );

    // NRI
    NRI_ABORT_ON_FAILURE( nri::GetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::GetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::GetInterface(*m_Device, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI) );

    // Command queue
    NRI_ABORT_ON_FAILURE( NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueue) );

    m_DepthFormat = nri::GetSupportedDepthFormat(NRI, *m_Device, 24, false);

    // Swap chain
    {
        nri::SwapChainDesc swapChainDesc = {};
        swapChainDesc.windowHandle = m_hWnd;
        swapChainDesc.commandQueue = m_CommandQueue;
        swapChainDesc.format = nri::SwapChainFormat::BT709_G22_10BIT;
        swapChainDesc.verticalSyncInterval = m_SwapInterval;
        swapChainDesc.width = GetWindowWidth();
        swapChainDesc.height = GetWindowHeight();
        swapChainDesc.textureNum = SWAP_CHAIN_TEXTURE_NUM;
        NRI_ABORT_ON_FAILURE( NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain) );
    }

    uint32_t swapChainTextureNum;
    nri::Format swapChainFormat;
    nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum, swapChainFormat);

    NRI_ABORT_ON_FAILURE( NRI.CreateQueueSemaphore(*m_Device, m_AcquireSemaphore) );
    NRI_ABORT_ON_FAILURE( NRI.CreateQueueSemaphore(*m_Device, m_ReleaseSemaphore) );

    // Buffered resources
    for (uint32_t i = 0; i < BUFFERED_FRAME_MAX_NUM; i++)
    {
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandAllocator(*m_CommandQueue, nri::WHOLE_DEVICE_GROUP, m_CommandAllocator[i]) );
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandBuffer(*m_CommandAllocator[i], m_CommandBuffer[i]) );
        NRI_ABORT_ON_FAILURE( NRI.CreateDeviceSemaphore(*m_Device, true, m_DeviceSemaphore[i]) );
    }

    // Pipeline
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    utils::ShaderCodeStorage shaderCodeStorage;
    {
        nri::DescriptorRangeDesc globalDescriptorRange[2];
        globalDescriptorRange[0] = { 0, 1, nri::DescriptorType::CONSTANT_BUFFER, nri::ShaderStage::ALL };
        globalDescriptorRange[1] = { 0, 1, nri::DescriptorType::SAMPLER, nri::ShaderStage::FRAGMENT };

        nri::DescriptorRangeDesc materialDescriptorRange[1];
        materialDescriptorRange[0] = { 0, TEXTURES_PER_MATERIAL, nri::DescriptorType::TEXTURE, nri::ShaderStage::FRAGMENT };

        nri::DescriptorSetDesc descriptorSetDescs[] =
        {
            {globalDescriptorRange, helper::GetCountOf(globalDescriptorRange)},
            {materialDescriptorRange, helper::GetCountOf(materialDescriptorRange)},
        };

        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.descriptorSetNum = helper::GetCountOf(descriptorSetDescs);
        pipelineLayoutDesc.descriptorSets = descriptorSetDescs;
        pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::VERTEX | nri::PipelineLayoutShaderStageBits::FRAGMENT;

        NRI_ABORT_ON_FAILURE( NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc, m_PipelineLayout) );

        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;
        vertexStreamDesc.stride = sizeof(utils::Vertex);

        nri::VertexAttributeDesc vertexAttributeDesc[4] = {};
        {
            vertexAttributeDesc[0].format = nri::Format::RGB32_SFLOAT;
            vertexAttributeDesc[0].offset = helper::GetOffsetOf(&utils::Vertex::position);
            vertexAttributeDesc[0].d3d = {"POSITION", 0};
            vertexAttributeDesc[0].vk = {0};

            vertexAttributeDesc[1].format = nri::Format::RG16_SFLOAT;
            vertexAttributeDesc[1].offset = helper::GetOffsetOf(&utils::Vertex::uv);
            vertexAttributeDesc[1].d3d = {"TEXCOORD", 0};
            vertexAttributeDesc[1].vk = {1};

            vertexAttributeDesc[2].format = nri::Format::R10_G10_B10_A2_UNORM;
            vertexAttributeDesc[2].offset = helper::GetOffsetOf(&utils::Vertex::normal);
            vertexAttributeDesc[2].d3d = {"NORMAL", 0};
            vertexAttributeDesc[2].vk = {2};

            vertexAttributeDesc[3].format = nri::Format::R10_G10_B10_A2_UNORM;
            vertexAttributeDesc[3].offset = helper::GetOffsetOf(&utils::Vertex::tangent);
            vertexAttributeDesc[3].d3d = {"TANGENT", 0};
            vertexAttributeDesc[3].vk = {3};
        }

        nri::InputAssemblyDesc inputAssemblyDesc = {};
        inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;
        inputAssemblyDesc.attributes = vertexAttributeDesc;
        inputAssemblyDesc.attributeNum = (uint8_t)helper::GetCountOf(vertexAttributeDesc);
        inputAssemblyDesc.streams = &vertexStreamDesc;
        inputAssemblyDesc.streamNum = 1;

        nri::RasterizationDesc rasterizationDesc = {};
        rasterizationDesc.viewportNum = 1;
        rasterizationDesc.fillMode = nri::FillMode::SOLID;
        rasterizationDesc.cullMode = nri::CullMode::NONE;
        rasterizationDesc.sampleNum = 1;
        rasterizationDesc.sampleMask = 0xFFFF;
        rasterizationDesc.frontCounterClockwise = true;

        nri::ColorAttachmentDesc colorAttachmentDesc = {};
        colorAttachmentDesc.format = swapChainFormat;
        colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colorNum = 1;
        outputMergerDesc.color = &colorAttachmentDesc;
        outputMergerDesc.depthStencilFormat = m_DepthFormat;
        outputMergerDesc.depth.write = true;
        outputMergerDesc.depth.compareFunc = CLEAR_DEPTH == 1.0f ? nri::CompareFunc::LESS : nri::CompareFunc::GREATER;

        nri::ShaderDesc shaderStages[] =
        {
            utils::LoadShader(deviceDesc.graphicsAPI, "02_Forward.vs", shaderCodeStorage),
            utils::LoadShader(deviceDesc.graphicsAPI, "02_Forward.fs", shaderCodeStorage),
        };

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = m_PipelineLayout;
        graphicsPipelineDesc.inputAssembly = &inputAssemblyDesc;
        graphicsPipelineDesc.rasterization = &rasterizationDesc;
        graphicsPipelineDesc.outputMerger = &outputMergerDesc;
        graphicsPipelineDesc.shaderStages = shaderStages;
        graphicsPipelineDesc.shaderStageNum = helper::GetCountOf(shaderStages);

        nri::Pipeline* pipeline;

        // Opaque
        {
            NRI_ABORT_ON_FAILURE( NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, pipeline) );
            m_Pipelines.push_back(pipeline);
        }

        // Alpha opaque
        {
            shaderStages[1] = utils::LoadShader(deviceDesc.graphicsAPI, "02_ForwardDiscard.fs", shaderCodeStorage);

            rasterizationDesc.cullMode = nri::CullMode::NONE;
            outputMergerDesc.depth.write = true;
            colorAttachmentDesc.blendEnabled = false;
            NRI_ABORT_ON_FAILURE( NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, pipeline) );
            m_Pipelines.push_back(pipeline);
        }

        shaderStages[1] = utils::LoadShader(deviceDesc.graphicsAPI, "02_ForwardTransparent.fs", shaderCodeStorage);

        // Transparent (back faces)
        {
            rasterizationDesc.cullMode = nri::CullMode::FRONT;
            outputMergerDesc.depth.write = false;
            colorAttachmentDesc.blendEnabled = true;
            colorAttachmentDesc.colorBlend = {nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendFunc::ADD};
            NRI_ABORT_ON_FAILURE( NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, pipeline) );
            m_Pipelines.push_back(pipeline);
        }

        // Transparent (front faces)
        {
            rasterizationDesc.cullMode = nri::CullMode::BACK;
            outputMergerDesc.depth.write = false;
            colorAttachmentDesc.blendEnabled = true;
            colorAttachmentDesc.colorBlend = {nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendFunc::ADD};
            NRI_ABORT_ON_FAILURE( NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, pipeline) );
            m_Pipelines.push_back(pipeline);
        }
    }

    // Scene
    bool isLoaded = false;
    if (IsAutomated())
    {
        std::string sceneFile = utils::GetFullPath(m_SceneFile, utils::DataFolder::SCENES);
        isLoaded = utils::LoadScene(sceneFile, m_Scene, true);
    }
    else
    {
        bool isSelected = false;
        do
        {
            char sceneFile[1024];
            isSelected = OpenFileDialog(isLoaded ? "Add scene" : "Open scene", sceneFile, sizeof(sceneFile));
            if (isSelected)
            {
                isLoaded |= utils::LoadScene(sceneFile, m_Scene, true);
                if(isLoaded)
                    strcpy_s(m_SceneFile, sceneFile);
            }
        }
        while (isSelected);
    }
    NRI_ABORT_ON_FALSE(isLoaded);

    // Camera
    m_Camera.Initialize(m_Scene.aabb.GetCenter(), m_Scene.aabb.vMin, false);

    const uint32_t textureNum = (uint32_t)m_Scene.textures.size();
    const uint32_t materialNum = (uint32_t)m_Scene.materials.size();

    // Textures
    {
        for (const utils::Texture* textureData : m_Scene.textures)
        {
            nri::CTextureDesc textureDesc = nri::CTextureDesc::Texture2D(textureData->GetFormat(),
                textureData->GetWidth(), textureData->GetHeight(), textureData->GetMipNum(), textureData->GetArraySize());

            nri::Texture* texture;
            NRI_ABORT_ON_FAILURE( NRI.CreateTexture(*m_Device, textureDesc, texture) );
            m_Textures.push_back(texture);
        }
    }

    // Depth attachment
    nri::Texture* depthTexture;
    {
        nri::CTextureDesc textureDesc = nri::CTextureDesc::Texture2D(m_DepthFormat, GetWindowWidth(), GetWindowHeight(), 1, 1,
            nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT);

        NRI_ABORT_ON_FAILURE( NRI.CreateTexture(*m_Device, textureDesc, depthTexture) );
        m_Textures.push_back(depthTexture);
    }

    const uint32_t constantBufferSize = helper::GetAlignedSize((uint32_t)sizeof(GlobalConstantBufferLayout), deviceDesc.constantBufferOffsetAlignment);

    // Buffers
    {
        // Constant buffer
        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = constantBufferSize * BUFFERED_FRAME_MAX_NUM;
        bufferDesc.usageMask = nri::BufferUsageBits::CONSTANT_BUFFER;
        nri::Buffer* buffer;
        NRI_ABORT_ON_FAILURE( NRI.CreateBuffer(*m_Device, bufferDesc, buffer) );
        m_Buffers.push_back(buffer);

        // Index buffer
        bufferDesc.size = helper::GetByteSizeOf(m_Scene.indices);
        bufferDesc.usageMask = nri::BufferUsageBits::INDEX_BUFFER;
        NRI_ABORT_ON_FAILURE( NRI.CreateBuffer(*m_Device, bufferDesc, buffer) );
        m_Buffers.push_back(buffer);

        // Vertex buffer
        bufferDesc.size = helper::GetByteSizeOf(m_Scene.vertices);
        bufferDesc.usageMask = nri::BufferUsageBits::VERTEX_BUFFER;
        NRI_ABORT_ON_FAILURE( NRI.CreateBuffer(*m_Device, bufferDesc, buffer) );
        m_Buffers.push_back(buffer);
    }

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_Buffers[0];

    size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + 1, nullptr);
    NRI_ABORT_ON_FAILURE( NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation) );

    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = 2;
    resourceGroupDesc.buffers = &m_Buffers[1];
    resourceGroupDesc.textureNum = (uint32_t)m_Textures.size();
    resourceGroupDesc.textures = m_Textures.data();

    baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE( NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation) );

    // Create descriptors
    nri::Descriptor* anisotropicSampler;
    nri::Descriptor* constantBufferViews[BUFFERED_FRAME_MAX_NUM];
    {
        // Material textures
        m_Descriptors.resize(textureNum);
        for (uint32_t i = 0; i < textureNum; i++)
        {
            const utils::Texture& texture = *m_Scene.textures[i];

            nri::Texture2DViewDesc texture2DViewDesc = {m_Textures[i], nri::Texture2DViewType::SHADER_RESOURCE_2D, texture.GetFormat()};
            NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(texture2DViewDesc, m_Descriptors[i]) );
        }

        // Sampler
        nri::SamplerDesc samplerDesc = {};
        samplerDesc.anisotropy = 8;
        samplerDesc.addressModes = {nri::AddressMode::REPEAT, nri::AddressMode::REPEAT};
        samplerDesc.minification = nri::Filter::LINEAR;
        samplerDesc.magnification = nri::Filter::LINEAR;
        samplerDesc.mip = nri::Filter::LINEAR;
        samplerDesc.mipMax = 16.0f;
        NRI_ABORT_ON_FAILURE( NRI.CreateSampler(*m_Device, samplerDesc, anisotropicSampler) );
        m_Descriptors.push_back(anisotropicSampler);

        // Constant buffer
        for (uint32_t i = 0; i < BUFFERED_FRAME_MAX_NUM; i++)
        {
            m_GlobalConstantBufferViewOffsets[i] = i * constantBufferSize;

            nri::BufferViewDesc bufferViewDesc = {};
            bufferViewDesc.buffer = m_Buffers[CONSTANT_BUFFER];
            bufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
            bufferViewDesc.offset = m_GlobalConstantBufferViewOffsets[i];
            bufferViewDesc.size = constantBufferSize;
            NRI_ABORT_ON_FAILURE( NRI.CreateBufferView(bufferViewDesc, constantBufferViews[i]) );
            m_Descriptors.push_back(constantBufferViews[i]);
        }

        // Depth buffer
        nri::Texture2DViewDesc texture2DViewDesc = {depthTexture, nri::Texture2DViewType::DEPTH_STENCIL_ATTACHMENT, m_DepthFormat};

        nri::Descriptor* depthAttachment;
        NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(texture2DViewDesc, depthAttachment) );
        m_Descriptors.push_back(depthAttachment);

        // Swap chain
        for (uint32_t i = 0; i < swapChainTextureNum; i++)
        {
            nri::Texture2DViewDesc textureViewDesc = {swapChainTextures[i], nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat};

            nri::Descriptor* colorAttachment;
            NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(textureViewDesc, colorAttachment) );

            nri::ClearValueDesc clearColor = {};
            clearColor.rgba32f = {0.0f, 0.63f, 1.0f};

            nri::ClearValueDesc clearDepth = {};
            clearDepth.depthStencil.depth = CLEAR_DEPTH;

            nri::FrameBufferDesc frameBufferDesc = {};
            frameBufferDesc.colorAttachmentNum = 1;
            frameBufferDesc.colorAttachments = &colorAttachment;
            frameBufferDesc.colorClearValues = &clearColor;
            frameBufferDesc.depthStencilAttachment = depthAttachment;
            frameBufferDesc.depthStencilClearValue = &clearDepth;
            nri::FrameBuffer* frameBuffer;
            NRI_ABORT_ON_FAILURE( NRI.CreateFrameBuffer(*m_Device, frameBufferDesc, frameBuffer) );

            const BackBuffer backBuffer = { frameBuffer, frameBuffer, colorAttachment, swapChainTextures[i] };
            m_SwapChainBuffers.push_back(backBuffer);
        }
    }

    // Descriptor pool
    {
        nri::DescriptorPoolDesc descriptorPoolDesc = {};
        descriptorPoolDesc.descriptorSetMaxNum = materialNum + BUFFERED_FRAME_MAX_NUM;
        descriptorPoolDesc.textureMaxNum = materialNum * TEXTURES_PER_MATERIAL;
        descriptorPoolDesc.samplerMaxNum = BUFFERED_FRAME_MAX_NUM;
        descriptorPoolDesc.constantBufferMaxNum = BUFFERED_FRAME_MAX_NUM;

        NRI_ABORT_ON_FAILURE( NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc, m_DescriptorPool) );
    }

    // Descriptor sets
    {
        m_DescriptorSets.resize(BUFFERED_FRAME_MAX_NUM + materialNum);

        // Global
        NRI_ABORT_ON_FAILURE( NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, GLOBAL_DESCRIPTOR_SET,
            &m_DescriptorSets[0], BUFFERED_FRAME_MAX_NUM, nri::WHOLE_DEVICE_GROUP, 0) );

        for (uint32_t i = 0; i < BUFFERED_FRAME_MAX_NUM; i++)
        {
            nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDescs[2] = {};
            descriptorRangeUpdateDescs[0].descriptorNum = 1;
            descriptorRangeUpdateDescs[0].descriptors = &constantBufferViews[i];
            descriptorRangeUpdateDescs[1].descriptorNum = 1;
            descriptorRangeUpdateDescs[1].descriptors = &anisotropicSampler;

            NRI.UpdateDescriptorRanges(*m_DescriptorSets[i], nri::WHOLE_DEVICE_GROUP, 0, helper::GetCountOf(descriptorRangeUpdateDescs), descriptorRangeUpdateDescs);
        }

        // Material
        NRI_ABORT_ON_FAILURE( NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, MATERIAL_DESCRIPTOR_SET,
            &m_DescriptorSets[BUFFERED_FRAME_MAX_NUM], materialNum, nri::WHOLE_DEVICE_GROUP, 0) );

        for (uint32_t i = 0; i < materialNum; i++)
        {
            const utils::Material& material = m_Scene.materials[i];

            nri::Descriptor* materialTextures[TEXTURES_PER_MATERIAL] =
            {
                m_Descriptors[material.diffuseMapIndex],
                m_Descriptors[material.specularMapIndex],
                m_Descriptors[material.normalMapIndex],
                m_Descriptors[material.emissiveMapIndex],
            };

            nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDescs = {};
            descriptorRangeUpdateDescs.descriptorNum = helper::GetCountOf(materialTextures);
            descriptorRangeUpdateDescs.descriptors = materialTextures;
            NRI.UpdateDescriptorRanges(*m_DescriptorSets[BUFFERED_FRAME_MAX_NUM + i], nri::WHOLE_DEVICE_GROUP, 0, 1, &descriptorRangeUpdateDescs);
        }
    }

    // Upload data
    {
        std::vector<nri::TextureUploadDesc> textureData(1 + textureNum);

        uint32_t subresourceNum = 0;
        for (uint32_t i = 0; i < textureNum; i++)
        {
            const utils::Texture& texture = *m_Scene.textures[i];
            subresourceNum += texture.GetArraySize() * texture.GetMipNum();
        }

        std::vector<nri::TextureSubresourceUploadDesc> subresources(subresourceNum);
        nri::TextureSubresourceUploadDesc* subresourceBegin = subresources.data();

        textureData[0] = {};
        textureData[0].subresources = nullptr;
        textureData[0].texture = depthTexture;
        textureData[0].nextLayout = nri::TextureLayout::DEPTH_STENCIL;
        textureData[0].nextAccess = nri::AccessBits::DEPTH_STENCIL_WRITE;

        for (uint32_t i = 0; i < textureNum; i++)
        {
            const utils::Texture& texture = *m_Scene.textures[i];

            for (uint32_t slice = 0; slice < texture.GetArraySize(); slice++)
            {
                for (uint32_t mip = 0; mip < texture.GetMipNum(); mip++)
                    texture.GetSubresource(subresourceBegin[slice * texture.GetMipNum() + mip], mip, slice);
            }

            const uint32_t j = i + 1;
            textureData[j] = {};
            textureData[j].subresources = subresourceBegin;
            textureData[j].mipNum = texture.GetMipNum();
            textureData[j].arraySize = texture.GetArraySize();
            textureData[j].texture = m_Textures[i];
            textureData[j].nextLayout = nri::TextureLayout::SHADER_RESOURCE;
            textureData[j].nextAccess = nri::AccessBits::SHADER_RESOURCE;

            subresourceBegin += texture.GetArraySize() * texture.GetMipNum();
        }

        nri::BufferUploadDesc bufferData[] =
        {
            {m_Scene.vertices.data(), helper::GetByteSizeOf(m_Scene.vertices), m_Buffers[VERTEX_BUFFER], 0, nri::AccessBits::UNKNOWN, nri::AccessBits::VERTEX_BUFFER},
            {m_Scene.indices.data(), helper::GetByteSizeOf(m_Scene.indices), m_Buffers[INDEX_BUFFER], 0, nri::AccessBits::UNKNOWN, nri::AccessBits::INDEX_BUFFER},
        };



        NRI_ABORT_ON_FAILURE( NRI.UploadData(*m_CommandQueue, textureData.data(), (uint32_t)textureData.size(), bufferData, helper::GetCountOf(bufferData)) );
    }

    m_Scene.UnloadResources();

    return true;
}

void Sample::PrepareFrame(uint32_t frameIndex)
{
    CameraDesc desc = {};
    desc.aspectRatio = float( GetWindowWidth() ) / float( GetWindowHeight() );
    desc.horizontalFov = 90.0f;
    desc.nearZ = 0.1f;
    desc.isProjectionReversed = (CLEAR_DEPTH == 0.0f);
    GetCameraDescFromInputDevices(desc);

    m_Camera.Update(desc, frameIndex);
}

void Sample::UpdateConstantBuffer(uint32_t frameIndex)
{
    const uint32_t bufferedFrameIndex = frameIndex % BUFFERED_FRAME_MAX_NUM;
    const uint64_t rangeOffset = m_GlobalConstantBufferViewOffsets[bufferedFrameIndex];

    auto constants = (GlobalConstantBufferLayout*)NRI.MapBuffer(*m_Buffers[CONSTANT_BUFFER], rangeOffset, sizeof(GlobalConstantBufferLayout));
    if (constants)
    {
        constants->gWorldToClip = m_Camera.state.mWorldToClip;
        constants->gCameraPos = m_Camera.state.position;

        NRI.UnmapBuffer(*m_Buffers[CONSTANT_BUFFER]);
    }
}

void Sample::RenderFrame(uint32_t frameIndex)
{
    const uint32_t bufferedFrameIndex = frameIndex % BUFFERED_FRAME_MAX_NUM;
    const uint32_t windowWidth = GetWindowWidth();
    const uint32_t windowHeight = GetWindowHeight();

    const uint32_t currentTextureIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain, *m_AcquireSemaphore);
    BackBuffer& currentBackBuffer = m_SwapChainBuffers[currentTextureIndex];

    nri::DeviceSemaphore& deviceSemaphore = *m_DeviceSemaphore[bufferedFrameIndex];
    NRI.WaitForSemaphore(*m_CommandQueue, deviceSemaphore);

    UpdateConstantBuffer(frameIndex);

    nri::CommandAllocator& commandAllocator = *m_CommandAllocator[bufferedFrameIndex];
    NRI.ResetCommandAllocator(commandAllocator);

    nri::CommandBuffer& commandBuffer = *m_CommandBuffer[bufferedFrameIndex];
    NRI.BeginCommandBuffer(commandBuffer, m_DescriptorPool, 0);
    {
        helper::Annotation annotation(NRI, commandBuffer, "Scene");

        nri::TextureTransitionBarrierDesc textureTransitionBarrierDesc = {};
        textureTransitionBarrierDesc.texture = currentBackBuffer.texture;
        textureTransitionBarrierDesc.prevAccess = nri::AccessBits::UNKNOWN;
        textureTransitionBarrierDesc.nextAccess = nri::AccessBits::COLOR_ATTACHMENT;
        textureTransitionBarrierDesc.prevLayout = nri::TextureLayout::UNKNOWN;
        textureTransitionBarrierDesc.nextLayout = nri::TextureLayout::COLOR_ATTACHMENT;
        textureTransitionBarrierDesc.arraySize = 1;
        textureTransitionBarrierDesc.mipNum = 1;

        nri::TransitionBarrierDesc transitionBarriers = {};
        transitionBarriers.textureNum = 1;
        transitionBarriers.textures = &textureTransitionBarrierDesc;
        NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);

        NRI.CmdBeginRenderPass(commandBuffer, *currentBackBuffer.frameBuffer, nri::RenderPassBeginFlag::NONE);
        {
            const nri::Viewport viewport = { 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f };
            const nri::Rect scissor = { 0, 0, windowWidth, windowHeight };
            NRI.CmdSetViewports(commandBuffer,  &viewport, 1);
            NRI.CmdSetScissors(commandBuffer,  &scissor, 1);
            NRI.CmdSetIndexBuffer(commandBuffer, *m_Buffers[INDEX_BUFFER], 0, nri::IndexType::UINT16);

            NRI.CmdSetPipelineLayout(commandBuffer, *m_PipelineLayout);
            NRI.CmdSetDescriptorSets(commandBuffer, GLOBAL_DESCRIPTOR_SET, 1, &m_DescriptorSets[bufferedFrameIndex], nullptr);

            for (size_t i = 0; i < m_Scene.materialsGroups.size(); i++)
            {
                const utils::MaterialGroup& materialGroup = m_Scene.materialsGroups[i];
                NRI.CmdSetPipeline(commandBuffer, *m_Pipelines[i]);

                constexpr uint64_t offset = 0;
                NRI.CmdSetVertexBuffers(commandBuffer, 0, 1, &m_Buffers[VERTEX_BUFFER], &offset);

                for (uint32_t j = 0; j < materialGroup.materialNum; j++)
                {
                    const uint32_t materialIndex = materialGroup.materialOffset + j;
                    nri::DescriptorSet* descriptorSet = m_DescriptorSets[BUFFERED_FRAME_MAX_NUM + materialIndex];
                    NRI.CmdSetDescriptorSets(commandBuffer, MATERIAL_DESCRIPTOR_SET, 1, &descriptorSet, nullptr);

                    const utils::Material& material = m_Scene.materials[materialIndex];
                    for (uint32_t k = 0; k < material.instanceNum; k++)
                    {
                        // TODO: add instance transform support!
                        const utils::Instance& instance = m_Scene.instances[material.instanceOffset + k];
                        const utils::Mesh& mesh = m_Scene.meshes[instance.meshIndex];
                        NRI.CmdDrawIndexed(commandBuffer, mesh.indexNum, 1, mesh.indexOffset, mesh.vertexOffset, 0);
                    }
                }
            }
        }
        NRI.CmdEndRenderPass(commandBuffer);

        textureTransitionBarrierDesc.prevAccess = textureTransitionBarrierDesc.nextAccess;
        textureTransitionBarrierDesc.nextAccess = nri::AccessBits::UNKNOWN;
        textureTransitionBarrierDesc.prevLayout = textureTransitionBarrierDesc.nextLayout;
        textureTransitionBarrierDesc.nextLayout = nri::TextureLayout::PRESENT;

        NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);
    }
    NRI.EndCommandBuffer(commandBuffer);

    const nri::CommandBuffer* commandBufferArray[] = { &commandBuffer };

    nri::WorkSubmissionDesc workSubmissionDesc = {};
    workSubmissionDesc.commandBufferNum = 1;
    workSubmissionDesc.commandBuffers = commandBufferArray;
    workSubmissionDesc.wait = &m_AcquireSemaphore;
    workSubmissionDesc.waitNum = 1;
    workSubmissionDesc.signal = &m_ReleaseSemaphore;
    workSubmissionDesc.signalNum = 1;
    NRI.SubmitQueueWork(*m_CommandQueue, workSubmissionDesc, &deviceSemaphore);

    NRI.SwapChainPresent(*m_SwapChain, *m_ReleaseSemaphore);
}

SAMPLE_MAIN(Sample, 0);
