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

#define FOLDER_DATA L"_Data"
#define FOLDER_BUILD L"_Build"
#define FOLDER_COMPILE_SHADERS L"CompileShaders"

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

bool CompileShader(ShaderStage stage, const std::wstring& sourcePath, const std::wstring& outputDir, const std::wstring& outputHeaderDir)
{
    const std::wstring ext = EXT_ARRAY[(size_t)stage];
    const std::wstring fileName = GetFileNameWithoutExt(sourcePath) + ext;
    const std::wstring profilePrefix = HLSL_PROFILE_ARRAY[(size_t)stage];
    const std::wstring outputPath = outputDir + fileName;
    const std::wstring outputHeaderPath = outputHeaderDir + fileName;
    const std::wstring commandLine = L"CompileShader.bat " + profilePrefix + L" \"" + sourcePath + L"\" \"" + outputPath + L"\" \"" + outputHeaderPath + L"\"";

    bool result = _wsystem(commandLine.c_str()) == 0;

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

void main()
{
    wchar_t currentDir[MAX_PATH];
    GetModuleFileNameW(nullptr, currentDir, MAX_PATH);

    std::wstring baseDir = std::wstring(currentDir) + L"\\..\\..\\..\\..\\..\\";

    wchar_t buffer[MAX_PATH];
    GetFullPathNameW(baseDir.c_str(), MAX_PATH, buffer, nullptr);
    baseDir = buffer;

    const std::wstring outputDir = baseDir + FOLDER_DATA + L"\\Shaders\\";
    const std::wstring outputHeaderDir = baseDir + FOLDER_BUILD + L"\\Shaders\\";

    std::vector<Shader> shaders;
    EnumerateFiles(baseDir, shaders);

    CreateAndClearOutputFolder(outputDir, outputHeaderDir);

    std::wstring currDir = baseDir + FOLDER_COMPILE_SHADERS;
    SetCurrentDirectoryW(currDir.c_str());

    size_t compiled = 0;
    for (const Shader& shader : shaders)
    {
        bool result = CompileShader(shader.stage, shader.file, outputDir, outputHeaderDir);
        compiled += result ? 1 : 0;
    }

    if( compiled != shaders.size() )
        printf("ERROR: %I64u/%I64u shaders compiled!\n", compiled, shaders.size());
}
