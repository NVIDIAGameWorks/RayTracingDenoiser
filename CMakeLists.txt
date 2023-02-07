cmake_minimum_required(VERSION 3.15)

# Options
option(NRD_DISABLE_SHADER_COMPILATION "Disable shader compilation" OFF)
option(NRD_USE_PRECOMPILED_SHADERS "Use precompiled (embedded) shaders" ON)
option(NRD_STATIC_LIBRARY "Build static library" OFF)

# Cached
set(NRD_DXC_CUSTOM_PATH "custom/path/to/dxc" CACHE STRING "Custom DXC to use if Vulkan SDK is not installed")
set(NRD_SHADERS_PATH "" CACHE STRING "Shader output path override")
set(NRD_NORMAL_ENCODING "2" CACHE STRING "Normal encoding variant (0-4, matches nrd::NormalEncoding)")
set(NRD_ROUGHNESS_ENCODING "1" CACHE STRING "Roughness encoding variant (0-2, matches nrd::RoughnessEncoding)")
set(GLOBAL_BIN_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/_Build" CACHE STRING "")
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "")

# Create project
file(READ "Include/NRD.h" ver_h)
string(REGEX MATCH "VERSION_MAJOR ([0-9]*)" _ ${ver_h})
set(VERSION_MAJOR ${CMAKE_MATCH_1})
string(REGEX MATCH "VERSION_MINOR ([0-9]*)" _ ${ver_h})
set(VERSION_MINOR ${CMAKE_MATCH_1})
string(REGEX MATCH "VERSION_BUILD ([0-9]*)" _ ${ver_h})
set(VERSION_BUILD ${CMAKE_MATCH_1})

message("NRD v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_BUILD}")
project(NRD LANGUAGES CXX)

# Generate "NRDEncoding.hlsli"
file(WRITE Shaders/Include/NRDEncoding.hlsli
"// This file is auto-generated during project deployment. Do not modify!\n"
"#define NRD_NORMAL_ENCODING ${NRD_NORMAL_ENCODING}\n"
"#define NRD_ROUGHNESS_ENCODING ${NRD_ROUGHNESS_ENCODING}\n")

message("NRD encoding: NRD_NORMAL_ENCODING = ${NRD_NORMAL_ENCODING}; NRD_ROUGHNESS_ENCODING = ${NRD_ROUGHNESS_ENCODING}")

# Globals?
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POSITION_INDEPENDENT_BINARIES ON)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
endif()

# Compile options
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(COMPILE_OPTIONS -Wextra -msse4.1)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(COMPILE_OPTIONS -Wextra -msse4.1)
elseif(MSVC)
    set(COMPILE_OPTIONS /W4 /WX)
else()
    message(WARNING "Unknown compiler!")
endif()

# Compile definitions
set(COMPILE_DEFINITIONS NRD_NORMAL_ENCODING=${NRD_NORMAL_ENCODING} NRD_ROUGHNESS_ENCODING=${NRD_ROUGHNESS_ENCODING})
if(WIN32)
    set(COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _UNICODE UNICODE _ENFORCE_MATCHING_ALLOCATORS=0)
else()
    set(COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} NRD_ONLY_SPIRV_SHADERS_AVAILABLE=1)
endif()

# Shaders
if("${NRD_SHADERS_PATH}" STREQUAL "")
    set(NRD_SHADERS_PATH "${PROJECT_BINARY_DIR}/Shaders")
endif()
message("NRD shaders output path: '${NRD_SHADERS_PATH}'")

if(NOT NRD_DISABLE_SHADER_COMPILATION)
    # Create output directory
    file(MAKE_DIRECTORY ${NRD_SHADERS_PATH})

    # External include needed for ShaderCompilation.cmake
    set(MATHLIB_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/External/MathLib")

    # Gather "hlsl/hlsli" files
    file(GLOB MATHLIB_HLSLI "${MATHLIB_INCLUDE_PATH}/*.hlsli")
    file(GLOB_RECURSE HLSLI "Shaders/*.hlsli" )
    file(GLOB_RECURSE HLSL "Shaders/*.hlsl")
    file(GLOB_RECURSE RESOURCES_HLSLI "Shaders/*.resources.hlsli")

    # Define build rules
    include(ShaderCompilation.cmake)

    list_hlsl_headers("${HLSL}" HEADER_FILES)
    list_hlsl_headers("${MATHLIB_HLSLI}" HEADER_FILES)
    list_hlsl_headers("${HLSLI}" HEADER_FILES)
    list_hlsl_shaders("${HLSL}" "${HEADER_FILES}" SHADER_FILES)

    source_group("MathLib" FILES ${MATHLIB_HLSLI})
    source_group("Source" FILES ${HLSL})
    source_group("Resources" FILES ${RESOURCES_HLSLI})

    add_custom_target(${PROJECT_NAME}_Shaders ALL DEPENDS ${SHADER_FILES} SOURCES "${HEADER_FILES}")
    set_property(TARGET ${PROJECT_NAME}_Shaders PROPERTY FOLDER "${PROJECT_NAME}")
endif()

# External/MathLib
file(READ "External/MathLib/MathLib.h" ver_h)
string(REGEX MATCH "MATHLIB_VERSION_MAJOR ([0-9]*)" _ ${ver_h})
set(MATHLIB_VERSION_MAJOR ${CMAKE_MATCH_1})
string(REGEX MATCH "MATHLIB_VERSION_MINOR ([0-9]*)" _ ${ver_h})
set(MATHLIB_VERSION_MINOR ${CMAKE_MATCH_1})
message("MathLib v${MATHLIB_VERSION_MAJOR}.${MATHLIB_VERSION_MINOR}")

file(GLOB MATHLIB_FILES "External/MathLib/*.h" "External/MathLib/*.hpp")
source_group("MathLib" FILES ${MATHLIB_FILES})

# NRD
file(GLOB GLOB_INCUDE "Include/*")
source_group("Include" FILES ${GLOB_INCUDE})
file(GLOB GLOB_SOURCE "Source/*.cpp" "Source/*.h" "Source/*.hpp")
source_group("Source" FILES ${GLOB_SOURCE})
file(GLOB GLOB_METHODS "Source/Methods/*.cpp" "Source/Methods/*.h" "Source/Methods/*.hpp")
source_group("Methods" FILES ${GLOB_METHODS})
file(GLOB GLOB_RESOURCES "Resources/*")
source_group("Resources" FILES ${GLOB_RESOURCES})

if(NRD_STATIC_LIBRARY)
    add_library(${PROJECT_NAME} STATIC ${GLOB_SOURCE} ${GLOB_METHODS} ${MATHLIB_FILES} ${GLOB_RESOURCES} ${GLOB_INCUDE})
else()
    add_library(${PROJECT_NAME} SHARED ${GLOB_SOURCE} ${GLOB_METHODS} ${MATHLIB_FILES} ${GLOB_RESOURCES} ${GLOB_INCUDE})

    if(WIN32)
        target_compile_definitions(${PROJECT_NAME} PRIVATE "NRD_API=extern \"C\" __declspec(dllexport)")
    else()
        target_compile_definitions(${PROJECT_NAME} PRIVATE "NRD_API=extern \"C\" __attribute__((visibility(\"default\")))")
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE "Include" "External")
if(NRD_USE_PRECOMPILED_SHADERS)
    target_include_directories(${PROJECT_NAME} PRIVATE "${NRD_SHADERS_PATH}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE NRD_USE_PRECOMPILED_SHADERS)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE ${COMPILE_DEFINITIONS})
target_compile_options(${PROJECT_NAME} PRIVATE ${COMPILE_OPTIONS})

set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "${PROJECT_NAME}")

if(NOT NRD_DISABLE_SHADER_COMPILATION)
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_Shaders)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GLOBAL_BIN_OUTPUT_PATH}/$<CONFIG>")
set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${GLOBAL_BIN_OUTPUT_PATH}/$<CONFIG>")
set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${GLOBAL_BIN_OUTPUT_PATH}/$<CONFIG>")
message("NRD output path: '${GLOBAL_BIN_OUTPUT_PATH}'")
