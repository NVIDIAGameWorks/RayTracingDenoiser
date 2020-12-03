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
#include <commdlg.h>

#define CLARG_START(cmd, arg, condition)    { const wchar_t* _pArg = wcsstr(cmd, arg); _pArg += helper::GetCountOf(arg) - 1; if (_pArg > (wchar_t*)128 || (condition)) {
#define CLARG_IF_VALUE(value)               !_wcsnicmp(_pArg, value, wcslen(value))
#define CLARG_TO_UINT                       (uint32_t)_wtoi(_pArg)
#define CLARG_TO_UINT64                     (uint64_t)_wtoi64(_pArg)
#define CLARG_TO_FLOAT                      (float)_wtof(_pArg)
#define CLARG_END                           }}

static const std::array<const wchar_t*, 3> g_GraphicsAPI =
{
    L"D3D11",
    L"D3D12",
    L"VULKAN"
};

//==================================================================================================================================================
// MEMORY
//==================================================================================================================================================

void* __CRTDECL operator new(size_t size)
{
    return _aligned_malloc(size, DEFAULT_MEMORY_ALIGNMENT);
}

void* __CRTDECL operator new[](size_t size)
{
    return _aligned_malloc(size, DEFAULT_MEMORY_ALIGNMENT);
}

void __CRTDECL operator delete(void* p)
{
    _aligned_free(p);
}

void __CRTDECL operator delete[](void* p)
{
    _aligned_free(p);
}

//==================================================================================================================================================
// USER INTERFACE
//==================================================================================================================================================

constexpr uint64_t STREAM_BUFFER_SIZE = 8 * 1024 * 1024;

bool UserInterface::Initialize(HWND hwnd, nri::Device& device, const nri::CoreInterface& coreInterface, const nri::HelperInterface& helperInterface, uint32_t windowWidth, uint32_t windowHeight, uint32_t maxBufferedFrames, nri::Format renderTargetFormat)
{
    m_Device = &device;
    NRI = &coreInterface;
    m_Helper = &helperInterface;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameBorderSize = 1;
    style.WindowBorderSize = 1;

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ImeWindowHandle = hwnd;
    io.DisplaySize = ImVec2((float)windowWidth, (float)windowHeight);

    // Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array that we will update during the application lifetime.
    io.KeyMap[ImGuiKey_Tab] = VK_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
    io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
    io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
    io.KeyMap[ImGuiKey_Home] = VK_HOME;
    io.KeyMap[ImGuiKey_End] = VK_END;
    io.KeyMap[ImGuiKey_Insert] = VK_INSERT;
    io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
    io.KeyMap[ImGuiKey_Space] = VK_SPACE;
    io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
    io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
    io.KeyMap[ImGuiKey_A] = 'A';
    io.KeyMap[ImGuiKey_C] = 'C';
    io.KeyMap[ImGuiKey_V] = 'V';
    io.KeyMap[ImGuiKey_X] = 'X';
    io.KeyMap[ImGuiKey_Y] = 'Y';
    io.KeyMap[ImGuiKey_Z] = 'Z';

    // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    // We can honor io.WantSetMousePos requests (optional, rarely used)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    const nri::DeviceDesc& deviceDesc = NRI->GetDeviceDesc(device);

    // Pipeline
    {
        nri::StaticSamplerDesc staticSamplerDesc = {};
        staticSamplerDesc.samplerDesc.anisotropy = 1;
        staticSamplerDesc.samplerDesc.addressModes = {nri::AddressMode::REPEAT, nri::AddressMode::REPEAT};
        staticSamplerDesc.samplerDesc.magnification = nri::Filter::LINEAR;
        staticSamplerDesc.samplerDesc.minification = nri::Filter::LINEAR;
        staticSamplerDesc.samplerDesc.mip = nri::Filter::LINEAR;
        staticSamplerDesc.registerIndex = 0;
        staticSamplerDesc.visibility = nri::ShaderStage::FRAGMENT;

        nri::DescriptorRangeDesc descriptorRange = {0, 1, nri::DescriptorType::TEXTURE, nri::ShaderStage::FRAGMENT};

        nri::DescriptorSetDesc descriptorSet = {&descriptorRange, 1, &staticSamplerDesc, 1};

        nri::PushConstantDesc pushConstant = {};
        pushConstant.registerIndex = 0;
        pushConstant.size = 8;
        pushConstant.visibility = nri::ShaderStage::VERTEX;

        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.descriptorSetNum = 1;
        pipelineLayoutDesc.descriptorSets = &descriptorSet;
        pipelineLayoutDesc.pushConstantNum = 1;
        pipelineLayoutDesc.pushConstants = &pushConstant;
        pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::VERTEX | nri::PipelineLayoutShaderStageBits::FRAGMENT;

        if (NRI->CreatePipelineLayout(device, pipelineLayoutDesc, m_PipelineLayout) != nri::Result::SUCCESS)
            return false;

        utils::ShaderCodeStorage shaderCodeStorage;
        nri::ShaderDesc shaderStages[] =
        {
            utils::LoadShader(deviceDesc.graphicsAPI, "ImGUI.vs", shaderCodeStorage),
            utils::LoadShader(deviceDesc.graphicsAPI, "ImGUI.fs", shaderCodeStorage),
        };

        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;
        vertexStreamDesc.stride = sizeof(ImDrawVert);

        nri::VertexAttributeDesc vertexAttributeDesc[3] = {};
        {
            vertexAttributeDesc[0].format = nri::Format::RG32_SFLOAT;
            vertexAttributeDesc[0].streamIndex = 0;
            vertexAttributeDesc[0].offset = helper::GetOffsetOf(&ImDrawVert::pos);
            vertexAttributeDesc[0].d3d = {"POSITION", 0};
            vertexAttributeDesc[0].vk = {0};

            vertexAttributeDesc[1].format = nri::Format::RG32_SFLOAT;
            vertexAttributeDesc[1].streamIndex = 0;
            vertexAttributeDesc[1].offset = helper::GetOffsetOf(&ImDrawVert::uv);
            vertexAttributeDesc[1].d3d = {"TEXCOORD", 0};
            vertexAttributeDesc[1].vk = {1};

            vertexAttributeDesc[2].format = nri::Format::RGBA8_UNORM;
            vertexAttributeDesc[2].streamIndex = 0;
            vertexAttributeDesc[2].offset = helper::GetOffsetOf(&ImDrawVert::col);
            vertexAttributeDesc[2].d3d = {"COLOR", 0};
            vertexAttributeDesc[2].vk = {2};
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

        nri::ColorAttachmentDesc colorAttachmentDesc = {};
        colorAttachmentDesc.format = renderTargetFormat;
        colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;
        colorAttachmentDesc.blendEnabled = true;
        colorAttachmentDesc.colorBlend = {nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendFunc::ADD};
        colorAttachmentDesc.alphaBlend = {nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendFactor::ZERO, nri::BlendFunc::ADD};

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colorNum = 1;
        outputMergerDesc.color = &colorAttachmentDesc;

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = m_PipelineLayout;
        graphicsPipelineDesc.inputAssembly = &inputAssemblyDesc;
        graphicsPipelineDesc.rasterization = &rasterizationDesc;
        graphicsPipelineDesc.outputMerger = &outputMergerDesc;
        graphicsPipelineDesc.shaderStages = shaderStages;
        graphicsPipelineDesc.shaderStageNum = helper::GetCountOf(shaderStages);

        if (NRI->CreateGraphicsPipeline(device, graphicsPipelineDesc, m_Pipeline) != nri::Result::SUCCESS)
            return false;
    }

    int fontTextureWidth = 0, fontTextureHeight = 0;
    unsigned char* fontPixels = nullptr;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontTextureWidth, &fontTextureHeight);

    // Resources
    constexpr nri::Format format = nri::Format::RGBA8_UNORM;
    {
        // Geometry
        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = STREAM_BUFFER_SIZE;
        bufferDesc.usageMask = nri::BufferUsageBits::VERTEX_BUFFER | nri::BufferUsageBits::INDEX_BUFFER;
        if (NRI->CreateBuffer(device, bufferDesc, m_GeometryBuffer) != nri::Result::SUCCESS)
            return false;

        // Texture
        nri::TextureDesc textureDesc = {};
        textureDesc.type = nri::TextureType::TEXTURE_2D;
        textureDesc.format = format;
        textureDesc.size[0] = (uint16_t)fontTextureWidth;
        textureDesc.size[1] = (uint16_t)fontTextureHeight;
        textureDesc.size[2] = 1;
        textureDesc.mipNum = 1;
        textureDesc.arraySize = 1;
        textureDesc.sampleNum = 1;
        textureDesc.usageMask = nri::TextureUsageBits::SHADER_RESOURCE;
        if (NRI->CreateTexture(device, textureDesc, m_FontTexture) != nri::Result::SUCCESS)
            return false;
    }

    m_MemoryAllocations.resize(2, nullptr);

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_GeometryBuffer;

    nri::Result result = m_Helper->AllocateAndBindMemory(device, resourceGroupDesc, m_MemoryAllocations.data());
    if (result != nri::Result::SUCCESS)
        return false;

    resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = 1;
    resourceGroupDesc.textures = &m_FontTexture;

    result = m_Helper->AllocateAndBindMemory(device, resourceGroupDesc, m_MemoryAllocations.data() + 1);
    if (result != nri::Result::SUCCESS)
        return false;

    // Descriptor
    {
        nri::Texture2DViewDesc texture2DViewDesc = {m_FontTexture, nri::Texture2DViewType::SHADER_RESOURCE_2D, format};
        if (NRI->CreateTexture2DView(texture2DViewDesc, m_FontShaderResource) != nri::Result::SUCCESS)
            return false;
    }

    utils::Texture texture;
    utils::LoadTextureFromMemory(format, fontTextureWidth, fontTextureHeight, fontPixels, texture);

    nri::CommandQueue* commandQueue = nullptr;
    NRI->GetCommandQueue(device, nri::CommandQueueType::GRAPHICS, commandQueue);

    // Upload data
    {

        nri::TextureSubresourceUploadDesc subresource = {};
        texture.GetSubresource(subresource, 0);

        nri::TextureUploadDesc textureData = {};
        textureData.subresources = &subresource;
        textureData.mipNum = 1;
        textureData.arraySize = 1;
        textureData.texture = m_FontTexture;
        textureData.nextLayout = nri::TextureLayout::SHADER_RESOURCE;
        textureData.nextAccess = nri::AccessBits::SHADER_RESOURCE;

        if ( m_Helper->UploadData(*commandQueue, &textureData, 1, nullptr, 0) != nri::Result::SUCCESS)
            return false;
    }

    // Descriptor pool
    {
        nri::DescriptorPoolDesc descriptorPoolDesc = {};
        descriptorPoolDesc.descriptorSetMaxNum = 1;
        descriptorPoolDesc.textureMaxNum = 1;
        descriptorPoolDesc.staticSamplerMaxNum = 1;

        if (NRI->CreateDescriptorPool(device, descriptorPoolDesc, m_DescriptorPool) != nri::Result::SUCCESS)
            return false;
    }

    // Texture & sampler descriptor set
    {
        if (NRI->AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 0, &m_DescriptorSet, 1, nri::WHOLE_DEVICE_GROUP, 0) != nri::Result::SUCCESS)
            return false;

        nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDesc = {};
        descriptorRangeUpdateDesc.descriptorNum = 1;
        descriptorRangeUpdateDesc.descriptors = &m_FontShaderResource;

        NRI->UpdateDescriptorRanges(*m_DescriptorSet, nri::WHOLE_DEVICE_GROUP, 0, 1, &descriptorRangeUpdateDesc);
    }

    return true;
}

void UserInterface::Shutdown()
{
    if (!IsInitialized())
        return;

    ImGui::DestroyContext();

    if (m_DescriptorPool)
        NRI->DestroyDescriptorPool(*m_DescriptorPool);

    if (m_Pipeline)
        NRI->DestroyPipeline(*m_Pipeline);

    if (m_PipelineLayout)
        NRI->DestroyPipelineLayout(*m_PipelineLayout);

    if (m_FontShaderResource)
        NRI->DestroyDescriptor(*m_FontShaderResource);

    if (m_FontTexture)
        NRI->DestroyTexture(*m_FontTexture);

    if (m_GeometryBuffer)
        NRI->DestroyBuffer(*m_GeometryBuffer);

    for (uint32_t i = 0; i < m_MemoryAllocations.size(); i++)
        NRI->FreeMemory(*m_MemoryAllocations[i]);
}

void UserInterface::Prepare()
{
    if (!IsInitialized())
        return;

    ImGuiIO& io = ImGui::GetIO();

    // Setup time step
    m_Timer.UpdateElapsedTimeSinceLastSave();
    m_Timer.SaveCurrentTime();
    io.DeltaTime = m_Timer.GetElapsedTime() * 0.001f;

    // Read keyboard modifiers inputs
    io.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    io.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    io.KeyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    io.KeySuper = false;

    // Set OS mouse position if requested (only used when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
    if (io.WantSetMousePos)
    {
        POINT pos = { (int)io.MousePos.x, (int)io.MousePos.y };
        ClientToScreen((HWND)io.ImeWindowHandle, &pos);
        SetCursorPos(pos.x, pos.y);
    }
    UpdateMouseCursor();

    // Start the frame. This call will update the io.WantCaptureMouse, io.WantCaptureKeyboard flag that you can use to dispatch inputs (or not) to your application.
    ImGui::NewFrame();
}

bool UserInterface::UpdateMouseCursor()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
        return false;

    ImGuiMouseCursor imgui_cursor = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
    if (imgui_cursor == ImGuiMouseCursor_None)
    {
        // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        ::SetCursor(nullptr);
    }
    else
    {
        // Hardware cursor type
        LPTSTR win32_cursor = IDC_ARROW;
        switch (imgui_cursor)
        {
        case ImGuiMouseCursor_Arrow:        win32_cursor = IDC_ARROW; break;
        case ImGuiMouseCursor_TextInput:    win32_cursor = IDC_IBEAM; break;
        case ImGuiMouseCursor_ResizeAll:    win32_cursor = IDC_SIZEALL; break;
        case ImGuiMouseCursor_ResizeEW:     win32_cursor = IDC_SIZEWE; break;
        case ImGuiMouseCursor_ResizeNS:     win32_cursor = IDC_SIZENS; break;
        case ImGuiMouseCursor_ResizeNESW:   win32_cursor = IDC_SIZENESW; break;
        case ImGuiMouseCursor_ResizeNWSE:   win32_cursor = IDC_SIZENWSE; break;
        }
        ::SetCursor(::LoadCursor(nullptr, win32_cursor));
    }

    return true;
}

void UserInterface::Render(nri::CommandBuffer& commandBuffer)
{
    if (!IsInitialized())
        return;

    ImGui::Render();
    const ImDrawData& drawData = *ImGui::GetDrawData();

    // Prepare
    uint32_t vertexDataSize = drawData.TotalVtxCount * sizeof(ImDrawVert);
    uint32_t indexDataSize = drawData.TotalIdxCount * sizeof(ImDrawIdx);
    uint32_t vertexDataSizeAligned = helper::GetAlignedSize(vertexDataSize, 16);
    uint32_t indexDataSizeAligned = helper::GetAlignedSize(indexDataSize, 16);
    uint32_t totalDataSizeAligned = vertexDataSizeAligned + indexDataSizeAligned;
    if (!totalDataSizeAligned)
        return;

    assert(totalDataSizeAligned < STREAM_BUFFER_SIZE / BUFFERED_FRAME_MAX_NUM);

    if (m_StreamBufferOffset + totalDataSizeAligned > STREAM_BUFFER_SIZE)
        m_StreamBufferOffset = 0;

    uint64_t indexBufferOffset = m_StreamBufferOffset;
    uint8_t* indexData = (uint8_t*)NRI->MapBuffer(*m_GeometryBuffer, m_StreamBufferOffset, totalDataSizeAligned);
    uint64_t vertexBufferOffset = indexBufferOffset + indexDataSizeAligned;
    uint8_t* vertexData = indexData + indexDataSizeAligned;

    for (int32_t n = 0; n < drawData.CmdListsCount; n++)
    {
        const ImDrawList& drawList = *drawData.CmdLists[n];

        uint32_t size = drawList.VtxBuffer.Size * sizeof(ImDrawVert);
        memcpy(vertexData, drawList.VtxBuffer.Data, size);
        vertexData += size;

        size = drawList.IdxBuffer.Size * sizeof(ImDrawIdx);
        memcpy(indexData, drawList.IdxBuffer.Data, size);
        indexData += size;
    }

    m_StreamBufferOffset += totalDataSizeAligned;

    NRI->UnmapBuffer(*m_GeometryBuffer);

    float invScreenSize[2];
    invScreenSize[0] = 1.0f / ImGui::GetIO().DisplaySize.x;
    invScreenSize[1] = 1.0f / ImGui::GetIO().DisplaySize.y;

    {
        helper::Annotation(*NRI, commandBuffer, "UserInterface");

        NRI->CmdSetDescriptorPool(commandBuffer, *m_DescriptorPool);
        NRI->CmdSetPipelineLayout(commandBuffer, *m_PipelineLayout);
        NRI->CmdSetPipeline(commandBuffer, *m_Pipeline);
        NRI->CmdSetConstants(commandBuffer, 0, invScreenSize, sizeof(invScreenSize));
        NRI->CmdSetIndexBuffer(commandBuffer, *m_GeometryBuffer, indexBufferOffset, sizeof(ImDrawIdx) == 2 ? nri::IndexType::UINT16 : nri::IndexType::UINT32);
        NRI->CmdSetVertexBuffers(commandBuffer, 0, 1, &m_GeometryBuffer, &vertexBufferOffset);
        NRI->CmdSetDescriptorSets(commandBuffer, 0, 1, &m_DescriptorSet, nullptr);

        const nri::Viewport viewport = { 0.0f, 0.0f, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 0.0f, 1.0f };
        NRI->CmdSetViewports(commandBuffer, &viewport, 1);

        int32_t vertexOffset = 0;
        int32_t indexOffset = 0;
        for (int32_t n = 0; n < drawData.CmdListsCount; n++)
        {
            const ImDrawList& drawList = *drawData.CmdLists[n];
            for (int32_t i = 0; i < drawList.CmdBuffer.Size; i++)
            {
                const ImDrawCmd& drawCmd = drawList.CmdBuffer[i];
                if (drawCmd.UserCallback)
                    drawCmd.UserCallback(&drawList, &drawCmd);
                else
                {
                    nri::Rect rect =
                    {
                        (int32_t)drawCmd.ClipRect.x,
                        (int32_t)drawCmd.ClipRect.y,
                        (uint32_t)(drawCmd.ClipRect.z - drawCmd.ClipRect.x),
                        (uint32_t)(drawCmd.ClipRect.w - drawCmd.ClipRect.y)
                    };
                    NRI->CmdSetScissors(commandBuffer, &rect, 1);

                    NRI->CmdDrawIndexed(commandBuffer, drawCmd.ElemCount, 1, indexOffset, vertexOffset, 0);
                }
                indexOffset += drawCmd.ElemCount;
            }
            vertexOffset += drawList.VtxBuffer.Size;
        }
    }
}

bool UserInterface::ProcessMessages(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Process Win32 mouse/keyboard inputs.
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
    // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
    // PS: In this Win32 handler, we use the capture API (GetCapture/SetCapture/ReleaseCapture) to be able to read mouse coordinations when dragging mouse outside of our window bounds.
    // PS: We treat DBLCLK messages as regular mouse down messages, so this code will work on windows classes that have the CS_DBLCLKS flag set. Our own example app code doesn't set this flag.

    if ( !ImGui::GetCurrentContext() )
        return false;

    ImGuiIO& io = ImGui::GetIO();
    switch (msg)
    {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
    {
        int button = 0;
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) button = 0;
        if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) button = 1;
        if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK) button = 2;
        if (!ImGui::IsAnyMouseDown() && ::GetCapture() == nullptr)
            ::SetCapture(hwnd);
        io.MouseDown[button] = true;
        return false;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    {
        int button = 0;
        if (msg == WM_LBUTTONUP) button = 0;
        if (msg == WM_RBUTTONUP) button = 1;
        if (msg == WM_MBUTTONUP) button = 2;
        io.MouseDown[button] = false;
        if (!ImGui::IsAnyMouseDown() && ::GetCapture() == hwnd)
            ::ReleaseCapture();
        return false;
    }
    case WM_MOUSEWHEEL:
        io.MouseWheel += GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1.0f : -1.0f;
        return false;
    case WM_MOUSEHWHEEL:
        io.MouseWheelH += GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1.0f : -1.0f;
        return false;
    case WM_MOUSEMOVE:
        io.MousePos.x = (signed short)(lParam);
        io.MousePos.y = (signed short)(lParam >> 16);
        return false;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam < 256)
            io.KeysDown[wParam] = 1;
        return false;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam < 256)
            io.KeysDown[wParam] = 0;
        return false;
    case WM_CHAR:
        if (wParam > 0 && wParam < 0x10000)
            io.AddInputCharacter((unsigned short)wParam);
        return false;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && UpdateMouseCursor())
            return true;
        return false;
    }
    return false;
}

//==================================================================================================================================================
// SAMPLE
//==================================================================================================================================================

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SampleBase* sample = (SampleBase*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    return sample->WindowProc(hWnd, msg, wParam, lParam);
}

LRESULT SampleBase::WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (m_UserInterface.ProcessMessages(hWnd, msg, wParam, lParam))
        return 0;

    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_INPUT:
        m_Input.Process((void*)lParam);
        return 0;

    case WM_ACTIVATE:
        m_IsActive = LOWORD(wParam) != 0;
        m_IsActive &= HIWORD(wParam) == 0;
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool SampleBase::ProcessMessages()
{
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT)
            return false;
    }

    return true;
}

bool SampleBase::OpenFileDialog(const char* title, char* path, uint32_t pathLen)
{
    *path = '\0';

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrTitle = title;
    ofn.lpstrFile = path;
    ofn.nMaxFile = pathLen;
    ofn.lpstrFilter = "Supported formats\0*.fbx;*.obj;*.fscene";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    BOOL result = GetOpenFileNameA(&ofn);

    return result == TRUE;
}

void SampleBase::GetCameraDescFromInputDevices(CameraDesc& cameraDesc)
{
    if (m_Input.IsButtonPressed(Button::Right))
    {
        if (m_Input.GetMouseWheel() > 0.0f)
            m_Camera.state.motionScale *= 1.1f;
        else if (m_Input.GetMouseWheel() < 0.0f)
            m_Camera.state.motionScale /= 1.1f;

        float motionScale = m_Camera.state.motionScale;

        cameraDesc.dYaw = -m_Input.GetMouseDx();
        if (m_Input.IsKeyPressed(Key::Right))
            cameraDesc.dYaw -= motionScale;
        if (m_Input.IsKeyPressed(Key::Left))
            cameraDesc.dYaw += motionScale;

        cameraDesc.dPitch = -m_Input.GetMouseDy();
        if (m_Input.IsKeyPressed(Key::Up))
            cameraDesc.dPitch += motionScale;
        if (m_Input.IsKeyPressed(Key::Down))
            cameraDesc.dPitch -= motionScale;

        if (m_Input.IsKeyPressed(Key::W))
            cameraDesc.dLocal.z += motionScale;
        if (m_Input.IsKeyPressed(Key::S))
            cameraDesc.dLocal.z -= motionScale;
        if (m_Input.IsKeyPressed(Key::D))
            cameraDesc.dLocal.x += motionScale;
        if (m_Input.IsKeyPressed(Key::A))
            cameraDesc.dLocal.x -= motionScale;
        if (m_Input.IsKeyPressed(Key::E))
            cameraDesc.dLocal.y += motionScale;
        if (m_Input.IsKeyPressed(Key::Q))
            cameraDesc.dLocal.y -= motionScale;
    }
}

bool SampleBase::Create(const wchar_t* windowTitle, HINSTANCE hInstance, int nCmdShow)
{
    if (!m_IgnoreDPI)
        SetProcessDPIAware();

    wchar_t windowName[256];
    swprintf_s(windowName, L"%s [%s]", windowTitle, g_GraphicsAPI[(size_t)m_GraphicsAPI]);

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = DefWindowProcW;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wcex.lpszClassName = windowName;
    RegisterClassExW(&wcex);

    // TODO: it doesn't work well for multi-monitor setup
    const uint32_t screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const uint32_t screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (m_WindowWidth > screenWidth)
        m_WindowWidth = screenWidth;

    if (m_WindowHeight > screenHeight)
        m_WindowHeight = screenHeight;

    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (m_WindowWidth == screenWidth || m_WindowHeight == screenHeight)
        style = WS_POPUP;

    RECT rect = { 0, 0, (LONG)m_WindowWidth, (LONG)m_WindowHeight };
    AdjustWindowRect(&rect, style, FALSE);

    int32_t x = (screenWidth - m_WindowWidth) >> 1;
    int32_t y = (screenHeight - m_WindowHeight) >> 1;

    m_hWnd = CreateWindowExW(0, windowName, L"Loading...", style, x, y, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr);
    if (!m_hWnd)
        return false;

    m_Input.Inititialize(m_hWnd);

    SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, (LONG_PTR)this);
    SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, (LONG_PTR)WndProc);

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);

    bool result = Initialize(m_GraphicsAPI);

    SetWindowTextW(m_hWnd, windowName);

    return result;
}

void SampleBase::RenderLoop()
{
    for (uint32_t i = 0; i < m_FrameNum; i++)
    {
        m_Input.Prepare();

        if (!ProcessMessages())
            break;

        if (!m_IsActive)
        {
            i--;
            continue;
        }

        if (m_Input.IsButtonPressed(Button::Right))
        {
            POINT windowCenter;
            windowCenter.x = m_WindowWidth >> 1;
            windowCenter.y = m_WindowHeight >> 1;
            ::ClientToScreen(m_hWnd, &windowCenter);
            ::SetCursorPos(windowCenter.x, windowCenter.y);

            for (uint32_t n = 0; ShowCursor(0) >= 0 && n < 256; n++)
                ;
        }
        else
        {
            for (uint32_t n = 0; ShowCursor(1) < 0 && n < 256; n++)
                ;
        }

        PrepareFrame(i);
        RenderFrame(i);
    }
}

void SampleBase::ParseCommandLine(const wchar_t* commandLine)
{
    CLARG_START(commandLine, L"--help", *commandLine == '\0')
        const wchar_t* pczText =
        L"--help - this message\n"
        L"--api=<D3D11/D3D12/VULKAN>\n"
        L"--width=<window width>\n"
        L"--height=<window height>\n"
        L"--frameNum=<num>\n"
        L"--swapInterval=<0/1>\n"
        L"--scene=<scene path relative to '_Data/Scenes' folder>\n"
        L"--debugAPI\n"
        L"--debugNRI\n"
        L"--ignoreDPI\n"
        L"--testMode\n"
        L"";
        MessageBox(nullptr, pczText, L"How to use?", MB_OK);
    CLARG_END;

    CLARG_START(commandLine, L"--api=", false)
        if (CLARG_IF_VALUE(g_GraphicsAPI[0]))
            m_GraphicsAPI = nri::GraphicsAPI::D3D11;
        else if (CLARG_IF_VALUE(g_GraphicsAPI[1]))
            m_GraphicsAPI = nri::GraphicsAPI::D3D12;
        else if (CLARG_IF_VALUE(g_GraphicsAPI[2]))
            m_GraphicsAPI = nri::GraphicsAPI::VULKAN;
    CLARG_END;

    CLARG_START(commandLine, L"--width=", false)
        m_WindowWidth = CLARG_TO_UINT;
    CLARG_END;

    CLARG_START(commandLine, L"--height=", false)
        m_WindowHeight = CLARG_TO_UINT;
    CLARG_END;

    CLARG_START(commandLine, L"--frameNum=", false)
        m_FrameNum = CLARG_TO_UINT;
    CLARG_END;

    CLARG_START(commandLine, L"--debugAPI", false)
        m_DebugAPI = true;
    CLARG_END;

    CLARG_START(commandLine, L"--debugNRI", false)
        m_DebugNRI = true;
    CLARG_END;

    CLARG_START(commandLine, L"--ignoreDPI", false)
        m_IgnoreDPI = true;
    CLARG_END;

    CLARG_START(commandLine, L"--testMode", false)
        m_TestMode = true;
    CLARG_END;

    CLARG_START(commandLine, L"--swapInterval=", false)
        m_SwapInterval = CLARG_TO_UINT;
    CLARG_END;

    CLARG_START(commandLine, L"--scene=", false)
        char* out = m_SceneFile;
        for (uint32_t i = 0; i < sizeof(m_SceneFile) - 1 && (*_pArg != 0) && (*_pArg != ' '); i++)
            *out++ = char(*_pArg++);
        *out = 0;
        m_FrameNum = 9999999;
    CLARG_END;
}

void SampleBase::EnableMemoryLeakDetection(uint32_t breakOnAllocationIndex)
{
#ifdef _DEBUG
    int flag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    flag |= _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag(flag);

    // https://msdn.microsoft.com/en-us/library/x98tx3cf.aspx
    if (breakOnAllocationIndex)
        _crtBreakAlloc = breakOnAllocationIndex;
#endif
}
