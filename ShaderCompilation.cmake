
# DXC on Windows does not like forward slashes
if (WIN32)
    string(REPLACE "/" "\\" NRD_SHADER_INCLUDE_PATH "${NRD_SHADER_FILES}/Include")
    string(REPLACE "/" "\\" NRD_MATHLIB_INCLUDE_PATH "${NRD_MATHLIB_INCLUDE_PATH}")
endif()

# Find FXC and DXC
if (WIN32)
    if (DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        set (NRD_WINDOWS_SDK_VERSION ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION})
    elseif (DEFINED ENV{WindowsSDKLibVersion})
        string (REGEX REPLACE "\\\\$" "" NRD_WINDOWS_SDK_VERSION "$ENV{WindowsSDKLibVersion}")
    else()
        message(FATAL_ERROR "WindowsSDK is not installed. (CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION is not defined; WindowsSDKLibVersion is '$ENV{WindowsSDKLibVersion}')")
    endif()

    get_filename_component(NRD_WINDOWS_SDK_ROOT
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)

    set(NRD_WINDOWS_SDK_BIN "${NRD_WINDOWS_SDK_ROOT}/bin/${NRD_WINDOWS_SDK_VERSION}/x64")

    # on Windows, FXC and DXC are part of WindowsSDK and there's also DXC in VulkanSDK which supports SPIR-V
    find_program(NRD_FXC_PATH "${NRD_WINDOWS_SDK_BIN}/fxc")
    if (NOT NRD_FXC_PATH)
        message(FATAL_ERROR "Can't find FXC: '${NRD_WINDOWS_SDK_BIN}/fxc'")
    endif()

    find_program(NRD_DXC_PATH "${NRD_WINDOWS_SDK_BIN}/dxc")
    if (NOT NRD_DXC_PATH)
        message(FATAL_ERROR "Can't find DXC: '${NRD_WINDOWS_SDK_BIN}/dxc'")
    endif()

    find_program(NRD_DXC_SPIRV_PATH "$ENV{VULKAN_SDK}/Bin/dxc")
    if (NOT NRD_DXC_SPIRV_PATH)
        message("Can't find VulkanSDK DXC: '$ENV{VULKAN_SDK}/Bin/dxc'")
        find_program(NRD_DXC_SPIRV_PATH "dxc" "${NRD_DXC_CUSTOM_PATH}")
        if (NOT NRD_DXC_SPIRV_PATH)
            message(FATAL_ERROR "Can't find DXC: Specify custom path using 'NRD_DXC_CUSTOM_PATH' parameter or install VulkanSDK")
        endif()
    endif()
else()
    # on Linux, VulkanSDK does not set VULKAN_SDK, but DXC can be called directly
    find_program(NRD_DXC_SPIRV_PATH "dxc")
    if (NOT NRD_DXC_SPIRV_PATH)
        find_program(NRD_DXC_SPIRV_PATH "${NRD_DXC_CUSTOM_PATH}")
        if (NOT NRD_DXC_SPIRV_PATH)
            message(FATAL_ERROR "Can't find DXC: VulkanSDK is not installed. Custom path can be specified using 'NRD_DXC_CUSTOM_PATH' parameter.")
        endif()
    endif()
endif()

message(STATUS "Using FXC path: '${NRD_FXC_PATH}'")
message(STATUS "Using DXC path: '${NRD_DXC_PATH}'")
message(STATUS "Using DXC (for SPIRV) path: '${NRD_DXC_SPIRV_PATH}'")

function(get_shader_profile_from_name FILE_NAME DXC_PROFILE FXC_PROFILE)
    get_filename_component(EXTENSION ${FILE_NAME} EXT)
    if ("${EXTENSION}" STREQUAL ".cs.hlsl")
        set(DXC_PROFILE "cs_6_3" PARENT_SCOPE)
        set(FXC_PROFILE "cs_5_0" PARENT_SCOPE)
    endif()
endfunction()

macro(list_hlsl_headers NRD_HLSL_FILES NRD_HEADER_FILES)
    foreach(FILE_NAME ${NRD_HLSL_FILES})
        set(DXC_PROFILE "")
        set(FXC_PROFILE "")
        get_shader_profile_from_name(${FILE_NAME} DXC_PROFILE FXC_PROFILE)
        if ("${DXC_PROFILE}" STREQUAL "" AND "${FXC_PROFILE}" STREQUAL "")
            list(APPEND NRD_HEADER_FILES ${FILE_NAME})
            set_source_files_properties(${FILE_NAME} PROPERTIES VS_TOOL_OVERRIDE "None")
        endif()
    endforeach()
endmacro()

set (NRD_VK_S_SHIFT 100)
set (NRD_VK_T_SHIFT 200)
set (NRD_VK_B_SHIFT 300)
set (NRD_VK_U_SHIFT 400)
set (NRD_DXC_VK_SHIFTS
    -fvk-s-shift ${NRD_VK_S_SHIFT} 0 -fvk-s-shift ${NRD_VK_S_SHIFT} 1 -fvk-s-shift ${NRD_VK_S_SHIFT} 2
    -fvk-t-shift ${NRD_VK_T_SHIFT} 0 -fvk-t-shift ${NRD_VK_T_SHIFT} 1 -fvk-t-shift ${NRD_VK_T_SHIFT} 2
    -fvk-b-shift ${NRD_VK_B_SHIFT} 0 -fvk-b-shift ${NRD_VK_B_SHIFT} 1 -fvk-b-shift ${NRD_VK_B_SHIFT} 2
    -fvk-u-shift ${NRD_VK_U_SHIFT} 0 -fvk-u-shift ${NRD_VK_U_SHIFT} 1 -fvk-u-shift ${NRD_VK_U_SHIFT} 2)

macro(list_hlsl_shaders NRD_HLSL_FILES NRD_HEADER_FILES NRD_SHADER_FILES)
    foreach(FILE_NAME ${NRD_HLSL_FILES})
        get_filename_component(NAME_ONLY ${FILE_NAME} NAME)
        string(REGEX REPLACE "\\.[^.]*$" "" NAME_ONLY ${NAME_ONLY})
        string(REPLACE "." "_" BYTECODE_ARRAY_NAME "${NAME_ONLY}")
        set(DXC_PROFILE "")
        set(FXC_PROFILE "")
        set(OUTPUT_PATH_DXBC "${NRD_SHADER_OUTPUT_PATH}/${NAME_ONLY}.dxbc")
        set(OUTPUT_PATH_DXIL "${NRD_SHADER_OUTPUT_PATH}/${NAME_ONLY}.dxil")
        set(OUTPUT_PATH_SPIRV "${NRD_SHADER_OUTPUT_PATH}/${NAME_ONLY}.spirv")
        get_shader_profile_from_name(${FILE_NAME} DXC_PROFILE FXC_PROFILE)
        # add FXC compilation step (DXBC)
        if (NOT "${FXC_PROFILE}" STREQUAL "" AND NOT "${NRD_FXC_PATH}" STREQUAL "")
            add_custom_command(
                    OUTPUT ${OUTPUT_PATH_DXBC} ${OUTPUT_PATH_DXBC}.h
                    COMMAND ${NRD_FXC_PATH} /nologo /E main /T ${FXC_PROFILE} /WX /O3 /all_resources_bound
                        /DNRD_COMPILER_FXC=1 /DNRD_USE_OCT_NORMAL_ENCODING=${NRD_USE_OCT_NORMAL_ENCODING} /DNRD_USE_MATERIAL_ID=${NRD_USE_MATERIAL_ID}
                        /I "${NRD_MATHLIB_INCLUDE_PATH}" /I "${NRD_SHADER_INCLUDE_PATH}"
                        "${FILE_NAME}" /Vn g_${BYTECODE_ARRAY_NAME}_dxbc /Fh ${OUTPUT_PATH_DXBC}.h /Fo ${OUTPUT_PATH_DXBC}
                    MAIN_DEPENDENCY ${FILE_NAME}
                    DEPENDS ${NRD_HEADER_FILES}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
                    VERBATIM
            )
            list(APPEND NRD_SHADER_FILES ${OUTPUT_PATH_DXBC} ${OUTPUT_PATH_DXBC}.h)
        endif()
        # add DXC compilation step (DXIL)
        if (NOT "${DXC_PROFILE}" STREQUAL "" AND NOT "${NRD_DXC_PATH}" STREQUAL "")
            add_custom_command(
                    OUTPUT ${OUTPUT_PATH_DXIL} ${OUTPUT_PATH_DXIL}.h
                    COMMAND ${NRD_DXC_PATH} -E main -T ${DXC_PROFILE} -WX -O3 -enable-16bit-types -all_resources_bound
                        -DNRD_COMPILER_DXC=1 -DNRD_USE_OCT_NORMAL_ENCODING=${NRD_USE_OCT_NORMAL_ENCODING} -DNRD_USE_MATERIAL_ID=${NRD_USE_MATERIAL_ID}
                        -I "${NRD_MATHLIB_INCLUDE_PATH}" -I "${NRD_SHADER_INCLUDE_PATH}"
                        "${FILE_NAME}" -Vn g_${BYTECODE_ARRAY_NAME}_dxil -Fh ${OUTPUT_PATH_DXIL}.h -Fo ${OUTPUT_PATH_DXIL}
                    MAIN_DEPENDENCY ${FILE_NAME}
                    DEPENDS ${NRD_HEADER_FILES}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
                    VERBATIM
            )
            list(APPEND NRD_SHADER_FILES ${OUTPUT_PATH_DXIL} ${OUTPUT_PATH_DXIL}.h)
        endif()
        # add one more DXC compilation step (SPIR-V)
        if (NOT "${DXC_PROFILE}" STREQUAL "" AND NOT "${NRD_DXC_SPIRV_PATH}" STREQUAL "")
            add_custom_command(
                    OUTPUT ${OUTPUT_PATH_SPIRV} ${OUTPUT_PATH_SPIRV}.h
                    COMMAND ${NRD_DXC_SPIRV_PATH} -E main -T ${DXC_PROFILE} -WX -O3 -enable-16bit-types -all_resources_bound
                        -DNRD_COMPILER_DXC=1 -DVULKAN=1 -DNRD_USE_OCT_NORMAL_ENCODING=${NRD_USE_OCT_NORMAL_ENCODING} -DNRD_USE_MATERIAL_ID=${NRD_USE_MATERIAL_ID}
                        -I "${NRD_MATHLIB_INCLUDE_PATH}" -I "${NRD_SHADER_INCLUDE_PATH}"
                        "${FILE_NAME}" -Vn g_${BYTECODE_ARRAY_NAME}_spirv -Fh ${OUTPUT_PATH_SPIRV}.h -Fo ${OUTPUT_PATH_SPIRV} ${NRD_DXC_VK_SHIFTS}
                        -spirv
                    MAIN_DEPENDENCY ${FILE_NAME}
                    DEPENDS ${NRD_HEADER_FILES}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
                    VERBATIM
            )
            list(APPEND NRD_SHADER_FILES ${OUTPUT_PATH_SPIRV} ${OUTPUT_PATH_SPIRV}.h)
        endif()
    endforeach()
endmacro()
