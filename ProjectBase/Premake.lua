local WIN_SDK_VERSION = "10.0.19041.0"
local VK_SDK_PATH = os.getenv("VULKAN_SDK") .. "/"

workspace "SANDBOX"

    local targetName = _ACTION
    local workspaceDir = "_Compiler/"..targetName
    local platformDir = "%{cfg.system}"
    local targetDirBinBase = "_Build/"..targetName.."/Bin"
    local targetDirBin = "_Build/"..targetName.."/Bin/%{cfg.buildcfg}"
    local targetDirLib = "_Build/"..targetName.."/Lib/%{cfg.buildcfg}"
    local currentAbsPath = path.getabsolute(".")
    local targetDepsDir = "_Build/TargetDeps"

    configurations { "Debug", "Release" }
    architecture "x86_64"
    objdir "_Build/Intermediate/%{prj.name}"
    implibdir (targetDirLib)
    symbols "Full"
    editandcontinue "Off"
    exceptionhandling "Off"
    rtti "Off"
    staticruntime "On"
    systemversion (WIN_SDK_VERSION)
    language "C++"
    cppdialect "C++17"
    startproject "CompileShaders"
    location (workspaceDir)
    disablewarnings { "4577", "4324", "4100" }
    startproject "09_RayTracing_NRD"
    debugargs { "Please, install Smart Command Line Arguments for Visual Studio" }
    warnings "Extra"

    filter { "configurations:Release" }
        optimize "Full"
        flags { "FatalCompileWarnings", "MultiProcessorCompile", "NoPCH", "UndefinedIdentifiers", "NoIncrementalLink" }
    filter {}

    filter { "configurations:Debug" }
        flags { "FatalCompileWarnings", "MultiProcessorCompile", "NoPCH", "UndefinedIdentifiers", "NoIncrementalLink" }
    filter {}

    filter { "files:**.hlsl" }
        flags { "ExcludeFromBuild" }
    filter {}

    filter { "kind:WindowedApp" }
        vectorextensions "AVX"
    filter {}

    targetdir (targetDirBin)
    filter { "kind:StaticLib" }
        targetdir (targetDirLib)
    filter {}

    filter { "system:windows" }

        group "COMPILE_SHADERS"
            project "CompileShaders"
                kind "ConsoleApp"
                location (workspaceDir.."/%{prj.name}")
                files { "%{prj.name}/*.cpp" }
                vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
                postbuildcommands { "\""..path.getabsolute(targetDirBin.."/%{prj.name}.exe").."\"" }
                defines { "_CRT_SECURE_NO_WARNINGS", "WIN32_LEAN_AND_MEAN", "NOMINMAX" }

    filter {}

    group "EXTERNAL"
        project "ImGui"
            kind "StaticLib"
            location (workspaceDir.."/%{prj.name}")
            files { targetDepsDir.."/%{prj.name}/*"}
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            warnings "Default"

        project "Detex"
            kind "StaticLib"
            location (workspaceDir.."/%{prj.name}")
            files { targetDepsDir.."/%{prj.name}/*"}
            vpaths { ["Sources"] = {"**.c", "**.h"} }
            defines { "_CRT_SECURE_NO_WARNINGS" }
            warnings "Default"

    group "NRD"
        project "NRD"
            kind "SharedLib"
            location (workspaceDir.."/%{prj.name}")
            includedirs { "%{prj.name}/Include", "External" }
            files { "%{prj.name}/Source/**", "%{prj.name}/Include/**", "External/MathLib/mathlib.cpp", "External/Timer/*", "External/StdAllocator/*" }
            vpaths { ["Methods"] = "**.hpp" }
            vpaths { ["Sources"] = {"**.cpp", "**.h", "**.rc"} }
            defines { "WIN32_LEAN_AND_MEAN", "NOMINMAX", "NRD_API=extern \"C\" __declspec(dllexport)" }
            dependson { "CompileShaders" }

    group "NRI"
        local src = "NRI/Source/"
        local inc = "NRI/Include/"
        local incs = { "NRI/Include/", "NRI/Source/Shared", "External", targetDepsDir }
        local defs = { "_CRT_SECURE_NO_WARNINGS", "WIN32_LEAN_AND_MEAN", "NOMINMAX", "_ENFORCE_MATCHING_ALLOCATORS=0" }

        project "NRI_D3D11"
            kind "StaticLib"
            location (workspaceDir.."/%{prj.name}")
            includedirs { incs, targetDepsDir.."/**" }
            files { src.."D3D11/*", inc.."**.h", src.."Shared/*", "External/StdAllocator/*"  }
            vpaths { ["NRI"] = inc.."**.h" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { defs, "D3D11_NO_HELPERS" }

        project "NRI_D3D12"
            kind "StaticLib"
            location (workspaceDir.."/%{prj.name}")
            includedirs { incs }
            files { src.."D3D12/*", inc.."**.h", src.."Shared/*", "External/StdAllocator/*"  }
            vpaths { ["NRI"] = inc.."**.h" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { defs }

        project "NRI_VK"
            kind "StaticLib"
            location (workspaceDir.."/%{prj.name}")
            includedirs { incs, VK_SDK_PATH }
            libdirs { VK_SDK_PATH }
            files { src.."VK/*", inc.."**.h", src.."Shared/*", "External/StdAllocator/*"  }
            vpaths { ["NRI"] = inc.."**.h" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { defs }

        project "NRI_Validation"
            kind "StaticLib"
            location (workspaceDir.."/%{prj.name}")
            includedirs { incs }
            files { src.."Validation/*", inc.."**.h", src.."Shared/*", "External/StdAllocator/*"  }
            vpaths { ["NRI"] = inc.."**.h" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { defs }

        project "NRI_Creation"
            kind "StaticLib"
            location (workspaceDir.."/%{prj.name}")
            includedirs { incs, VK_SDK_PATH }
            files { src.."Creation/*", inc.."**.h" }
            vpaths { ["NRI"] = inc.."**.h" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { defs }

        project "NRI"
            kind "SharedLib"
            location (workspaceDir.."/%{prj.name}")
            includedirs { incs, VK_SDK_PATH }
            files { inc.."**.h*", src.."Creation/*" }
            vpaths { ["NRI"] = inc.."**.h*" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            links { "NRI_D3D11", "d3d11", "dxguid", "nvapi64", "amd_ags_x64", "DelayImp" }
            links { "NRI_D3D12", "d3d12", "dxguid" }
            links { "NRI_VK", VK_SDK_PATH.."Lib/vulkan-1" }
            links { "NRI_Validation" }
            links { "dxgi" }
            linkoptions { "/DELAYLOAD:amd_ags_x64.dll", "/DELAYLOAD:vulkan-1.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }
            defines { defs, "D3D11_NO_HELPERS", "NRI_API=extern \"C\" __declspec(dllexport)" }

    group "Samples"
        local dir = "Samples/"
        local inc = { "External", targetDepsDir, targetDepsDir.."/Assimp/include", "NRI/Include", dir.."SampleBase" }
        local defs = { "_CRT_SECURE_NO_WARNINGS", "WIN32_LEAN_AND_MEAN", "NOMINMAX", "_ENFORCE_MATCHING_ALLOCATORS=0" }
        local lnks = { "NRI", "SampleBase", "ImGui", "Detex", "DelayImp", "assimp" }

        project "SampleBase"
            kind "StaticLib"
            location (workspaceDir.."/%{prj.name}")
            includedirs { "External", targetDepsDir, targetDepsDir.."/Assimp/include", "NRI/Include" }
            files { dir.."%{prj.name}/**", "External/MathLib/mathlib*.*", "External/MathLib/packed.h", "External/Timer/*" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            dependson { "CompileShaders" }
            defines { defs }

        project "00_Clear"
            kind "WindowedApp"
            location (workspaceDir.."/%{prj.name}")
            includedirs { inc }
            files { dir.."%{prj.name}.cpp" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { "PROJECT_NAME=%{prj.name}" }
            links { lnks }
            linkoptions { "/DELAYLOAD:assimp-vc141-mt.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }

        project "01_Triangle"
            kind "WindowedApp"
            location (workspaceDir.."/%{prj.name}")
            includedirs { inc }
            files { dir.."%{prj.name}.cpp" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { "PROJECT_NAME=%{prj.name}" }
            links { lnks }
            linkoptions { "/DELAYLOAD:assimp-vc141-mt.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }

        project "02_SceneViewer"
            kind "WindowedApp"
            location (workspaceDir.."/%{prj.name}")
            includedirs { inc }
            files { dir.."%{prj.name}.cpp" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { "PROJECT_NAME=%{prj.name}" }
            links { lnks }
            linkoptions { "/DELAYLOAD:assimp-vc141-mt.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }

        project "03_Readback"
            kind "WindowedApp"
            location (workspaceDir.."/%{prj.name}")
            includedirs { inc }
            files { dir.."%{prj.name}.cpp" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { "PROJECT_NAME=%{prj.name}" }
            links { lnks }
            linkoptions { "/DELAYLOAD:assimp-vc141-mt.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }

        project "04_AsyncCompute"
            kind "WindowedApp"
            location (workspaceDir.."/%{prj.name}")
            includedirs { inc }
            files { dir.."%{prj.name}.cpp" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { "PROJECT_NAME=%{prj.name}" }
            links { lnks }
            linkoptions { "/DELAYLOAD:assimp-vc141-mt.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }

        project "05_Multithreading"
            kind "WindowedApp"
            location (workspaceDir.."/%{prj.name}")
            includedirs { inc }
            files { dir.."%{prj.name}.cpp" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { "PROJECT_NAME=%{prj.name}" }
            links { lnks }
            linkoptions { "/DELAYLOAD:assimp-vc141-mt.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }

        project "06_MultiGPU"
            kind "WindowedApp"
            location (workspaceDir.."/%{prj.name}")
            includedirs { inc }
            files { dir.."%{prj.name}.cpp" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { "PROJECT_NAME=%{prj.name}" }
            links { lnks }
            linkoptions { "/DELAYLOAD:assimp-vc141-mt.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }

        project "07_RayTracing_Triangle"
            kind "WindowedApp"
            location (workspaceDir.."/%{prj.name}")
            includedirs { inc }
            files { dir.."%{prj.name}.cpp" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { "PROJECT_NAME=%{prj.name}" }
            links { lnks }
            linkoptions { "/DELAYLOAD:assimp-vc141-mt.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }

        project "08_RayTracing_Boxes"
            kind "WindowedApp"
            location (workspaceDir.."/%{prj.name}")
            includedirs { inc }
            files { dir.."%{prj.name}.cpp" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { "PROJECT_NAME=%{prj.name}" }
            links { lnks }
            linkoptions { "/DELAYLOAD:assimp-vc141-mt.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }

        project "09_RayTracing_NRD"
            kind "WindowedApp"
            location (workspaceDir.."/%{prj.name}")
            includedirs { inc, "NRD/Include", "NRD/Integration", VK_SDK_PATH }
            files { dir.."%{prj.name}.cpp", "NRD/Integration/*.*" }
            vpaths { ["Sources"] = {"**.cpp", "**.inl", "**.h", "**.hpp"} }
            defines { "PROJECT_NAME=%{prj.name}" }
            links { lnks, "NRD" }
            linkoptions { "/DELAYLOAD:assimp-vc141-mt.dll" }
            libdirs { "External/**", targetDepsDir.."/**" }
