/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SampleBase.h"
#include "Extensions/NRIRayTracing.h"

#include <array>

constexpr auto BUILD_FLAGS = nri::AccelerationStructureBuildBits::PREFER_FAST_TRACE;

struct NRIInterface
    : public nri::CoreInterface
    , public nri::SwapChainInterface
    , public nri::RayTracingInterface
    , public nri::HelperInterface
{};

struct Frame
{
    nri::DeviceSemaphore* deviceSemaphore;
    nri::CommandAllocator* commandAllocator;
    nri::CommandBuffer* commandBuffer;
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
    void CreateRayTracingPipeline();
    void CreateRayTracingOutput(nri::Format swapChainFormat);
    void CreateDescriptorSet();
    void CreateBottomLevelAccelerationStructure();
    void CreateTopLevelAccelerationStructure();
    void CreateShaderTable();
    void CreateUploadBuffer(uint64_t size, nri::BufferUsageBits usage, nri::Buffer*& buffer, nri::Memory*& memory);
    void CreateScratchBuffer(nri::AccelerationStructure& accelerationStructure, nri::Buffer*& buffer, nri::Memory*& memory);
    void BuildBottomLevelAccelerationStructure(nri::AccelerationStructure& accelerationStructure, const nri::GeometryObject* objects, const uint32_t objectNum);
    void BuildTopLevelAccelerationStructure(nri::AccelerationStructure& accelerationStructure, uint32_t instanceNum, nri::Buffer& instanceBuffer);

    NRIInterface NRI = {};
    nri::Device* m_Device = nullptr;
    nri::SwapChain* m_SwapChain = nullptr;
    nri::CommandQueue* m_CommandQueue = nullptr;
    nri::QueueSemaphore* m_BackBufferAcquireSemaphore = nullptr;
    nri::QueueSemaphore* m_BackBufferReleaseSemaphore = nullptr;

    std::array<Frame, BUFFERED_FRAME_MAX_NUM> m_Frames;

    nri::Pipeline* m_Pipeline = nullptr;
    nri::PipelineLayout* m_PipelineLayout = nullptr;

    nri::Buffer* m_ShaderTable = nullptr;
    nri::Memory* m_ShaderTableMemory = nullptr;
    uint64_t m_ShaderGroupIdentifierSize = 0;
    uint64_t m_MissShaderOffset = 0;
    uint64_t m_HitShaderGroupOffset = 0;

    nri::Texture* m_RayTracingOutput = nullptr;
    nri::Descriptor* m_RayTracingOutputView = nullptr;

    nri::DescriptorPool* m_DescriptorPool = nullptr;
    nri::DescriptorSet* m_DescriptorSet = nullptr;

    nri::AccelerationStructure* m_BLAS = nullptr;
    nri::AccelerationStructure* m_TLAS = nullptr;
    nri::Descriptor* m_TLASDescriptor = nullptr;
    nri::Memory* m_BLASMemory = nullptr;
    nri::Memory* m_TLASMemory = nullptr;

    const BackBuffer* m_BackBuffer = nullptr;
    std::vector<BackBuffer> m_SwapChainBuffers;
    std::vector<nri::Memory*> m_MemoryAllocations;
};

Sample::~Sample()
{
    NRI.WaitForIdle(*m_CommandQueue);

    for (uint32_t i = 0; i < m_Frames.size(); i++)
    {
        NRI.DestroyDeviceSemaphore(*m_Frames[i].deviceSemaphore);
        NRI.DestroyCommandBuffer(*m_Frames[i].commandBuffer);
        NRI.DestroyCommandAllocator(*m_Frames[i].commandAllocator);
    }

    for (uint32_t i = 0; i < m_SwapChainBuffers.size(); i++)
    {
        NRI.DestroyDescriptor(*m_SwapChainBuffers[i].colorAttachment);
        NRI.DestroyFrameBuffer(*m_SwapChainBuffers[i].frameBufferUI);
    }

    NRI.DestroyDescriptor(*m_RayTracingOutputView);
    NRI.DestroyTexture(*m_RayTracingOutput);

    NRI.DestroyDescriptorPool(*m_DescriptorPool);

    NRI.DestroyAccelerationStructure(*m_BLAS);
    NRI.DestroyAccelerationStructure(*m_TLAS);
    NRI.DestroyDescriptor(*m_TLASDescriptor);
    NRI.DestroyBuffer(*m_ShaderTable);

    NRI.DestroyPipeline(*m_Pipeline);
    NRI.DestroyPipelineLayout(*m_PipelineLayout);

    NRI.DestroyQueueSemaphore(*m_BackBufferAcquireSemaphore);
    NRI.DestroyQueueSemaphore(*m_BackBufferReleaseSemaphore);

    NRI.DestroySwapChain(*m_SwapChain);

    for (size_t i = 0; i < m_MemoryAllocations.size(); i++)
        NRI.FreeMemory(*m_MemoryAllocations[i]);

    NRI.FreeMemory(*m_BLASMemory);
    NRI.FreeMemory(*m_TLASMemory);
    NRI.FreeMemory(*m_ShaderTableMemory);

    m_UserInterface.Shutdown();

    nri::DestroyDevice(*m_Device);
}

bool Sample::Initialize(nri::GraphicsAPI graphicsAPI)
{
    nri::DeviceCreationDesc deviceCreationDesc = {};
    deviceCreationDesc.graphicsAPI = graphicsAPI;
    deviceCreationDesc.enableAPIValidation = m_DebugAPI;
    deviceCreationDesc.enableNRIValidation = m_DebugNRI;
    deviceCreationDesc.spirvBindingOffsets = SPIRV_BINDING_OFFSETS;
    NRI_ABORT_ON_FAILURE( nri::CreateDevice(deviceCreationDesc, m_Device) );

    NRI_ABORT_ON_FAILURE( nri::GetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::GetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::GetInterface(*m_Device, NRI_INTERFACE(nri::RayTracingInterface), (nri::RayTracingInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::GetInterface(*m_Device, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI) );

    NRI_ABORT_ON_FAILURE( NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueue));
    NRI_ABORT_ON_FAILURE( NRI.CreateQueueSemaphore(*m_Device, m_BackBufferAcquireSemaphore));
    NRI_ABORT_ON_FAILURE( NRI.CreateQueueSemaphore(*m_Device, m_BackBufferReleaseSemaphore));

    CreateCommandBuffers();

    nri::Format swapChainFormat = nri::Format::UNKNOWN;
    CreateSwapChain(swapChainFormat);

    CreateRayTracingPipeline();
    CreateDescriptorSet();
    CreateRayTracingOutput(swapChainFormat);
    CreateBottomLevelAccelerationStructure();
    CreateTopLevelAccelerationStructure();
    CreateShaderTable();

    return m_UserInterface.Initialize(m_hWnd, *m_Device, NRI, NRI, GetWindowWidth(), GetWindowHeight(), BUFFERED_FRAME_MAX_NUM, swapChainFormat);
}

void Sample::PrepareFrame(uint32_t frameIndex)
{
    m_UserInterface.Prepare();
}

void Sample::RenderFrame(uint32_t frameIndex)
{
    static nri::AccessBits rayTracingOutputAccessMask = nri::AccessBits::UNKNOWN;

    frameIndex = frameIndex % BUFFERED_FRAME_MAX_NUM;

    const uint32_t backBufferIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain, *m_BackBufferAcquireSemaphore);
    m_BackBuffer = &m_SwapChainBuffers[backBufferIndex];

    nri::DeviceSemaphore& deviceSemaphore = *m_Frames[frameIndex].deviceSemaphore;
    NRI.WaitForSemaphore(*m_CommandQueue, deviceSemaphore);

    nri::CommandAllocator& commandAllocator = *m_Frames[frameIndex].commandAllocator;
    NRI.ResetCommandAllocator(commandAllocator);

    nri::CommandBuffer& commandBuffer = *m_Frames[frameIndex].commandBuffer;
    NRI.BeginCommandBuffer(commandBuffer, m_DescriptorPool, 0);

    nri::TextureTransitionBarrierDesc textureTransitions[2] = {};
    textureTransitions[0].texture = m_BackBuffer->texture;
    textureTransitions[0].prevAccess = nri::AccessBits::UNKNOWN;
    textureTransitions[0].nextAccess = nri::AccessBits::COPY_DESTINATION;
    textureTransitions[0].prevLayout = nri::TextureLayout::UNKNOWN;
    textureTransitions[0].nextLayout = nri::TextureLayout::GENERAL;
    textureTransitions[0].arraySize = 1;
    textureTransitions[0].mipNum = 1;

    textureTransitions[1].texture = m_RayTracingOutput;
    textureTransitions[1].prevAccess = rayTracingOutputAccessMask;
    textureTransitions[1].nextAccess = rayTracingOutputAccessMask = nri::AccessBits::SHADER_RESOURCE_STORAGE;
    textureTransitions[1].prevLayout = nri::TextureLayout::UNKNOWN;
    textureTransitions[1].nextLayout = nri::TextureLayout::GENERAL;
    textureTransitions[1].arraySize = 1;
    textureTransitions[1].mipNum = 1;

    nri::TransitionBarrierDesc transitionBarriers = {};
    transitionBarriers.textures = textureTransitions;
    transitionBarriers.textureNum = helper::GetCountOf(textureTransitions);

    NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);

    NRI.CmdSetPipelineLayout(commandBuffer, *m_PipelineLayout);
    NRI.CmdSetPipeline(commandBuffer, *m_Pipeline);
    NRI.CmdSetDescriptorSets(commandBuffer, 0, 1, &m_DescriptorSet, nullptr);

    nri::DispatchRaysDesc dispatchRaysDesc = {};
    dispatchRaysDesc.raygenShader = { m_ShaderTable, 0, m_ShaderGroupIdentifierSize, m_ShaderGroupIdentifierSize };
    dispatchRaysDesc.missShaders = { m_ShaderTable, m_MissShaderOffset, m_ShaderGroupIdentifierSize, m_ShaderGroupIdentifierSize };
    dispatchRaysDesc.hitShaderGroups = { m_ShaderTable, m_HitShaderGroupOffset, m_ShaderGroupIdentifierSize, m_ShaderGroupIdentifierSize };
    dispatchRaysDesc.width = GetWindowWidth();
    dispatchRaysDesc.height = GetWindowHeight();
    dispatchRaysDesc.depth = 1;
    NRI.CmdDispatchRays(commandBuffer, dispatchRaysDesc);

    textureTransitions[0].texture = m_RayTracingOutput;
    textureTransitions[0].prevAccess = rayTracingOutputAccessMask;
    textureTransitions[0].nextAccess = rayTracingOutputAccessMask = nri::AccessBits::COPY_SOURCE;
    textureTransitions[0].prevLayout = nri::TextureLayout::GENERAL;
    textureTransitions[0].nextLayout = nri::TextureLayout::GENERAL;
    textureTransitions[0].arraySize = 1;
    textureTransitions[0].mipNum = 1;
    transitionBarriers.textures = textureTransitions;
    transitionBarriers.textureNum = 1;

    NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::RAYTRACING_STAGE);

    NRI.CmdCopyTexture(commandBuffer, *m_BackBuffer->texture, 0, nullptr, *m_RayTracingOutput, 0, nullptr);

    textureTransitions[0].texture = m_BackBuffer->texture;
    textureTransitions[0].prevAccess = nri::AccessBits::COPY_DESTINATION;
    textureTransitions[0].nextAccess = nri::AccessBits::COLOR_ATTACHMENT;
    textureTransitions[0].prevLayout = nri::TextureLayout::GENERAL;
    textureTransitions[0].nextLayout = nri::TextureLayout::COLOR_ATTACHMENT;
    transitionBarriers.textures = textureTransitions;
    transitionBarriers.textureNum = 1;
    NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::COPY_STAGE);

    NRI.CmdBeginRenderPass(commandBuffer, *m_BackBuffer->frameBufferUI, nri::RenderPassBeginFlag::SKIP_FRAME_BUFFER_CLEAR);
    m_UserInterface.Render(commandBuffer);
    NRI.CmdEndRenderPass(commandBuffer);

    textureTransitions[0].texture = m_BackBuffer->texture;
    textureTransitions[0].prevAccess = nri::AccessBits::COLOR_ATTACHMENT;
    textureTransitions[0].nextAccess = nri::AccessBits::UNKNOWN;
    textureTransitions[0].prevLayout = nri::TextureLayout::COLOR_ATTACHMENT;
    textureTransitions[0].nextLayout = nri::TextureLayout::PRESENT;
    transitionBarriers.textures = textureTransitions;
    transitionBarriers.textureNum = 1;

    NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);

    NRI.EndCommandBuffer(commandBuffer);

    const nri::CommandBuffer* commandBufferArray[] = { &commandBuffer };

    nri::WorkSubmissionDesc workSubmissionDesc = {};
    workSubmissionDesc.commandBuffers = commandBufferArray;
    workSubmissionDesc.commandBufferNum = 1;
    workSubmissionDesc.signal = &m_BackBufferReleaseSemaphore;
    workSubmissionDesc.signalNum = 1;
    workSubmissionDesc.wait = &m_BackBufferAcquireSemaphore;
    workSubmissionDesc.waitNum = 1;

    NRI.SubmitQueueWork(*m_CommandQueue, workSubmissionDesc, &deviceSemaphore);
    NRI.SwapChainPresent(*m_SwapChain, *m_BackBufferReleaseSemaphore);
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

    nri::ClearValueDesc clearColor = {};
    nri::FrameBufferDesc frameBufferDesc = {};
    frameBufferDesc.colorAttachmentNum = 1;
    frameBufferDesc.colorClearValues = &clearColor;

    for (uint32_t i = 0; i < swapChainTextureNum; i++)
    {
        m_SwapChainBuffers.emplace_back();
        BackBuffer& backBuffer = m_SwapChainBuffers.back();

        backBuffer = {};
        backBuffer.texture = swapChainTextures[i];

        nri::Texture2DViewDesc textureViewDesc = {backBuffer.texture, nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat};
        NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(textureViewDesc, backBuffer.colorAttachment));

        frameBufferDesc.colorAttachments = &backBuffer.colorAttachment;
        NRI_ABORT_ON_FAILURE(NRI.CreateFrameBuffer(*m_Device, frameBufferDesc, backBuffer.frameBufferUI));
    }
}

void Sample::CreateCommandBuffers()
{
    for (uint32_t i = 0; i < m_Frames.size(); i++)
    {
        Frame& frame = m_Frames[i];
        NRI_ABORT_ON_FAILURE(NRI.CreateDeviceSemaphore(*m_Device, true, frame.deviceSemaphore));
        NRI_ABORT_ON_FAILURE(NRI.CreateCommandAllocator(*m_CommandQueue, nri::WHOLE_DEVICE_GROUP, frame.commandAllocator));
        NRI_ABORT_ON_FAILURE(NRI.CreateCommandBuffer(*frame.commandAllocator, frame.commandBuffer));
    }
}

void Sample::CreateRayTracingPipeline()
{
    nri::DescriptorRangeDesc descriptorRanges[2] = {};
    descriptorRanges[0].descriptorNum = 1;
    descriptorRanges[0].descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
    descriptorRanges[0].baseRegisterIndex = 0;
    descriptorRanges[0].visibility = nri::ShaderStage::RAYGEN;

    descriptorRanges[1].descriptorNum = 1;
    descriptorRanges[1].descriptorType = nri::DescriptorType::ACCELERATION_STRUCTURE;
    descriptorRanges[1].baseRegisterIndex = 1;
    descriptorRanges[1].visibility = nri::ShaderStage::RAYGEN;

    nri::DescriptorSetDesc descriptorSetDesc = { descriptorRanges, helper::GetCountOf(descriptorRanges) };

    nri::PipelineLayoutDesc pipelineLayoutDesc = {};
    pipelineLayoutDesc.descriptorSets = &descriptorSetDesc;
    pipelineLayoutDesc.descriptorSetNum = 1;
    pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::RAYGEN;

    NRI_ABORT_ON_FAILURE(NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc, m_PipelineLayout));

    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    utils::ShaderCodeStorage shaderCodeStorage;
    nri::ShaderDesc shaderDescs[] =
    {
        utils::LoadShader(deviceDesc.graphicsAPI, "07_RayTracing_Triangle.rgen", shaderCodeStorage, "raygen"),
        utils::LoadShader(deviceDesc.graphicsAPI, "07_RayTracing_Triangle.rmiss", shaderCodeStorage, "miss"),
        utils::LoadShader(deviceDesc.graphicsAPI, "07_RayTracing_Triangle.rchit", shaderCodeStorage, "closest_hit"),
    };

    nri::ShaderLibrary shaderLibrary = {};
    shaderLibrary.shaderDescs = shaderDescs;
    shaderLibrary.shaderNum = helper::GetCountOf(shaderDescs);

    const nri::ShaderGroupDesc shaderGroupDescs[] = { { 1 }, { 2 }, { 3 } };

    nri::RayTracingPipelineDesc pipelineDesc = {};
    pipelineDesc.recursionDepthMax = 1;
    pipelineDesc.payloadAttributeSizeMax = 3 * sizeof(float);
    pipelineDesc.intersectionAttributeSizeMax = 2 * sizeof(float);
    pipelineDesc.pipelineLayout = m_PipelineLayout;
    pipelineDesc.shaderGroupDescs = shaderGroupDescs;
    pipelineDesc.shaderGroupDescNum = helper::GetCountOf(shaderGroupDescs);
    pipelineDesc.shaderLibrary = &shaderLibrary;

    NRI_ABORT_ON_FAILURE(NRI.CreateRayTracingPipeline(*m_Device, pipelineDesc, m_Pipeline));
}

void Sample::CreateRayTracingOutput(nri::Format swapChainFormat)
{
    nri::TextureDesc rayTracingOutputDesc = {};
    rayTracingOutputDesc.type = nri::TextureType::TEXTURE_2D;
    rayTracingOutputDesc.format = swapChainFormat;
    rayTracingOutputDesc.size[0] = GetWindowWidth();
    rayTracingOutputDesc.size[1] = GetWindowHeight();
    rayTracingOutputDesc.size[2] = 1;
    rayTracingOutputDesc.arraySize = 1;
    rayTracingOutputDesc.mipNum = 1;
    rayTracingOutputDesc.sampleNum = 1;
    rayTracingOutputDesc.usageMask = nri::TextureUsageBits::SHADER_RESOURCE_STORAGE;

    NRI_ABORT_ON_FAILURE(NRI.CreateTexture(*m_Device, rayTracingOutputDesc, m_RayTracingOutput));

    nri::MemoryDesc memoryDesc = {};
    NRI.GetTextureMemoryInfo(*m_RayTracingOutput, nri::MemoryLocation::DEVICE, memoryDesc);

    nri::Memory* memory = nullptr;
    NRI_ABORT_ON_FAILURE(NRI.AllocateMemory(*m_Device, nri::WHOLE_DEVICE_GROUP, memoryDesc.type, memoryDesc.size, memory));
    m_MemoryAllocations.push_back(memory);

    const nri::TextureMemoryBindingDesc memoryBindingDesc = { memory, m_RayTracingOutput };
    NRI_ABORT_ON_FAILURE(NRI.BindTextureMemory(*m_Device, &memoryBindingDesc, 1));

    nri::Texture2DViewDesc textureViewDesc = {m_RayTracingOutput, nri::Texture2DViewType::SHADER_RESOURCE_STORAGE_2D, swapChainFormat};
    NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(textureViewDesc, m_RayTracingOutputView));

    const nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDesc = { &m_RayTracingOutputView, 1, 0 };
    NRI.UpdateDescriptorRanges(*m_DescriptorSet, nri::WHOLE_DEVICE_GROUP, 0, 1, &descriptorRangeUpdateDesc);
}

void Sample::CreateDescriptorSet()
{
    nri::DescriptorPoolDesc descriptorPoolDesc = {};
    descriptorPoolDesc.storageTextureMaxNum = 1;
    descriptorPoolDesc.accelerationStructureMaxNum = 1;
    descriptorPoolDesc.descriptorSetMaxNum = 1;

    NRI_ABORT_ON_FAILURE(NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc, m_DescriptorPool));
    NRI_ABORT_ON_FAILURE(NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 0, &m_DescriptorSet, 1, nri::WHOLE_DEVICE_GROUP, 0));
}

void Sample::CreateBottomLevelAccelerationStructure()
{
    const uint64_t vertexDataSize = 3 * 3 * sizeof(float);
    const uint64_t indexDataSize = 3 * sizeof(uint16_t);

    nri::Buffer* buffer = nullptr;
    nri::Memory* memory = nullptr;
    CreateUploadBuffer(vertexDataSize + indexDataSize, nri::BufferUsageBits::NONE, buffer, memory);

    const float positions[] = { -0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, -0.5f, 0.0f };
    const uint16_t indices[] = { 0, 1, 2 };

    uint8_t* data = (uint8_t*)NRI.MapBuffer(*buffer, 0, vertexDataSize + indexDataSize);
    memcpy(data, positions, sizeof(positions));
    memcpy(data + vertexDataSize, indices, sizeof(indices));
    NRI.UnmapBuffer(*buffer);

    nri::GeometryObject geometryObject = {};
    geometryObject.type = nri::GeometryType::TRIANGLES;
    geometryObject.flags = nri::BottomLevelGeometryBits::OPAQUE_GEOMETRY;
    geometryObject.triangles.vertexBuffer = buffer;
    geometryObject.triangles.vertexFormat = nri::Format::RGB32_SFLOAT;
    geometryObject.triangles.vertexNum = 3;
    geometryObject.triangles.vertexStride = 3 * sizeof(float);
    geometryObject.triangles.indexBuffer = buffer;
    geometryObject.triangles.indexOffset = vertexDataSize;
    geometryObject.triangles.indexNum = 3;
    geometryObject.triangles.indexType = nri::IndexType::UINT16;

    nri::AccelerationStructureDesc accelerationStructureBLASDesc = {};
    accelerationStructureBLASDesc.type = nri::AccelerationStructureType::BOTTOM_LEVEL;
    accelerationStructureBLASDesc.flags = BUILD_FLAGS;
    accelerationStructureBLASDesc.instanceOrGeometryObjectNum = 1;
    accelerationStructureBLASDesc.geometryObjects = &geometryObject;

    NRI_ABORT_ON_FAILURE(NRI.CreateAccelerationStructure(*m_Device, accelerationStructureBLASDesc, m_BLAS));

    nri::MemoryDesc memoryDesc = {};
    NRI.GetAccelerationStructureMemoryInfo(*m_BLAS, memoryDesc);

    NRI_ABORT_ON_FAILURE(NRI.AllocateMemory(*m_Device, nri::WHOLE_DEVICE_GROUP, memoryDesc.type, memoryDesc.size, m_BLASMemory));

    const nri::AccelerationStructureMemoryBindingDesc memoryBindingDesc = { m_BLASMemory, m_BLAS };
    NRI_ABORT_ON_FAILURE(NRI.BindAccelerationStructureMemory(*m_Device, &memoryBindingDesc, 1));

    BuildBottomLevelAccelerationStructure(*m_BLAS, &geometryObject, 1);

    NRI.DestroyBuffer(*buffer);
    NRI.FreeMemory(*memory);
}

void Sample::CreateTopLevelAccelerationStructure()
{
    nri::AccelerationStructureDesc accelerationStructureTLASDesc = {};
    accelerationStructureTLASDesc.type = nri::AccelerationStructureType::TOP_LEVEL;
    accelerationStructureTLASDesc.flags = BUILD_FLAGS;
    accelerationStructureTLASDesc.instanceOrGeometryObjectNum = 1;

    NRI_ABORT_ON_FAILURE(NRI.CreateAccelerationStructure(*m_Device, accelerationStructureTLASDesc, m_TLAS));

    nri::MemoryDesc memoryDesc = {};
    NRI.GetAccelerationStructureMemoryInfo(*m_TLAS, memoryDesc);

    NRI_ABORT_ON_FAILURE(NRI.AllocateMemory(*m_Device, nri::WHOLE_DEVICE_GROUP, memoryDesc.type, memoryDesc.size, m_TLASMemory));

    const nri::AccelerationStructureMemoryBindingDesc memoryBindingDesc = { m_TLASMemory, m_TLAS };
    NRI_ABORT_ON_FAILURE(NRI.BindAccelerationStructureMemory(*m_Device, &memoryBindingDesc, 1));

    nri::Buffer* buffer = nullptr;
    nri::Memory* memory = nullptr;
    CreateUploadBuffer(sizeof(nri::GeometryObjectInstance), nri::BufferUsageBits::RAY_TRACING_BUFFER, buffer, memory);

    nri::GeometryObjectInstance geometryObjectInstance = {};
    geometryObjectInstance.accelerationStructureHandle = NRI.GetAccelerationStructureHandle(*m_BLAS, 0);
    geometryObjectInstance.transform[0][0] = 1.0f;
    geometryObjectInstance.transform[1][1] = 1.0f;
    geometryObjectInstance.transform[2][2] = 1.0f;
    geometryObjectInstance.mask = 0xFF;
    geometryObjectInstance.flags = nri::TopLevelInstanceBits::FORCE_OPAQUE;

    void* data = NRI.MapBuffer(*buffer, 0, sizeof(geometryObjectInstance));
    memcpy(data, &geometryObjectInstance, sizeof(geometryObjectInstance));
    NRI.UnmapBuffer(*buffer);

    BuildTopLevelAccelerationStructure(*m_TLAS, 1, *buffer);

    NRI.DestroyBuffer(*buffer);
    NRI.FreeMemory(*memory);

    NRI.CreateAccelerationStructureDescriptor(*m_TLAS, 0, m_TLASDescriptor);

    const nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDesc = { &m_TLASDescriptor, 1, 0 };
    NRI.UpdateDescriptorRanges(*m_DescriptorSet, nri::WHOLE_DEVICE_GROUP, 1, 1, &descriptorRangeUpdateDesc);
}

void Sample::CreateUploadBuffer(uint64_t size, nri::BufferUsageBits usage, nri::Buffer*& buffer, nri::Memory*& memory)
{
    const nri::BufferDesc bufferDesc = { size, 0, usage };
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, buffer));

    nri::MemoryDesc memoryDesc = {};
    NRI.GetBufferMemoryInfo(*buffer, nri::MemoryLocation::HOST_UPLOAD, memoryDesc);

    NRI_ABORT_ON_FAILURE(NRI.AllocateMemory(*m_Device, nri::WHOLE_DEVICE_GROUP, memoryDesc.type, memoryDesc.size, memory));

    const nri::BufferMemoryBindingDesc bufferMemoryBindingDesc = { memory, buffer };
    NRI_ABORT_ON_FAILURE(NRI.BindBufferMemory(*m_Device, &bufferMemoryBindingDesc, 1));
}

void Sample::CreateScratchBuffer(nri::AccelerationStructure& accelerationStructure, nri::Buffer*& buffer, nri::Memory*& memory)
{
    const uint64_t scratchBufferSize = NRI.GetAccelerationStructureBuildScratchBufferSize(accelerationStructure);

    const nri::BufferDesc bufferDesc = { scratchBufferSize, 0, nri::BufferUsageBits::RAY_TRACING_BUFFER };
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, buffer));

    nri::MemoryDesc bufferMemoryDesc = {};
    NRI.GetBufferMemoryInfo(*buffer, nri::MemoryLocation::DEVICE, bufferMemoryDesc);

    NRI_ABORT_ON_FAILURE(NRI.AllocateMemory(*m_Device, nri::WHOLE_DEVICE_GROUP, bufferMemoryDesc.type, bufferMemoryDesc.size, memory));

    const nri::BufferMemoryBindingDesc bufferMemoryBindingDesc = { memory, buffer };
    NRI_ABORT_ON_FAILURE(NRI.BindBufferMemory(*m_Device, &bufferMemoryBindingDesc, 1));
}

void Sample::BuildBottomLevelAccelerationStructure(nri::AccelerationStructure& accelerationStructure, const nri::GeometryObject* objects, const uint32_t objectNum)
{
    nri::Buffer* scratchBuffer = nullptr;
    nri::Memory* scratchBufferMemory = nullptr;
    CreateScratchBuffer(accelerationStructure, scratchBuffer, scratchBufferMemory);

    nri::CommandAllocator* commandAllocator = nullptr;
    nri::CommandBuffer* commandBuffer = nullptr;
    NRI.CreateCommandAllocator(*m_CommandQueue, nri::WHOLE_DEVICE_GROUP, commandAllocator);
    NRI.CreateCommandBuffer(*commandAllocator, commandBuffer);

    nri::WorkSubmissionDesc workSubmissionDesc = {};
    workSubmissionDesc.commandBuffers = &commandBuffer;
    workSubmissionDesc.commandBufferNum = 1;

    NRI.BeginCommandBuffer(*commandBuffer, nullptr, 0);
    NRI.CmdBuildBottomLevelAccelerationStructure(*commandBuffer, objectNum, objects, BUILD_FLAGS, accelerationStructure, *scratchBuffer, 0);
    NRI.EndCommandBuffer(*commandBuffer);
    NRI.SubmitQueueWork(*m_CommandQueue, workSubmissionDesc, nullptr);
    NRI.WaitForIdle(*m_CommandQueue);

    NRI.DestroyCommandBuffer(*commandBuffer);
    NRI.DestroyCommandAllocator(*commandAllocator);

    NRI.DestroyBuffer(*scratchBuffer);
    NRI.FreeMemory(*scratchBufferMemory);
}

void Sample::BuildTopLevelAccelerationStructure(nri::AccelerationStructure& accelerationStructure, uint32_t instanceNum, nri::Buffer& instanceBuffer)
{
    nri::Buffer* scratchBuffer = nullptr;
    nri::Memory* scratchBufferMemory = nullptr;
    CreateScratchBuffer(accelerationStructure, scratchBuffer, scratchBufferMemory);

    nri::CommandAllocator* commandAllocator = nullptr;
    nri::CommandBuffer* commandBuffer = nullptr;
    NRI.CreateCommandAllocator(*m_CommandQueue, nri::WHOLE_DEVICE_GROUP, commandAllocator);
    NRI.CreateCommandBuffer(*commandAllocator, commandBuffer);

    nri::WorkSubmissionDesc workSubmissionDesc = {};
    workSubmissionDesc.commandBuffers = &commandBuffer;
    workSubmissionDesc.commandBufferNum = 1;

    NRI.BeginCommandBuffer(*commandBuffer, nullptr, 0);
    NRI.CmdBuildTopLevelAccelerationStructure(*commandBuffer, instanceNum, instanceBuffer, 0, BUILD_FLAGS, accelerationStructure, *scratchBuffer, 0);
    NRI.EndCommandBuffer(*commandBuffer);
    NRI.SubmitQueueWork(*m_CommandQueue, workSubmissionDesc, nullptr);
    NRI.WaitForIdle(*m_CommandQueue);

    NRI.DestroyCommandBuffer(*commandBuffer);
    NRI.DestroyCommandAllocator(*commandAllocator);

    NRI.DestroyBuffer(*scratchBuffer);
    NRI.FreeMemory(*scratchBufferMemory);
}

void Sample::CreateShaderTable()
{
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    const uint64_t identifierSize = deviceDesc.rayTracingShaderGroupIdentifierSize;
    const uint64_t tableAlignment = deviceDesc.rayTracingShaderTableAligment;

    m_ShaderGroupIdentifierSize = identifierSize;
    m_MissShaderOffset = helper::GetAlignedSize(identifierSize, tableAlignment);
    m_HitShaderGroupOffset = helper::GetAlignedSize(m_MissShaderOffset + identifierSize, tableAlignment);
    const uint64_t shaderTableSize = helper::GetAlignedSize(m_HitShaderGroupOffset + identifierSize, tableAlignment);

    const nri::BufferDesc bufferDesc = { shaderTableSize, 0, (nri::BufferUsageBits)0 };
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, m_ShaderTable));

    nri::MemoryDesc memoryDesc = {};
    NRI.GetBufferMemoryInfo(*m_ShaderTable, nri::MemoryLocation::DEVICE, memoryDesc);

    NRI_ABORT_ON_FAILURE(NRI.AllocateMemory(*m_Device, nri::WHOLE_DEVICE_GROUP, memoryDesc.type, memoryDesc.size, m_ShaderTableMemory));

    const nri::BufferMemoryBindingDesc bufferMemoryBindingDesc = { m_ShaderTableMemory, m_ShaderTable };
    NRI_ABORT_ON_FAILURE(NRI.BindBufferMemory(*m_Device, &bufferMemoryBindingDesc, 1));

    nri::Buffer* buffer = nullptr;
    nri::Memory* memory = nullptr;
    CreateUploadBuffer(shaderTableSize, nri::BufferUsageBits::NONE, buffer, memory);

    uint8_t* data = (uint8_t*)NRI.MapBuffer(*buffer, 0, shaderTableSize);
    for (uint32_t i = 0; i < 3; i++)
        NRI.WriteShaderGroupIdentifiers(*m_Pipeline, i, 1, data + i * helper::GetAlignedSize(identifierSize, tableAlignment));
    NRI.UnmapBuffer(*buffer);

    nri::CommandAllocator* commandAllocator = nullptr;
    nri::CommandBuffer* commandBuffer = nullptr;
    NRI.CreateCommandAllocator(*m_CommandQueue, nri::WHOLE_DEVICE_GROUP, commandAllocator);
    NRI.CreateCommandBuffer(*commandAllocator, commandBuffer);

    nri::WorkSubmissionDesc workSubmissionDesc = {};
    workSubmissionDesc.commandBuffers = &commandBuffer;
    workSubmissionDesc.commandBufferNum = 1;

    NRI.BeginCommandBuffer(*commandBuffer, nullptr, 0);
    NRI.CmdCopyBuffer(*commandBuffer, *m_ShaderTable, 0, 0, *buffer, 0, 0, shaderTableSize);
    NRI.EndCommandBuffer(*commandBuffer);
    NRI.SubmitQueueWork(*m_CommandQueue, workSubmissionDesc, nullptr);
    NRI.WaitForIdle(*m_CommandQueue);

    NRI.DestroyCommandBuffer(*commandBuffer);
    NRI.DestroyCommandAllocator(*commandAllocator);

    NRI.DestroyBuffer(*buffer);
    NRI.FreeMemory(*memory);
}

SAMPLE_MAIN(Sample, 0);
