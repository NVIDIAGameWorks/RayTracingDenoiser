/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <windows.h>
#undef OPAQUE
#undef TRANSPARENT

#include "ImGui/imgui.h"

#include "NRIDescs.hpp"
#include "Extensions/NRIDeviceCreation.h"
#include "Extensions/NRISwapChain.h"
#include "Extensions/NRIWrapperD3D11.h"
#include "Extensions/NRIWrapperD3D12.h"

#include "Helper.h"
#include "Utils.h"
#include "Input.h"
#include "Camera.h"
#include "Timer/Timer.h"

constexpr nri::SPIRVBindingOffsets SPIRV_BINDING_OFFSETS = {100, 200, 300, 400}; // It comes from CompileHLSL.bat
constexpr bool D3D11_COMMANDBUFFER_EMULATION = false;
constexpr uint32_t DEFAULT_MEMORY_ALIGNMENT = 16;
constexpr uint32_t BUFFERED_FRAME_MAX_NUM = 2;
constexpr uint32_t SWAP_CHAIN_TEXTURE_NUM = BUFFERED_FRAME_MAX_NUM;

void* __CRTDECL operator new(size_t size);
void* __CRTDECL operator new[](size_t size);
void __CRTDECL operator delete(void* p);
void __CRTDECL operator delete[](void* p);

class SampleBase;

struct BackBuffer
{
    nri::FrameBuffer* frameBuffer;
    nri::FrameBuffer* frameBufferUI;
    nri::Descriptor* colorAttachment;
    nri::Texture* texture;
};

class UserInterface
{
public:
    friend class SampleBase;

    UserInterface()
    {}

    ~UserInterface()
    {}

    inline bool IsInitialized() const
    { return NRI != nullptr; }

    bool Initialize(HWND hwnd, nri::Device& device, const nri::CoreInterface& coreInterface, uint32_t windowWidth, uint32_t windowHeight, uint32_t maxBufferedFrames, nri::Format renderTargetFormat);
    void Shutdown();
    void Prepare();
    void Render(nri::CommandBuffer& commandBuffer);

private:
    bool UpdateMouseCursor();
    bool ProcessMessages(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    std::vector<nri::Memory*> m_Memories;
    const nri::CoreInterface* NRI = nullptr;
    nri::Device* m_Device = nullptr;
    nri::DescriptorPool* m_DescriptorPool = nullptr;
    nri::DescriptorSet* m_DescriptorSet = nullptr;
    nri::Descriptor* m_FontShaderResource = nullptr;
    nri::Pipeline* m_Pipeline = nullptr;
    nri::PipelineLayout* m_PipelineLayout = nullptr;
    nri::Texture* m_FontTexture = nullptr;
    nri::Buffer* m_GeometryBuffer = nullptr;
    uint64_t m_StreamBufferOffset = 0;
    ImGuiMouseCursor m_LastMouseCursor = ImGuiMouseCursor_COUNT;
    Timer m_Timer;
};

class SampleBase
{
public:
    SampleBase()
    {}

    virtual ~SampleBase()
    {}

    inline uint16_t GetWindowWidth() const
    { return (uint16_t)m_WindowWidth; }

    inline uint16_t GetWindowHeight() const
    { return (uint16_t)m_WindowHeight; }

    inline bool IsAutomated() const
    { return m_FrameNum != uint32_t(-1); }

    bool Create(const wchar_t* windowTitle, HINSTANCE hInstance, int nCmdShow);
    void RenderLoop();
    void ParseCommandLine(const wchar_t* commandLine);
    bool OpenFileDialog(const char* title, char* path, uint32_t pathLen);
    void GetCameraDescFromInputDevices(CameraDesc& cameraDesc);
    LRESULT WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    virtual bool Initialize(nri::GraphicsAPI graphicsAPI) = 0;
    virtual void PrepareFrame(uint32_t frameIndex) = 0;
    virtual void RenderFrame(uint32_t frameIndex) = 0;

    static void EnableMemoryLeakDetection(uint32_t breakOnAllocationIndex);

private:
    bool ProcessMessages();

protected:
    UserInterface m_UserInterface;
    Camera m_Camera;
    Input m_Input;
    HWND m_hWnd = nullptr;
    uint32_t m_SwapInterval = 0;
    bool m_DebugAPI = false;
    bool m_DebugNRI = false;
    bool m_IgnoreDPI = false;
    bool m_IsActive = true;
    bool m_TestMode = false;
    char m_SceneFile[512] = "ShaderBalls/ShaderBalls.obj";

private:
    uint32_t m_WindowWidth = 1280;
    uint32_t m_WindowHeight = 720;
    uint32_t m_FrameNum = uint32_t(-1);
    nri::GraphicsAPI m_GraphicsAPI = nri::GraphicsAPI::D3D11;
};

#define _STRINGIFY(s) L#s
#define STRINGIFY(s) _STRINGIFY(s)

#define SAMPLE_MAIN(className, memoryAllocationIndexForBreak) \
    int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) \
    { \
        SampleBase::EnableMemoryLeakDetection(memoryAllocationIndexForBreak); \
        SampleBase* sample = (className*)_aligned_malloc( sizeof(className), alignof(className) ); \
        new((className*)sample) className(); \
        sample->ParseCommandLine(lpCmdLine); \
        bool result = sample->Create(STRINGIFY(PROJECT_NAME), hInstance, nCmdShow); \
        if (result) \
            sample->RenderLoop(); \
        sample->~SampleBase(); \
        _aligned_free((void*)sample); \
        return result ? 0 : 1; \
    }
