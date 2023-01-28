# DXC on Windows does not like forward slashes
if(WIN32)
    string(REPLACE "/" "\\" SHADER_INCLUDE_PATH "${SHADER_FILES}/Include")

    # Needed only for WinSDK before 22000
    string(REPLACE "/" "\\" MATHLIB_INCLUDE_PATH "${MATHLIB_INCLUDE_PATH}")
endif()

# Find FXC and DXC
if(WIN32)
    if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        set(WINDOWS_SDK_VERSION ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION})
    elseif(DEFINED ENV{WindowsSDKLibVersion})
        string(REGEX REPLACE "\\\\$" "" WINDOWS_SDK_VERSION "$ENV{WindowsSDKLibVersion}")
    else()
        message(FATAL_ERROR "WindowsSDK is not installed.(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION is not defined; WindowsSDKLibVersion is '$ENV{WindowsSDKLibVersion}')")
    endif()

    get_filename_component(WINDOWS_SDK_ROOT
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)

    set(WINDOWS_SDK_BIN "${WINDOWS_SDK_ROOT}/bin/${WINDOWS_SDK_VERSION}/x64")

    # On Windows, FXC and DXC are part of WindowsSDK and there's also DXC in VulkanSDK which supports SPIR-V
    find_program(DXC_PATH "${WINDOWS_SDK_BIN}/dxc")
    if(NOT DXC_PATH)
        message(FATAL_ERROR "Can't find DXC: '${WINDOWS_SDK_BIN}/dxc'")
    endif()

    find_program(DXC_SPIRV_PATH "$ENV{VULKAN_SDK}/Bin/dxc")
    if(NOT DXC_SPIRV_PATH)
        message("Can't find VulkanSDK DXC: '$ENV{VULKAN_SDK}/Bin/dxc'")
        find_program(DXC_SPIRV_PATH "dxc" "${NRD_DXC_CUSTOM_PATH}")
        if(NOT DXC_SPIRV_PATH)
            message(FATAL_ERROR "Can't find DXC: Specify custom path using 'NRD_DXC_CUSTOM_PATH' parameter or install VulkanSDK")
        endif()
    endif()
else()
    # On Linux, VulkanSDK does not set VULKAN_SDK, but DXC can be called directly
    find_program(DXC_SPIRV_PATH "dxc")
    if(NOT DXC_SPIRV_PATH)
        find_program(DXC_SPIRV_PATH "${NRD_DXC_CUSTOM_PATH}")
        if(NOT DXC_SPIRV_PATH)
            message(FATAL_ERROR "Can't find DXC: Specify custom path using 'NRD_DXC_CUSTOM_PATH' parameter or install VulkanSDK")
        endif()
    endif()
endif()

message("Using DXC path: '${DXC_PATH}'")
message("Using DXC(for SPIRV) path: '${DXC_SPIRV_PATH}'")

function(get_shader_profile_from_name FILE_NAME DXC_PROFILE)
    get_filename_component(EXTENSION ${FILE_NAME} EXT)
    if("${EXTENSION}" STREQUAL ".cs.hlsl")
        set(DXC_PROFILE "cs_6_3" PARENT_SCOPE)
    endif()
endfunction()

macro(list_hlsl_headers HLSL_FILES HEADER_FILES)
    foreach(FILE_NAME ${HLSL_FILES})
        set(DXC_PROFILE "")
        get_shader_profile_from_name(${FILE_NAME} DXC_PROFILE)
        if("${DXC_PROFILE}" STREQUAL "" AND "${FXC_PROFILE}" STREQUAL "")
            list(APPEND HEADER_FILES ${FILE_NAME})
            set_source_files_properties(${FILE_NAME} PROPERTIES VS_TOOL_OVERRIDE "None")
        endif()
    endforeach()
endmacro()

set(VK_S_SHIFT 100)
set(VK_T_SHIFT 200)
set(VK_B_SHIFT 300)
set(VK_U_SHIFT 400)
set(DXC_VK_SHIFTS
    -fvk-s-shift ${VK_S_SHIFT} 0 -fvk-s-shift ${VK_S_SHIFT} 1 -fvk-s-shift ${VK_S_SHIFT} 2
    -fvk-t-shift ${VK_T_SHIFT} 0 -fvk-t-shift ${VK_T_SHIFT} 1 -fvk-t-shift ${VK_T_SHIFT} 2
    -fvk-b-shift ${VK_B_SHIFT} 0 -fvk-b-shift ${VK_B_SHIFT} 1 -fvk-b-shift ${VK_B_SHIFT} 2
    -fvk-u-shift ${VK_U_SHIFT} 0 -fvk-u-shift ${VK_U_SHIFT} 1 -fvk-u-shift ${VK_U_SHIFT} 2)

macro(list_hlsl_shaders HLSL_FILES HEADER_FILES SHADER_FILES)
    foreach(FILE_NAME ${HLSL_FILES})
        get_filename_component(NAME_ONLY ${FILE_NAME} NAME)
        string(REGEX REPLACE "\\.[^.]*$" "" NAME_ONLY ${NAME_ONLY})
        string(REPLACE "." "_" BYTECODE_ARRAY_NAME "${NAME_ONLY}")
        set(DXC_PROFILE "")
        set(OUTPUT_PATH_DXIL "${NRD_SHADERS_PATH}/${NAME_ONLY}.dxil")
        set(OUTPUT_PATH_SPIRV "${NRD_SHADERS_PATH}/${NAME_ONLY}.spirv")
        get_shader_profile_from_name(${FILE_NAME} DXC_PROFILE FXC_PROFILE)

        # DXIL
        if(NOT "${DXC_PROFILE}" STREQUAL "" AND NOT "${DXC_PATH}" STREQUAL "")
            add_custom_command(
                    OUTPUT ${OUTPUT_PATH_DXIL} ${OUTPUT_PATH_DXIL}.h
                    COMMAND ${DXC_PATH} -E main -T ${DXC_PROFILE} -WX -O3 -enable-16bit-types -all_resources_bound
                        -DNRD_COMPILER_DXC=1 -DNRD_NORMAL_ENCODING=${NRD_NORMAL_ENCODING} -DNRD_ROUGHNESS_ENCODING=${NRD_ROUGHNESS_ENCODING}
                        -I "${MATHLIB_INCLUDE_PATH}" -I "${SHADER_INCLUDE_PATH}"
                        "${FILE_NAME}" -Vn g_${BYTECODE_ARRAY_NAME}_dxil -Fh ${OUTPUT_PATH_DXIL}.h -Fo ${OUTPUT_PATH_DXIL}
                    MAIN_DEPENDENCY ${FILE_NAME}
                    DEPENDS ${HEADER_FILES}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
                    VERBATIM
            )
            list(APPEND SHADER_FILES ${OUTPUT_PATH_DXIL} ${OUTPUT_PATH_DXIL}.h)
        endif()

        # SPIRV
        if(NOT "${DXC_PROFILE}" STREQUAL "" AND NOT "${DXC_SPIRV_PATH}" STREQUAL "")
            add_custom_command(
                    OUTPUT ${OUTPUT_PATH_SPIRV} ${OUTPUT_PATH_SPIRV}.h
                    COMMAND ${DXC_SPIRV_PATH} -E main -T ${DXC_PROFILE} -WX -O3 -enable-16bit-types -all_resources_bound
                        -DNRD_COMPILER_DXC=1 -DVULKAN=1 -DNRD_NORMAL_ENCODING=${NRD_NORMAL_ENCODING} -DNRD_ROUGHNESS_ENCODING=${NRD_ROUGHNESS_ENCODING}
                        -I "${MATHLIB_INCLUDE_PATH}" -I "${SHADER_INCLUDE_PATH}"
                        "${FILE_NAME}" -Vn g_${BYTECODE_ARRAY_NAME}_spirv -Fh ${OUTPUT_PATH_SPIRV}.h -Fo ${OUTPUT_PATH_SPIRV} ${DXC_VK_SHIFTS}
                        -spirv
                    MAIN_DEPENDENCY ${FILE_NAME}
                    DEPENDS ${HEADER_FILES}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
                    VERBATIM
            )
            list(APPEND SHADER_FILES ${OUTPUT_PATH_SPIRV} ${OUTPUT_PATH_SPIRV}.h)
        endif()
    endforeach()
endmacro()