/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SampleBase.h"

#include <array>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>

constexpr uint32_t BOX_NUM = 100000;
constexpr uint32_t DRAW_CALLS_PER_PIPELINE = 4;
constexpr uint32_t THREAD_MAX_NUM = 256;
constexpr uint32_t CACHELINE_SIZE = 64;

struct Vertex
{
    float position[3];
    float texCoords[2];
};

struct Box
{
    uint32_t dynamicConstantBufferOffset;
    nri::DescriptorSet* descriptorSet;
    nri::Pipeline* pipeline;
};

struct ThreadContext
{
    std::array<nri::CommandAllocator*, BUFFERED_FRAME_MAX_NUM> commandAllocators;
    std::array<nri::CommandBuffer*, BUFFERED_FRAME_MAX_NUM> commandBuffers;
    std::thread thread;
    alignas(CACHELINE_SIZE) std::atomic_uint32_t control;
    alignas(CACHELINE_SIZE) uint32_t padding;
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
    {
    }

    ~Sample();

private:
    bool Initialize(nri::GraphicsAPI graphicsAPI);
    void PrepareFrame(uint32_t frameIndex);
    void RenderFrame(uint32_t frameIndex);
    void RenderBoxes(nri::CommandBuffer& commandBuffer, uint32_t offset, uint32_t number);
    void ThreadEntryPoint(uint32_t threadIndex);
    void CreateSwapChain(nri::Format& swapChainFormat);
    void CreateCommandBuffers();
    bool CreatePipeline(nri::Format swapChainFormat);
    void CreateDepthTexture();
    void CreateVertexBuffer();
    void CreateDescriptorPool();
    void LoadTextures();
    void CreateTransformConstantBuffer();
    void CreateDescriptorSets();
    void CreateFakeConstantBuffers();
    void CreateViewConstantBuffer();
    void SetupProjViewMatrix(float4x4& projViewMatrix);
    uint32_t GetPhysicalCoreNum() const;

    bool m_IsMultithreadingEnabled = true;

    Timer m_RecordingTime;
    Timer m_SubmitTime;

    NRIInterface NRI = {};
    nri::Device* m_Device = nullptr;
    nri::SwapChain* m_SwapChain = nullptr;
    nri::CommandQueue* m_CommandQueue = nullptr;
    nri::QueueSemaphore* m_BackBufferAcquireSemaphore = nullptr;
    nri::QueueSemaphore* m_BackBufferReleaseSemaphore = nullptr;

    std::array<nri::DeviceSemaphore*, BUFFERED_FRAME_MAX_NUM> m_DeviceSemaphore = {};
    std::vector<nri::CommandBuffer*> m_FrameCommandBuffers;
    uint32_t m_FrameIndex = 0;

    ThreadContext m_ThreadContexts[THREAD_MAX_NUM];
    uint32_t m_ThreadNum = 0;

    std::vector<nri::Pipeline*> m_Pipelines;

    std::vector<nri::Texture*> m_Textures;
    std::vector<nri::Descriptor*> m_TextureViews;
    nri::Texture* m_DepthTexture = nullptr;
    nri::Descriptor* m_DepthTextureView = nullptr;

    nri::Buffer* m_VertexBuffer = nullptr;
    nri::Buffer* m_IndexBuffer = nullptr;
    nri::Buffer* m_TransformConstantBuffer = nullptr;
    nri::Buffer* m_ViewConstantBuffer = nullptr;
    nri::Buffer* m_FakeConstantBuffer = nullptr;

    nri::Descriptor* m_TransformConstantBufferView = nullptr;
    nri::Descriptor* m_ViewConstantBufferView = nullptr;
    std::vector<nri::Descriptor*> m_FakeConstantBufferViews;

    nri::PipelineLayout* m_PipelineLayout = nullptr;

    nri::DescriptorPool* m_DescriptorPool = nullptr;

    std::vector<Box> m_Boxes;
    uint32_t m_BoxesPerThread = 0;

    uint32_t m_IndexNum = 0;

    const BackBuffer* m_BackBuffer = nullptr;

    nri::Format m_DepthFormat = nri::Format::UNKNOWN;

    std::vector<BackBuffer> m_SwapChainBuffers;
    std::vector<nri::Memory*> m_MemoryAllocations;

    alignas(CACHELINE_SIZE) std::atomic_uint32_t m_ReadyCount;
};

Sample::~Sample()
{
    NRI.WaitForIdle(*m_CommandQueue);

    for (size_t i = 1; m_IsMultithreadingEnabled && i < m_ThreadNum; i++)
    {
        ThreadContext& context = m_ThreadContexts[i];
        context.control.store(2);
        context.thread.join();
    }

    for (size_t i = 0; i < m_ThreadNum; i++)
    {
        ThreadContext& context = m_ThreadContexts[i];

        for (size_t j = 0; j < context.commandAllocators.size(); j++)
        {
            NRI.DestroyCommandBuffer(*context.commandBuffers[j]);
            NRI.DestroyCommandAllocator(*context.commandAllocators[j]);
        }
    }

    for (uint32_t i = 0; i < BUFFERED_FRAME_MAX_NUM; i++)
        NRI.DestroyDeviceSemaphore(*m_DeviceSemaphore[i]);

    for (uint32_t i = 0; i < m_SwapChainBuffers.size(); i++)
    {
        NRI.DestroyFrameBuffer(*m_SwapChainBuffers[i].frameBuffer);
        NRI.DestroyFrameBuffer(*m_SwapChainBuffers[i].frameBufferUI);
        NRI.DestroyDescriptor(*m_SwapChainBuffers[i].colorAttachment);
    }

    for (size_t i = 0; i < m_Textures.size(); i++)
        NRI.DestroyDescriptor(*m_TextureViews[i]);

    for (size_t i = 0; i < m_Textures.size(); i++)
        NRI.DestroyTexture(*m_Textures[i]);

    NRI.DestroyDescriptor(*m_DepthTextureView);
    NRI.DestroyTexture(*m_DepthTexture);

    NRI.DestroyDescriptor(*m_TransformConstantBufferView);
    NRI.DestroyDescriptor(*m_ViewConstantBufferView);
    for (size_t i = 0; i < m_FakeConstantBufferViews.size(); i++)
        NRI.DestroyDescriptor(*m_FakeConstantBufferViews[i]);

    NRI.DestroyBuffer(*m_TransformConstantBuffer);
    NRI.DestroyBuffer(*m_ViewConstantBuffer);
    NRI.DestroyBuffer(*m_FakeConstantBuffer);

    NRI.DestroyBuffer(*m_VertexBuffer);
    NRI.DestroyBuffer(*m_IndexBuffer);

    for (size_t i = 0; i < m_Pipelines.size(); i++)
        NRI.DestroyPipeline(*m_Pipelines[i]);
    NRI.DestroyPipelineLayout(*m_PipelineLayout);

    NRI.DestroyDescriptorPool(*m_DescriptorPool);

    NRI.DestroyQueueSemaphore(*m_BackBufferAcquireSemaphore);
    NRI.DestroyQueueSemaphore(*m_BackBufferReleaseSemaphore);

    NRI.DestroySwapChain(*m_SwapChain);

    for (size_t i = 0; i < m_MemoryAllocations.size(); i++)
        NRI.FreeMemory(*m_MemoryAllocations[i]);

    m_UserInterface.Shutdown();

    nri::DestroyDevice(*m_Device);
}

bool Sample::Initialize(nri::GraphicsAPI graphicsAPI)
{
    const uint32_t logicalCoreNum = std::thread::hardware_concurrency();
    const uint32_t phyiscalCoreNum = GetPhysicalCoreNum();
    const uint32_t ratio = std::max(logicalCoreNum / std::max(phyiscalCoreNum, 1u), 1u);

    m_ThreadNum = std::min((phyiscalCoreNum - 1) * ratio, THREAD_MAX_NUM);
    for (uint32_t i = 0; i < m_ThreadNum; i++)
    {
        ThreadContext& context = m_ThreadContexts[i];
        context.control.store(0, std::memory_order_relaxed);
        context.commandAllocators.fill(nullptr);
        context.commandBuffers.fill(nullptr);
    }

    m_FrameCommandBuffers.resize(m_ThreadNum);

    m_Boxes.resize(std::max(BOX_NUM, m_ThreadNum));
    m_BoxesPerThread = (uint32_t)m_Boxes.size() / m_ThreadNum;

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

    NRI_ABORT_ON_FAILURE( NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueue));
    NRI_ABORT_ON_FAILURE( NRI.CreateQueueSemaphore(*m_Device, m_BackBufferAcquireSemaphore));
    NRI_ABORT_ON_FAILURE( NRI.CreateQueueSemaphore(*m_Device, m_BackBufferReleaseSemaphore));

    CreateCommandBuffers();

    m_DepthFormat = nri::GetSupportedDepthFormat(NRI, *m_Device, 24, false);

    CreateDepthTexture();

    nri::Format swapChainFormat = nri::Format::UNKNOWN;
    CreateSwapChain(swapChainFormat);

    NRI_ABORT_ON_FALSE( CreatePipeline(swapChainFormat) );

    LoadTextures();
    CreateFakeConstantBuffers();
    CreateViewConstantBuffer();
    CreateVertexBuffer();
    CreateDescriptorPool();

    CreateTransformConstantBuffer();
    CreateDescriptorSets();

    if (m_IsMultithreadingEnabled)
    {
        for (uint32_t i = 1; i < m_ThreadNum; i++)
            m_ThreadContexts[i].thread = std::thread(&Sample::ThreadEntryPoint, this, i);
    }

    return m_UserInterface.Initialize(m_hWnd, *m_Device, NRI, NRI, GetWindowWidth(), GetWindowHeight(), BUFFERED_FRAME_MAX_NUM, swapChainFormat);
}

void Sample::PrepareFrame(uint32_t frameIndex)
{
    m_UserInterface.Prepare();

    ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoResize);
    {
        ImGui::Text("Box number: %u", (uint32_t)m_Boxes.size());
        ImGui::Text("Draw calls per pipeline: %u", DRAW_CALLS_PER_PIPELINE);

        ImGui::Text("Command buffer recording: %.2f ms", m_RecordingTime.GetSmoothedElapsedTime());
        ImGui::Text("Command buffer submit: %.2f ms", m_SubmitTime.GetSmoothedElapsedTime());

        bool isMultithreadingEnabled = m_IsMultithreadingEnabled;
        ImGui::Checkbox("Multithreading", &isMultithreadingEnabled);

        if (isMultithreadingEnabled != m_IsMultithreadingEnabled)
        {
            m_IsMultithreadingEnabled = isMultithreadingEnabled;

            if (m_IsMultithreadingEnabled)
            {
                for (uint32_t i = 1; i < m_ThreadNum; i++)
                {
                    ThreadContext& context = m_ThreadContexts[i];
                    context.control.store(0);
                    context.thread = std::thread(&Sample::ThreadEntryPoint, this, i);
                }
            }
            else
            {
                for (size_t i = 1; i < m_ThreadNum; i++)
                {
                    ThreadContext& context = m_ThreadContexts[i];
                    context.control.store(2);
                    context.thread.join();
                }
            }
        }
    }
    ImGui::End();
}

void Sample::RenderFrame(uint32_t frameIndex)
{
    frameIndex = frameIndex % BUFFERED_FRAME_MAX_NUM;

    const uint32_t backBufferIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain, *m_BackBufferAcquireSemaphore);
    m_BackBuffer = &m_SwapChainBuffers[backBufferIndex];

    nri::DeviceSemaphore& deviceSemaphore = *m_DeviceSemaphore[frameIndex];
    NRI.WaitForSemaphore(*m_CommandQueue, deviceSemaphore);

    m_FrameIndex = frameIndex;

    m_RecordingTime.SaveCurrentTime();

    if (m_IsMultithreadingEnabled)
    {
        m_ReadyCount.store(0, std::memory_order_seq_cst);
        for (uint32_t i = 1; i < m_ThreadNum; i++)
            m_ThreadContexts[i].control.store(1, std::memory_order_relaxed);
    }

    const uint32_t threadIndex = 0;
    const ThreadContext& context = m_ThreadContexts[threadIndex];

    nri::CommandAllocator& commandAllocator = *context.commandAllocators[frameIndex];
    NRI.ResetCommandAllocator(commandAllocator);

    nri::CommandBuffer& commandBuffer = *context.commandBuffers[frameIndex];
    m_FrameCommandBuffers[threadIndex] = &commandBuffer;

    NRI.BeginCommandBuffer(commandBuffer, m_DescriptorPool, 0);

    nri::TextureTransitionBarrierDesc backBufferTransition = {};
    backBufferTransition.texture = m_BackBuffer->texture;
    backBufferTransition.prevAccess = nri::AccessBits::UNKNOWN;
    backBufferTransition.nextAccess = nri::AccessBits::COLOR_ATTACHMENT;
    backBufferTransition.prevLayout = nri::TextureLayout::UNKNOWN;
    backBufferTransition.nextLayout = nri::TextureLayout::COLOR_ATTACHMENT;
    backBufferTransition.arraySize = nri::REMAINING_ARRAY_LAYERS;
    backBufferTransition.mipNum = nri::REMAINING_MIP_LEVELS;

    nri::TransitionBarrierDesc transitionBarriers = {};
    transitionBarriers.textures = &backBufferTransition;
    transitionBarriers.textureNum = 1;

    NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);

    NRI.CmdBeginAnnotation(commandBuffer, "Thread0");
    NRI.CmdBeginRenderPass(commandBuffer, *m_BackBuffer->frameBuffer, nri::RenderPassBeginFlag::NONE);

    if (m_IsMultithreadingEnabled)
    {
        const uint32_t baseBoxIndex = threadIndex * m_BoxesPerThread;
        const uint32_t boxNum = std::min(m_BoxesPerThread, (uint32_t)m_Boxes.size() - baseBoxIndex);
        RenderBoxes(commandBuffer, baseBoxIndex, boxNum);
    }
    else
    {
        RenderBoxes(commandBuffer, 0, (uint32_t)m_Boxes.size());
    }

    NRI.CmdEndRenderPass(commandBuffer);
    NRI.CmdEndAnnotation(commandBuffer);

    if (!m_IsMultithreadingEnabled)
    {
        NRI.CmdBeginRenderPass(commandBuffer, *m_BackBuffer->frameBufferUI, nri::RenderPassBeginFlag::SKIP_FRAME_BUFFER_CLEAR);
        m_UserInterface.Render(commandBuffer);
        NRI.CmdEndRenderPass(commandBuffer);

        backBufferTransition.texture = m_BackBuffer->texture;
        backBufferTransition.prevAccess = nri::AccessBits::COLOR_ATTACHMENT;
        backBufferTransition.nextAccess = nri::AccessBits::UNKNOWN;
        backBufferTransition.prevLayout = nri::TextureLayout::COLOR_ATTACHMENT;
        backBufferTransition.nextLayout = nri::TextureLayout::PRESENT;
        backBufferTransition.arraySize = 1;
        backBufferTransition.mipNum = 1;

        transitionBarriers.textures = &backBufferTransition;
        transitionBarriers.textureNum = 1;

        NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);
    }

    NRI.EndCommandBuffer(commandBuffer);

    while (m_IsMultithreadingEnabled && m_ReadyCount.load(std::memory_order_relaxed) != m_ThreadNum - 1)
    {
        for (volatile uint32_t i = 0; i < 100; i++)
            ;
    }

    m_RecordingTime.UpdateElapsedTimeSinceLastSave();

    // Submit work
    nri::WorkSubmissionDesc workSubmissionDesc = {};
    workSubmissionDesc.commandBuffers = m_FrameCommandBuffers.data();
    workSubmissionDesc.commandBufferNum = m_IsMultithreadingEnabled ? m_ThreadNum : 1;
    workSubmissionDesc.signal = &m_BackBufferReleaseSemaphore;
    workSubmissionDesc.signalNum = 1;
    workSubmissionDesc.wait = &m_BackBufferAcquireSemaphore;
    workSubmissionDesc.waitNum = 1;

    m_SubmitTime.SaveCurrentTime();
    NRI.SubmitQueueWork(*m_CommandQueue, workSubmissionDesc, &deviceSemaphore);
    m_SubmitTime.UpdateElapsedTimeSinceLastSave();

    NRI.SwapChainPresent(*m_SwapChain, *m_BackBufferReleaseSemaphore);
}

void Sample::RenderBoxes(nri::CommandBuffer& commandBuffer, uint32_t offset, uint32_t number)
{
    const nri::Rect scissorRect = { 0, 0, GetWindowWidth(), GetWindowHeight() };
    const nri::Viewport viewport = { 0.0f, 0.0f, (float)scissorRect.width, (float)scissorRect.height, 0.0f, 1.0f };
    NRI.CmdSetViewports(commandBuffer, &viewport, 1);
    NRI.CmdSetScissors(commandBuffer, &scissorRect, 1);
    NRI.CmdSetPipelineLayout(commandBuffer, *m_PipelineLayout);

    const uint64_t nullOffset = 0;

    for (uint32_t i = 0; i < number; i++)
    {
        const Box& box = m_Boxes[offset + i];

        NRI.CmdSetPipeline(commandBuffer, *box.pipeline);
        NRI.CmdSetDescriptorSets(commandBuffer, 0, 1, &box.descriptorSet, &box.dynamicConstantBufferOffset);
        NRI.CmdSetIndexBuffer(commandBuffer, *m_IndexBuffer, 0, nri::IndexType::UINT16);
        NRI.CmdSetVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffer, &nullOffset);
        NRI.CmdDrawIndexed(commandBuffer, m_IndexNum, 1, 0, 0, 0);
    }
}

void Sample::ThreadEntryPoint(uint32_t threadIndex)
{
    ThreadContext& context = m_ThreadContexts[threadIndex];

    uint32_t control = 0;

    while (control != 2)
    {
        control = context.control.load(std::memory_order_relaxed);

        if (control != 1)
            continue;

        context.control.store(0, std::memory_order_seq_cst);

        nri::CommandAllocator& commandAllocator = *context.commandAllocators[m_FrameIndex];
        NRI.ResetCommandAllocator(commandAllocator);

        nri::CommandBuffer& commandBuffer = *context.commandBuffers[m_FrameIndex];
        m_FrameCommandBuffers[threadIndex] = &commandBuffer;

        NRI.BeginCommandBuffer(commandBuffer, m_DescriptorPool, 0);

        char annotation[64];
        snprintf(annotation, sizeof(annotation), "Thread%u", threadIndex);

        NRI.CmdBeginAnnotation(commandBuffer, annotation);
        NRI.CmdBeginRenderPass(commandBuffer, *m_BackBuffer->frameBuffer, nri::RenderPassBeginFlag::SKIP_FRAME_BUFFER_CLEAR);

        const uint32_t baseBoxIndex = threadIndex * m_BoxesPerThread;
        const uint32_t boxNum = std::min(m_BoxesPerThread, (uint32_t)m_Boxes.size() - baseBoxIndex);
        RenderBoxes(commandBuffer, baseBoxIndex, boxNum);

        NRI.CmdEndRenderPass(commandBuffer);
        NRI.CmdEndAnnotation(commandBuffer);

        if (threadIndex == m_ThreadNum - 1)
        {
            NRI.CmdBeginRenderPass(commandBuffer, *m_BackBuffer->frameBufferUI, nri::RenderPassBeginFlag::SKIP_FRAME_BUFFER_CLEAR);
            m_UserInterface.Render(commandBuffer);
            NRI.CmdEndRenderPass(commandBuffer);
        }

        if (threadIndex == m_ThreadNum - 1)
        {
            nri::TextureTransitionBarrierDesc backBufferTransition = {};
            backBufferTransition.texture = m_BackBuffer->texture;
            backBufferTransition.prevAccess = nri::AccessBits::COLOR_ATTACHMENT;
            backBufferTransition.nextAccess = nri::AccessBits::UNKNOWN;
            backBufferTransition.prevLayout = nri::TextureLayout::COLOR_ATTACHMENT;
            backBufferTransition.nextLayout = nri::TextureLayout::PRESENT;
            backBufferTransition.arraySize = 1;
            backBufferTransition.mipNum = 1;

            nri::TransitionBarrierDesc transitionBarriers = {};
            transitionBarriers.textures = &backBufferTransition;
            transitionBarriers.textureNum = 1;

            NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);
        }

        NRI.EndCommandBuffer(commandBuffer);

        m_ReadyCount.fetch_add(1, std::memory_order_release);
    }
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

    nri::ClearValueDesc clearDepth = {};
    clearDepth.depthStencil = { 1.0f, 0 };

    nri::ClearValueDesc clearColor = {};
    nri::FrameBufferDesc frameBufferDesc = {};
    frameBufferDesc.colorAttachmentNum = 1;
    frameBufferDesc.colorClearValues = &clearColor;
    frameBufferDesc.depthStencilClearValue = &clearDepth;
    frameBufferDesc.depthStencilAttachment = m_DepthTextureView;

    for (uint32_t i = 0; i < swapChainTextureNum; i++)
    {
        m_SwapChainBuffers.emplace_back();
        BackBuffer& backBuffer = m_SwapChainBuffers.back();

        backBuffer.texture = swapChainTextures[i];

        nri::Texture2DViewDesc textureViewDesc = {backBuffer.texture, nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat};
        NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(textureViewDesc, backBuffer.colorAttachment));

        frameBufferDesc.depthStencilAttachment = m_DepthTextureView;
        frameBufferDesc.colorAttachments = &backBuffer.colorAttachment;
        NRI_ABORT_ON_FAILURE(NRI.CreateFrameBuffer(*m_Device, frameBufferDesc, backBuffer.frameBuffer));

        frameBufferDesc.depthStencilAttachment = nullptr;
        NRI_ABORT_ON_FAILURE(NRI.CreateFrameBuffer(*m_Device, frameBufferDesc, backBuffer.frameBufferUI));
    }
}

void Sample::CreateCommandBuffers()
{
    for (uint32_t j = 0; j < BUFFERED_FRAME_MAX_NUM; j++)
    {
        for (uint32_t i = 0; i < m_ThreadNum; i++)
        {
            ThreadContext& context = m_ThreadContexts[i];
            NRI_ABORT_ON_FAILURE(NRI.CreateCommandAllocator(*m_CommandQueue, nri::WHOLE_DEVICE_GROUP, context.commandAllocators[j]));
            NRI_ABORT_ON_FAILURE(NRI.CreateCommandBuffer(*context.commandAllocators[j], context.commandBuffers[j]));
        }

        NRI_ABORT_ON_FAILURE(NRI.CreateDeviceSemaphore(*m_Device, true, m_DeviceSemaphore[j]));
    }
}

bool Sample::CreatePipeline(nri::Format swapChainFormat)
{
    nri::DescriptorRangeDesc descriptorRanges[] =
    {
        { 1, 3, nri::DescriptorType::CONSTANT_BUFFER, nri::ShaderStage::ALL },
        { 0, 3, nri::DescriptorType::TEXTURE, nri::ShaderStage::FRAGMENT },
    };

    nri::DynamicConstantBufferDesc dynamicConstantBufferDesc = { 0, nri::ShaderStage::VERTEX };

    nri::SamplerDesc samplerDesc = {};
    samplerDesc.anisotropy = 4;
    samplerDesc.addressModes = {nri::AddressMode::MIRRORED_REPEAT, nri::AddressMode::MIRRORED_REPEAT};
    samplerDesc.minification = nri::Filter::LINEAR;
    samplerDesc.magnification = nri::Filter::LINEAR;
    samplerDesc.mip = nri::Filter::LINEAR;
    samplerDesc.mipMax = 16.0f;

    nri::StaticSamplerDesc staticSamplerDesc = { samplerDesc, 0, nri::ShaderStage::FRAGMENT };

    nri::DescriptorSetDesc descriptorSetDesc = { descriptorRanges, helper::GetCountOf(descriptorRanges), &staticSamplerDesc, 1, &dynamicConstantBufferDesc, 1 };

    nri::PipelineLayoutDesc pipelineLayoutDesc = {};
    pipelineLayoutDesc.descriptorSets = &descriptorSetDesc;
    pipelineLayoutDesc.descriptorSetNum = 1;
    pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::VERTEX | nri::PipelineLayoutShaderStageBits::FRAGMENT;

    NRI_ABORT_ON_FAILURE(NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc, m_PipelineLayout));

    constexpr uint32_t pipelineNum = 8;

    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    utils::ShaderCodeStorage shaderCodeStorage;

    nri::ShaderDesc shaders[1 + pipelineNum];
    shaders[0] = utils::LoadShader(deviceDesc.graphicsAPI, "05_Box.vs", shaderCodeStorage);
    for (uint32_t i = 0; i < pipelineNum; i++)
        shaders[1 + i] = utils::LoadShader(deviceDesc.graphicsAPI, "05_Box" + std::to_string(i) + ".fs", shaderCodeStorage);

    nri::VertexStreamDesc vertexStreamDesc = {};
    vertexStreamDesc.bindingSlot = 0;
    vertexStreamDesc.stride = sizeof(Vertex);

    nri::VertexAttributeDesc vertexAttributeDesc[2] =
    {
        {
            { "POSITION", 0 }, { 0 },
            helper::GetOffsetOf(&Vertex::position),
            nri::Format::RGB32_SFLOAT,
        },
        {
            { "TEXCOORD", 0 }, { 1 },
            helper::GetOffsetOf(&Vertex::texCoords),
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

    nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
    graphicsPipelineDesc.pipelineLayout = m_PipelineLayout;
    graphicsPipelineDesc.inputAssembly = &inputAssemblyDesc;
    graphicsPipelineDesc.rasterization = &rasterizationDesc;
    graphicsPipelineDesc.outputMerger = &outputMergerDesc;

    m_Pipelines.resize(pipelineNum);

    for (size_t i = 0; i < m_Pipelines.size(); i++)
    {
        nri::ShaderDesc shaderStages[] = {shaders[0], shaders[1 + i]};
        graphicsPipelineDesc.shaderStages = shaderStages;
        graphicsPipelineDesc.shaderStageNum = helper::GetCountOf(shaderStages);

        NRI_ABORT_ON_FAILURE(NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, m_Pipelines[i]));
    }

    return true;
}

void Sample::CreateDepthTexture()
{
    nri::CTextureDesc textureDesc = nri::CTextureDesc::Texture2D(m_DepthFormat, GetWindowWidth(), GetWindowHeight(), 1, 1,
        nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT);

    NRI_ABORT_ON_FAILURE(NRI.CreateTexture(*m_Device, textureDesc, m_DepthTexture));

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = 1;
    resourceGroupDesc.textures = &m_DepthTexture;

    const size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + 1, nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));

    nri::Texture2DViewDesc texture2DViewDesc = {m_DepthTexture, nri::Texture2DViewType::DEPTH_STENCIL_ATTACHMENT, m_DepthFormat};
    NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(texture2DViewDesc, m_DepthTextureView));

    nri::TextureUploadDesc textureData = {};
    textureData.texture = m_DepthTexture;
    textureData.nextLayout = nri::TextureLayout::DEPTH_STENCIL;
    textureData.nextAccess = nri::AccessBits::DEPTH_STENCIL_WRITE;
    NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_CommandQueue, &textureData, 1, nullptr, 0));
}

void Sample::CreateVertexBuffer()
{
    const float boxHalfSize = 0.5f;

    std::vector<Vertex> vertices
    {
        { { -boxHalfSize, -boxHalfSize, -boxHalfSize }, { 0.0f, 0.0f } }, { { -boxHalfSize, -boxHalfSize, boxHalfSize }, { 4.0f, 0.0f } },
        { { -boxHalfSize, boxHalfSize, -boxHalfSize }, { 0.0f, 4.0f } }, { { -boxHalfSize, boxHalfSize, boxHalfSize }, { 4.0f, 4.0f } },
        { { boxHalfSize, -boxHalfSize, -boxHalfSize }, { 0.0f, 0.0f } }, { { boxHalfSize, -boxHalfSize, boxHalfSize }, { 4.0f, 0.0f } },
        { { boxHalfSize, boxHalfSize, -boxHalfSize }, { 0.0f, 4.0f } }, { { boxHalfSize, boxHalfSize, boxHalfSize }, { 4.0f, 4.0f } },
        { { -boxHalfSize, -boxHalfSize, -boxHalfSize }, { 0.0f, 0.0f } }, { { -boxHalfSize, -boxHalfSize, boxHalfSize }, { 4.0f, 0.0f } },
        { { boxHalfSize, -boxHalfSize, -boxHalfSize }, { 0.0f, 4.0f } }, { { boxHalfSize, -boxHalfSize, boxHalfSize }, { 4.0f, 4.0f } },
        { { -boxHalfSize, boxHalfSize, -boxHalfSize }, { 0.0f, 0.0f } }, { { -boxHalfSize, boxHalfSize, boxHalfSize }, { 4.0f, 0.0f } },
        { { boxHalfSize, boxHalfSize, -boxHalfSize }, { 0.0f, 4.0f } }, { { boxHalfSize, boxHalfSize, boxHalfSize }, { 4.0f, 4.0f } },
        { { -boxHalfSize, -boxHalfSize, -boxHalfSize }, { 0.0f, 0.0f } }, { { -boxHalfSize, boxHalfSize, -boxHalfSize }, { 4.0f, 0.0f } },
        { { boxHalfSize, -boxHalfSize, -boxHalfSize }, { 0.0f, 4.0f } }, { { boxHalfSize, boxHalfSize, -boxHalfSize }, { 4.0f, 4.0f } },
        { { -boxHalfSize, -boxHalfSize, boxHalfSize }, { 0.0f, 0.0f } }, { { -boxHalfSize, boxHalfSize, boxHalfSize }, { 4.0f, 0.0f } },
        { { boxHalfSize, -boxHalfSize, boxHalfSize }, { 0.0f, 4.0f } }, { { boxHalfSize, boxHalfSize, boxHalfSize }, { 4.0f, 4.0f } },
    };

    std::vector<uint16_t> indices
    {
        0, 1, 2, 1, 2, 3,
        4, 5, 6, 5, 6, 7,
        8, 9, 10, 9, 10, 11,
        12, 13, 14, 13, 14, 15,
        16, 17, 18, 17, 18, 19,
        20, 21, 22, 21, 22, 23
    };

    m_IndexNum = (uint32_t)indices.size();

    nri::BufferDesc bufferDesc = {};
    bufferDesc.size = helper::GetByteSizeOf(vertices);
    bufferDesc.usageMask = nri::BufferUsageBits::VERTEX_BUFFER;
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, m_VertexBuffer));

    bufferDesc.size = helper::GetByteSizeOf(indices);
    bufferDesc.usageMask = nri::BufferUsageBits::INDEX_BUFFER;
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, m_IndexBuffer));

    nri::Buffer* const buffers[] = { m_VertexBuffer, m_IndexBuffer };

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = helper::GetCountOf(buffers);
    resourceGroupDesc.buffers = buffers;

    const size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));

    nri::BufferUploadDesc vertexBufferUpdate = {};
    vertexBufferUpdate.buffer = m_VertexBuffer;
    vertexBufferUpdate.data = vertices.data();
    vertexBufferUpdate.dataSize = helper::GetByteSizeOf(vertices);
    vertexBufferUpdate.nextAccess = nri::AccessBits::VERTEX_BUFFER;

    nri::BufferUploadDesc indexBufferUpdate = {};
    indexBufferUpdate.buffer = m_IndexBuffer;
    indexBufferUpdate.data = indices.data();
    indexBufferUpdate.dataSize = helper::GetByteSizeOf(indices);
    indexBufferUpdate.nextAccess = nri::AccessBits::INDEX_BUFFER;

    const nri::BufferUploadDesc bufferUpdates[] = { vertexBufferUpdate, indexBufferUpdate };
    NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_CommandQueue, nullptr, 0, bufferUpdates, helper::GetCountOf(bufferUpdates)));
}

void Sample::CreateTransformConstantBuffer()
{
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);

    const uint32_t matrixSize = uint32_t(sizeof(float4x4));
    const uint32_t alignedMatrixSize = helper::GetAlignedSize(matrixSize, deviceDesc.constantBufferOffsetAlignment);

    nri::BufferDesc bufferDesc = {};
    bufferDesc.size = m_Boxes.size() * alignedMatrixSize;
    bufferDesc.usageMask = nri::BufferUsageBits::CONSTANT_BUFFER;
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, m_TransformConstantBuffer));

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_TransformConstantBuffer;

    const size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + 1, nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));

    nri::BufferViewDesc constantBufferViewDesc = {};
    constantBufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
    constantBufferViewDesc.buffer = m_TransformConstantBuffer;
    constantBufferViewDesc.size = alignedMatrixSize;
    NRI.CreateBufferView(constantBufferViewDesc, m_TransformConstantBufferView);

    uint32_t dynamicConstantBufferOffset = 0;

    std::vector<uint8_t> bufferContent(bufferDesc.size, 0);
    uint8_t* bufferContentRange = bufferContent.data();

    constexpr uint32_t lineSize = 17;

    for (size_t i = 0; i < m_Boxes.size(); i++)
    {
        Box& box = m_Boxes[i];

        float4x4& matrix = *(float4x4*)(bufferContentRange + dynamicConstantBufferOffset);
        matrix = float4x4::Identity();

        const size_t x = i % lineSize;
        const size_t y = i / lineSize;
        matrix.PreTranslation(float3(-1.35f * 0.5f * (lineSize - 1) + 1.35f * x, 8.0f + 1.25f * y, 0.0f));
        matrix.AddScale(float3(1.0f + 0.0001f * (rand() % 2001)));

        box.dynamicConstantBufferOffset = dynamicConstantBufferOffset;
        dynamicConstantBufferOffset += alignedMatrixSize;
    }

    nri::BufferUploadDesc bufferUpdate = {};
    bufferUpdate.buffer = m_TransformConstantBuffer;
    bufferUpdate.data = bufferContent.data();
    bufferUpdate.dataSize = bufferContent.size();
    bufferUpdate.nextAccess = nri::AccessBits::CONSTANT_BUFFER;
    NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_CommandQueue, nullptr, 0, &bufferUpdate, 1));
}

void Sample::CreateDescriptorSets()
{
    std::vector<nri::DescriptorSet*> descriptorSets(m_Boxes.size());
    NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 0, descriptorSets.data(), (uint32_t)descriptorSets.size(),
        nri::WHOLE_DEVICE_GROUP, 0);

    for (size_t i = 0; i < m_Boxes.size(); i++)
    {
        Box& box = m_Boxes[i];

        nri::Descriptor* constantBuffers[] = {
            m_FakeConstantBufferViews[0],
            m_ViewConstantBufferView,
            m_FakeConstantBufferViews[rand() % m_FakeConstantBufferViews.size()]
        };

        const nri::Descriptor* textureViews[3] = {};

        for (size_t j = 0; j < helper::GetCountOf(textureViews); j++)
            textureViews[j] = m_TextureViews[rand() % m_TextureViews.size()];

        const nri::DescriptorRangeUpdateDesc rangeUpdates[] = {
            { constantBuffers, helper::GetCountOf(constantBuffers) },
            { textureViews, helper::GetCountOf(textureViews) }
        };

        box.pipeline = m_Pipelines[(i / DRAW_CALLS_PER_PIPELINE) % m_Pipelines.size()];

        box.descriptorSet = descriptorSets[i];
        NRI.UpdateDescriptorRanges(*box.descriptorSet, nri::WHOLE_DEVICE_GROUP, 0, helper::GetCountOf(rangeUpdates), rangeUpdates);
        NRI.UpdateDynamicConstantBuffers(*box.descriptorSet, nri::WHOLE_DEVICE_GROUP, 0, 1, &m_TransformConstantBufferView);
    }
}

void Sample::CreateDescriptorPool()
{
    const uint32_t boxNum = (uint32_t)m_Boxes.size();

    nri::DescriptorPoolDesc descriptorPoolDesc = {};
    descriptorPoolDesc.constantBufferMaxNum = 3 * boxNum;
    descriptorPoolDesc.dynamicConstantBufferMaxNum = 1 * boxNum;
    descriptorPoolDesc.textureMaxNum = 3 * boxNum;
    descriptorPoolDesc.descriptorSetMaxNum = boxNum;
    descriptorPoolDesc.staticSamplerMaxNum = boxNum;

    NRI_ABORT_ON_FAILURE(NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc, m_DescriptorPool));
}

void Sample::LoadTextures()
{
    constexpr uint32_t textureNum = 8;

    std::vector<utils::Texture> loadedTextures(textureNum);
    std::string texturePath = utils::GetFullPath("", utils::DataFolder::TEXTURES);

    for (uint32_t i = 0; i < loadedTextures.size(); i++)
    {
        if (!utils::LoadTexture(texturePath + "checkerboard" + std::to_string(i) + ".dds", loadedTextures[i]))
            std::abort();
    }

    const uint32_t textureVariations = 1024;

    m_Textures.resize(textureVariations);
    for (size_t i = 0; i < m_Textures.size(); i++)
    {
        const utils::Texture& loadedTexture = loadedTextures[i % textureNum];

        nri::CTextureDesc textureDesc = nri::CTextureDesc::Texture2D(loadedTexture.GetFormat(),
            loadedTexture.GetWidth(), loadedTexture.GetHeight(), loadedTexture.GetMipNum());
        NRI_ABORT_ON_FAILURE(NRI.CreateTexture(*m_Device, textureDesc, m_Textures[i]));
    }

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = (uint32_t)m_Textures.size();
    resourceGroupDesc.textures = m_Textures.data();

    const size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));

    constexpr uint32_t MAX_MIP_NUM = 16;
    std::vector<nri::TextureUploadDesc> textureUpdates(m_Textures.size());
    std::vector<nri::TextureSubresourceUploadDesc> subresources(m_Textures.size() * MAX_MIP_NUM);

    for (size_t i = 0; i < textureUpdates.size(); i++)
    {
        const size_t subresourceOffset = MAX_MIP_NUM * i;
        const utils::Texture& texture = loadedTextures[i % textureNum];

        for (uint32_t mip = 0; mip < texture.GetMipNum(); mip++)
            texture.GetSubresource(subresources[subresourceOffset + mip], mip);

        nri::TextureUploadDesc& textureUpdate = textureUpdates[i];
        textureUpdate.subresources = &subresources[subresourceOffset];
        textureUpdate.mipNum = texture.GetMipNum();
        textureUpdate.arraySize = texture.GetArraySize();
        textureUpdate.texture = m_Textures[i];
        textureUpdate.nextLayout = nri::TextureLayout::SHADER_RESOURCE;
        textureUpdate.nextAccess = nri::AccessBits::SHADER_RESOURCE;
    }

    NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_CommandQueue, textureUpdates.data(), (uint32_t)textureUpdates.size(), nullptr, 0));

    m_TextureViews.resize(m_Textures.size());
    for (size_t i = 0; i < m_Textures.size(); i++)
    {
        const utils::Texture& texture = loadedTextures[i % textureNum];

        nri::Texture2DViewDesc texture2DViewDesc = {m_Textures[i], nri::Texture2DViewType::SHADER_RESOURCE_2D, texture.GetFormat()};
        NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(texture2DViewDesc, m_TextureViews[i]));
    }
}

void Sample::CreateFakeConstantBuffers()
{
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);

    const uint32_t constantRangeSize = (uint32_t)helper::GetAlignedSize(sizeof(float4), deviceDesc.constantBufferOffsetAlignment);
    constexpr uint32_t fakeConstantBufferRangeNum = 16384;

    nri::BufferDesc bufferDesc = {};
    bufferDesc.size = fakeConstantBufferRangeNum * constantRangeSize;
    bufferDesc.usageMask = nri::BufferUsageBits::CONSTANT_BUFFER;
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, m_FakeConstantBuffer));

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_FakeConstantBuffer;

    const size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + 1, nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));

    nri::BufferViewDesc constantBufferViewDesc = {};
    constantBufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
    constantBufferViewDesc.buffer = m_FakeConstantBuffer;
    constantBufferViewDesc.size = constantRangeSize;

    m_FakeConstantBufferViews.resize(fakeConstantBufferRangeNum);
    for (size_t i = 0; i < m_FakeConstantBufferViews.size(); i++)
    {
        NRI.CreateBufferView(constantBufferViewDesc, m_FakeConstantBufferViews[i]);
        constantBufferViewDesc.offset += constantRangeSize;
    }

    std::vector<uint8_t> bufferContent(bufferDesc.size, 0);

    nri::BufferUploadDesc bufferUpdate = {};
    bufferUpdate.buffer = m_FakeConstantBuffer;
    bufferUpdate.data = bufferContent.data();
    bufferUpdate.dataSize = bufferContent.size();
    bufferUpdate.nextAccess = nri::AccessBits::CONSTANT_BUFFER;
    NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_CommandQueue, nullptr, 0, &bufferUpdate, 1));
}

void Sample::CreateViewConstantBuffer()
{
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);

    const uint32_t constantRangeSize = (uint32_t)helper::GetAlignedSize(sizeof(float4x4), deviceDesc.constantBufferOffsetAlignment);

    nri::BufferDesc bufferDesc = {};
    bufferDesc.size = constantRangeSize;
    bufferDesc.usageMask = nri::BufferUsageBits::CONSTANT_BUFFER;
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, m_ViewConstantBuffer));

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_ViewConstantBuffer;

    const size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + 1, nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));

    nri::BufferViewDesc constantBufferViewDesc = {};
    constantBufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
    constantBufferViewDesc.buffer = m_ViewConstantBuffer;
    constantBufferViewDesc.size = constantRangeSize;
    NRI.CreateBufferView(constantBufferViewDesc, m_ViewConstantBufferView);

    std::vector<uint8_t> bufferContent(bufferDesc.size, 0);
    SetupProjViewMatrix(*(float4x4*)(bufferContent.data()));

    nri::BufferUploadDesc bufferUpdate = {};
    bufferUpdate.buffer = m_ViewConstantBuffer;
    bufferUpdate.data = bufferContent.data();
    bufferUpdate.dataSize = bufferContent.size();
    bufferUpdate.nextAccess = nri::AccessBits::CONSTANT_BUFFER;
    NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_CommandQueue, nullptr, 0, &bufferUpdate, 1));
}

void Sample::SetupProjViewMatrix(float4x4& projViewMatrix)
{
    const float aspect = float( GetWindowWidth() ) / float( GetWindowHeight() );

    float4x4 projectionMatrix;
    projectionMatrix.SetupByHalfFovxInf(DegToRad(45.0f), aspect, 0.1f, 0);

    float4x4 viewMatrix = float4x4::Identity();
    viewMatrix.SetupByRotationYPR(DegToRad(0.0f), DegToRad(0.0f), 0.0f);
    viewMatrix.WorldToView();

    const float3 cameraPosition = float3(0.0f, -2.5f, 2.0f);
    viewMatrix.PreTranslation(-cameraPosition);

    projViewMatrix = projectionMatrix * viewMatrix;
}

uint32_t Sample::GetPhysicalCoreNum() const
{
    const char* moduleName = "kernel32";
    const char* funcName = "GetLogicalProcessorInformation";

    const HMODULE kernel32 = GetModuleHandleA(moduleName);
    assert(kernel32 != nullptr);

    void* address = (void*)GetProcAddress(kernel32, funcName);
    assert(address != nullptr);

    typedef BOOL(WINAPI *LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);
    const LPFN_GLPI GetLogicalProcessorInformation = (LPFN_GLPI)address;

    typedef SYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer;
    Buffer* buffer = nullptr;
    DWORD bufferSize = 0;
    while (true)
    {
        const DWORD res = GetLogicalProcessorInformation(buffer, &bufferSize);
        if (res == TRUE)
            break;

        const DWORD error = GetLastError();
        assert(error == ERROR_INSUFFICIENT_BUFFER);

        SetLastError(0);

        free(buffer);
        buffer = (Buffer*)malloc(bufferSize); // TODO: Use alloca?
    }

    uint32_t coreNum = 0;
    char* ptr = (char*)buffer;
    char* end = ptr + bufferSize;
    for (; ptr < end; ptr += sizeof(Buffer))
    {
        Buffer* info = (Buffer*)ptr;
        coreNum += (info->Relationship == RelationProcessorCore) ? 1 : 0;
    }

    free(buffer);

    return coreNum;
}

SAMPLE_MAIN(Sample, 0);
