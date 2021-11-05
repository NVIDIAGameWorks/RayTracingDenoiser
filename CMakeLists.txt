cmake_minimum_required(VERSION 3.10)

# NRD_DXC_CUSTOM_PATH="custom/path/to/dxc"
# NRD_SHADER_OUTPUT_PATH="custom/path/to/shaders"
# NRD_PROJECT_FOLDER="External/NRD"
option(NRD_DISABLE_INTERPROCEDURAL_OPTIMIZATION "disable interprocedural optimization" OFF)
option(NRD_DISABLE_SHADER_COMPILATION "disable compilation of shaders" OFF)
option(NRD_USE_PRECOMPILED_SHADERS "use precompiled shaders" ON)
option(NRD_STATIC_LIBRARY "build static lib" OFF)
option(NRD_CROSSCOMPILE_AARCH64 "cross compilation for aarch64" OFF)
option(NRD_CROSSCOMPILE_X86_64 "cross compilation for x86_64" OFF)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Include/NRD.h" ver_h)
string(REGEX MATCH "NRD_VERSION_MAJOR ([0-9]*)" _ ${ver_h})
set(ver_major ${CMAKE_MATCH_1})
string(REGEX MATCH "NRD_VERSION_MINOR ([0-9]*)" _ ${ver_h})
set(ver_minor ${CMAKE_MATCH_1})
string(REGEX MATCH "NRD_VERSION_BUILD ([0-9]*)" _ ${ver_h})
set(ver_patch ${CMAKE_MATCH_1})

project(NRD VERSION ${ver_major}.${ver_minor}.${ver_patch} LANGUAGES CXX)
message(STATUS "${PROJECT_NAME} version: ${ver_major}.${ver_minor}.${ver_patch}")

if (NRD_CROSSCOMPILE_AARCH64)
    set(CMAKE_SYSTEM_PROCESSOR "aarch64")
    message(STATUS "CROSSCOMPILE_AARCH64 enabled.")
endif()
if (NRD_CROSSCOMPILE_X86_64)
    set(CMAKE_SYSTEM_PROCESSOR "x86_64")
    message(STATUS "CROSSCOMPILE_X86_64 enabled.")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POSITION_INDEPENDENT_BINARIES ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if (NRD_DISABLE_INTERPROCEDURAL_OPTIMIZATION)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

if ("${NRD_SHADER_OUTPUT_PATH}" STREQUAL "")
    set(NRD_SHADER_OUTPUT_PATH "${PROJECT_BINARY_DIR}/Shaders")
endif()

if (NOT NRD_DISABLE_SHADER_COMPILATION)
    # include path for DXC and FXC (used in ShaderCompilation.cmake)
    set(NRD_SHADER_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Source/Shaders")
    set(NRD_HEADER_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Include")
    set(NRD_MATHLIB_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/External/MathLib")

    # list HLSL shaders and headers
    file(GLOB_RECURSE NRD_HLSL_FILES "Source/Shaders/*.hlsl" "Source/Shaders/*.hlsli")
    file(GLOB_RECURSE NRD_HLSL_MATHLIB_HEADER_FILES "${NRD_MATHLIB_INCLUDE_PATH}/*.hlsli")
    file(GLOB_RECURSE NRD_HLSL_INCLUDE "Include/*.hlsli")

    include(ShaderCompilation.cmake)

    # create "build/Shaders"
    add_custom_target(CreateFolderForShaders ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${NRD_SHADER_OUTPUT_PATH}")
    set_property(TARGET CreateFolderForShaders PROPERTY FOLDER "${NRD_PROJECT_FOLDER}")

    # define build rules for the shaders
    list_hlsl_headers("${NRD_HLSL_FILES}" NRD_HEADER_FILES)
    list_hlsl_headers("${NRD_HLSL_MATHLIB_HEADER_FILES}" NRD_HEADER_FILES)
    list_hlsl_headers("${NRD_HLSL_INCLUDE}" NRD_HEADER_FILES)
    list_hlsl_shaders("${NRD_HLSL_FILES}" "${NRD_HEADER_FILES}" NRD_SHADER_FILES)
    add_custom_target(NrdShaders ALL DEPENDS ${NRD_SHADER_FILES} SOURCES "${NRD_HEADER_FILES}")
    add_dependencies(NrdShaders CreateFolderForShaders)
    set_property(TARGET NrdShaders PROPERTY FOLDER "${NRD_PROJECT_FOLDER}")
endif()

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    set(NRD_IS_SUBMODULE OFF)
else()
    set(NRD_IS_SUBMODULE ON)
endif()

# list C/C++ files and add them to NRD project
file(GLOB NRD_SOURCE "Source/*.cpp" "Source/*.h" "Source/*.hpp")
source_group("Sources" FILES ${NRD_SOURCE})
file(GLOB NRD_SOURCE_METHODS "Source/Methods/*.cpp" "Source/Methods/*.h" "Source/Methods/*.hpp")
source_group("Methods" FILES ${NRD_SOURCE_METHODS})
file(GLOB NRD_MATHLIB_HEADERS "External/MathLib/*.h" "External/MathLib/*.hpp")
source_group("MathLib" FILES ${NRD_MATHLIB_HEADERS})
file(GLOB NRD_RESOURCE "Source/*.rc")
source_group("Sources" FILES ${NRD_RESOURCE})
file(GLOB NRD_HEADERS "Include/*")
source_group("Include" FILES ${NRD_HEADERS})

if (NRD_STATIC_LIBRARY)
    add_library(${PROJECT_NAME} STATIC ${NRD_SOURCE} ${NRD_SOURCE_METHODS} ${NRD_MATHLIB_HEADERS} ${NRD_RESOURCE} ${NRD_HEADERS})
else()
    add_library(${PROJECT_NAME} SHARED ${NRD_SOURCE} ${NRD_SOURCE_METHODS} ${NRD_MATHLIB_HEADERS} ${NRD_RESOURCE} ${NRD_HEADERS})
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${PROJECT_VERSION})
target_include_directories(${PROJECT_NAME} PUBLIC "Include" "External")
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "${NRD_PROJECT_FOLDER}")

if (NOT NRD_DISABLE_SHADER_COMPILATION)
    add_dependencies(${PROJECT_NAME} NrdShaders)
endif()

if (NRD_USE_PRECOMPILED_SHADERS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NRD_USE_PRECOMPILED_SHADERS)
    target_include_directories(${PROJECT_NAME} PRIVATE "${NRD_SHADER_OUTPUT_PATH}")
endif()

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _UNICODE UNICODE)
else()
    # on non-Windows, FXC is not available and VulkanSDK DXC does not produce signed DXIL
    target_compile_definitions(${PROJECT_NAME} PRIVATE NRD_ONLY_SPIRV_SHADERS_AVAILABLE=1)
endif()

if (NRD_STATIC_LIBRARY)
    target_compile_definitions(${PROJECT_NAME} PUBLIC "NRD_API=")
else()
    if(WIN32)
        target_compile_definitions(${PROJECT_NAME} PRIVATE "NRD_API=extern \"C\" __declspec(dllexport)")
    else()
        target_compile_definitions(${PROJECT_NAME} PRIVATE "NRD_API=extern \"C\" __attribute__((visibility(\"default\")))")
    endif()
endif()

if ((CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64") OR (CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64"))
    set(NRD_ARCHITECTURE_COMPILE_OPTIONS -msse4.1)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE ${NRD_ARCHITECTURE_COMPILE_OPTIONS} -Wno-missing-braces -Wno-return-type-c-linkage)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE ${NRD_ARCHITECTURE_COMPILE_OPTIONS})
endif()

if (NOT NRD_IS_SUBMODULE)
    set(NRD_OUTPUT_BIN_PATH "${CMAKE_SOURCE_DIR}/_Build")
endif()

if (NOT ${NRD_OUTPUT_BIN_PATH} STREQUAL "")
    message(STATUS "NRD output path: '${NRD_OUTPUT_BIN_PATH}'")
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${NRD_OUTPUT_BIN_PATH}/$<CONFIG>")
    set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${NRD_OUTPUT_BIN_PATH}/$<CONFIG>")
    set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${NRD_OUTPUT_BIN_PATH}/$<CONFIG>")
endif()

