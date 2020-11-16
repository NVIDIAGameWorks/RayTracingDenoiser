/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SampleBase.h"
#include "Extensions/NRIRayTracing.h"

#include <array>

struct Box
{
    std::vector<float> positions;
    std::vector<float> texcoords;
    std::vector<uint16_t> indices;
};

void SetBoxGeometry(uint32_t subdivisions, float boxHalfSize, Box& box);

constexpr uint32_t BOX_NUM = 1000;

struct NRIInterface
    : public nri::CoreInterface
    , public nri::SwapChainInterface
    , public nri::HelperInterface
{
};

struct Frame
{
    nri::DeviceSemaphore* deviceSemaphore;
    nri::CommandAllocator* commandAllocator;
    std::vector<nri::CommandBuffer*> commandBuffers;
};

class Sample : public SampleBase
{
public:
    Sample()
    {
    }

    ~Sample();

private:
    bool Initialize(nri::GraphicsAPI graphicsAPI);
    void PrepareFrame(uint32_t frameIndex);
    void RenderFrame(uint32_t frameIndex);
    void CreateSwapChain(nri::Format& swapChainFormat);
    void CreateCommandBuffers();
    void CreatePipeline(nri::Format swapChainFormat);
    void CreateGeometry();
    void CreateMainFrameBuffer(nri::Format swapChainFormat);
    void CreateDescriptorSet();
    void SetupProjViewMatrix(float4x4& projViewMatrix);
    void RecordGraphics(nri::CommandBuffer& commandBuffer, uint32_t physicalDeviceIndex);
    void CopyToSwapChainTexture(nri::CommandBuffer& commandBuffer, uint32_t renderingDeviceIndex, uint32_t presentingDeviceIndex);

    NRIInterface NRI = { };
    nri::Device* m_Device = nullptr;
    nri::SwapChain* m_SwapChain = nullptr;
    nri::CommandQueue* m_CommandQueue = nullptr;
    nri::QueueSemaphore* m_BackBufferAcquireSemaphore = nullptr;
    nri::QueueSemaphore* m_BackBufferReleaseSemaphore = nullptr;

    std::array<Frame, BUFFERED_FRAME_MAX_NUM> m_Frames;
    std::vector<nri::QueueSemaphore*> m_QueueSemaphores;

    nri::DescriptorSet* m_DescriptorSet = nullptr;
    nri::Descriptor* m_TransformBufferView = nullptr;
    nri::Descriptor* m_ColorTextureView = nullptr;
    nri::Descriptor* m_DepthTextureView = nullptr;
    nri::FrameBuffer* m_FrameBuffer = nullptr;
    nri::FrameBuffer* m_FrameBufferUI = nullptr;

    uint32_t m_PhyiscalDeviceGroupSize = 0;
    uint32_t m_BoxIndexNum = 0;
    bool m_IsMGPUEnabled = true;

    Timer m_FrameTime;

    nri::Pipeline* m_Pipeline = nullptr;
    nri::PipelineLayout* m_PipelineLayout = nullptr;
    nri::Buffer* m_VertexBuffer = nullptr;
    nri::Buffer* m_IndexBuffer = nullptr;
    nri::Buffer* m_TransformBuffer = nullptr;
    nri::Texture* m_DepthTexture = nullptr;
    nri::Texture* m_ColorTexture = nullptr;
    nri::DescriptorPool* m_DescriptorPool = nullptr;

    nri::Format m_DepthFormat = nri::Format::UNKNOWN;

    const BackBuffer* m_BackBuffer = nullptr;
    std::vector<BackBuffer> m_SwapChainBuffers;
    std::vector<nri::Memory*> m_MemoryAllocations;
};

Sample::~Sample()
{
    NRI.WaitForIdle(*m_CommandQueue);

    for (uint32_t i = 0; i < m_Frames.size(); i++)
    {
        Frame& frame = m_Frames[i];

        NRI.DestroyDeviceSemaphore(*frame.deviceSemaphore);

        for (uint32_t j = 0; j < frame.commandBuffers.size(); j++)
            NRI.DestroyCommandBuffer(*frame.commandBuffers[j]);

        NRI.DestroyCommandAllocator(*frame.commandAllocator);
    }

    NRI.DestroyFrameBuffer(*m_FrameBuffer);
    NRI.DestroyFrameBuffer(*m_FrameBufferUI);
    NRI.DestroyDescriptor(*m_ColorTextureView);
    NRI.DestroyDescriptor(*m_DepthTextureView);
    NRI.DestroyDescriptor(*m_TransformBufferView);

    for (uint32_t i = 0; i < m_QueueSemaphores.size(); i++)
        NRI.DestroyQueueSemaphore(*m_QueueSemaphores[i]);

    NRI.DestroyTexture(*m_ColorTexture);
    NRI.DestroyTexture(*m_DepthTexture);
    NRI.DestroyBuffer(*m_VertexBuffer);
    NRI.DestroyBuffer(*m_IndexBuffer);
    NRI.DestroyBuffer(*m_TransformBuffer);

    NRI.DestroyPipeline(*m_Pipeline);
    NRI.DestroyPipelineLayout(*m_PipelineLayout);

    NRI.DestroyQueueSemaphore(*m_BackBufferAcquireSemaphore);
    NRI.DestroyQueueSemaphore(*m_BackBufferReleaseSemaphore);
    NRI.DestroyDescriptorPool(*m_DescriptorPool);

    NRI.DestroySwapChain(*m_SwapChain);

    for (size_t i = 0; i < m_MemoryAllocations.size(); i++)
        NRI.FreeMemory(*m_MemoryAllocations[i]);

    m_UserInterface.Shutdown();

    nri::DestroyDevice(*m_Device);
}

bool Sample::Initialize(nri::GraphicsAPI graphicsAPI)
{
    uint32_t physicalDeviceGroupNum = 0;
    NRI_ABORT_ON_FAILURE(nri::GetPhysicalDevices(nullptr, physicalDeviceGroupNum));

    std::vector<nri::PhysicalDeviceGroup> physicalDeviceGroups(physicalDeviceGroupNum);
    NRI_ABORT_ON_FAILURE(nri::GetPhysicalDevices(physicalDeviceGroups.data(), physicalDeviceGroupNum));

    if (physicalDeviceGroupNum == 0)
        exit(1);

    nri::DeviceCreationDesc deviceCreationDesc = { };
    deviceCreationDesc.physicalDeviceGroup = &physicalDeviceGroups[0];
    deviceCreationDesc.graphicsAPI = graphicsAPI;
    deviceCreationDesc.enableAPIValidation = m_DebugAPI;
    deviceCreationDesc.enableNRIValidation = m_DebugNRI;
    deviceCreationDesc.enableMGPU = true;
    deviceCreationDesc.D3D11CommandBufferEmulation = D3D11_COMMANDBUFFER_EMULATION;
    deviceCreationDesc.spirvBindingOffsets = SPIRV_BINDING_OFFSETS;
    NRI_ABORT_ON_FAILURE(nri::CreateDevice(deviceCreationDesc, m_Device));

    NRI_ABORT_ON_FAILURE(nri::GetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI));
    NRI_ABORT_ON_FAILURE(nri::GetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&NRI));
    NRI_ABORT_ON_FAILURE(nri::GetInterface(*m_Device, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI));

    NRI_ABORT_ON_FAILURE(NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueue));
    NRI_ABORT_ON_FAILURE(NRI.CreateQueueSemaphore(*m_Device, m_BackBufferAcquireSemaphore));
    NRI_ABORT_ON_FAILURE(NRI.CreateQueueSemaphore(*m_Device, m_BackBufferReleaseSemaphore));

    m_DepthFormat = nri::GetSupportedDepthFormat(NRI, *m_Device, 24, false);

    m_PhyiscalDeviceGroupSize = NRI.GetDeviceDesc(*m_Device).phyiscalDeviceGroupSize;

    m_QueueSemaphores.resize(m_PhyiscalDeviceGroupSize);
    for (size_t i = 0; i < m_QueueSemaphores.size(); i++)
        NRI_ABORT_ON_FAILURE(NRI.CreateQueueSemaphore(*m_Device, m_QueueSemaphores[i]));

    CreateCommandBuffers();

    nri::Format swapChainFormat = nri::Format::UNKNOWN;
    CreateSwapChain(swapChainFormat);

    CreateMainFrameBuffer(swapChainFormat);
    CreatePipeline(swapChainFormat);
    CreateDescriptorSet();
    CreateGeometry();

    return m_UserInterface.Initialize(m_hWnd, *m_Device, NRI, NRI, GetWindowWidth(), GetWindowHeight(), BUFFERED_FRAME_MAX_NUM, swapChainFormat);
}

void Sample::PrepareFrame(uint32_t frameIndex)
{
    m_UserInterface.Prepare();

    ImGui::Begin("Multi-GPU", nullptr, ImGuiWindowFlags_NoResize);
    {
        if (m_PhyiscalDeviceGroupSize == 1)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

        ImGui::Checkbox("Use multiple GPUs", &m_IsMGPUEnabled);

        if (m_PhyiscalDeviceGroupSize == 1)
        {
            ImGui::PopStyleVar();
            m_IsMGPUEnabled = false;
        }

        m_FrameTime.UpdateElapsedTimeSinceLastSave();
        m_FrameTime.SaveCurrentTime();

        ImGui::Text("Phyiscal device group size: %u", m_PhyiscalDeviceGroupSize);
        ImGui::Text("Frametime: %.2f ms", m_FrameTime.GetSmoothedElapsedTime());
    }
    ImGui::End();
}

void Sample::RecordGraphics(nri::CommandBuffer& commandBuffer, uint32_t physicalDeviceIndex)
{
    NRI.BeginCommandBuffer(commandBuffer, m_DescriptorPool, physicalDeviceIndex);

    nri::TextureTransitionBarrierDesc textureTransition = { };
    textureTransition.texture = m_ColorTexture;
    textureTransition.prevAccess = nri::AccessBits::COPY_SOURCE;
    textureTransition.nextAccess = nri::AccessBits::COLOR_ATTACHMENT;
    textureTransition.prevLayout = nri::TextureLayout::GENERAL;
    textureTransition.nextLayout = nri::TextureLayout::COLOR_ATTACHMENT;
    textureTransition.arraySize = 1;
    textureTransition.mipNum = 1;

    nri::TransitionBarrierDesc transitionBarriers = { };
    transitionBarriers.textures = &textureTransition;
    transitionBarriers.textureNum = 1;

    NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);

    NRI.CmdBeginRenderPass(commandBuffer, *m_FrameBuffer, nri::RenderPassBeginFlag::NONE);

    const nri::Rect scissorRect = { 0, 0, GetWindowWidth(), GetWindowHeight() };
    const nri::Viewport viewport = { 0.0f, 0.0f, (float)scissorRect.width, (float)scissorRect.height, 0.0f, 1.0f };
    NRI.CmdSetViewports(commandBuffer, &viewport, 1);
    NRI.CmdSetScissors(commandBuffer, &scissorRect, 1);

    NRI.CmdSetPipelineLayout(commandBuffer, *m_PipelineLayout);
    NRI.CmdSetPipeline(commandBuffer, *m_Pipeline);
    NRI.CmdSetIndexBuffer(commandBuffer, *m_IndexBuffer, 0, nri::IndexType::UINT16);

    const uint64_t nullOffset = 0;
    NRI.CmdSetVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffer, &nullOffset);

    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    const uint32_t constantRangeSize = (uint32_t)helper::GetAlignedSize(sizeof(float4x4), deviceDesc.constantBufferOffsetAlignment);

    for (uint32_t i = 0; i < BOX_NUM; i++)
    {
        const uint32_t dynamicOffset = i * constantRangeSize;
        NRI.CmdSetDescriptorSets(commandBuffer, 0, 1, &m_DescriptorSet, &dynamicOffset);
        NRI.CmdDrawIndexed(commandBuffer, m_BoxIndexNum, 1, 0, 0, 0);
    }

    NRI.CmdEndRenderPass(commandBuffer);

    NRI.CmdBeginRenderPass(commandBuffer, *m_FrameBufferUI, nri::RenderPassBeginFlag::SKIP_FRAME_BUFFER_CLEAR);
    m_UserInterface.Render(commandBuffer);
    NRI.CmdEndRenderPass(commandBuffer);

    textureTransition.texture = m_ColorTexture;
    textureTransition.prevAccess = nri::AccessBits::COLOR_ATTACHMENT;
    textureTransition.nextAccess = nri::AccessBits::COPY_SOURCE;
    textureTransition.prevLayout = nri::TextureLayout::COLOR_ATTACHMENT;
    textureTransition.nextLayout = nri::TextureLayout::GENERAL;
    textureTransition.arraySize = 1;
    textureTransition.mipNum = 1;

    NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);

    NRI.EndCommandBuffer(commandBuffer);
}

void Sample::CopyToSwapChainTexture(nri::CommandBuffer& commandBuffer, uint32_t renderingDeviceIndex, uint32_t presentingDeviceIndex)
{
    nri::TextureTransitionBarrierDesc initialTransition = { };
    initialTransition.texture = m_BackBuffer->texture;
    initialTransition.prevAccess = nri::AccessBits::UNKNOWN;
    initialTransition.nextAccess = nri::AccessBits::COPY_DESTINATION;
    initialTransition.prevLayout = nri::TextureLayout::UNKNOWN;
    initialTransition.nextLayout = nri::TextureLayout::GENERAL;
    initialTransition.arraySize = 1;
    initialTransition.mipNum = 1;

    nri::TextureTransitionBarrierDesc finalTransition = { };
    finalTransition.texture = m_BackBuffer->texture;
    finalTransition.prevAccess = nri::AccessBits::COPY_DESTINATION;
    finalTransition.nextAccess = nri::AccessBits::UNKNOWN;
    finalTransition.prevLayout = nri::TextureLayout::GENERAL;
    finalTransition.nextLayout = nri::TextureLayout::PRESENT;
    finalTransition.arraySize = 1;
    finalTransition.mipNum = 1;

    nri::TransitionBarrierDesc initialTransitionBarriers = { };
    initialTransitionBarriers.textures = &initialTransition;
    initialTransitionBarriers.textureNum = 1;

    nri::TransitionBarrierDesc finalTransitionBarriers = { };
    finalTransitionBarriers.textures = &finalTransition;
    finalTransitionBarriers.textureNum = 1;

    NRI.BeginCommandBuffer(commandBuffer, nullptr, presentingDeviceIndex);
    NRI.CmdPipelineBarrier(commandBuffer, &initialTransitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);
    NRI.CmdCopyTexture(commandBuffer, *m_BackBuffer->texture, presentingDeviceIndex, nullptr, *m_ColorTexture, renderingDeviceIndex, nullptr);
    NRI.CmdPipelineBarrier(commandBuffer, &finalTransitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);
    NRI.EndCommandBuffer(commandBuffer);
}

void Sample::RenderFrame(uint32_t frameIndex)
{
    const Frame& frame = m_Frames[frameIndex % BUFFERED_FRAME_MAX_NUM];

    nri::DeviceSemaphore& deviceSemaphore = *frame.deviceSemaphore;
    NRI.WaitForSemaphore(*m_CommandQueue, deviceSemaphore);

    nri::CommandAllocator& commandAllocator = *frame.commandAllocator;
    NRI.ResetCommandAllocator(commandAllocator);

    constexpr uint32_t presentingDeviceIndex = 0;
    const uint32_t renderingDeviceIndex = m_IsMGPUEnabled ? (frameIndex % m_PhyiscalDeviceGroupSize) : presentingDeviceIndex;

    nri::WorkSubmissionDesc workSubmissionDesc = {};

    nri::CommandBuffer* graphics = frame.commandBuffers[0];
    RecordGraphics(*graphics, renderingDeviceIndex);

    workSubmissionDesc.commandBuffers = &graphics;
    workSubmissionDesc.commandBufferNum = 1;
    workSubmissionDesc.signal = &m_QueueSemaphores[renderingDeviceIndex];
    workSubmissionDesc.signalNum = 1;
    workSubmissionDesc.physicalDeviceIndex = renderingDeviceIndex;

    NRI.SubmitQueueWork(*m_CommandQueue, workSubmissionDesc, nullptr);

    const uint32_t backBufferIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain, *m_BackBufferAcquireSemaphore);
    m_BackBuffer = &m_SwapChainBuffers[backBufferIndex];

    nri::CommandBuffer* presenting = frame.commandBuffers[1];
    CopyToSwapChainTexture(*presenting, renderingDeviceIndex, presentingDeviceIndex);

    nri::QueueSemaphore* waitSemaphores[] = { m_BackBufferAcquireSemaphore, m_QueueSemaphores[renderingDeviceIndex] };

    workSubmissionDesc.commandBuffers = &presenting;
    workSubmissionDesc.commandBufferNum = 1;
    workSubmissionDesc.wait = waitSemaphores;
    workSubmissionDesc.waitNum = helper::GetCountOf(waitSemaphores);
    workSubmissionDesc.signal = &m_BackBufferReleaseSemaphore;
    workSubmissionDesc.signalNum = 1;
    workSubmissionDesc.physicalDeviceIndex = presentingDeviceIndex;

    NRI.SubmitQueueWork(*m_CommandQueue, workSubmissionDesc, &deviceSemaphore);
    NRI.SwapChainPresent(*m_SwapChain, *m_BackBufferReleaseSemaphore);
}

void Sample::CreateMainFrameBuffer(nri::Format swapChainFormat)
{
    nri::TextureDesc textureDesc = { };
    textureDesc.type = nri::TextureType::TEXTURE_2D;
    textureDesc.size[0] = GetWindowWidth();
    textureDesc.size[1] = GetWindowHeight();
    textureDesc.size[2] = 1;
    textureDesc.mipNum = 1;
    textureDesc.arraySize = 1;
    textureDesc.sampleNum = 1;

    textureDesc.format = m_DepthFormat;
    textureDesc.usageMask = nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;
    NRI_ABORT_ON_FAILURE(NRI.CreateTexture(*m_Device, textureDesc, m_DepthTexture));

    textureDesc.format = swapChainFormat;
    textureDesc.usageMask = nri::TextureUsageBits::COLOR_ATTACHMENT;
    NRI_ABORT_ON_FAILURE(NRI.CreateTexture(*m_Device, textureDesc, m_ColorTexture));

    nri::Texture* textures[] = { m_DepthTexture, m_ColorTexture };

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = 2;
    resourceGroupDesc.textures = textures;

    const size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));

    nri::TextureTransitionBarrierDesc textureTransitionBarriers[2] = {};
    textureTransitionBarriers[0].texture = m_DepthTexture;
    textureTransitionBarriers[0].prevLayout = nri::TextureLayout::UNKNOWN;
    textureTransitionBarriers[0].nextLayout = nri::TextureLayout::DEPTH_STENCIL;
    textureTransitionBarriers[0].nextAccess = nri::AccessBits::DEPTH_STENCIL_WRITE;
    textureTransitionBarriers[1].texture = m_ColorTexture;
    textureTransitionBarriers[1].prevLayout = nri::TextureLayout::UNKNOWN;
    textureTransitionBarriers[1].nextLayout = nri::TextureLayout::GENERAL;
    textureTransitionBarriers[1].nextAccess = nri::AccessBits::COPY_SOURCE;

    nri::TransitionBarrierDesc transitionBarrierDesc = {};
    transitionBarrierDesc.textureNum = helper::GetCountOf(textureTransitionBarriers);
    transitionBarrierDesc.textures = textureTransitionBarriers;

    NRI_ABORT_ON_FAILURE(NRI.ChangeResourceStates(*m_CommandQueue, transitionBarrierDesc));

    nri::Texture2DViewDesc depthViewDesc = {m_DepthTexture, nri::Texture2DViewType::DEPTH_STENCIL_ATTACHMENT, m_DepthFormat};
    NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(depthViewDesc, m_DepthTextureView));

    nri::Texture2DViewDesc colorViewDesc = {m_ColorTexture, nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat};
    NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(colorViewDesc, m_ColorTextureView));

    nri::ClearValueDesc clearColor = {};

    nri::ClearValueDesc clearDepth = {};
    clearDepth.depthStencil = { 1.0f, 0 };

    nri::FrameBufferDesc frameBufferDesc = {};
    frameBufferDesc.colorAttachmentNum = 1;
    frameBufferDesc.colorClearValues = &clearColor;
    frameBufferDesc.depthStencilClearValue = &clearDepth;
    frameBufferDesc.colorAttachments = &m_ColorTextureView;
    frameBufferDesc.depthStencilAttachment = m_DepthTextureView;
    NRI_ABORT_ON_FAILURE(NRI.CreateFrameBuffer(*m_Device, frameBufferDesc, m_FrameBuffer));

    frameBufferDesc.depthStencilAttachment = nullptr;
    NRI_ABORT_ON_FAILURE(NRI.CreateFrameBuffer(*m_Device, frameBufferDesc, m_FrameBufferUI));
}

void Sample::CreateSwapChain(nri::Format& swapChainFormat)
{
    nri::SwapChainDesc swapChainDesc = {};
    swapChainDesc.windowHandle = m_hWnd;
    swapChainDesc.commandQueue = m_CommandQueue;
    swapChainDesc.format = nri::SwapChainFormat::BT709_G22_8BIT;
    swapChainDesc.verticalSyncInterval = m_SwapInterval;
    swapChainDesc.width = GetWindowWidth();
    swapChainDesc.height = GetWindowHeight();
    swapChainDesc.textureNum = SWAP_CHAIN_TEXTURE_NUM;

    NRI_ABORT_ON_FAILURE(NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain));

    uint32_t swapChainTextureNum = 0;
    nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum, swapChainFormat);

    for (uint32_t i = 0; i < swapChainTextureNum; i++)
    {
        m_SwapChainBuffers.emplace_back();
        m_SwapChainBuffers.back().texture = swapChainTextures[i];
    }
}

void Sample::CreateCommandBuffers()
{
    for (uint32_t i = 0; i < m_Frames.size(); i++)
    {
        Frame& frame = m_Frames[i];
        NRI_ABORT_ON_FAILURE(NRI.CreateDeviceSemaphore(*m_Device, true, frame.deviceSemaphore));
        NRI_ABORT_ON_FAILURE(NRI.CreateCommandAllocator(*m_CommandQueue, nri::WHOLE_DEVICE_GROUP, frame.commandAllocator));

        frame.commandBuffers.resize(2);
        for (uint32_t k = 0; k < frame.commandBuffers.size(); k++)
            NRI_ABORT_ON_FAILURE(NRI.CreateCommandBuffer(*frame.commandAllocator, frame.commandBuffers[k]));
    }
}

void Sample::CreatePipeline(nri::Format swapChainFormat)
{
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    utils::ShaderCodeStorage shaderCodeStorage;

    nri::DynamicConstantBufferDesc dynamicConstantBufferDesc = { 0, nri::ShaderStage::VERTEX };

    nri::DescriptorSetDesc descriptorSetDesc = {};
    descriptorSetDesc.dynamicConstantBuffers = &dynamicConstantBufferDesc;
    descriptorSetDesc.dynamicConstantBufferNum = 1;

    nri::PipelineLayoutDesc pipelineLayoutDesc = {};
    pipelineLayoutDesc.descriptorSets = &descriptorSetDesc;
    pipelineLayoutDesc.descriptorSetNum = 1;
    pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::VERTEX | nri::PipelineLayoutShaderStageBits::FRAGMENT;

    NRI_ABORT_ON_FAILURE(NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc, m_PipelineLayout));

    nri::VertexStreamDesc vertexStreamDesc = { };
    vertexStreamDesc.bindingSlot = 0;
    vertexStreamDesc.stride = 5 * sizeof(float);

    nri::VertexAttributeDesc vertexAttributeDesc[2] =
    {
        {
            { "POSITION", 0 }, { 0 },
            0,
            nri::Format::RGB32_SFLOAT,
        },
        {
            { "TEXCOORD", 0 }, { 1 },
            3 * sizeof(float),
            nri::Format::RG32_SFLOAT,
        }
    };

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

    nri::ColorAttachmentDesc colorAttachmentDesc = {};
    colorAttachmentDesc.format = swapChainFormat;
    colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;

    nri::DepthAttachmentDesc depthAttachmentDesc = {};
    depthAttachmentDesc.compareFunc = nri::CompareFunc::LESS;
    depthAttachmentDesc.write = true;

    nri::OutputMergerDesc outputMergerDesc = {};
    outputMergerDesc.colorNum = 1;
    outputMergerDesc.color = &colorAttachmentDesc;

    outputMergerDesc.depthStencilFormat = m_DepthFormat;
    outputMergerDesc.depth.compareFunc = nri::CompareFunc::LESS;
    outputMergerDesc.depth.write = true;

    nri::ShaderDesc shaderStages[] =
    {
        utils::LoadShader(deviceDesc.graphicsAPI, "06_Simple.vs", shaderCodeStorage),
        utils::LoadShader(deviceDesc.graphicsAPI, "06_Simple.fs", shaderCodeStorage),
    };

    nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
    graphicsPipelineDesc.pipelineLayout = m_PipelineLayout;
    graphicsPipelineDesc.inputAssembly = &inputAssemblyDesc;
    graphicsPipelineDesc.rasterization = &rasterizationDesc;
    graphicsPipelineDesc.outputMerger = &outputMergerDesc;
    graphicsPipelineDesc.shaderStages = shaderStages;
    graphicsPipelineDesc.shaderStageNum = helper::GetCountOf(shaderStages);

    NRI_ABORT_ON_FAILURE(NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, m_Pipeline));
}

void Sample::CreateDescriptorSet()
{
    nri::DescriptorPoolDesc descriptorPoolDesc = {};
    descriptorPoolDesc.dynamicConstantBufferMaxNum = m_PhyiscalDeviceGroupSize;
    descriptorPoolDesc.descriptorSetMaxNum = m_PhyiscalDeviceGroupSize;
    NRI_ABORT_ON_FAILURE(NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc, m_DescriptorPool));

    NRI_ABORT_ON_FAILURE(NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 0, &m_DescriptorSet, 1,
        nri::WHOLE_DEVICE_GROUP, 0));
}

void Sample::CreateGeometry()
{
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    const uint64_t constantRangeSize = helper::GetAlignedSize(sizeof(float4x4), deviceDesc.constantBufferOffsetAlignment);

    Box box;
    SetBoxGeometry(64, 0.5f, box);

    const uint32_t vertexNum = uint32_t(box.positions.size() / 3);
    std::vector<float> vertexData(vertexNum * 5);
    for (uint32_t i = 0; i < vertexNum; i++)
    {
        vertexData[i * 5] = box.positions[i * 3];
        vertexData[i * 5 + 1] = box.positions[i * 3 + 1];
        vertexData[i * 5 + 2] = box.positions[i * 3 + 2];
        vertexData[i * 5 + 3] = box.texcoords[i * 2 + 0];
        vertexData[i * 5 + 4] = box.texcoords[i * 2 + 1];
    }

    m_BoxIndexNum = (uint32_t)box.indices.size();

    const nri::BufferDesc vertexBufferDesc = { helper::GetByteSizeOf(vertexData), 0, nri::BufferUsageBits::VERTEX_BUFFER };
    const nri::BufferDesc indexBufferDesc = { helper::GetByteSizeOf(box.indices), 0, nri::BufferUsageBits::INDEX_BUFFER };
    const nri::BufferDesc transformBufferDesc = { BOX_NUM * constantRangeSize, 0, nri::BufferUsageBits::CONSTANT_BUFFER };
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, vertexBufferDesc, m_VertexBuffer));
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, indexBufferDesc, m_IndexBuffer));
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, transformBufferDesc, m_TransformBuffer));

    nri::Buffer* buffers[] = { m_VertexBuffer, m_IndexBuffer, m_TransformBuffer };

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = helper::GetCountOf(buffers);
    resourceGroupDesc.buffers = buffers;

    const size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation))

    std::vector<uint8_t> transforms(transformBufferDesc.size);

    float4x4 projViewMatrix;
    SetupProjViewMatrix(projViewMatrix);

    constexpr uint32_t lineSize = 17;

    for (uint32_t i = 0; i < BOX_NUM; i++)
    {
        float4x4 matrix = float4x4::identity;

        const size_t x = i % lineSize;
        const size_t y = i / lineSize;
        matrix.PreTranslation(float3(-1.35f * 0.5f * (lineSize - 1) + 1.35f * x, 8.0f + 1.25f * y, 0.0f));
        matrix.AddScale(float3(1.0f + 0.0001f * (rand() % 2001)));

        float4x4& transform = *(float4x4*)(transforms.data() + i * constantRangeSize);
        transform = projViewMatrix * matrix;
    }

    nri::BufferUploadDesc dataDescArray[] = {
        { vertexData.data(), vertexBufferDesc.size, m_VertexBuffer, 0, nri::AccessBits::UNKNOWN, nri::AccessBits::VERTEX_BUFFER },
        { box.indices.data(), indexBufferDesc.size, m_IndexBuffer, 0, nri::AccessBits::UNKNOWN, nri::AccessBits::INDEX_BUFFER },
        { transforms.data(), transformBufferDesc.size, m_TransformBuffer, 0, nri::AccessBits::UNKNOWN, nri::AccessBits::CONSTANT_BUFFER }
    };
    NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_CommandQueue, nullptr, 0, dataDescArray, helper::GetCountOf(dataDescArray)));

    nri::BufferViewDesc bufferViewDesc = { };
    bufferViewDesc.buffer = m_TransformBuffer;
    bufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
    bufferViewDesc.offset = 0;
    bufferViewDesc.size = constantRangeSize;

    NRI_ABORT_ON_FAILURE(NRI.CreateBufferView(bufferViewDesc, m_TransformBufferView));
    NRI.UpdateDynamicConstantBuffers(*m_DescriptorSet, nri::WHOLE_DEVICE_GROUP, 0, 1, &m_TransformBufferView);
}

void Sample::SetupProjViewMatrix(float4x4& projViewMatrix)
{
    const uint32_t windowWidth = GetWindowWidth();
    const uint32_t windowHeight = GetWindowHeight();
    const float aspect = float(windowWidth) / float(windowHeight);

    float4x4 projectionMatrix;
    projectionMatrix.SetupByHalfFovxInf(DegToRad(45.0f), aspect, 0.1f, 0);

    float4x4 viewMatrix;
    viewMatrix.SetIdentity();
    viewMatrix.SetupByRotationYPR(DegToRad(0.0f), DegToRad(0.0f), 0.0f);
    viewMatrix.WorldToView();

    const float3 cameraPosition = float3(0.0f, -4.5f, 2.0f);
    viewMatrix.PreTranslation(-cameraPosition);

    projViewMatrix = projectionMatrix * viewMatrix;
}

SAMPLE_MAIN(Sample, 0);

void SetPositions(Box& box, uint32_t positionOffset, float x, float y, float z)
{
    box.positions[positionOffset + 0] = x;
    box.positions[positionOffset + 1] = y;
    box.positions[positionOffset + 2] = z;
}

void SetTexcoords(Box& box, uint32_t texcoordOffset, float x, float y)
{
    box.texcoords[texcoordOffset + 0] = x;
    box.texcoords[texcoordOffset + 1] = y;
}

void SetQuadIndices(Box& box, uint32_t indexOffset, uint16_t topVertexIndex, uint16_t bottomVertexIndex)
{
    box.indices[indexOffset + 0] = bottomVertexIndex;
    box.indices[indexOffset + 1] = topVertexIndex;
    box.indices[indexOffset + 2] = topVertexIndex + 1;
    box.indices[indexOffset + 3] = bottomVertexIndex;
    box.indices[indexOffset + 4] = topVertexIndex + 1;
    box.indices[indexOffset + 5] = bottomVertexIndex + 1;
}

void SetBoxGeometry(uint32_t subdivisions, float boxHalfSize, Box& box)
{
    const uint32_t edgeVertexNum = subdivisions + 1;

    constexpr uint32_t positionsPerVertex = 3;
    constexpr uint32_t texcoordsPerVertex = 2;
    constexpr uint32_t verticesPerTriangle = 3;
    constexpr uint32_t trianglesPerQuad = 2;
    constexpr uint32_t facesPerBox = 6;

    const float positionStep = 2.0f * boxHalfSize / (edgeVertexNum - 1);
    const float texcoordStep = 1.0f / (edgeVertexNum - 1);

    const uint32_t verticesPerFace = edgeVertexNum * edgeVertexNum;
    const uint32_t quadsPerFace = subdivisions * subdivisions;

    if (facesPerBox * verticesPerFace > UINT16_MAX)
        exit(1);

    const uint32_t positionFaceStride = verticesPerFace * positionsPerVertex;
    const uint32_t texcoordFaceStride = verticesPerFace * texcoordsPerVertex;

    box.positions.resize(facesPerBox * verticesPerFace * positionsPerVertex, 0);
    box.texcoords.resize(facesPerBox * verticesPerFace * texcoordsPerVertex, 0);

    for (uint32_t i = 0; i < edgeVertexNum; i++)
    {
        const float positionX = -boxHalfSize + i * positionStep;
        const float texcoordX = i * texcoordStep;

        for (uint32_t j = 0; j < edgeVertexNum; j++)
        {
            const float positionY = -boxHalfSize + j * positionStep;
            const float texcoordY = j * texcoordStep;

            const uint32_t vertexIndex = i + j * edgeVertexNum;
            uint32_t positionOffset = vertexIndex * positionsPerVertex;
            uint32_t texcoordOffset = vertexIndex * texcoordsPerVertex;

            SetPositions(box, positionOffset + 0 * positionFaceStride, positionX, positionY, -boxHalfSize);
            SetPositions(box, positionOffset + 1 * positionFaceStride, positionX, positionY, +boxHalfSize);
            SetPositions(box, positionOffset + 2 * positionFaceStride, -boxHalfSize, positionX, positionY);
            SetPositions(box, positionOffset + 3 * positionFaceStride, +boxHalfSize, positionX, positionY);
            SetPositions(box, positionOffset + 4 * positionFaceStride, positionX, -boxHalfSize, positionY);
            SetPositions(box, positionOffset + 5 * positionFaceStride, positionX, +boxHalfSize, positionY);

            for (uint32_t k = 0; k < facesPerBox; k++)
                SetTexcoords(box, texcoordOffset + k * texcoordFaceStride, texcoordX, texcoordY);
        }
    }

    const uint32_t indexFaceStride = quadsPerFace * trianglesPerQuad * verticesPerTriangle;

    box.indices.resize(facesPerBox * quadsPerFace * trianglesPerQuad * verticesPerTriangle, 0);

    for (uint32_t i = 0; i < subdivisions; i++)
    {
        for (uint32_t j = 0; j < subdivisions; j++)
        {
            const uint32_t quadIndex = j + i * subdivisions;
            const uint32_t indexOffset = quadIndex * trianglesPerQuad * verticesPerTriangle;

            const uint32_t topVertexIndex = j + i * edgeVertexNum;
            const uint32_t bottomVertexIndex = j + (i + 1) * edgeVertexNum;

            for (uint32_t k = 0; k < facesPerBox; k++)
            {
                SetQuadIndices(box, indexOffset + k * indexFaceStride, uint16_t(topVertexIndex + k * verticesPerFace),
                    uint16_t(bottomVertexIndex + k * verticesPerFace));
            }
        }
    }
}