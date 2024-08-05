cmake_minimum_required (VERSION 3.15)

# Options
if (WIN32)
    set (IS_WIN ON)
else ()
    set (IS_WIN OFF)
endif ()

option (NRD_STATIC_LIBRARY "Build static library" OFF)
option (NRD_EMBEDS_SPIRV_SHADERS "NRD embeds SPIRV shaders" ON)
option (NRD_EMBEDS_DXIL_SHADERS "NRD embeds DXIL shaders" ${IS_WIN})
option (NRD_EMBEDS_DXBC_SHADERS "NRD embeds DXBC shaders" ${IS_WIN})
option (NRD_DISABLE_SHADER_COMPILATION "Disable shader compilation" OFF)

# Is submodule?
if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    set (IS_SUBMODULE OFF)
else ()
    set (IS_SUBMODULE ON)
endif ()

# Cached
if (NOT IS_SUBMODULE)
    set (CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "")
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/_Bin" CACHE STRING "")
endif ()

set (NRD_DXC_CUSTOM_PATH "custom/path/to/dxc" CACHE STRING "Custom DXC to use if Vulkan SDK is not installed")
set (NRD_SHADERS_PATH "" CACHE STRING "Shader output path override")
set (NRD_NORMAL_ENCODING "2" CACHE STRING "Normal encoding variant (0-4, matches nrd::NormalEncoding)")
set (NRD_ROUGHNESS_ENCODING "1" CACHE STRING "Roughness encoding variant (0-2, matches nrd::RoughnessEncoding)")

# Generate PDB for Release builds
if (MSVC)
    set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set (CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif ()

# Create project
file (READ "Include/NRD.h" ver_h)
string (REGEX MATCH "VERSION_MAJOR ([0-9]*)" _ ${ver_h})
set (VERSION_MAJOR ${CMAKE_MATCH_1})
string (REGEX MATCH "VERSION_MINOR ([0-9]*)" _ ${ver_h})
set (VERSION_MINOR ${CMAKE_MATCH_1})
string (REGEX MATCH "VERSION_BUILD ([0-9]*)" _ ${ver_h})
set (VERSION_BUILD ${CMAKE_MATCH_1})
message ("NRD v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_BUILD}")

project (NRD LANGUAGES C CXX)
message ("NRD encoding: NRD_NORMAL_ENCODING = ${NRD_NORMAL_ENCODING}; NRD_ROUGHNESS_ENCODING = ${NRD_ROUGHNESS_ENCODING}")

# Generate "NRDEncoding.hlsli"
file (WRITE Shaders/Include/NRDEncoding.hlsli
    "// This file is auto-generated during project deployment. Do not modify!\n"
    "#define NRD_NORMAL_ENCODING ${NRD_NORMAL_ENCODING}\n"
    "#define NRD_ROUGHNESS_ENCODING ${NRD_ROUGHNESS_ENCODING}\n")

# Globals?
set_property (GLOBAL PROPERTY USE_FOLDERS ON)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_C_STANDARD 99)

if (MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

# Compile options
if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64")
  set (SIMD -msse4.1)
elseif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64")
  set (SIMD -fPIC)
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set (COMPILE_OPTIONS ${SIMD} -Wextra)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set (COMPILE_OPTIONS ${SIMD} -Wextra)
elseif (MSVC)
    set (COMPILE_OPTIONS /W4 /WX /wd4324)
else ()
    message (WARNING "Unknown compiler!")
endif ()

# Compile definitions
set (COMPILE_DEFINITIONS NRD_NORMAL_ENCODING=${NRD_NORMAL_ENCODING} NRD_ROUGHNESS_ENCODING=${NRD_ROUGHNESS_ENCODING})

if (NRD_EMBEDS_SPIRV_SHADERS)
    set (COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} NRD_EMBEDS_SPIRV_SHADERS)
endif ()

if (NRD_EMBEDS_DXIL_SHADERS)
    set (COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} NRD_EMBEDS_DXIL_SHADERS)
endif ()

if (NRD_EMBEDS_DXBC_SHADERS)
    set (COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} NRD_EMBEDS_DXBC_SHADERS)
endif ()

if (WIN32)
    set (COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _UNICODE UNICODE _ENFORCE_MATCHING_ALLOCATORS=0)
endif ()

# External/MathLib
file (READ "External/MathLib/ml.h" ver_h)
string (REGEX MATCH "ML_VERSION_MAJOR ([0-9]*)" _ ${ver_h})
set (ML_VERSION_MAJOR ${CMAKE_MATCH_1})
string (REGEX MATCH "ML_VERSION_MINOR ([0-9]*)" _ ${ver_h})
set (ML_VERSION_MINOR ${CMAKE_MATCH_1})
message ("MathLib v${ML_VERSION_MAJOR}.${ML_VERSION_MINOR}")

file (GLOB ML_FILES "External/MathLib/*.h" "External/MathLib/Guts/*.h")
source_group ("MathLib" FILES ${ML_FILES})

# NRD
file (GLOB GLOB_INCUDE "Include/*")
source_group ("Include" FILES ${GLOB_INCUDE})
file (GLOB GLOB_SOURCE "Source/*.cpp" "Source/*.h" "Source/*.hpp")
source_group ("Source" FILES ${GLOB_SOURCE})
file (GLOB GLOB_DENOISERS "Source/Denoisers/*.cpp" "Source/Denoisers/*.h" "Source/Denoisers/*.hpp")
source_group ("Denoisers" FILES ${GLOB_DENOISERS})
file (GLOB GLOB_RESOURCES "Resources/*")
source_group ("Resources" FILES ${GLOB_RESOURCES})

if (NRD_STATIC_LIBRARY)
    add_library (${PROJECT_NAME} STATIC ${GLOB_SOURCE} ${GLOB_DENOISERS} ${ML_FILES} ${GLOB_RESOURCES} ${GLOB_INCUDE})
else ()
    add_library (${PROJECT_NAME} SHARED ${GLOB_SOURCE} ${GLOB_DENOISERS} ${ML_FILES} ${GLOB_RESOURCES} ${GLOB_INCUDE})

    if (WIN32)
        target_compile_definitions (${PROJECT_NAME} PRIVATE "NRD_API=extern \"C\" __declspec(dllexport)")
    else ()
        target_compile_definitions (${PROJECT_NAME} PRIVATE "NRD_API=extern \"C\" __attribute__((visibility(\"default\")))")
    endif ()
endif ()

if ("${NRD_SHADERS_PATH}" STREQUAL "")
    set (NRD_SHADERS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/_Shaders")
else ()
    set (NRD_SHADER_BINARIES "--binary")
endif ()

message ("NRD shaders output path: '${NRD_SHADERS_PATH}'")

target_include_directories (${PROJECT_NAME} PUBLIC "Include")
target_include_directories (${PROJECT_NAME} PRIVATE "External")
target_compile_definitions (${PROJECT_NAME} PRIVATE ${COMPILE_DEFINITIONS})
target_compile_options (${PROJECT_NAME} PRIVATE ${COMPILE_OPTIONS})

set_property (TARGET ${PROJECT_NAME} PROPERTY FOLDER "${PROJECT_NAME}")

set_target_properties (${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message ("NRD output path: '${CMAKE_RUNTIME_OUTPUT_DIRECTORY}'")

# Shaders
if (NOT NRD_DISABLE_SHADER_COMPILATION)
    target_include_directories (${PROJECT_NAME} PRIVATE "${NRD_SHADERS_PATH}")

    file (GLOB_RECURSE SHADERS "Shaders/*.hlsl" "Shaders/*.hlsli" "External/MathLib/*.hlsli")
    set_source_files_properties (${SHADERS} PROPERTIES VS_TOOL_OVERRIDE "None")

    # External/ShaderMake
    if (NOT TARGET ShaderMake)
        set (SHADERMAKE_BIN_OUTPUT_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} CACHE STRING "")
        add_subdirectory (External/ShaderMake)
    endif ()

    # ShaderMake general arguments
    set (SHADERMAKE_GENERAL_ARGS
        --useAPI --header ${NRD_SHADER_BINARIES}
        --flatten
        --stripReflection
        --sourceDir "Shaders/Source"
        --allResourcesBound
        --WX
        --vulkanVersion 1.2
        -c Shaders.cfg
        -o "${NRD_SHADERS_PATH}"
        -I "External/MathLib"
        -I "Shaders/Include"
        -I "Shaders/Resources"
        -D NRD_NORMAL_ENCODING=${NRD_NORMAL_ENCODING}
        -D NRD_ROUGHNESS_ENCODING=${NRD_ROUGHNESS_ENCODING}
        -D NRD_INTERNAL
    )

    # ShaderMake commands for each shader code container
    set (SHADERMAKE_COMMANDS "")

    if (NRD_EMBEDS_DXIL_SHADERS)
        set (SHADERMAKE_COMMANDS ${SHADERMAKE_COMMANDS} COMMAND ShaderMake -p DXIL --compiler "${DXC_PATH}" ${SHADERMAKE_GENERAL_ARGS})
    endif ()

    if (NRD_EMBEDS_SPIRV_SHADERS)
        set (SHADERMAKE_COMMANDS ${SHADERMAKE_COMMANDS} COMMAND ShaderMake -p SPIRV --compiler "${DXC_SPIRV_PATH}" ${SHADERMAKE_GENERAL_ARGS}
            --sRegShift 100 --tRegShift 200 --bRegShift 300 --uRegShift 400
        )
    endif ()

    if (NRD_EMBEDS_DXBC_SHADERS)
        set (SHADERMAKE_COMMANDS ${SHADERMAKE_COMMANDS} COMMAND ShaderMake -p DXBC --compiler "${FXC_PATH}" ${SHADERMAKE_GENERAL_ARGS})
    endif ()

    # Add the target with the commands
    add_custom_target (${PROJECT_NAME}_Shaders ALL ${SHADERMAKE_COMMANDS}
        DEPENDS ShaderMake
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
        SOURCES ${SHADERS}
    )

    set_property (TARGET ${PROJECT_NAME}_Shaders PROPERTY FOLDER ${PROJECT_NAME})
    add_dependencies (${PROJECT_NAME} ${PROJECT_NAME}_Shaders)
endif ()
