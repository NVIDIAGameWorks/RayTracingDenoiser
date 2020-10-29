/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <windows.h>

#include <vector>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <atomic>
#include <ctime>

#define FOLDER_DATA L"_Data"
#define FOLDER_BUILD L"_Build"
#define FOLDER_COMPILE_SHADERS L"CompileShaders"

std::wstring g_OutputDir;
std::wstring g_OutputHeaderDir;
CRITICAL_SECTION g_LogCriticalSection;

#define CHECK_WINAPI(cond, comment) if (!(cond)) { Log(comment " (error %d)\n", GetLastError()); }

enum class ShaderStage
{
    NONE,
    VERTEX,
    TESS_CONTROL,
    TESS_EVALUTATION,
    GEOMETRY,
    FRAGMENT,
    COMPUTE,
    RAYGEN,
    MISS,
    CLOSEST_HIT,
    ANY_HIT,
    MAX_NUM
};

static const wchar_t* EXT_ARRAY[] = {
    L"",
    L".vs",
    L".tcs",
    L".tes",
    L".gs",
    L".fs",
    L".cs",
    L".rgen",
    L".rmiss",
    L".rchit",
    L".rahit"
};

static const wchar_t* HLSL_PROFILE_ARRAY[] = {
    L"",
    L"vs",
    L"hs",
    L"ds",
    L"gs",
    L"ps",
    L"cs",
    L"lib",
    L"lib",
    L"lib",
    L"lib"
};

static const wchar_t* SPIRV_PROFILE_ARRAY[] = {
    L"",
    L"vert",
    L"tesc",
    L"tese",
    L"geom",
    L"frag",
    L"comp",
    L"rgen",
    L"rmiss",
    L"rchit",
    L"rahit"
};

struct Shader
{
    std::wstring file;
    ShaderStage stage;
};

void EnumerateFiles(const std::wstring& path, std::vector<Shader>& shaders)
{
    for (const auto& entry : std::filesystem::directory_iterator(path))
    {
        if (entry.is_directory())
            EnumerateFiles(entry.path(), shaders);
        else
        {
            const std::wstring& file = entry.path();

            if (file.find(FOLDER_BUILD) == std::wstring::npos && file.find(FOLDER_DATA) == std::wstring::npos)
            {
                for (size_t k = 1; k < (size_t)ShaderStage::MAX_NUM; k++)
                {
                    if (file.rfind(std::wstring(EXT_ARRAY[k]) + L".") != std::wstring::npos)
                    {
                        shaders.push_back( { file, (ShaderStage)k } );
                        break;
                    }
                }
            }
        }
    }
}

std::wstring GetFileNameWithoutExt(const std::wstring& path)
{
    std::wstring fileName = path;

    const size_t slashPos = fileName.find_last_of(L"\\/");
    if (slashPos != std::wstring::npos)
        fileName.erase(fileName.begin(), fileName.begin() + slashPos + 1);

    const size_t dotPos = fileName.find_first_of(L'.');
    if (dotPos != std::wstring::npos)
        fileName.erase(fileName.begin() + dotPos, fileName.end());

    return fileName;
}

void Log(const char* format, ...)
{
    if (format == nullptr || *format == 0)
        return;

    char buffer[65536];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end (args);

    EnterCriticalSection(&g_LogCriticalSection);
    printf("%s", buffer);
    LeaveCriticalSection(&g_LogCriticalSection);
}

struct CompilationThread
{
    CompilationThread() = default;
    CompilationThread(const Shader* shaders, size_t shaderNum, std::atomic_size_t& compiledShaderNum);
    ~CompilationThread();

private:
    static DWORD WorkerThread(void* arg);
    static DWORD InputThread(void* arg);

    DWORD CompileShader(ShaderStage stage, const std::wstring& sourcePath);
    DWORD ExecuteCommandLine(const std::wstring& commandLine);

    const Shader* m_Shaders = nullptr;
    size_t m_ShaderNum = 0;
    std::atomic_size_t* m_CompiledShaderNum = nullptr;

    std::atomic<HANDLE> m_InputPipe = nullptr;
    HANDLE m_ChildApplicationInputPipe = nullptr;
    HANDLE m_ChildApplicationOutputPipe = nullptr;
    HANDLE m_InputEvent = nullptr;

    HANDLE m_WorkerThread = nullptr;
};

CompilationThread::CompilationThread(const Shader* shaders, size_t shaderNum, std::atomic_size_t& compiledShaderNum) :
    m_Shaders(shaders),
    m_ShaderNum(shaderNum),
    m_CompiledShaderNum(&compiledShaderNum)
{
    m_WorkerThread = CreateThread(nullptr, 0, &WorkerThread, this, 0, nullptr);
    CHECK_WINAPI(m_WorkerThread != nullptr, "failed to create compilation thread");
}

CompilationThread::~CompilationThread()
{
    WaitForSingleObject(m_WorkerThread, INFINITE);
    CHECK_WINAPI(CloseHandle(m_WorkerThread), "CloseHandle(m_WorkerThread)");
}

DWORD CompilationThread::WorkerThread(void* arg)
{
    CompilationThread& thread = *(CompilationThread*)arg;

    if (thread.m_ShaderNum == 0)
        return 0;

    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipes[2] = {};
    HANDLE writePipes[2] = {};
    CHECK_WINAPI(CreatePipe(&readPipes[0], &writePipes[0], &securityAttributes, 0), "CreatePipe() failed");
    CHECK_WINAPI(CreatePipe(&readPipes[1], &writePipes[1], &securityAttributes, 0), "second CreatePipe() failed");
    CHECK_WINAPI(SetHandleInformation(readPipes[0], HANDLE_FLAG_INHERIT, 0), "SetHandleInformation(readPipes[0]) failed");
    CHECK_WINAPI(SetHandleInformation(writePipes[1], HANDLE_FLAG_INHERIT, 0), "SetHandleInformation(writePipes[1]) failed");

    thread.m_InputPipe.store(readPipes[0]);
    thread.m_ChildApplicationInputPipe = readPipes[1];
    thread.m_ChildApplicationOutputPipe = writePipes[0];
    thread.m_InputEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CHECK_WINAPI(thread.m_InputEvent != nullptr, "CreateEventW() failed");

    const HANDLE inputThread = CreateThread(nullptr, 0, &InputThread, &thread, 0, nullptr);
    CHECK_WINAPI(inputThread != nullptr, "CreateThread() for input thread failed");

    size_t compiled = 0;
    for (size_t i = 0; i < thread.m_ShaderNum; i++)
        compiled += thread.CompileShader(thread.m_Shaders[i].stage, thread.m_Shaders[i].file) == 0 ? 1 : 0;

    thread.m_InputPipe.store(nullptr);
    CHECK_WINAPI(CloseHandle(writePipes[0]), "CloseHandle(writePipes[0]) failed");
    CHECK_WINAPI(CloseHandle(writePipes[1]), "CloseHandle(writePipes[1]) failed");
    CHECK_WINAPI(CloseHandle(readPipes[0]), "CloseHandle(readPipes[0]) failed");
    CHECK_WINAPI(CloseHandle(readPipes[1]), "CloseHandle(readPipes[1]) failed");
    CHECK_WINAPI(SetEvent(thread.m_InputEvent), "SetEvent() failed");

    const DWORD timeout = 30000; // 30 seconds
    const DWORD waitResult = WaitForSingleObject(inputThread, timeout);
    CHECK_WINAPI(CloseHandle(inputThread), "CloseHandle(inputThread) failed");

    if (waitResult == WAIT_OBJECT_0)
        CHECK_WINAPI(CloseHandle(thread.m_InputEvent), "CloseHandle(thread.m_InputEvent) failed");

    thread.m_CompiledShaderNum->fetch_add(compiled);

    return 0;
}

DWORD CompilationThread::InputThread(void* arg)
{
    CompilationThread& thread = *(CompilationThread*)arg;

    std::vector<char> buffer(65536);
    DWORD readSize = 1;

    do
    {
        WaitForSingleObject(thread.m_InputEvent, INFINITE);
        const HANDLE pipe = thread.m_InputPipe.load(std::memory_order_seq_cst);

        readSize = 0;
        while (ReadFile(pipe, buffer.data(), (DWORD)buffer.size(), &readSize, nullptr))
        {
            const size_t lastChar = std::min<size_t>(readSize, buffer.size() - 1);
            buffer[lastChar] = 0;

            if (readSize > 0)
                Log(buffer.data());
        }
    }
    while (thread.m_InputPipe.load(std::memory_order_relaxed) != nullptr);

    return 0;
}

DWORD CompilationThread::ExecuteCommandLine(const std::wstring& commandLine)
{
    wchar_t currentDirectory[MAX_PATH] = {};
    GetCurrentDirectoryW(_countof(currentDirectory), currentDirectory);

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = (DWORD)sizeof(startupInfo);
    startupInfo.hStdError = m_ChildApplicationOutputPipe;
    startupInfo.hStdOutput = m_ChildApplicationOutputPipe;
    startupInfo.hStdInput = m_ChildApplicationInputPipe;
    startupInfo.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION processInfo = {};

    const BOOL result = CreateProcessW(nullptr, (wchar_t*)commandLine.data(), nullptr, nullptr,
        TRUE, 0, nullptr, currentDirectory, &startupInfo, &processInfo);

    if (result == FALSE)
    {
        const DWORD errorCode = GetLastError();
        Log("ERROR: failed to create process: %d\n", errorCode);
        return 1;
    }

    SetEvent(m_InputEvent);

    const DWORD timeout = 30000; // 30 seconds
    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, timeout);

    if (waitResult != WAIT_OBJECT_0)
        Log("ERROR: failed to wait for process: %d ('%ls')\n", waitResult, commandLine.data());

    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);

    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    return exitCode;
}

DWORD CompilationThread::CompileShader(ShaderStage stage, const std::wstring& sourcePath)
{
    const std::wstring ext = EXT_ARRAY[(size_t)stage];
    const std::wstring fileName = GetFileNameWithoutExt(sourcePath) + ext;
    const std::wstring profilePrefix = HLSL_PROFILE_ARRAY[(size_t)stage];
    const std::wstring outputPath = g_OutputDir + fileName;
    const std::wstring outputHeaderPath = g_OutputHeaderDir + fileName;
    const std::wstring commandLine = L"CompileShader.bat " + profilePrefix + L" \"" + sourcePath + L"\" \"" + outputPath + L"\" \"" + outputHeaderPath + L"\"";

    const DWORD result = ExecuteCommandLine(commandLine);

    if (result != 0)
        Log("ERROR: failed to execute the command line: '%ls' (result: %d)\n", commandLine.data(), result);

    return result;
}

std::wstring SanitizePath(const std::wstring& path)
{
    std::wstring result = path;
    std::replace(result.begin(), result.end(), L'/', L'\\');
    return result;
}

void DeleteFiles(const std::wstring& directory)
{
    const std::wstring commandLine = L"del /Q \"" + SanitizePath(directory) + L"*.*\"";
    _wsystem(commandLine.c_str());
}

void CreateAndClearOutputFolder(const std::wstring& directoryBin, const std::wstring& directoryHeader)
{
    const std::wstring commandLine1 = L"md \"" + SanitizePath(directoryBin) + L"\"";
    _wsystem(commandLine1.c_str());

    const std::wstring commandLine2 = L"md \"" + SanitizePath(directoryHeader) + L"\"";
    _wsystem(commandLine2.c_str());

    DeleteFiles(directoryBin);
    DeleteFiles(directoryHeader);
}

int main()
{
    uint64_t timeBegin = 0;
    QueryPerformanceCounter((LARGE_INTEGER*)&timeBegin);

    InitializeCriticalSection(&g_LogCriticalSection);

    wchar_t currentDir[MAX_PATH];
    GetModuleFileNameW(nullptr, currentDir, MAX_PATH);

    std::wstring baseDir = std::wstring(currentDir) + L"\\..\\..\\..\\..\\..\\";

    wchar_t buffer[MAX_PATH];
    GetFullPathNameW(baseDir.c_str(), MAX_PATH, buffer, nullptr);
    baseDir = buffer;

    g_OutputDir = baseDir + FOLDER_DATA + L"\\Shaders\\";
    g_OutputHeaderDir = baseDir + FOLDER_BUILD + L"\\Shaders\\";

    std::vector<Shader> shaders;
    EnumerateFiles(baseDir, shaders);

    CreateAndClearOutputFolder(g_OutputDir, g_OutputHeaderDir);

    std::wstring currDir = baseDir + FOLDER_COMPILE_SHADERS;
    SetCurrentDirectoryW(currDir.c_str());

    const size_t threadNum = std::thread::hardware_concurrency();
    std::vector<std::unique_ptr<CompilationThread>> threads(threadNum);

    size_t shaderArrayOffset = 0;
    const size_t shadersPerThread = std::max<size_t>((shaders.size() + (threadNum - 1)) / threadNum, 1);

    std::atomic_size_t compiledShaders = 0;

    for (size_t i = 0; i < threads.size(); i++)
    {
        const size_t shaderNum = std::min<size_t>(shadersPerThread, shaders.size() - shaderArrayOffset);

        threads[i] = std::make_unique<CompilationThread>(shaders.data() + shaderArrayOffset, shaderNum, compiledShaders);
        shaderArrayOffset += shaderNum;
    }

    threads.clear();

    const size_t compiled = compiledShaders.load();

    if (compiled != shaders.size())
        Log("ERROR: %I64u/%I64u shaders compiled!\n", compiled, shaders.size());
    else
        Log("SUCCESS!\n");

    time_t t = time(nullptr);
    tm tm = *localtime(&t);
    Log("TIMESTAMP: %d-%02d-%02d %02d:%02d:%02d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    uint64_t timeEnd = 0;
    QueryPerformanceCounter((LARGE_INTEGER*)&timeEnd);

    uint64_t frequency = 1;
    QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
    Log("Time: %.2lfs\n", double(timeEnd - timeBegin) / frequency);

    DeleteCriticalSection(&g_LogCriticalSection);

    return compiled == shaders.size() ? 0 : 1;
}
