cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POSITION_INDEPENDENT_BINARIES ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Include/NRD.h" ver_h)

string(REGEX MATCH "NRD_VERSION_MAJOR ([0-9]*)" _ ${ver_h})
set(ver_major ${CMAKE_MATCH_1})

string(REGEX MATCH "NRD_VERSION_MINOR ([0-9]*)" _ ${ver_h})
set(ver_minor ${CMAKE_MATCH_1})

string(REGEX MATCH "NRD_VERSION_BUILD ([0-9]*)" _ ${ver_h})
set(ver_patch ${CMAKE_MATCH_1})

project(NRD VERSION ${ver_major}.${ver_minor}.${ver_patch} LANGUAGES CXX)

message(STATUS "${PROJECT_NAME} version: ${ver_major}.${ver_minor}.${ver_patch}")

set(DEFAULT_BUILD_TYPE "Release")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse -msse2 -msse3 -msse4 -msse4.1 -mavx -mavx2 -mfma -fvisibility=hidden")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

include(GNUInstallDirs)

file(GLOB_RECURSE SOURCES Source/*)
file(GLOB TIMER ${CMAKE_CURRENT_SOURCE_DIR}/../External/Timer/*.cpp)
file(GLOB MATHLIB ${CMAKE_CURRENT_SOURCE_DIR}/../External/MathLib/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${TIMER} ${MATHLIB})

if (MSVC)
    set(NRD_API_EXPORT "extern \"C\" __declspec(dllexport)")
else()
    set(NRD_API_EXPORT "extern \"C\" __attribute__((visibility(\"default\")))")
endif()

target_link_libraries(${PROJECT_NAME} pthread)

target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX NRD_API=${NRD_API_EXPORT})

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE Include ../_Build ../External ../External/Timer ../External/StdAllocator ../External/MathLib)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1)
